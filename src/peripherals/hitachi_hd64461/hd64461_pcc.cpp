#include "hd64461_pcc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_gpio.h"
#include "hd64461_intc.h"
#include "hd64461_socket_wiring.h"
#include "hd64461_system_control.h"

#include <string>

namespace cerfcore {

REGISTER_SERVICE(Hd64461Pcc);

namespace {

constexpr uint32_t kChannels    = 2u;
constexpr uint32_t kChannelRegs = 8u;
constexpr uint32_t kIsr         = 0u;
constexpr uint32_t kGcr         = 1u;
constexpr uint32_t kCscr        = 2u;
constexpr uint32_t kCscier      = 3u;
constexpr uint32_t kScr         = 4u;
constexpr uint32_t kArea6       = 6u;

constexpr uint16_t kIsrReady        = 0x80u;
constexpr uint16_t kIsrWriteProtect = 0x40u;
constexpr uint16_t kIsrVs2          = 0x20u;
constexpr uint16_t kIsrVs1          = 0x10u;
constexpr uint16_t kIsrCd2          = 0x08u;
constexpr uint16_t kIsrCd1          = 0x04u;
constexpr uint16_t kIsrBvd2         = 0x02u;
constexpr uint16_t kIsrBvd1         = 0x01u;

constexpr uint16_t kGcrDriveEnable  = 0x80u;
constexpr uint16_t kGcrCardReset    = 0x40u;
constexpr uint16_t kGcrIoInterface  = 0x20u;
constexpr uint16_t kGcrVccSel0      = 0x10u;
constexpr uint16_t kGcr16MbAreaMode = 0x08u;
constexpr uint16_t kGcrPa25         = 0x04u;
constexpr uint16_t kGcrPa24         = 0x02u;
constexpr uint16_t kGcrReg          = 0x01u;

constexpr uint16_t kCscrSoftwareDetect = 0x80u;
constexpr uint16_t kCscrIreq           = 0x20u;
constexpr uint16_t kCscrStatusChange   = 0x10u;
constexpr uint16_t kCscrDetectChange   = 0x08u;
constexpr uint16_t kCscrReadyChange    = 0x04u;
constexpr uint16_t kCscrBatteryWarning = 0x02u;
constexpr uint16_t kCscrBatteryDead    = 0x01u;
constexpr uint16_t kCscrWriteZeroClear = 0x1Fu;

constexpr uint16_t kCscierSourceEnables = 0x1Fu;
constexpr uint16_t kCscierDetectEnable  = 0x08u;
constexpr uint16_t kCscierIreqModeMask  = 0x60u;
constexpr uint32_t kCscierIreqModeShift = 5u;

constexpr uint32_t kIreqNone        = 0u;
constexpr uint32_t kIreqFallingEdge = 2u;
constexpr uint32_t kIreqRisingEdge  = 3u;

constexpr uint16_t kScrVccSel1      = 0x02u;
constexpr uint16_t kScrWriteProtect = 0x01u;

constexpr uint32_t kBlockShift     = 24u;
constexpr uint32_t kBlockMask      = 0x00FFFFFFu;
constexpr uint32_t kAttributeBlock = 0u;
constexpr uint32_t kIoBlock        = 2u;
constexpr uint32_t kPaShift        = 23u;
constexpr uint32_t kWindow32Mask   = 0x01FFFFFFu;
constexpr uint32_t kCardA25        = 0x02000000u;

}

Hd64461Pcc::Hd64461Pcc(CerfEmulator& emu)
    : RegWindow(emu), slot0_(emu, *this, std::string()), slot1_(emu, *this, std::string()),
      slots_{&slot0_, &slot1_} {}

bool Hd64461Pcc::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461Pcc::OnReady() {
    intc_         = &emu_.Get<Hd64461Intc>();
    gpio_         = &emu_.Get<Hd64461Gpio>();
    wiring_       = &emu_.Get<Hd64461SocketWiring>();
    system_       = &emu_.Get<Hd64461SystemControl>();
    auto& widgets = emu_.Get<HostWidgetRegistry>();
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        slots_[ch]->SetLabel(wiring_->SocketLabel(ch));
        widgets.Register(slots_[ch]);
    }
    RegWindow::OnReady();
    ApplySocketDrive();
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(lock_);
        for (uint32_t ch = 0; ch < kChannels; ++ch) ready_pin_high_[ch] = ReadyPinHighLocked(ch);
    });
    reset.RegisterResetReleaseListener([this] {
        ApplySocketDrive();
        std::lock_guard<std::mutex> lk(lock_);
        for (uint32_t ch = 0; ch < kChannels; ++ch) UpdateRequestLocked(ch);
    });
}

