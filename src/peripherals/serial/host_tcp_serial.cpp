#include "host_tcp_serial.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/core/log.h>
#include "../../platform/host_network.h"

#include <chrono>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kPollMs         = 50;
constexpr uint32_t kWriteTimeoutMs = 2000;
constexpr size_t   kRxChunk        = 512;

using host_platform::TcpIoResult;
using host_platform::TcpWaitResult;

}

HostTcpSerial::HostTcpSerial(SerialTcpAddress address, CerfEmulator& emu)
    : HostIoEndpoint(emu), address_(std::move(address)) {}

HostTcpSerial::~HostTcpSerial() {
    OnClose();
    listener_.Close();
}

bool HostTcpSerial::WaitStopped(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lk(rx_mu_);
    rx_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this] { return !running_.load(); });
    return !running_.load();
}

void HostTcpSerial::OnOpen() {
    if (running_.load()) return;
    const std::string binding = address_.Binding();
    int               network = 0;
    uint32_t          error   = 0;
    if (!host_platform::StartNetworking(network)) {
        LOG(Caution, "[SerialTcp] host networking did not start (err %d)\n", network);
        uart_->SetModemInputs(false, false, false, false);
        ReportDead("tcp_listen_failed", static_cast<uint32_t>(network));
        return;
    }
    if (!listener_.IsListening() && !listener_.Listen(address_.address, address_.port, error)) {
        LOG(Caution, "[SerialTcp] cannot listen on %s (err %u)\n", binding.c_str(), error);
        uart_->SetModemInputs(false, false, false, false);
        ReportDead("tcp_listen_failed", error);
        return;
    }

    uart_->SetModemInputs(false, false, false, false);
    lines_dirty_.store(false);
    uart_->SetRxDrainCallback([this] {
        {
            std::lock_guard<std::mutex> lk(rx_mu_);
            rx_drained_ = true;
        }
        rx_cv_.notify_all();
    });

    InstanceThreads& threads = emu_.Get<InstanceThreads>();
    running_ = true;
    reader_  = threads.Start([this] { ReaderLoop(); });
    writer_  = threads.Start([this] { WriterLoop(); });
    LOG(Periph, "[SerialTcp] listening on %s\n", binding.c_str());
}

void HostTcpSerial::OnClose() {
    StopHostIo();
    uart_->SetModemInputs(false, false, false, false);
}

void HostTcpSerial::StopHostIo() {
    uart_->SetRxDrainCallback(nullptr);
    running_.store(false);
    DropClient(nullptr, "stopped");
    tx_cv_.notify_all();
    rx_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();
}

void HostTcpSerial::ResendModemInputs() { lines_dirty_.store(true); }

void HostTcpSerial::PostRestore() {
    if (!running_.load()) {
        OnOpen();
        return;
    }
    lines_dirty_.store(true);
}

void HostTcpSerial::OnBreak(bool) {}

void HostTcpSerial::OnGuestTx(const uint8_t* data, size_t n) {
    if (n == 0 || !connected_.load()) return;
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        tx_buf_.insert(tx_buf_.end(), data, data + n);
    }
    tx_cv_.notify_one();
}

std::shared_ptr<HostTcpSerial::Connection> HostTcpSerial::Client() {
    std::lock_guard<std::mutex> lk(client_mu_);
    return client_;
}

void HostTcpSerial::ClearGuestTx() {
    std::lock_guard<std::mutex> lk(tx_mu_);
    tx_buf_.clear();
}

void HostTcpSerial::DropClient(const Connection* expected, const char* why) {
    std::shared_ptr<Connection> gone;
    {
        std::lock_guard<std::mutex> lk(client_mu_);
        if (!client_ || (expected != nullptr && client_.get() != expected)) return;
        gone = std::move(client_);
    }
    connected_.store(false);
    ClearGuestTx();
    gone->Shutdown();
    lines_dirty_.store(true);
    LOG(Periph, "[SerialTcp] %s disconnected from %s (%s)\n", gone->PeerName().c_str(),
        address_.Binding().c_str(), why);
}

bool HostTcpSerial::AcceptClient() {
    uint32_t            error = 0;
    const TcpWaitResult wait  = listener_.WaitAcceptable(kPollMs, error);
    if (wait == TcpWaitResult::kTimeout) return true;
    if (wait == TcpWaitResult::kFailed) {
        LOG(Caution, "[SerialTcp] %s listener failed (err %u)\n", address_.Binding().c_str(), error);
        ReportDead("tcp_failed", error);
        return false;
    }
    std::unique_ptr<Connection> accepted = listener_.Accept(error);
    if (!accepted) {
        if (error == 0) return true;
        LOG(Caution, "[SerialTcp] %s accept failed (err %u)\n", address_.Binding().c_str(), error);
        return !WaitStopped(kPollMs);
    }
    LOG(Periph, "[SerialTcp] %s connected to %s\n", accepted->PeerName().c_str(), address_.Binding().c_str());
    ClearGuestTx();
    {
        std::lock_guard<std::mutex> lk(client_mu_);
        client_ = std::move(accepted);
    }
    connected_.store(true);
    lines_dirty_.store(true);
    return true;
}

