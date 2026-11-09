#include "nec_vrc4171a.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/compaq_aero_1500/compaq_aero_1500_family_id.h"
#include "../../boards/ibm_workpad_z50/ibm_workpad_z50_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kPortTopLevelFirst = 0x5DCu;
constexpr uint32_t kPortGpioPcs = 0x5EEu;
constexpr uint32_t kPortInterruptStatus = 0x5FAu;
constexpr uint32_t kPortConfig2 = 0x5FCu;
constexpr uint32_t kPortConfig1 = 0x5FEu;

constexpr uint16_t kConfig1Reset      = 0x1000u;
constexpr uint16_t kConfig1Reserved   = 0x0407u;
constexpr uint16_t kConfig1SlotB      = 0xC000u;
constexpr uint16_t kConfig1SlotBNone  = 0x0000u;
constexpr uint16_t kConfig1SlotBPcCard = 0x4000u;
constexpr uint16_t kConfig1SlotBCompactFlash = 0x8000u;
constexpr uint16_t kConfig1IoUpperDecode = 0x0200u;
constexpr uint16_t kConfig1EnableVclk = 0x1000u;
constexpr uint16_t kConfig1ExternalOr = 0x2000u;
constexpr uint16_t kConfig2Reset      = 0x2000u;
constexpr uint16_t kConfig2SiliconRevision = 0xF000u;
constexpr uint16_t kConfig2Writable   = 0x040Eu;
constexpr uint16_t kConfig2Reserved   = 0x0BF1u;

constexpr uint16_t kGpioPcsUnmodeled = 0xFF00u;
constexpr uint16_t kGpioDirShift     = 4u;
constexpr uint16_t kGpioDataMask     = 0x000Fu;

constexpr uint16_t kIntStatusSlotA = 0x0002u;
constexpr uint16_t kIntStatusSlotB = 0x0004u;

constexpr uint8_t kRegIdentification   = 0x00u;
constexpr uint8_t kRegCardDetectControl = 0x16u;
constexpr uint8_t kRegGlobalControl    = 0x1Eu;
constexpr uint8_t kRegVoltageSense     = 0x1Fu;
constexpr uint8_t kRegVoltageSelect    = 0x2Fu;
constexpr uint8_t kRegProbe3B          = 0x3Bu;
constexpr uint8_t kRegProbe3C          = 0x3Cu;
constexpr uint8_t kRegGap17            = 0x17u;
constexpr uint8_t kRegGap26            = 0x26u;
constexpr uint8_t kRegGap27            = 0x27u;
constexpr uint8_t kRegGap2E            = 0x2Eu;
constexpr uint8_t kGapReadValue        = 0x00u;

constexpr uint8_t kIdentification = 0x83u;
constexpr uint8_t kProbe3BValue   = 0x30u;
constexpr uint8_t kProbe3CValue   = 0x04u;

constexpr uint8_t kCdcReserved          = 0xCCu;
constexpr uint8_t kCdcSoftwareCardDetect = 0x20u;
constexpr uint8_t kCdcCardDetectResume  = 0x10u;
constexpr uint8_t kCdcConfigurationReset = 0x02u;

constexpr uint8_t kGlobalReserved          = 0xE3u;
constexpr uint8_t kGlobalClearPulseIrq     = 0x10u;
constexpr uint8_t kGlobalPulseMode         = 0x08u;
constexpr uint8_t kGlobalExplicitWriteBack = 0x04u;

constexpr uint8_t kVoltageSelectReserved = 0xFCu;
constexpr uint8_t kVoltageSelectXv       = 0x02u;
constexpr uint8_t kVoltageSenseVs1       = 0x01u;
constexpr uint8_t kVoltageSenseVs2       = 0x02u;

constexpr uint8_t kIfsReservedHigh = 0x80u;
constexpr uint8_t kIfsCardPowerOn  = 0x40u;
constexpr uint8_t kVoltageSenseReserved = 0xFCu;
constexpr uint8_t kStatusChangeCardDetect = 0x08u;
constexpr uint8_t kNoIrqSelect = 0x00u;

constexpr unsigned kMemcs16FullDecode = 12u;

