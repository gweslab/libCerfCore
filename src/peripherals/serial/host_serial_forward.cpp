#include "host_serial_forward.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/core/log.h>
#include "../../platform/host_clock.h"
#include "uart_frame.h"

#include <utility>

namespace cerfcore {

namespace {
constexpr uint32_t kReadTimeoutMs  = 50;
constexpr uint32_t kWriteTimeoutMs = 2000;

host_platform::SerialPortSettings ToPortSettings(const SerialLine::LineConfig& c,
                                                 bool dtr, bool rts) {
    using Settings = host_platform::SerialPortSettings;
    Settings s;
    if (c.baud) s.baud = c.baud;
    s.data_bits = c.data_bits;
    switch (c.parity) {
        case SerialLine::LineConfig::Parity::None:  s.parity = Settings::Parity::kNone;  break;
        case SerialLine::LineConfig::Parity::Odd:   s.parity = Settings::Parity::kOdd;   break;
        case SerialLine::LineConfig::Parity::Even:  s.parity = Settings::Parity::kEven;  break;
        case SerialLine::LineConfig::Parity::Mark:  s.parity = Settings::Parity::kMark;  break;
        case SerialLine::LineConfig::Parity::Space: s.parity = Settings::Parity::kSpace; break;
    }
    switch (c.stop) {
        case SerialLine::LineConfig::Stop::One:          s.stop = Settings::StopBits::kOne;          break;
        case SerialLine::LineConfig::Stop::OnePointFive: s.stop = Settings::StopBits::kOnePointFive; break;
        case SerialLine::LineConfig::Stop::Two:          s.stop = Settings::StopBits::kTwo;          break;
    }
    s.dtr = dtr;
    s.rts = rts;
    return s;
}
}

HostSerialForward::HostSerialForward(std::string host_port, CerfEmulator& emu)
    : HostIoEndpoint(emu), port_name_(std::move(host_port)) {}

HostSerialForward::~HostSerialForward() { OnClose(); }

bool HostSerialForward::WaitStopped(uint32_t timeout_ms) { return port_.WaitStopped(timeout_ms); }

void HostSerialForward::OnOpen() {
    uint32_t err = 0;
    if (!port_.Open(port_name_, err)) {
        LOG(Caution, "[SerialFwd] cannot open host %s (err %u)\n", port_name_.c_str(), err);
        if (uart_) uart_->SetModemInputs(false, false, false, false);
        ReportDead("open_failed", err);
        return;
    }

    ApplyLineConfig(uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{});
    {
        std::lock_guard<std::mutex> lk(ctl_mu_);
        if (break_) ApplyBreak(true);
    }

    last_ms_.store(kModemLinesUnpushed);

    port_.StartIo(kReadTimeoutMs, kWriteTimeoutMs);

    if (uart_)
        uart_->SetLineConfigCallback([this](const SerialLine::LineConfig& c) {
            TxItem item{TxItem::Kind::kConfig, 0u};
            item.cfg = c;
            Queue(item);
        });

    InstanceThreads& threads = emu_.Get<InstanceThreads>();
    running_ = true;
    reader_  = threads.Start([this] { ReaderLoop(); });
    writer_  = threads.Start([this] { WriterLoop(); });
    LOG(Periph, "[SerialFwd] bridging guest COM <-> host %s\n", port_name_.c_str());
}

void HostSerialForward::OnClose() {
    StopHostIo();
    if (uart_) uart_->SetModemInputs(false, false, false, false);
    port_.Close();
}

void HostSerialForward::StopHostIo() {
    if (uart_) uart_->SetLineConfigCallback(nullptr);
    running_.store(false);
    port_.RequestStop();
    tx_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();
}

void HostSerialForward::ApplyLineConfig(const SerialLine::LineConfig& c) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    if (!port_.IsOpen()) return;
    uint32_t err = 0;
    const host_platform::SerialConfigureResult result =
        port_.Configure(ToPortSettings(c, dtr_, rts_), err);
    if (result == host_platform::SerialConfigureResult::kStateUnreadable) return;
    if (result == host_platform::SerialConfigureResult::kRejected) {
        LOG(Caution, "[SerialFwd] %s line configuration failed (err %u): the port did not "
            "take baud=%u bits=%u parity=%u stop=%u DTR=%d RTS=%d\n",
            port_name_.c_str(), err, c.baud, c.data_bits,
            (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
        return;
    }
    LOG(Periph, "[SerialFwd] line cfg %s baud=%u bits=%u parity=%u stop=%u "
        "DTR=%d RTS=%d\n", port_name_.c_str(), c.baud, c.data_bits,
        (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
}

void HostSerialForward::OnGuestTx(const uint8_t* data, size_t n) {
    if (!port_.IsOpen() || n == 0) return;
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] TX guest->host %s n=%zu b0=0x%02X\n",
        port_name_.c_str(), n, data[0]);
#endif
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        for (size_t i = 0; i < n; ++i) tx_buf_.push_back({TxItem::Kind::kByte, data[i]});
    }
    tx_cv_.notify_one();
}

void HostSerialForward::OnBreak(bool on) {
    {
        std::lock_guard<std::mutex> lk(ctl_mu_);
        break_ = on;
        if (!port_.IsOpen()) return;
    }
    LOG(Periph, "[SerialFwd] break %s on host %s\n", on ? "on" : "off", port_name_.c_str());
    TxItem item{on ? TxItem::Kind::kBreakOn : TxItem::Kind::kBreakOff, 0u};
    item.at = host_platform::MonotonicTicks();
    Queue(item);
}

void HostSerialForward::Queue(const TxItem& item) {
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        tx_buf_.push_back(item);
    }
    tx_cv_.notify_one();
}