void Hd64461Pcc::OnShutdown() {
    slot0_.OnShutdown();
    slot1_.OnShutdown();
}

void Hd64461Pcc::WriteByte(uint32_t addr, uint8_t value) {
    RegWindow::WriteByte(addr, value);
    ApplySocketDrive();
}

void Hd64461Pcc::WriteHalf(uint32_t addr, uint16_t value) {
    RegWindow::WriteHalf(addr, value);
    ApplySocketDrive();
}

uint32_t Hd64461Pcc::ChannelOf(const PcmciaSlot& slot) const { return &slot == slots_[0] ? 0u : 1u; }

bool Hd64461Pcc::ReadyPinHighLocked(uint32_t ch) const {
    return !(slots_[ch]->IsPowered() && ready_ireq_low_[ch]);
}

bool Hd64461Pcc::IoInterfaceLocked(uint32_t ch) const {
    return ch == 0u && (StoredReg(kGcr) & kGcrIoInterface) != 0u;
}

uint32_t Hd64461Pcc::IreqModeLocked() const {
    return (StoredReg(kCscier) & kCscierIreqModeMask) >> kCscierIreqModeShift;
}

bool Hd64461Pcc::IreqEdgeModeLocked() const {
    const uint32_t mode = IreqModeLocked();
    return mode == kIreqFallingEdge || mode == kIreqRisingEdge;
}

Hd64461Pcc::SocketPins Hd64461Pcc::PinsLocked(uint32_t ch) const {
    const PcmciaSlot& slot    = *slots_[ch];
    const bool        no_card = !slot.HasCard();
    return SocketPins{ReadyPinHighLocked(ch),
                      !IoInterfaceLocked(ch) && !slot.IsPowered(),
                      !slot.Vs2Grounded(),
                      !slot.Vs1Grounded(),
                      no_card,
                      no_card,
                      true,
                      true,
                      true};
}

Hd64461Pcc::SocketPins Hd64461Pcc::Pins(uint32_t ch) {
    std::lock_guard<std::mutex> lk(lock_);
    return PinsLocked(ch);
}

uint16_t Hd64461Pcc::InterfaceStatusLocked(uint32_t ch) const {
    const SocketPins p = PinsLocked(ch);
    uint16_t         v = 0u;
    if (p.ready) v |= kIsrReady;
    if (p.write_protect) v |= kIsrWriteProtect;
    if (p.vs2) v |= kIsrVs2;
    if (p.vs1) v |= kIsrVs1;
    if (p.cd2) v |= kIsrCd2;
    if (p.cd1) v |= kIsrCd1;
    if (p.bvd2) v |= kIsrBvd2;
    if (p.bvd1) v |= kIsrBvd1;
    return v;
}

uint16_t Hd64461Pcc::ChangeStatusLocked(uint32_t ch) const {
    const uint16_t latched = StoredReg(ch * kChannelRegs + kCscr);
    uint16_t       v       = latched & (kCscrSoftwareDetect | kCscrDetectChange);
    if (!IoInterfaceLocked(ch)) {
        return static_cast<uint16_t>(v | (latched & (kCscrReadyChange | kCscrBatteryWarning | kCscrBatteryDead)));
    }
    v |= latched & kCscrStatusChange;
    if (IreqEdgeModeLocked()) return static_cast<uint16_t>(v | (latched & kCscrIreq));
    return ReadyPinHighLocked(ch) ? v : static_cast<uint16_t>(v | kCscrIreq);
}

bool Hd64461Pcc::RequestLocked(uint32_t ch) const {
    const uint16_t status  = ChangeStatusLocked(ch);
    const uint16_t enables = StoredReg(ch * kChannelRegs + kCscier);
    if ((status & enables & kCscierSourceEnables) != 0u) return true;
    if ((status & kCscrSoftwareDetect) != 0u && (enables & kCscierDetectEnable) != 0u) return true;
    return IoInterfaceLocked(ch) && IreqModeLocked() != kIreqNone && (status & kCscrIreq) != 0u;
}