constexpr ExcaPartRules kExcaRules = [] {
    ExcaPartRules rules{};
    rules.ready_change_any_edge = false;
    rules.latch_requires_enable = false;
    rules.status_read_masked_by_enable = true;
    rules.auto_power = false;
    rules.card_inputs_high_without_vcc = false;
    rules.power_control_reserved = 0x6Cu;
    rules.power_reserved_field_a = 0x00u;
    rules.power_reserved_code_a = 0x00u;
    rules.power_reserved_field_b = 0x00u;
    rules.power_reserved_code_b = 0x00u;
    rules.window_enable_reserved = 0x20u;
    rules.memory_end_high_reserved = 0x00u;
    rules.io_offset_low_reserved = 0x00u;
    rules.window_page_registers = false;
    rules.socket_control_power = false;
    rules.stschg_change_any_edge = false;
    rules.battery_dead_pin = ExcaBatteryDeadPin::kBvd1Low;
    rules.battery_warning = ExcaBatteryWarning::kEnterBvd1HighBvd2Low;
    rules.outputs_require_card = false;
    rules.ring_indicate_masks_status_change = true;
    rules.irq_select_reserved = 0x2146u;
    rules.io_offset_registers = false;
    rules.memory_address_high_mask = 0x3Fu;
    rules.interrupt_control_reserved = 0x10u;
    rules.interrupt_control_reset = 0x03u;
    rules.removal_resets_interrupt_control = 0xAFu;
    rules.removal_resets_power_control = true;
    rules.removal_resets_window_enable = true;
    rules.vcc_requires_card = true;
    rules.io_control_reserved = 0x88u;
    rules.reserved_bits_halt = true;
    rules.memory_start_high_reserved = 0x40u;
    rules.status_change_reserved = 0xF0u;
    return rules;
}();

}

NecVrc4171a::NecVrc4171a(CerfEmulator& emu)
    : IsaExcaSocketPair(emu, kExcaRules),
      config1_(kConfig1Reset),
      config2_(kConfig2Reset) {}

bool NecVrc4171a::FittedOnFamily(std::string_view family) {
    return family == BoardFamilyId::IbmWorkpadZ50 || family == BoardFamilyId::CompaqAero1500;
}

bool NecVrc4171a::IsTopLevelPort(uint32_t port) {
    return port >= kPortTopLevelFirst && port <= kPortConfig1;
}

bool NecVrc4171a::ShouldRegister() {
    return FittedOnFamily(emu_.Get<BoardContext>().GetBoardFamilyId());
}

void NecVrc4171a::OnReady() {
    board_ = &emu_.Get<NecVrc4171aBoard>();
    StartSockets(board_->SlotALabel(), SlotBLabel());
}

std::optional<std::string> NecVrc4171a::SlotBLabel() {
    switch (board_->SlotBConfiguration()) {
        case kConfig1SlotBNone:         return std::nullopt;
        case kConfig1SlotBPcCard:       return std::string("PC Card");
        case kConfig1SlotBCompactFlash: return std::string("CompactFlash");
        default:
            emu_.Get<Fatal>().Die("[VRC4171A] the board wires slot B as a Miniature Card socket, which is not "
                                  "modeled");
    }
}

bool NecVrc4171a::IoUpperDecode() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return (config1_ & kConfig1IoUpperDecode) != 0u;
}

bool NecVrc4171a::DisplayClocksEnabled() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return (config1_ & kConfig1EnableVclk) != 0u;
}

uint16_t NecVrc4171a::ReadTopLevel(uint32_t port) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (port) {
        case kPortConfig1: return config1_;
        case kPortConfig2: return config2_;
        case kPortInterruptStatus: return InterruptStatusLocked();
        case kPortGpioPcs: {
            const uint16_t outputs = static_cast<uint16_t>((gpio_pcs_ >> kGpioDirShift) & kGpioDataMask);
            const uint16_t input_mask = static_cast<uint16_t>(~outputs & kGpioDataMask);
            const uint16_t inputs =
                input_mask != 0u ? static_cast<uint16_t>(board_->GpioInputLevels() & input_mask) : 0u;
            return static_cast<uint16_t>((gpio_pcs_ & ~kGpioDataMask) | (gpio_pcs_ & outputs) | inputs);
        }
        default:
            emu_.Get<Fatal>().Die("[VRC4171A] read of top-level register 0x%03X, a GPIO/*PCS register the model "
                                  "does not cover", port);
    }
}