void HostSerialForward::ApplyBreak(bool on) {
    uint32_t err = 0;
    if (on && !port_.Drain(err)) {
        LOG(Caution, "[SerialFwd] %s did not drain its output before a break (err %u)\n", port_name_.c_str(), err);
    }
    if (port_.SetBreak(on, err)) return;
    LOG(Caution, "[SerialFwd] %s refused to turn the break %s (err %u)\n", port_name_.c_str(), on ? "on" : "off",
        err);
}

void HostSerialForward::ApplyControlLines(bool dtr, bool rts) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    dtr_ = dtr;
    rts_ = rts;
    LOG(Periph, "[SerialFwd] drive host %s DTR=%d RTS=%d\n", port_name_.c_str(), dtr, rts);
    port_.SetControlLines(dtr, rts);
}

void HostSerialForward::ResendModemInputs() { last_ms_.store(kModemLinesUnpushed); }

void HostSerialForward::PostRestore() {
    if (!port_.IsOpen()) {
        OnOpen();
        return;
    }
    TxItem item{TxItem::Kind::kConfig, 0u};
    item.cfg = uart_->GetLineConfig();
    Queue(item);
    ResendModemInputs();
}

void HostSerialForward::OnControlLines(bool dtr, bool rts) {
    {
        std::lock_guard<std::mutex> lk(ctl_mu_);
        if (!port_.IsOpen()) {
            dtr_ = dtr;
            rts_ = rts;
            return;
        }
    }
    TxItem item{TxItem::Kind::kLines, 0u};
    item.dtr = dtr;
    item.rts = rts;
    Queue(item);
}

