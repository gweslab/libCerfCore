#include "hd64461_lcdc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/cpu/memory_kind.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_system_control.h"

#include <algorithm>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(Hd64461Lcdc);

namespace {

constexpr uint32_t kLcdcbar = 0u;
constexpr uint32_t kLcdclor = 1u;
constexpr uint32_t kLcdccr  = 2u;
constexpr uint32_t kLdr1    = 8u;
constexpr uint32_t kLdr2    = 9u;
constexpr uint32_t kLdhncr  = 10u;
constexpr uint32_t kLdvndr  = 13u;
constexpr uint32_t kLdr3    = 15u;
constexpr uint32_t kCptwar  = 24u;
constexpr uint32_t kCptwdr  = 25u;
constexpr uint32_t kCptrar  = 26u;

constexpr uint16_t kCcrControl      = 0x01DFu;
constexpr uint16_t kCcrStback       = 0x0400u;
constexpr uint16_t kCcrStreq        = 0x0100u;
constexpr uint16_t kCcrRefsel       = 0x0040u;
constexpr uint16_t kCcrEpon         = 0x0020u;
constexpr uint16_t kCcrSpon         = 0x0010u;
constexpr uint16_t kDspselMask      = 0x0007u;
constexpr uint16_t kDspselLcd       = 0x0001u;
constexpr uint16_t kDspselCrt       = 0x0002u;
constexpr uint16_t kDspselLcdAndCrt = 0x0004u;
constexpr uint16_t kLdr1Don         = 0x0001u;
constexpr uint16_t kLdr1Dinv        = 0x0100u;
constexpr uint16_t kLmMono4Bit      = 0x0000u;
constexpr uint16_t kLmMono8Bit      = 0x0001u;
constexpr uint16_t kLmColor         = 0x0004u;

constexpr uint32_t kVramPa = 0x12000000u;

constexpr uint32_t kCgStorageBits[16] = {1u, 2u, 4u, 8u, 8u, 0u, 0u, 0u, 16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

}

bool Hd64461Lcdc::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461Lcdc::OnReady() {
    Hd64461Panel& panel = emu_.Get<Hd64461Panel>();
    panel_              = &panel;
    panel.SetPowerListener([this] { OnPanelPowerChanged(); });
    system_                = &emu_.Get<Hd64461SystemControl>();
    EmulatedMemory& memory = emu_.Get<EmulatedMemory>();
    memory.AddRegion(kVramPa, kDisplayMemoryBytes, MemoryKind::kRam);
    vram_ = memory.TryTranslate(kVramPa);
    RegWindow::OnReady();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc) return;
        std::lock_guard<std::mutex> lk(lock_);
        write_index_  = 0u;
        write_phase_  = 0u;
        read_index_   = 0u;
        read_phase_   = 0u;
        epon_         = false;
        refresh_lost_ = false;
        palette_defined_.fill(0u);
        mode_latch_.Publish(emu_, false, 0u, 0u);
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        std::lock_guard<std::mutex> lk(lock_);
        ApplyStandby();
    });
}

void Hd64461Lcdc::OnModuleClockChanged() {
    std::lock_guard<std::mutex> lk(lock_);
    ApplyStandby();
    PublishPresentingLocked();
}

void Hd64461Lcdc::OnPanelPowerChanged() {
    std::lock_guard<std::mutex> lk(lock_);
    PublishPresentingLocked();
}

void Hd64461Lcdc::PostRestore() {
    std::lock_guard<std::mutex> lk(lock_);
    ApplyStandby();
}

void Hd64461Lcdc::ApplyStandby() {
    const uint16_t ccr     = StoredReg(kLcdccr);
    const bool     standby = (ccr & kCcrStreq) != 0u;
    if (standby && (ccr & kCcrRefsel) != 0u) refresh_lost_ = true;
    if (!standby && refresh_lost_) {
        std::memset(vram_, 0, kDisplayMemoryBytes);
        refresh_lost_ = false;
    }
    emu_.Get<EmulatedMemory>().SetBusAccess(kVramPa, MemoryInterfaceRunning());
}

bool Hd64461Lcdc::MemoryInterfaceRunning() const {
    return (StoredReg(kLcdccr) & kCcrStreq) == 0u && system_->LcdcClockRunning();
}

