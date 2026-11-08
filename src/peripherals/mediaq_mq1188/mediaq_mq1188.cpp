#include "mediaq_mq1188.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/askey_pc3xx/askey_pc3xx_family_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace {

struct ResetBits {
    uint32_t first;
    uint32_t last;
    uint32_t mask;
    uint32_t value;
};

constexpr uint32_t kAll = 0xFFFFFFFFu;

constexpr ResetBits kResetBits[] = {
    {0x000u, 0x010u, kAll,    0u},
    {0x080u, 0x088u, 0xFu,    0u},
    {0x100u, 0x10Cu, kAll,    0u},
    {0x180u, 0x180u, kAll,    0u},
    {0x184u, 0x184u, 0xFFu,   0u},
    {0x198u, 0x19Cu, 0xFFFFu, 0u},
    {0x200u, 0x200u, kAll,    0u},
    {0x230u, 0x230u, kAll,    0u},
    {0x280u, 0x284u, kAll,    0u},
    {0x290u, 0x290u, 0xFu,    0u},
    {0x2A0u, 0x2ACu, 0xFFu,   0u},
    {0x2C0u, 0x2C4u, kAll,    0u},
    {0x2CCu, 0x2CCu, kAll,    0u},
    {0x300u, 0x320u, kAll,    0u},
    {0x380u, 0x380u, kAll,    0u},
    {0x384u, 0x384u, kAll,    0xF0000000u},
    {0x388u, 0x39Cu, kAll,    0u},
    {0x500u, 0x500u, 0xFFu,   0x10u},
    {0x504u, 0x530u, kAll,    0u},
    {0x534u, 0x534u, kAll,    0x2EDFu},
    {0x538u, 0x53Cu, kAll,    0u},
    {0x540u, 0x540u, 0xFFFFu, 0u},
    {0x544u, 0x544u, kAll,    0x0628u},
    {0x548u, 0x548u, kAll,    0x1102u},
    {0x54Cu, 0x558u, kAll,    0u},
    {0x600u, 0x604u, kAll,    0u},
    {0x608u, 0x608u, kAll,    0x0FFCFCFFu},
    {0x60Cu, 0x61Cu, kAll,    0u},
    {0x628u, 0x628u, kAll,    0xF0030000u},
    {0x62Cu, 0x62Cu, 0xFFFu,  0u},
    {0x638u, 0x638u, 0xFFu,   0u},
    {0x63Cu, 0x63Cu, kAll,    0u},
    {0x1000u, 0x1028u, kAll,  0u},
    {0x102Cu, 0x102Cu, 0xFFu, 0u},
    {0x1030u, 0x1048u, kAll,  0u},
    {0x1060u, 0x107Cu, kAll,  0u},
};

}

bool MediaQMq1188::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
}

Fatal& MediaQMq1188::OwnerFatal() {
    return emu_.Get<Fatal>();
}

