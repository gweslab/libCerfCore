#include "ricoh_rf5c396.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/nec_cronos3e/nec_cronos3e_family_id.h"
#include "../../boards/nec_mobilepro_700/nec_mobilepro_700_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

namespace {

constexpr uint8_t kIfsGpiInverted = 0x80u;

constexpr uint8_t kIntrCscToIntr    = 0x10u;
constexpr uint8_t kNoIrqSelect      = 0x00u;
constexpr uint8_t kStatusChangeCardDetect = 0x08u;
constexpr uint8_t kDataBit7         = 0x80u;

constexpr bool kUndrivenIrqNetLevel = true;

constexpr ExcaPartRules kExcaRules = [] {
    ExcaPartRules rules{};
    rules.ready_change_any_edge = false;
    rules.latch_requires_enable = false;
    rules.status_read_masked_by_enable = false;
    rules.auto_power = true;
    rules.card_inputs_high_without_vcc = true;
    rules.power_control_reserved = 0x00u;
    rules.power_reserved_field_a = 0x13u;
    rules.power_reserved_code_a = 0x13u;
    rules.power_reserved_field_b = 0x00u;
    rules.power_reserved_code_b = 0x00u;
    rules.window_enable_reserved = 0x00u;
    rules.memory_end_high_reserved = 0x00u;
    rules.io_offset_low_reserved = 0x01u;
    rules.window_page_registers = false;
    rules.socket_control_power = false;
    rules.stschg_change_any_edge = false;
    rules.battery_dead_pin = ExcaBatteryDeadPin::kBvd1Low;
    rules.battery_warning = ExcaBatteryWarning::kEnterBvd1HighBvd2Low;
    rules.outputs_require_card = false;
    rules.ring_indicate_masks_status_change = true;
    rules.irq_select_reserved = 0x2146u;
    rules.io_offset_registers = true;
    rules.memory_address_high_mask = 0x0Fu;
    return rules;
}();

constexpr uint8_t  kWinMemcs16FullDecode = 0x20u;
constexpr unsigned kMemcs16UpperDecode   = 17u;
constexpr unsigned kMemcs16FullDecode    = 12u;

}

RicohRf5c396::RicohRf5c396(CerfEmulator& emu)
    : IsaExcaSocketPair(emu, kExcaRules, "PCMCIA #1", "PCMCIA #2") {}

bool RicohRf5c396::FittedOnFamily(std::string_view family) {
    return family == BoardFamilyId::NecMobilepro700 || family == BoardFamilyId::NecCronos3e;
}

bool RicohRf5c396::ShouldRegister() {
    return FittedOnFamily(emu_.Get<BoardContext>().GetBoardFamilyId());
}

void RicohRf5c396::OnReady() {
    board_ = &emu_.Get<RicohRf5c396Board>();
    StartSockets();
}

void RicohRf5c396::ResetChipLocked() {
    chip_.Reset();
    ApplyChipModesLocked();
    TrackGpiLocked(0);
    TrackGpiLocked(1);
}

void RicohRf5c396::ApplyChipModesLocked() {
    for (int b = 0; b < kSockets; ++b) {
        Exca(b).SetExplicitStatusClear(chip_.ExplicitStatusClear());
        Exca(b).SetConfigurationResetOnRemoval(chip_.ConfigurationResetOnRemoval(b));
    }
}

bool RicohRf5c396::GpiLowLocked(int bank) {
    return board_->GpiWiredToVs1(bank) && Slot(bank).Vs1Grounded();
}

void RicohRf5c396::TrackGpiLocked(int bank) {
    const bool low = GpiLowLocked(bank);
    if (low == gpi_low_[bank]) return;
    gpi_low_[bank] = low;
    if (chip_.GpiEnabled(bank) && low != chip_.GpiInterruptOnRise(bank))
        Exca(bank).LatchGpiChange();
}

uint8_t RicohRf5c396::UnbankedIndexReadLocked(uint32_t port) {
    return Floating().Read8("RF5C396 index register", port);
}

uint8_t RicohRf5c396::UnbankedDataReadLocked(uint32_t port) {
    return Floating().Read8("RF5C396 data register", port);
}

bool RicohRf5c396::ChipReadLocked(int bank, uint8_t sub, uint8_t* value) {
    return chip_.Read(bank, sub, value);
}

uint8_t RicohRf5c396::InterfaceStatusLocked(int bank, uint8_t exca_status) {
    return GpiLowLocked(bank) ? static_cast<uint8_t>(exca_status | kIfsGpiInverted) : exca_status;
}