bool Hd64461Lcdc::DisplayMemoryAccessible() {
    std::lock_guard<std::mutex> lk(lock_);
    return MemoryInterfaceRunning();
}

Hd64461Lcdc::Format Hd64461Lcdc::CurrentFormat() {
    std::lock_guard<std::mutex> lk(lock_);
    return DecodeFormat();
}

Hd64461Lcdc::Format Hd64461Lcdc::DecodeFormat() const {
    const uint32_t lm = StoredReg(kLdr2) & 0x7u;
    const uint32_t cg = StoredReg(kLdr3) & 0xFu;
    if (lm == kLmColor && cg == 0x4u) return Format::kIndexed8;
    if (lm == kLmColor && cg == 0x8u) return Format::kRgb565;
    if (lm == kLmMono8Bit && cg == 0x3u) return Format::kGray64;
    if (lm == kLmMono8Bit && cg == 0x2u) return Format::kGray16;
    if (lm == kLmMono8Bit && cg == 0x1u) return Format::kGray4;
    if (lm == kLmMono8Bit && cg == 0x0u) return Format::kGray2;
    return Format::kUndefined;
}

uint16_t Hd64461Lcdc::SampleRegister(uint32_t i, uint32_t addr) {
    if (i == kLcdccr) {
        const uint16_t ccr = static_cast<uint16_t>(StoredReg(kLcdccr) & kCcrControl);
        return static_cast<uint16_t>(ccr | ((ccr & kCcrStreq) != 0u ? kCcrStback : 0u) | (epon_ ? kCcrEpon : 0u));
    }
    const uint32_t slot = read_index_ * 3u + read_phase_;
    if (palette_defined_[slot] == 0u) {
        HaltUnsupportedAccess("ReadHalf of an undefined CPTRDR palette entry", addr, slot);
    }
    if (++read_phase_ == 3u) {
        read_phase_ = 0u;
        read_index_ = (read_index_ + 1u) & 0xFFu;
    }
    return palette_[slot];
}

void Hd64461Lcdc::BeforeStore(uint32_t i, uint16_t value) {
    if (i == kLcdcbar && value != 0u) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: LCDCBAR write 0x%04X", value);
    }
    const uint16_t dspsel = value & kDspselMask;
    if (i == kLcdccr && dspsel != 0u && dspsel != kDspselLcd && dspsel != kDspselCrt && dspsel != kDspselLcdAndCrt) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: LCDCCR write 0x%04X selects reserved DSPSEL %u", value, dspsel);
    }
    if (i == kLdr2 && (value & 0x7u) == kLmMono4Bit) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: LDR2 write 0x%04X selects LM 000 (4-bit monochrome output)", value);
    }
    if (i == kLdr2 && (value & 0x7u) != kLmMono4Bit && (value & 0x7u) != kLmMono8Bit && (value & 0x7u) != kLmColor) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: LDR2 write 0x%04X selects reserved LM %u", value, value & 0x7u);
    }
    if (i == kLdr3 && kCgStorageBits[value & 0xFu] == 0u) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: LDR3 write 0x%04X selects reserved CG %u", value, value & 0xFu);
    }
}

void Hd64461Lcdc::AfterStore(uint32_t i) {
    switch (i) {
    case kLcdccr:
        if ((StoredReg(kLcdccr) & kCcrSpon) != 0u) epon_ = true;
        ApplyStandby();
        break;
    case kCptwar:
        write_index_ = StoredReg(kCptwar) >> 8;
        write_phase_ = 0u;
        return;
    case kCptwdr: {
        const uint32_t slot    = write_index_ * 3u + write_phase_;
        palette_[slot]         = static_cast<uint8_t>(StoredReg(kCptwdr));
        palette_defined_[slot] = 1u;
        if (++write_phase_ == 3u) {
            write_phase_ = 0u;
            write_index_ = (write_index_ + 1u) & 0xFFu;
        }
        return;
    }
    case kCptrar:
        read_index_ = StoredReg(kCptrar) >> 8;
        read_phase_ = 0u;
        return;
    default:
        break;
    }
    PublishPresentingLocked();
}

