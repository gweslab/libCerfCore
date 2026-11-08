#include "hp_palmtop_vga_card.h"

#include "../pcmcia/pcmcia_slot.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

/* Minimal PC-Card CIS: enough for CE's PCMCIA bus driver to enumerate the
   card and run the generic Detect chain (no specific PnP match -> falls
   through to Detect\49 = hpvgaout!DetectVGA, which confirms via the 0xC0000
   magic). Attribute memory is 8-bit aliased across even addresses (like the
   NE2000 card), so the driver reads tuple byte n at attribute offset 2n. */
const uint8_t kCisData[] = {
    0x01, 0x03, 0xDC, 0x00, 0xFF,             /* CISTPL_DEVICE                */
    0x15, 0x12, 0x04, 0x01,                   /* CISTPL_VERS_1 v4.1           */
        'H', 'P', 0x00,
        'P', 'a', 'l', 'm', 't', 'o', 'p', ' ', 'V', 'G', 'A', 0x00,
        0xFF,
    0x21, 0x02, 0xFF, 0x00,                   /* CISTPL_FUNCID: vendor-spec   */
    0xFF, 0x00,                               /* CISTPL_END                   */
};
constexpr size_t kCisSize = sizeof(kCisData);

/* HP identity magic in ATTRIBUTE memory at card offset 0xC0000, checked by
   DetectVGA (sub_10006C70): even bytes 0/2/4/6 = 0x55,0x40,0x61,0x34. */
const uint8_t kMagic[8] = { 0x55, 0x00, 0x40, 0x00, 0x61, 0x00, 0x34, 0x00 };

struct DetectProbeReadRange {
    uint32_t first;
    uint32_t last;
};

constexpr DetectProbeReadRange kDetectProbeReads[] = {
    {0x00000u, 0x00200u}, {0x00230u, 0x00230u}, {0x00240u, 0x00240u}, {0x00280u, 0x00280u},
    {0x002A0u, 0x002A0u}, {0x00300u, 0x00300u}, {0x00310u, 0x00310u}, {0x00380u, 0x00380u},
    {0x00400u, 0x00400u}, {0x01007u, 0x01007u}, {0x80000u, 0x8000Eu},
};

struct DetectProbeWrite {
    uint32_t first;
    uint32_t last;
    uint8_t  count;
    uint8_t  values[5];
};

constexpr DetectProbeWrite kDetectProbeWrites[] = {
    {0x00000u, 0x00000u, 5, {0x00, 0x35, 0x90, 0xF0, 0xFF}},
    {0x00001u, 0x00001u, 5, {0x00, 0x69, 0x90, 0xF0, 0xFF}},
    {0x00002u, 0x00002u, 5, {0x00, 0x90, 0xAC, 0xF0, 0xFF}},
    {0x00003u, 0x00003u, 5, {0x00, 0x63, 0x90, 0xF0, 0xFF}},
    {0x00004u, 0x0000Eu, 3, {0x90, 0xF0, 0xFF}},
    {0x00055u, 0x00055u, 2, {0x98, 0xFF}},
    {0x000AAu, 0x000AAu, 2, {0x98, 0xFF}},
    {0x000ABu, 0x000ABu, 1, {0x00}},
    {0x000FFu, 0x000FFu, 2, {0x98, 0xFF}},
    {0x00154u, 0x00154u, 2, {0x98, 0xFF}},
    {0x001A9u, 0x001A9u, 2, {0x98, 0xFF}},
    {0x001FEu, 0x001FEu, 2, {0x98, 0xFF}},
    {0x00253u, 0x00253u, 2, {0x98, 0xFF}},
    {0x002A8u, 0x002A8u, 2, {0x98, 0xFF}},
    {0x00400u, 0x00401u, 1, {0x00}},
    {0x01002u, 0x01002u, 1, {0x85}},
    {0x01003u, 0x01003u, 1, {0x00}},
    {0x01005u, 0x01005u, 1, {0x00}},
    {0x02AAAu, 0x02AAAu, 1, {0x55}},
    {0x05554u, 0x05554u, 1, {0x55}},
    {0x05555u, 0x05555u, 3, {0x55, 0x90, 0xAA}},
    {0x0AAA8u, 0x0AAA9u, 1, {0x55}},
    {0x0AAAAu, 0x0AAABu, 3, {0x55, 0x90, 0xAA}},
    {0x15554u, 0x15557u, 2, {0x90, 0xAA}},
    {0x80000u, 0x8000Eu, 2, {0x90, 0xFF}},
};

bool IsDetectProbeRead(uint32_t offset) {
    for (const auto& r : kDetectProbeReads) {
        if (offset - r.first <= r.last - r.first) return true;
    }
    return false;
}

bool IsDetectProbeWrite(uint32_t offset, uint8_t value) {
    for (const auto& w : kDetectProbeWrites) {
        if (offset - w.first > w.last - w.first) continue;
        for (uint8_t i = 0; i < w.count; ++i) {
            if (w.values[i] == value) return true;
        }
    }
    return false;
}

}  /* namespace */

HpPalmtopVgaCard::HpPalmtopVgaCard(CerfEmulator& emu) : PcmciaCard(emu), controller_(emu) {}
HpPalmtopVgaCard::~HpPalmtopVgaCard() { if (window_) window_->Close(); }

