#include "hd64461_gpio.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_gpio_wiring.h"
#include "hd64461_pcc.h"
#include "hd64461_socket_wiring.h"

namespace cerfcore {

REGISTER_SERVICE(Hd64461Gpio);

namespace {

using cerf_hd64461_gpio_detail::kDataIndex;
using cerf_hd64461_gpio_detail::kPins;
using cerf_hd64461_gpio_detail::kPorts;

constexpr uint32_t kControlShift   = 16u;
constexpr uint32_t kPcmcia1Channel = 1u;

constexpr uint8_t kFunctionOutputs[kPorts] = {0xCCu, 0xA8u, 0x1Fu, 0x0Cu};

constexpr uint8_t  kPortCPcmcia1Inputs  = 0xE0u;
constexpr uint8_t  kPortDPcmcia1Inputs  = 0xF3u;
constexpr uint16_t kPortCPcmcia1Outputs = 0x03FFu;
constexpr uint16_t kPortDPcmcia1Outputs = 0x00F0u;

uint32_t ModeOf(uint32_t pins, uint32_t pin) { return (pins >> (kControlShift + pin * 2u)) & 3u; }

}

bool Hd64461Gpio::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461Gpio::OnReady() {
    wiring_              = &emu_.Get<Hd64461GpioWiring>();
    const uint16_t smode = emu_.Get<Hd64461SocketWiring>().ModePins();
    if (smode != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Gpio: SMODE %u takes ports C and D out of the PCMCIA1 function", smode);
    }
    RegWindow::OnReady();
    std::lock_guard<std::mutex> lk(lock_);
    PublishLocked();
}

void Hd64461Gpio::SaveState(StateWriter& w) {
    RegWindow::SaveState(w);
    std::lock_guard<std::mutex> lk(lock_);
    wiring_->SaveState(w);
}

void Hd64461Gpio::RestoreState(StateReader& r) {
    RegWindow::RestoreState(r);
    std::lock_guard<std::mutex> lk(lock_);
    wiring_->RestoreState(r);
    PublishLocked();
}

void Hd64461Gpio::PostRestore() { NotifyListeners(); }

void Hd64461Gpio::AddPinListener(std::function<void()> listener) { listeners_.Add(std::move(listener)); }

uint32_t Hd64461Gpio::PinMode(uint32_t port, uint32_t pin) const {
    return ModeOf(pins_[port].load(std::memory_order_acquire), pin);
}

PinDrive Hd64461Gpio::Drive(uint32_t port, uint32_t pin) const {
    const uint32_t pins = pins_[port].load(std::memory_order_acquire);
    switch (ModeOf(pins, pin)) {
        case kHd64461PinModeFunction2:
            return ((kFunctionOutputs[port] >> pin) & 1u) != 0u ? PinDrive::kFunction : PinDrive::kUndriven;
        case kHd64461PinModeOutput:      return ((pins >> pin) & 1u) != 0u ? PinDrive::kHigh : PinDrive::kLow;
        case kHd64461PinModePullUpInput: return PinDrive::kPulledUp;
        default:           return PinDrive::kUndriven;
    }
}

bool Hd64461Gpio::Pcmcia1PinsConnected() const {
    const uint32_t port_c = pins_[kPortC].load(std::memory_order_acquire) >> kControlShift;
    const uint32_t port_d = pins_[kPortD].load(std::memory_order_acquire) >> kControlShift;
    return (port_c & kPortCPcmcia1Outputs) == 0u && (port_d & kPortDPcmcia1Outputs) == 0u;
}

uint8_t Hd64461Gpio::ModePins(uint16_t control, uint32_t mode) const {
    uint8_t pins = 0u;
    for (uint32_t pin = 0; pin < kPins; ++pin) {
        if (((control >> (pin * 2u)) & 3u) == mode) pins |= static_cast<uint8_t>(1u << pin);
    }
    return pins;
}

uint8_t Hd64461Gpio::Pcmcia1Inputs(uint32_t port) const {
    if (port == kPortC) return kPortCPcmcia1Inputs;
    if (port == kPortD) return kPortDPcmcia1Inputs;
    return 0u;
}

uint8_t Hd64461Gpio::Pcmcia1InputLevels(uint32_t port) {
    const Hd64461Pcc::SocketPins p = emu_.Get<Hd64461Pcc>().Pins(kPcmcia1Channel);
    if (port == kPortC) {
        return static_cast<uint8_t>((p.wait ? 0x20u : 0u) | (p.cd1 ? 0x40u : 0u) | (p.cd2 ? 0x80u : 0u));
    }
    return static_cast<uint8_t>((p.vs1 ? 0x01u : 0u) | (p.vs2 ? 0x02u : 0u) | (p.write_protect ? 0x10u : 0u) |
                                (p.ready ? 0x20u : 0u) | (p.bvd1 ? 0x40u : 0u) | (p.bvd2 ? 0x80u : 0u));
}

uint16_t Hd64461Gpio::SampleRegister(uint32_t i, uint32_t addr) {
    (void)addr;
    const uint32_t port    = i - kDataIndex;
    const uint16_t control = StoredReg(port);
    const uint8_t  latch   = static_cast<uint8_t>(StoredReg(i));
    const uint8_t  output  = static_cast<uint8_t>(ModePins(control, kHd64461PinModeFunction2) |
                                                  ModePins(control, kHd64461PinModeOutput));
    const uint8_t  socket  = Pcmcia1Inputs(port);
    uint8_t        nets    = wiring_->NetLevels(port);
    if (socket != 0u) nets = static_cast<uint8_t>((nets & ~socket) | (Pcmcia1InputLevels(port) & socket));
    return static_cast<uint8_t>((latch & output) | (nets & ~output));
}

void Hd64461Gpio::BeforeStore(uint32_t i, uint16_t value) {
    if (i != kPortC && i != kPortD) return;
    const uint8_t driven = static_cast<uint8_t>(ModePins(value, kHd64461PinModeOutput) & Pcmcia1Inputs(i));
    if (driven != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Gpio: GP%cCR write 0x%04X drives PCMCIA1 input pins 0x%02X",
                              static_cast<char>('A' + i), value, driven);
    }
}

void Hd64461Gpio::AfterStore(uint32_t i) {
    (void)i;
    PublishLocked();
    NotifyListeners();
}

void Hd64461Gpio::AfterReset() {
    PublishLocked();
    NotifyListeners();
}

void Hd64461Gpio::PublishLocked() {
    for (uint32_t port = 0; port < kPorts; ++port) {
        pins_[port].store((static_cast<uint32_t>(StoredReg(port)) << kControlShift) |
                              (StoredReg(kDataIndex + port) & 0xFFu),
                          std::memory_order_release);
    }
    wiring_->TrackDrive(*this);
}

void Hd64461Gpio::NotifyListeners() {
    listeners_.Call();
}

}