void MediaQMq1188::OnReady() {
    sram_.assign(kApertureSize, 0u);
    ResetRegisters();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        ResetRegisters();
        mode_latch_.Publish(emu_, false, 0u, 0u);
        ge_.PowerOnReset();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void MediaQMq1188::ResetRegisters() {
    for (const ResetBits& b : kResetBits) {
        for (uint32_t off = b.first; off <= b.last; off += 4u) {
            reg_[off / 4u] = (reg_[off / 4u] & ~b.mask) | b.value;
        }
    }
}

bool MediaQMq1188::IsEnabled() const {
    /* GC08R/GC09R reset to 0, which the (n-1) geometry encoding reads back as a
       1-pixel dimension; require >1 so the enable edge fires on the programmed
       mode, not the transient state before the mode-set writes geometry. */
    return (Reg(kDc05R) & 0x1u) != 0u && GetGuestW() > 1u &&
           GetGuestH() > 1u && Stride() != 0u;
}

uint32_t MediaQMq1188::Bpp() const {
    switch ((Reg(kGc00R) >> 4u) & 0xFu) {  /* GC00R[7:4] color depth (Reg 4-31). */
        case 0x0u: return 1u;
        case 0x1u: return 2u;
        case 0x2u: return 4u;
        case 0x3u: return 8u;
        case 0x4u:
        case 0xCu: return 16u;
        default:   return 0u;
    }
}

uint32_t MediaQMq1188::GetGuestW()      const { return ((Reg(kGc08R) >> 16u) & 0xFFFFu) + 1u; }
uint32_t MediaQMq1188::GetGuestH()      const { return ((Reg(kGc09R) >> 16u) & 0xFFFFu) + 1u; }
uint32_t MediaQMq1188::FbWindowOffset() const { return Reg(kGc0CR) & 0x3FFFFu; }
uint32_t MediaQMq1188::Stride()         const { return Reg(kGc0ER) & 0x3FFFFu; }

uint32_t MediaQMq1188::PaletteEntry(uint32_t index) const {
    return Reg(kPaletteBase + (index & 0xFFu) * 4u);
}

void MediaQMq1188::PublishScreenSizeOnEnableEdge() {
    const bool on = IsEnabled();
    const uint32_t w = on ? GetGuestW() : 0u, h = on ? GetGuestH() : 0u;
    if (!mode_latch_.Publish(emu_, on, w, h)) return;
    LOG(Lcd, "MediaQMq1188: display enabled %ux%u %ubpp stride=%u fb_off=0x%X\n",
        w, h, Bpp(), Stride(), FbWindowOffset());
}

uint32_t MediaQMq1188::RegRead(uint32_t addr) {
    const uint32_t roff = (addr - MmioBase()) - kRegBase;
    if (roff == kCc01R) { ge_.FlushPending(); return ge_.StatusReady(); }
    if (roff >= kGeCmdLo && roff < kGeCmdHi) return ge_.ReadReg((roff - kGeCmdLo) / 4u);
    if (IsUsbHost(roff)) {
        const uint32_t uoff = roff - kUsbLo;
        if (uoff == kHcRevision)      return 0x10u;  /* OHCI 1.0a. */
        if (uoff == kHcCommandStatus) return Reg(roff) & ~kHcrResetBit;
    }
    return Reg(roff);
}

void MediaQMq1188::RegWrite(uint32_t addr, uint32_t value) {
    const uint32_t roff = (addr - MmioBase()) - kRegBase;
    /* GE registers are reached only via the queued alias (0x1400). The 0x200
       block is the GE direct block on the MQ-1132 but the SD/MMC controller on
       the MQ-1188 (mq1188sdmmc.dll maps PA 0x08040200, writes DMA descriptors at
       0x22C) - routing it to the GE corrupts GE0BR. SD/MMC not yet modelled. */
    if (roff >= kGeCmdLo && roff < kGeCmdHi) { ge_.WriteReg((roff - kGeCmdLo) / 4u, value); return; }
    if (roff >= kSrcFifoLo && roff < kSrcFifoHi) { ge_.PushSourceFifo(value); return; }
    reg_[roff / 4u] = value;
    PublishScreenSizeOnEnableEdge();
}

uint8_t MediaQMq1188::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) {
        const uint32_t word = RegRead(addr & ~0x3u);
        return static_cast<uint8_t>(word >> ((off & 0x3u) * 8u));
    }
    return sram_[off];
}

uint16_t MediaQMq1188::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) {
        const uint32_t word = RegRead(addr & ~0x3u);
        return static_cast<uint16_t>(word >> ((off & 0x2u) * 8u));
    }
    return cerf::le::U16(&sram_[off]);
}

uint32_t MediaQMq1188::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) return RegRead(addr);
    return cerf::le::U32(&sram_[off]);
}

void MediaQMq1188::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) {
        const uint32_t shift = (off & 0x3u) * 8u;
        const uint32_t word  = RegRead(addr & ~0x3u);
        RegWrite(addr & ~0x3u,
                 (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift));
        return;
    }
    sram_[off] = value;
}

void MediaQMq1188::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) {
        const uint32_t shift = (off & 0x2u) * 8u;
        const uint32_t word  = RegRead(addr & ~0x3u);
        RegWrite(addr & ~0x3u,
                 (word & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift));
        return;
    }
    cerf::le::Put16(&sram_[off], value);
}

void MediaQMq1188::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (InRegWindow(off)) { RegWrite(addr, value); return; }
    cerf::le::Put32(&sram_[off], value);
}

void MediaQMq1188::SaveState(StateWriter& w) {
    w.WriteBytes("sram", sram_.data(), sram_.size());
    w.WriteBytes("reg", reg_, sizeof(reg_));
    mode_latch_.SaveState(w);
    ge_.SaveState(w);
}

void MediaQMq1188::RestoreState(StateReader& r) {
    r.ReadBytes("sram", sram_.data(), sram_.size());
    r.ReadBytes("reg", reg_, sizeof(reg_));
    mode_latch_.RestoreState(r);
    ge_.RestoreState(r);
}

REGISTER_SERVICE(MediaQMq1188);

}