void NecVrc4171a::WriteTopLevel(uint32_t port, uint16_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (port) {
        case kPortConfig1:
            if ((value & kConfig1Reserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] Configuration I write 0x%04X sets reserved bits", value);
            if ((value & kConfig1ExternalOr) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] Configuration I write 0x%04X enables the external OR gate, whose "
                                      "board wiring is not modeled", value);
            if ((value & kConfig1SlotB) != board_->SlotBConfiguration())
                emu_.Get<Fatal>().Die("[VRC4171A] Configuration I write 0x%04X selects slot B type 0x%04X; the board "
                                      "wires 0x%04X", value, value & kConfig1SlotB, board_->SlotBConfiguration());
            config1_ = value;
            return;
        case kPortConfig2:
            if ((value & kConfig2Reserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] Configuration II write 0x%04X sets reserved bits", value);
            config2_ = static_cast<uint16_t>((kConfig2Reset & kConfig2SiliconRevision) | (value & kConfig2Writable));
            return;
        case kPortGpioPcs:
            if ((value & kGpioPcsUnmodeled) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] GPIO/*PCS write 0x%04X sets *PCS or reserved bits", value);
            if (!board_->GpioPinsWired())
                emu_.Get<Fatal>().Die("[VRC4171A] GPIO/*PCS write 0x%04X on a board whose GPIO pin wiring is "
                                      "not modeled", value);
            gpio_pcs_ = value;
            return;
        default:
            emu_.Get<Fatal>().Die("[VRC4171A] write 0x%04X to top-level register 0x%03X, which the model does not "
                                  "cover", value, port);
    }
}

uint8_t NecVrc4171a::UnbankedIndexReadLocked(uint32_t port) {
    emu_.Get<Fatal>().Die("[VRC4171A] read of the index register at port 0x%03X holding INDEX 0x%02X, past the "
                          "documented index range", port, IndexLocked());
}

uint8_t NecVrc4171a::UnbankedDataReadLocked(uint32_t port) {
    emu_.Get<Fatal>().Die("[VRC4171A] data read at port 0x%03X with INDEX 0x%02X, past the documented index range",
                          port, IndexLocked());
}

void NecVrc4171a::UnbankedDataWriteLocked(uint8_t value) {
    emu_.Get<Fatal>().Die("[VRC4171A] data write 0x%02X with INDEX 0x%02X, past the documented index range", value,
                          IndexLocked());
}

bool NecVrc4171a::ChipOwns(uint8_t sub) const {
    switch (sub) {
        case kRegIdentification:
        case kRegCardDetectControl:
        case kRegGlobalControl:
        case kRegVoltageSense:
        case kRegVoltageSelect:
        case kRegProbe3B:
        case kRegProbe3C:
        case kRegGap17:
        case kRegGap26:
        case kRegGap27:
        case kRegGap2E:
            return true;
        default:
            return false;
    }
}

bool NecVrc4171a::ChipReadLocked(int bank, uint8_t sub, uint8_t* value) {
    switch (sub) {
        case kRegIdentification:
            *value = kIdentification;
            return true;
        case kRegCardDetectControl:
            *value = card_detect_control_[bank];
            return true;
        case kRegGlobalControl:
            *value = global_control_[bank];
            return true;
        case kRegVoltageSense: {
            if (!SocketWired(bank))
                emu_.Get<Fatal>().Die("[VRC4171A] read of INDEX 0x%02X, the voltage sense pins of a socket the "
                                      "board does not wire", IndexLocked());
            const bool pulled_up = !board_->SensePullupsOnCardVcc(bank) ||
                                   (Exca(bank).InterfaceStatus() & kIfsCardPowerOn) != 0u;
            if (!pulled_up) {
                *value = 0u;
                return true;
            }
            *value = static_cast<uint8_t>((Slot(bank).Vs1Grounded() ? 0u : kVoltageSenseVs1) |
                                          (Slot(bank).Vs2Grounded() ? 0u : kVoltageSenseVs2));
            return true;
        }
        case kRegVoltageSelect:
            *value = voltage_select_[bank];
            return true;
        case kRegGap17:
        case kRegGap26:
        case kRegGap27:
        case kRegGap2E:
            *value = kGapReadValue;
            return true;
        case kRegProbe3B:
        case kRegProbe3C:
            emu_.Get<Fatal>().Die("[VRC4171A] read of INDEX 0x%02X, which the VRC4171A does not document",
                                  IndexLocked());
        default:
            return false;
    }
}

void NecVrc4171a::ChipWriteLocked(int bank, uint8_t sub, uint8_t value, ExcaWriteEffect*) {
    switch (sub) {
        case kRegCardDetectControl:
            if ((value & kCdcReserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X value 0x%02X sets reserved bits", IndexLocked(), value);
            card_detect_control_[bank] = static_cast<uint8_t>(value & ~kCdcSoftwareCardDetect);
            if ((value & kCdcSoftwareCardDetect) != 0u) Exca(bank).SoftwareCardDetect();
            break;
        case kRegGlobalControl:
            if ((value & kGlobalReserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X value 0x%02X sets reserved bits", IndexLocked(), value);
            if ((value & kGlobalPulseMode) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X value 0x%02X enables IRQ pulse mode, which the model "
                                      "does not cover", IndexLocked(), value);
            global_control_[bank] = static_cast<uint8_t>(value & ~kGlobalClearPulseIrq);
            break;
        case kRegVoltageSelect:
            if ((value & kVoltageSelectReserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X value 0x%02X sets reserved bits", IndexLocked(), value);
            if (value == kVoltageSelectXv)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X selects the X.X V supply, whose board wiring is not "
                                      "modeled", IndexLocked());
            voltage_select_[bank] = value;
            break;
        case kRegProbe3B:
            if (value != kProbe3BValue)
                emu_.Get<Fatal>().Die("[VRC4171A] write 0x%02X to INDEX 0x%02X, which the VRC4171A does not document",
                                      value, IndexLocked());
            break;
        case kRegProbe3C:
            if (bank != 0 || value != kProbe3CValue)
                emu_.Get<Fatal>().Die("[VRC4171A] write 0x%02X to INDEX 0x%02X, which the VRC4171A does not document",
                                      value, IndexLocked());
            break;
        case kRegVoltageSense:
            if ((value & kVoltageSenseReserved) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A] INDEX 0x%02X value 0x%02X sets reserved bits", IndexLocked(), value);
            break;
        case kRegIdentification:
        case kRegGap17:
        case kRegGap26:
        case kRegGap27:
        case kRegGap2E:
            break;
    }
    ApplyChipModesLocked();
}

uint8_t NecVrc4171a::InterfaceStatusLocked(int, uint8_t exca_status) {
    return static_cast<uint8_t>(exca_status | kIfsReservedHigh);
}

void NecVrc4171a::ResetChipLocked() {
    config1_ = kConfig1Reset;
    config2_ = kConfig2Reset;
    gpio_pcs_ = 0u;
    for (int b = 0; b < kSockets; ++b) {
        card_detect_control_[b] = 0u;
        global_control_[b] = 0u;
        voltage_select_[b] = 0u;
    }
    ApplyChipModesLocked();
}

void NecVrc4171a::CardDetectLocked(int bank) {
    if (!Slot(bank).HasCard()) voltage_select_[bank] = 0u;
}

void NecVrc4171a::ApplyChipModesLocked() {
    for (int b = 0; b < kSockets; ++b) {
        Exca(b).SetExplicitStatusClear((global_control_[b] & kGlobalExplicitWriteBack) != 0u);
        Exca(b).SetConfigurationResetOnRemoval((card_detect_control_[b] & kCdcConfigurationReset) != 0u);
    }
}

bool NecVrc4171a::Memcs16AssertedLocked(uint32_t off) {
    for (int b = 0; b < kSockets; ++b) {
        if (Exca(b).SixteenBitWindowDecodes(off, kMemcs16FullDecode)) return true;
    }
    return false;
}

uint16_t NecVrc4171a::InterruptStatusLocked() {
    uint16_t status = 0u;
    for (int b = 0; b < kSockets; ++b) {
        Pcic82365& exca = Exca(b);
        const uint16_t slot_bit = b == 0 ? kIntStatusSlotA : kIntStatusSlotB;
        if (exca.CscIrqSelect() != kNoIrqSelect && exca.StatusChangeInterruptActive())
            status = static_cast<uint16_t>(status | slot_bit | (1u << exca.CscIrqSelect()));
        if (exca.IoCardInterface() && exca.CardIrqSelect() != kNoIrqSelect && exca.IreqPinLow())
            status = static_cast<uint16_t>(status | slot_bit | (1u << exca.CardIrqSelect()));
    }
    return status;
}

void NecVrc4171a::UpdateIrqLineLocked() {
    for (int b = 0; b < kSockets; ++b) {
        if ((card_detect_control_[b] & kCdcCardDetectResume) != 0u &&
            (Exca(b).ActiveStatusChangeBits() & kStatusChangeCardDetect) != 0u)
            emu_.Get<Fatal>().Die("[VRC4171A] socket %d card detect resume drives *RIO, whose board connection is "
                                  "not modeled", b);
    }
    board_->SetIrqLevel(InterruptStatusLocked() != 0u);
}

void NecVrc4171a::SaveChipGlobal(StateWriter& w) const {
    w.Write("config1", config1_);
    w.Write("config2", config2_);
    w.Write("gpio_pcs", gpio_pcs_);
}

void NecVrc4171a::RestoreChipGlobal(StateReader& r) {
    r.Read("config1", config1_);
    r.Read("config2", config2_);
    r.Read("gpio_pcs", gpio_pcs_);
}

void NecVrc4171a::SaveChipBank(StateWriter& w, int bank) const {
    w.Write("card_detect_control", card_detect_control_[bank]);
    w.Write("global_control", global_control_[bank]);
    w.Write("voltage_select", voltage_select_[bank]);
}

void NecVrc4171a::RestoreChipBank(StateReader& r, int bank) {
    r.Read("card_detect_control", card_detect_control_[bank]);
    r.Read("global_control", global_control_[bank]);
    r.Read("voltage_select", voltage_select_[bank]);
}

void NecVrc4171a::AfterRestoreLocked() {
    ApplyChipModesLocked();
}

REGISTER_SERVICE(NecVrc4171a);

}