void Hd64461Lcdc::PublishPresentingLocked() {
    const bool presenting = Presenting();
    if (presenting && FrameBytes() > kDisplayMemoryBytes) {
        emu_.Get<Fatal>().Die("Hd64461Lcdc: frame of %u dots x %u lines, stride %u, line step %u, CG %u ends at 0x%X",
                              Dots(), Lines(), StoredReg(kLcdclor), LineStep(), StoredReg(kLdr3) & 0xFu, FrameBytes());
    }
    if (mode_latch_.Publish(emu_, presenting, PresentedWidth(), Lines())) {
        LOG(Lcd, "Hd64461Lcdc: panel %ux%u, LCDCCR 0x%04X LCDCLOR 0x%04X LDR2 0x%04X LDR3 0x%04X\n", PresentedWidth(),
            Lines(), StoredReg(kLcdccr), StoredReg(kLcdclor), StoredReg(kLdr2), StoredReg(kLdr3));
    }
}

bool Hd64461Lcdc::Presenting() const {
    const uint16_t ccr    = StoredReg(kLcdccr);
    const uint16_t dspsel = ccr & kDspselMask;
    return (StoredReg(kLdr1) & kLdr1Don) != 0u && (dspsel == kDspselLcd || dspsel == kDspselLcdAndCrt) &&
           (ccr & kCcrStreq) == 0u && system_->LcdcClockRunning() && panel_->Powered();
}

uint32_t Hd64461Lcdc::LineStep() const {
    return (StoredReg(kLcdccr) & kDspselMask) == kDspselLcdAndCrt ? 2u : 1u;
}

uint32_t Hd64461Lcdc::Dots() const { return ((StoredReg(kLdhncr) >> 8) + 1u) * 8u; }

uint32_t Hd64461Lcdc::Lines() const { return (StoredReg(kLdvndr) & 0x3FFu) + 1u; }

uint32_t Hd64461Lcdc::PresentedWidth() const {
    return panel_->PresentedWidth(Dots(), (StoredReg(kLdr2) & 0x7u) == kLmMono8Bit);
}

uint32_t Hd64461Lcdc::FrameBytes() const {
    return (Lines() - 1u) * LineStep() * StoredReg(kLcdclor) + (Dots() * kCgStorageBits[StoredReg(kLdr3) & 0xFu] + 7u) / 8u;
}

Hd64461Lcdc::Scanout Hd64461Lcdc::GetScanout() {
    std::lock_guard<std::mutex> lk(lock_);
    Scanout s;
    s.presenting       = Presenting();
    s.fb               = vram_;
    s.stride           = StoredReg(kLcdclor);
    s.line_step        = LineStep();
    s.dots             = Dots();
    s.lines            = Lines();
    s.frame_bytes      = FrameBytes();
    s.presented_width  = PresentedWidth();
    s.format           = DecodeFormat();
    s.dinv             = (StoredReg(kLdr1) & kLdr1Dinv) != 0u;
    s.palette_complete = std::all_of(palette_defined_.begin(), palette_defined_.end(), [](uint8_t d) { return d != 0u; });
    s.palette          = palette_;
    return s;
}

void Hd64461Lcdc::SaveState(StateWriter& w) {
    RegWindow::SaveState(w);
    std::lock_guard<std::mutex> lk(lock_);
    w.WriteBytes("palette", palette_.data(), palette_.size());
    w.WriteBytes("palette_defined", palette_defined_.data(), palette_defined_.size());
    w.Write("write_index", write_index_);
    w.Write("write_phase", write_phase_);
    w.Write("read_index", read_index_);
    w.Write("read_phase", read_phase_);
    w.Write("epon", epon_);
    w.Write("refresh_lost", refresh_lost_);
    mode_latch_.SaveState(w);
}

void Hd64461Lcdc::RestoreState(StateReader& r) {
    RegWindow::RestoreState(r);
    std::lock_guard<std::mutex> lk(lock_);
    r.ReadBytes("palette", palette_.data(), palette_.size());
    r.ReadBytes("palette_defined", palette_defined_.data(), palette_defined_.size());
    r.Read("write_index", write_index_);
    r.Read("write_phase", write_phase_);
    r.Read("read_index", read_index_);
    r.Read("read_phase", read_phase_);
    r.Read("epon", epon_);
    r.Read("refresh_lost", refresh_lost_);
    mode_latch_.RestoreState(r);
}

}