void Hd64461Pcc::LatchLocked(uint32_t ch, uint16_t bits) {
    const uint32_t i = ch * kChannelRegs + kCscr;
    SetStoredReg(i, static_cast<uint16_t>(StoredReg(i) | bits));
}

void Hd64461Pcc::TrackReadyPinLocked(uint32_t ch) {
    const bool high = ReadyPinHighLocked(ch);
    if (high == ready_pin_high_[ch]) return;
    ready_pin_high_[ch] = high;
    if (!IoInterfaceLocked(ch)) {
        if (high) LatchLocked(ch, kCscrReadyChange);
        return;
    }
    const uint32_t mode = IreqModeLocked();
    if ((mode == kIreqFallingEdge && !high) || (mode == kIreqRisingEdge && high)) LatchLocked(ch, kCscrIreq);
}

void Hd64461Pcc::UpdateRequestLocked(uint32_t ch) {
    intc_->SetSourceRequest(ch == 0u ? Hd64461IntcSource::kPcc0 : Hd64461IntcSource::kPcc1, RequestLocked(ch));
}

uint16_t Hd64461Pcc::SampleRegister(uint32_t i, uint32_t addr) {
    (void)addr;
    const uint32_t ch = i / kChannelRegs;
    if (i % kChannelRegs == kIsr) return InterfaceStatusLocked(ch);
    return ChangeStatusLocked(ch);
}

void Hd64461Pcc::WriteHooked(uint32_t i, uint32_t addr, uint16_t value) {
    (void)addr;
    const uint32_t ch     = i / kChannelRegs;
    const uint16_t clears = ch == 0u && IreqEdgeModeLocked() ? static_cast<uint16_t>(kCscrWriteZeroClear | kCscrIreq)
                                                             : kCscrWriteZeroClear;
    const uint16_t old    = StoredReg(i);
    const uint16_t next   = static_cast<uint16_t>((old & ~(clears | kCscrSoftwareDetect)) | (old & value & clears) |
                                                (value & kCscrSoftwareDetect));
    SetStoredReg(i, next);
    UpdateRequestLocked(ch);
}

void Hd64461Pcc::AfterStore(uint32_t i) {
    (void)i;
    for (uint32_t ch = 0; ch < kChannels; ++ch) UpdateRequestLocked(ch);
}

Hd64461Pcc::SocketDrive Hd64461Pcc::SocketDriveLocked(uint32_t ch) const {
    const uint32_t base = ch * kChannelRegs;
    const uint16_t gcr  = StoredReg(base + kGcr);
    const bool     sel0 = (gcr & kGcrVccSel0) != 0u;
    const bool     sel1 = (StoredReg(base + kScr) & kScrVccSel1) != 0u;
    return SocketDrive{wiring_->SocketPowered(ch, sel0, sel1), (gcr & kGcrCardReset) != 0u,
                       (gcr & kGcrDriveEnable) != 0u};
}

void Hd64461Pcc::ApplySocketDrive() {
    SocketDrive drive[kChannels];
    {
        std::lock_guard<std::mutex> lk(lock_);
        for (uint32_t ch = 0; ch < kChannels; ++ch) drive[ch] = SocketDriveLocked(ch);
    }
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        slots_[ch]->SetPowered(drive[ch].powered);
        slots_[ch]->SetResetLine(drive[ch].reset);
        slots_[ch]->SetCardEnabled(drive[ch].enabled);
    }
}

