#include "serial_cradle.h"

#include "host_serial_forward.h"
#include "host_tcp_serial.h"
#include "modem_personality.h"
#include "serial_endpoint.h"
#include "serial_forward_commands.h"
#include "serial_line.h"
#include "serial_tcp_address.h"
#include "serial_tcp_commands.h"
#include "serial_tcp_startup.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/execution/jit_runner.h>
#include "../../core/deferred_jobs.h"
#include <cerfcore/core/utf16.h>
#include <cerfcore/state/emulation_freeze.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <utility>

namespace cerfcore {

namespace {

constexpr char kModemName[]   = "Serial Modem";
constexpr char kForwardName[] = "Serial Port Forwarder";
constexpr char kTcpName[]     = "TCP Serial Port";

}

SerialCradle::SerialCradle(CerfEmulator& emu, SerialLine& line, ObjectIdentity identity)
    : emu_(emu), line_(line), identity_(std::move(identity)) {
    const std::optional<SerialTcpAddress> startup = emu_.Get<SerialTcpStartup>().Claim(identity_.id);
    if (!startup) return;
    startup_tcp_ = startup->Binding();
    emu_.Get<JitRunner>().RegisterPreStartHook([this, binding = *startup_tcp_] { Replug(Plugged::Tcp, binding); });
}

SerialCradle::~SerialCradle() = default;

void SerialCradle::OnShutdown() {
    std::lock_guard<std::mutex> lk(mtx_);
    ++generation_;
    shut_down_ = true;
    if (endpoint_) {
        line_.SetEndpoint(nullptr);
        endpoint_->OnClose();
        endpoint_.reset();
    }
    plugged_ = Plugged::None;
}

void SerialCradle::SaveCradleState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write<uint8_t>("kind", static_cast<uint8_t>(plugged_));
    const std::u16string port = Utf8ToUtf16(host_port_);
    const uint32_t n = static_cast<uint32_t>(port.size());
    w.Write("host_port_count", n);
    if (n) w.WriteBytes("host_port", port.data(), n * sizeof(char16_t));
}

void SerialCradle::RestoreCradleState(StateReader& r) {
    Plugged before = Plugged::None;
    Plugged after  = Plugged::None;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        uint8_t k = 0;
        r.Read("kind", k);
        uint32_t n = 0;
        r.Read("host_port_count", n);
        std::u16string port(n, u'\0');
        if (n) r.ReadBytes("host_port", port.data(), n * sizeof(char16_t));
        before = plugged_;
        if (startup_tcp_ && !emu_.Get<JitRunner>().Started()) SetPluggedLocked(Plugged::Tcp, *startup_tcp_, false);
        else                                                   SetPluggedLocked(static_cast<Plugged>(k), Utf16ToUtf8(port), false);
        after = plugged_;
    }
    Announce(before, after);
}

void SerialCradle::SetPluggedLocked(Plugged plugged, std::string host_port, bool power_up) {
    if (endpoint_) {
        line_.SetEndpoint(nullptr);
        endpoint_->OnClose();
        endpoint_.reset();
    }

    plugged_   = plugged;
    host_port_ = std::move(host_port);
    ++generation_;

    if (plugged == Plugged::None) return;

    const uint64_t gen     = generation_;
    DeferredJobs&  jobs    = emu_.Get<DeferredJobs>();
    const auto     on_dead = [this, gen, &jobs](const char* reason, uint32_t code) {
        jobs.Post([this, gen, reason, code] { ReportFailure(gen, reason, code); });
    };
    if (plugged == Plugged::Modem) {
        endpoint_ = std::make_unique<ModemPersonality>(emu_, "ppp:" + identity_.id);
    } else if (plugged == Plugged::Tcp) {
        auto tcp = std::make_unique<HostTcpSerial>(*SerialTcpAddress::Parse(host_port_), emu_);
        tcp->SetOnBridgeDead(on_dead);
        endpoint_ = std::move(tcp);
    } else {
        auto fwd = std::make_unique<HostSerialForward>(host_port_, emu_);
        fwd->SetOnBridgeDead(on_dead);
        endpoint_ = std::move(fwd);
    }

    endpoint_->BindUart(line_);
    if (power_up) endpoint_->OnOpen();
    line_.SetEndpoint(endpoint_.get());
}

void SerialCradle::Announce(Plugged before, Plugged after) {
    if (before != Plugged::None) ReportEjected();
    if (after != Plugged::None) ReportInserted();
}

void SerialCradle::Replug(Plugged plugged, std::string host_port) {
    Plugged before = Plugged::None;
    for (;;) {
        uint64_t gen = 0;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (shut_down_ || (plugged == Plugged::None && plugged_ == Plugged::None)) return;
            if (endpoint_) endpoint_->StopHostIo();
            gen = generation_;
        }
        auto                        frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        std::lock_guard<std::mutex> lk(mtx_);
        if (shut_down_) return;
        if (generation_ != gen) continue;
        before = plugged_;
        SetPluggedLocked(plugged, std::move(host_port), true);
        break;
    }
    Announce(before, plugged);
}

void SerialCradle::UnplugAt(uint64_t gen) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (generation_ != gen) return;
        if (endpoint_) endpoint_->StopHostIo();
    }
    Plugged before = Plugged::None;
    {
        auto                        frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        std::lock_guard<std::mutex> lk(mtx_);
        if (generation_ != gen) return;
        before = plugged_;
        SetPluggedLocked(Plugged::None, {}, true);
    }
    Announce(before, Plugged::None);
}

void SerialCradle::ReportFailure(uint64_t gen, const char* reason, uint32_t code) {
    std::string port;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (generation_ != gen) return;
        port = host_port_;
    }
    Failed(reason, code, std::move(port));
    UnplugAt(gen);
}

std::optional<InsertedItem> SerialCradle::Inserted() const {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (plugged_) {
        case Plugged::Modem:   return InsertedItem{kModemId, kModemName, NoCardFacts{}};
        case Plugged::Forward: return InsertedItem{kForwardId, kForwardName, SerialLinkFacts{host_port_}};
        case Plugged::Tcp:     return InsertedItem{kTcpId, kTcpName, SerialLinkFacts{host_port_}};
        case Plugged::None:    return std::nullopt;
    }
    return std::nullopt;
}

std::vector<InsertableType> SerialCradle::Accepted() const {
    return {
        InsertableType{kModemId, kModemName, {InsertCommand{"insert", {}}}},
        InsertableType{kForwardId, kForwardName, emu_.Get<SerialForwardCommands>().Commands()},
        InsertableType{kTcpId, kTcpName, emu_.Get<SerialTcpCommands>().Commands()},
    };
}

void SerialCradle::InsertAccepted(const std::string& type_id, const std::string&, const std::string& argument) {
    if (type_id == kModemId) {
        Replug(Plugged::Modem, {});
        return;
    }
    if (type_id == kTcpId) {
        emu_.Get<SerialTcpCommands>().Run(
            *this, [this](const SerialTcpAddress& address) { Replug(Plugged::Tcp, address.Binding()); });
        return;
    }
    Replug(Plugged::Forward, argument);
}

void SerialCradle::Eject() { Replug(Plugged::None, {}); }

}