void HostSerialForward::ReaderLoop() {
    uint8_t buf[512];
    while (running_.load()) {
        uint8_t cur = 0;
        if (port_.ModemLines(cur)) {
            if (cur != last_ms_.load()) {
                const bool cts = (cur & host_platform::kModemCts)  != 0;
                const bool dsr = (cur & host_platform::kModemDsr)  != 0;
                const bool ri  = (cur & host_platform::kModemRing) != 0;
                const bool dcd = (cur & host_platform::kModemDcd)  != 0;
                LOG(Periph, "[SerialFwd] host modem CTS=%d DSR=%d RI=%d DCD=%d\n",
                    cts, dsr, ri, dcd);
                if (uart_) {
                    if (!WithGuestAccess([&] {
                            uart_->SetModemInputs(cts, dsr, ri, dcd);
                        }))
                        break;
                }
                last_ms_.store(cur);
            }
        }

        uint32_t got = 0;
        uint32_t err = 0;
        const host_platform::SerialIoResult result = port_.Read(buf, sizeof buf, got, err);
        if (result == host_platform::SerialIoResult::kFailed) {
            LOG(Caution, "[SerialFwd] %s reader err=%u; host->guest "
                "bridge is dead\n", port_name_.c_str(), err);
            ReportDead("read_failed", err);
            break;
        }
        if (result == host_platform::SerialIoResult::kStopped) break;
        if (got > 0 && uart_) {
#if CERF_DEV_MODE
            LOG(Periph, "[SerialFwd] RX host->guest %s n=%u b0=0x%02X\n",
                port_name_.c_str(), got, buf[0]);
#endif
            if (!WithGuestAccess([&] { uart_->PushRx(buf, got); })) break;
        }
    }
}

void HostSerialForward::WriterLoop() {
    const int64_t freq = host_platform::MonotonicFrequency();
    int64_t next = host_platform::MonotonicTicks();
    const auto char_ticks = [freq](const SerialLine::LineConfig& c) {
        return c.baud ? (int64_t)(UartCharBitTimes(c) * (double)freq / c.baud) : 0;
    };
    int64_t byte_ticks     = char_ticks(uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{});
    int64_t break_guest_at = 0;
    int64_t break_host_at  = 0;
    std::vector<TxItem> local;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(tx_mu_);
            tx_cv_.wait(lk, [this] { return !tx_buf_.empty() || !running_.load(); });
            if (!running_.load() && tx_buf_.empty()) return;
            local.swap(tx_buf_);
        }

        for (size_t i = 0; i < local.size() && running_.load(); ++i) {
            const int64_t now = host_platform::MonotonicTicks();
            if (next < now) next = now;
            if (next > now && !port_.PaceWait(next - now)) {
                LOG(Periph, "[SerialFwd] writer pace-wait aborted at byte %zu/%zu\n",
                    i, local.size());
                break;
            }

            if (local[i].kind == TxItem::Kind::kConfig) {
                ApplyLineConfig(local[i].cfg);
                byte_ticks = char_ticks(local[i].cfg);
                continue;
            }
            if (local[i].kind == TxItem::Kind::kLines) {
                ApplyControlLines(local[i].dtr, local[i].rts);
                continue;
            }
            if (local[i].kind == TxItem::Kind::kBreakOn) {
                ApplyBreak(true);
                break_guest_at = local[i].at;
                break_host_at  = host_platform::MonotonicTicks();
                continue;
            }
            if (local[i].kind == TxItem::Kind::kBreakOff) {
                const int64_t until = break_host_at + (local[i].at - break_guest_at);
                const int64_t held  = host_platform::MonotonicTicks();
                if (until > held && !port_.PaceWait(until - held)) break;
                ApplyBreak(false);
                continue;
            }

            uint32_t wrote = 0;
            uint32_t err   = 0;
            const host_platform::SerialIoResult result = port_.WriteByte(local[i].byte, wrote, err);
            if (result == host_platform::SerialIoResult::kStopped) {
                LOG(Periph, "[SerialFwd] writer wait aborted at byte %zu/%zu; "
                    "thread exits\n", i, local.size());
                return;
            }
            if (result == host_platform::SerialIoResult::kFailed) {
                LOG(Caution, "[SerialFwd] %s writer err=%u at byte "
                    "%zu/%zu; guest->host bytes dropped\n", port_name_.c_str(),
                    err, i, local.size());
                ReportDead("write_failed", err);
                break;
            }
            if (wrote == 0) {
                LOG(Caution, "[SerialFwd] %s writer timed out at byte %zu/%zu; "
                    "guest->host bytes dropped\n", port_name_.c_str(), i,
                    local.size());
                ReportDead("write_timeout", 0);
                break;
            }
            next += byte_ticks;
        }
        local.clear();
    }
}

}