Hd64461Pcc::CardCycle Hd64461Pcc::DecodeCycle(uint32_t area, uint32_t offset, bool io, bool write) {
    const uint32_t              ch   = area == kArea6 ? 0u : 1u;
    const uint32_t              base = ch * kChannelRegs;
    if (!system_->PccChannelClockRunning(ch)) {
        emu_.Get<Fatal>().Die("Hd64461Pcc: card cycle at offset 0x%08X to PCC%u while the channel is in standby", offset,
                              ch);
    }
    if (ch == 1u && !gpio_->Pcmcia1PinsConnected()) {
        emu_.Get<Fatal>().Die("Hd64461Pcc: card cycle at offset 0x%08X to PCC1 while GPCCR or GPDCR puts a PCC1 "
                              "output pin in port mode", offset);
    }
    std::lock_guard<std::mutex> lk(lock_);
    const uint16_t              gcr        = StoredReg(base + kGcr);
    const bool     sixteen_mb = (gcr & kGcr16MbAreaMode) != 0u;
    const bool     protect    = write && (StoredReg(base + kScr) & kScrWriteProtect) != 0u;
    CardCycle      c{slots_[ch], 0u, false, false};
    if (io) {
        if (!IoInterfaceLocked(ch)) {
            emu_.Get<Fatal>().Die("Hd64461Pcc: I/O cycle at offset 0x%08X to PCC%u in the IC memory card "
                                  "interface", offset, ch);
        }
        if (sixteen_mb && (offset >> kBlockShift) != kIoBlock) {
            emu_.Get<Fatal>().Die("Hd64461Pcc: I/O cycle at offset 0x%08X in the unused 16 MB block of PCC%u",
                                  offset, ch);
        }
        if (protect) {
            emu_.Get<Fatal>().Die("Hd64461Pcc: I/O write at offset 0x%08X to PCC%u with SCR write protect set",
                                  offset, ch);
        }
        c.card_address = offset & (sixteen_mb ? kBlockMask : kWindow32Mask);
        return c;
    }
    if (sixteen_mb) {
        c.attribute    = (offset >> kBlockShift) == kAttributeBlock;
        c.card_address = offset & kBlockMask;
        if (!c.attribute) c.card_address |= static_cast<uint32_t>(gcr & (kGcrPa25 | kGcrPa24)) << kPaShift;
    } else {
        c.attribute    = (gcr & kGcrReg) == 0u;
        c.card_address = offset & kWindow32Mask;
        if (!c.attribute && (gcr & kGcrPa25) != 0u) c.card_address |= kCardA25;
    }
    c.dropped = protect;
    return c;
}

void Hd64461Pcc::OnCardDetectChanged(PcmciaSlot& slot) {
    const uint32_t              ch = ChannelOf(slot);
    std::lock_guard<std::mutex> lk(lock_);
    LatchLocked(ch, kCscrDetectChange);
    TrackReadyPinLocked(ch);
    UpdateRequestLocked(ch);
}

void Hd64461Pcc::OnReadyIreqLow(PcmciaSlot& slot) { SetReadyIreqLow(slot, true); }

void Hd64461Pcc::OnReadyIreqHigh(PcmciaSlot& slot) { SetReadyIreqLow(slot, false); }

void Hd64461Pcc::SetReadyIreqLow(PcmciaSlot& slot, bool low) {
    const uint32_t              ch = ChannelOf(slot);
    std::lock_guard<std::mutex> lk(lock_);
    ready_ireq_low_[ch] = low;
    TrackReadyPinLocked(ch);
    UpdateRequestLocked(ch);
}

void Hd64461Pcc::OnCardPowerChanged(PcmciaSlot& slot) {
    const uint32_t              ch = ChannelOf(slot);
    std::lock_guard<std::mutex> lk(lock_);
    TrackReadyPinLocked(ch);
    UpdateRequestLocked(ch);
}

void Hd64461Pcc::SaveState(StateWriter& w) {
    RegWindow::SaveState(w);
    {
        std::lock_guard<std::mutex> lk(lock_);
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            w.Write<uint8_t>("ready_ireq_low", ready_ireq_low_[ch] ? 1u : 0u);
            w.Write<uint8_t>("ready_pin_high", ready_pin_high_[ch] ? 1u : 0u);
        }
    }
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        w.BeginFrame(ch);
        slots_[ch]->SaveSlotState(w);
        w.EndFrame();
    }
}

void Hd64461Pcc::RestoreState(StateReader& r) {
    RegWindow::RestoreState(r);
    {
        std::lock_guard<std::mutex> lk(lock_);
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            uint8_t low  = 0u;
            uint8_t high = 0u;
            r.Read("ready_ireq_low", low);
            r.Read("ready_pin_high", high);
            ready_ireq_low_[ch] = low != 0u;
            ready_pin_high_[ch] = high != 0u;
        }
    }
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        if (const uint32_t frame = r.EnterFrame(); frame != ch) {
            r.Reject("PC Card socket frame 0x%X where this build writes socket %u", frame, ch);
        }
        slots_[ch]->RestoreSlotState(r);
        r.LeaveFrame();
    }
}

void Hd64461Pcc::PostRestore() {
    ApplySocketDrive();
    for (uint32_t ch = 0; ch < kChannels; ++ch) slots_[ch]->PostRestoreSlot();
    std::lock_guard<std::mutex> lk(lock_);
    for (uint32_t ch = 0; ch < kChannels; ++ch) UpdateRequestLocked(ch);
}

}