void HostTcpSerial::RefuseExtraClients() {
    uint32_t error = 0;
    while (listener_.WaitAcceptable(0, error) == TcpWaitResult::kReady) {
        const std::unique_ptr<Connection> extra = listener_.Accept(error);
        if (!extra) return;
        LOG(Periph, "[SerialTcp] refused %s: %s already has a client\n", extra->PeerName().c_str(),
            address_.Binding().c_str());
    }
}

bool HostTcpSerial::WaitRxDrained(Connection& client) {
    {
        std::unique_lock<std::mutex> lk(rx_mu_);
        rx_cv_.wait_for(lk, std::chrono::milliseconds(kPollMs), [this] { return rx_drained_ || !running_.load(); });
    }
    uint32_t error = 0;
    if (client.WaitReadable(0, error) != TcpWaitResult::kReady) return running_.load();
    const TcpIoResult peeked = client.Peek(error);
    if (peeked == TcpIoResult::kClosed) DropClient(&client, "closed by client");
    if (peeked == TcpIoResult::kFailed) {
        LOG(Caution, "[SerialTcp] %s peek failed (err %u)\n", address_.Binding().c_str(), error);
        DropClient(&client, "peek failed");
    }
    return running_.load();
}

bool HostTcpSerial::ServeClient(Connection& client) {
    {
        std::lock_guard<std::mutex> lk(rx_mu_);
        rx_drained_ = false;
    }
    bool room = false;
    if (!WithGuestAccess([&] { room = uart_->RxEmpty(); })) return false;
    if (!room) return WaitRxDrained(client);

    uint32_t            error = 0;
    const TcpWaitResult wait  = client.WaitReadable(kPollMs, error);
    if (wait == TcpWaitResult::kTimeout) return true;
    if (wait == TcpWaitResult::kFailed) {
        LOG(Caution, "[SerialTcp] %s wait failed (err %u)\n", address_.Binding().c_str(), error);
        DropClient(&client, "wait failed");
        return true;
    }

    uint8_t           buf[kRxChunk];
    uint32_t          got    = 0;
    const TcpIoResult result = client.Read(buf, sizeof buf, got, error);
    if (result == TcpIoResult::kClosed) {
        DropClient(&client, "closed by client");
        return true;
    }
    if (result == TcpIoResult::kFailed) {
        LOG(Caution, "[SerialTcp] %s read failed (err %u)\n", address_.Binding().c_str(), error);
        DropClient(&client, "read failed");
        return true;
    }
    if (got == 0) return true;
    return WithGuestAccess([&] { uart_->PushRx(buf, got); });
}

void HostTcpSerial::ReaderLoop() {
    while (running_.load()) {
        if (lines_dirty_.exchange(false)) {
            const bool on = connected_.load();
            if (!WithGuestAccess([&] { uart_->SetModemInputs(on, on, false, on); })) break;
        }
        const std::shared_ptr<Connection> client = Client();
        if (!client) {
            if (!AcceptClient()) break;
            continue;
        }
        RefuseExtraClients();
        if (!ServeClient(*client)) break;
    }
}

void HostTcpSerial::WriterLoop() {
    std::vector<uint8_t> local;
    for (;;) {
        std::shared_ptr<Connection> client;
        {
            std::unique_lock<std::mutex> lk(tx_mu_);
            tx_cv_.wait(lk, [this] { return !tx_buf_.empty() || !running_.load(); });
            if (!running_.load()) return;
            local.swap(tx_buf_);
            client = Client();
        }
        if (client) {
            const uint32_t    size   = static_cast<uint32_t>(local.size());
            uint32_t          wrote  = 0;
            uint32_t          error  = 0;
            const TcpIoResult result = client->Write(local.data(), size, kWriteTimeoutMs, wrote, error);
            if (result == TcpIoResult::kClosed) {
                DropClient(client.get(), "closed by client");
            } else if (result == TcpIoResult::kFailed) {
                LOG(Caution, "[SerialTcp] %s write failed (err %u)\n", address_.Binding().c_str(), error);
                DropClient(client.get(), "write failed");
            } else if (wrote < size) {
                DropClient(client.get(), "write timed out");
            }
        }
        local.clear();
    }
}

}