void HpPalmtopVgaCard::OnInserted() {
    controller_.Attach();
    PcmciaSlot*    slot = slot_;
    const uint64_t id   = card_id_;
    window_ = emu_.Get<ExternalDisplayFactory>().Create(
        controller_, kDisplayName, [slot, id] { slot->EjectCardIfResident(id); });
    window_->Open(640u, 480u);
    last_w_ = 640u;
    last_h_ = 480u;
}

void HpPalmtopVgaCard::OnShutdown() {
    if (window_) window_->Close();
}

void HpPalmtopVgaCard::PowerOn() {
    controller_.PowerOnReset();
#if CERF_DEV_MODE
    LOG(Periph, "[HpVga] PowerOn\n");
#endif
}
void HpPalmtopVgaCard::PowerOff() {
#if CERF_DEV_MODE
    LOG(Periph, "[HpVga] PowerOff\n");
#endif
}

void HpPalmtopVgaCard::SyncWindowToMode() {
    uint32_t w, h, bpp;
    if (!controller_.CurrentMode(w, h, bpp)) return;
    if (w == last_w_ && h == last_h_) return;
    last_w_ = w;
    last_h_ = h;
    if (window_) window_->SetSurfaceSize(w, h);
    LOG(Lcd, "[HpVga] mode %ux%u %ubpp\n", w, h, bpp);
}

uint8_t HpPalmtopVgaCard::ReadAttribute8(uint32_t offset) {
    if (offset < kCisSize * 2u) return kCisData[offset / 2u];
    if (offset - kMagicBase < kMagicLen) return kMagic[offset - kMagicBase];
    HaltUnmapped("attribute", "read", offset);
}

void HpPalmtopVgaCard::WriteAttribute8(uint32_t offset, uint8_t value) {
    emu_.Get<Fatal>().Die("HpPalmtopVgaCard: attribute write 0x%02X at 0x%06X", value, offset);
}

void HpPalmtopVgaCard::HaltUnmapped(const char* space, const char* access, uint32_t offset) {
    emu_.Get<Fatal>().Die("HpPalmtopVgaCard: %s %s at unmapped card offset 0x%06X", space, access,
                          offset);
}

uint8_t HpPalmtopVgaCard::ReadCommon8(uint32_t offset) {
    if (offset < kRegEnd) {
        if (IsDetectProbeRead(offset)) return slot_->UndrivenDataLane();
        return controller_.ReadReg8(offset);
    }
    uint32_t at = 0u;
    if (controller_.LinearOffset(offset, at)) return controller_.Vram()[at];
    if (IsDetectProbeRead(offset)) return slot_->UndrivenDataLane();
    HaltUnmapped("common", "read", offset);
}

void HpPalmtopVgaCard::WriteCommon8(uint32_t offset, uint8_t value) {
    if (offset < kRegEnd) {
        if (IsDetectProbeWrite(offset, value)) return;
        controller_.WriteReg8(offset, value);
        SyncWindowToMode();
        return;
    }
    uint32_t at = 0u;
    if (controller_.LinearOffset(offset, at)) {
        controller_.Vram()[at] = value;
        return;
    }
    if (IsDetectProbeWrite(offset, value)) return;
    HaltUnmapped("common", "write", offset);
}

uint16_t HpPalmtopVgaCard::ReadCommon16(uint32_t offset) {
    uint32_t at = 0u;
    if (offset >= kRegEnd && controller_.LinearOffset(offset, at) &&
        at + 1u < VgaController::kVramBytes) {
        return cerf::le::U16(controller_.Vram(), at);
    }
    const uint16_t even = ReadCommon8(offset);
    const uint16_t odd = ReadCommon8(offset + 1u);
    return static_cast<uint16_t>(even | (odd << 8));
}

/* window_ is the host external-display window (re-derived from the restored
   mode); the VGA register file + framebuffer is the card state. */
void HpPalmtopVgaCard::SaveState(StateWriter& w) {
    controller_.SaveState(w);
}

void HpPalmtopVgaCard::RestoreState(StateReader& r) {
    controller_.RestoreState(r);
    /* last_w_/last_h_ is the window-resize dedup cache, not guest state.
       OnInserted opened the window at the boot-default 640x480; reset the
       cache so SyncWindowToMode re-applies the restored mode to it. */
    last_w_ = 0;
    last_h_ = 0;
    SyncWindowToMode();
}

void HpPalmtopVgaCard::WriteCommon16(uint32_t offset, uint16_t value) {
    uint32_t at = 0u;
    if (offset >= kRegEnd && controller_.LinearOffset(offset, at) &&
        at + 1u < VgaController::kVramBytes) {
        cerf::le::Put16(controller_.Vram() + at, value);
        return;
    }
    WriteCommon8(offset, (uint8_t)(value & 0xFFu));
    WriteCommon8(offset + 1u, (uint8_t)(value >> 8));
}

uint8_t  HpPalmtopVgaCard::ReadIo8(uint32_t o)             { return ReadCommon8(o);  }
uint16_t HpPalmtopVgaCard::ReadIo16(uint32_t o)             { return ReadCommon16(o); }
void     HpPalmtopVgaCard::WriteIo8 (uint32_t o, uint8_t v)  { WriteCommon8(o, v);  }
void     HpPalmtopVgaCard::WriteIo16(uint32_t o, uint16_t v) { WriteCommon16(o, v); }

}