void RicohRf5c396::ChipWriteLocked(int bank, uint8_t sub, uint8_t value, ExcaWriteEffect* effect) {
    using Regs = RicohRf5c396ChipRegisters;
    if (const char* refused = chip_.Write(bank, sub, value))
        emu_.Get<Fatal>().Die("[RF5C396] INDEX 0x%02X value 0x%02X %s", IndexLocked(), value, refused);
    if (sub == Regs::kRegCardDetectControl) {
        if ((value & Regs::kCdcSoftwareCardDetect) != 0u) Exca(bank).SoftwareCardDetect();
        if (!chip_.GpiEnabled(bank)) Exca(bank).ClearGpiChange();
    }
    ApplyChipModesLocked();
    if (sub == Regs::kRegModeControl3)
        Exca(bank).SetInterfaceIsolated(chip_.InterfaceDisabled(bank), effect);
}

uint32_t RicohRf5c396::CardAddressLocked(int bank, uint32_t card_addr) const {
    return chip_.CardAddressHighLines(bank, card_addr);
}

bool RicohRf5c396::Memcs16AssertedLocked(uint32_t off) {
    for (int b = 0; b < kSockets; ++b) {
        const unsigned shift = (Exca(b).WindowEnable() & kWinMemcs16FullDecode) != 0u
            ? kMemcs16FullDecode : kMemcs16UpperDecode;
        if (Exca(b).SixteenBitWindowDecodes(off, shift)) return true;
    }
    return false;
}

uint32_t RicohRf5c396::MaskIoReadLocked(int bank, uint32_t off, uint32_t value, unsigned bytes) {
    for (unsigned lane = 0; lane < bytes; ++lane) {
        if (!chip_.DisablesDataBit7(bank, off + lane)) continue;
        const uint32_t bit = static_cast<uint32_t>(kDataBit7) << (8u * lane);
        const uint32_t floating = static_cast<uint32_t>(Floating().Read8("RF5C396 data bit 7", off + lane)) << (8u * lane);
        value = (value & ~bit) | (floating & bit);
    }
    return value;
}

bool RicohRf5c396::CardInterruptLocked(int bank) {
    Pcic82365& exca = Exca(bank);
    return chip_.IreqActiveHigh(bank) ? exca.ReadyPin() : exca.IreqPinLow();
}

void RicohRf5c396::UpdateIrqLineLocked() {
    const uint8_t wired = board_->WiredIrqSelect();
    const bool edge = !chip_.LevelInterrupts();
    bool steered = false;
    bool pending = false;
    for (int b = 0; b < kSockets; ++b) {
        Pcic82365& exca = Exca(b);
        if (chip_.CardDetectResume(b) && (exca.ActiveStatusChangeBits() & kStatusChangeCardDetect) != 0u)
            emu_.Get<Fatal>().Die("[RF5C396] socket %d card detect resume drives RI_OUT#, whose board "
                                  "connection is not modeled", b);
        const bool csc_active = !chip_.CardDetectResume(b) && exca.StatusChangeInterruptActive();
        if ((exca.InterruptAndGeneralControl() & kIntrCscToIntr) != 0u) {
            emu_.Get<Fatal>().Die("[RF5C396] socket %d status change interrupt routed to INTR#, whose board "
                                  "connection is not modeled", b);
        } else if (exca.CscIrqSelect() == wired) {
            steered = true;
            pending = pending || csc_active;
        } else if (exca.CscIrqSelect() != kNoIrqSelect && (edge || csc_active)) {
            emu_.Get<Fatal>().Die("[RF5C396] socket %d status change interrupt routed to IRQ%u, whose board "
                                  "connection is not modeled", b, exca.CscIrqSelect());
        }
        if (!exca.IoCardInterface())
            continue;
        const bool card_active = CardInterruptLocked(b);
        if (exca.CardIrqSelect() == wired) {
            steered = true;
            pending = pending || card_active;
        } else if (exca.CardIrqSelect() != kNoIrqSelect && (edge || card_active)) {
            emu_.Get<Fatal>().Die("[RF5C396] socket %d card interrupt routed to IRQ%u, whose board "
                                  "connection is not modeled", b, exca.CardIrqSelect());
        }
    }
    if (!edge) {
        board_->SetIrqLevel(pending ? false : kUndrivenIrqNetLevel);
        return;
    }
    board_->SetIrqLevel(pending || !steered);
}

void RicohRf5c396::AfterRestoreLocked() {
    ApplyChipModesLocked();
    gpi_low_[0] = GpiLowLocked(0);
    gpi_low_[1] = GpiLowLocked(1);
}

REGISTER_SERVICE(RicohRf5c396);

}
