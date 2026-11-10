#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "../../socs/reg_window_impl.h"
#include "hd64461_lcdc.h"

namespace cerfcore {

namespace {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindow;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

constexpr RegWindowModel kModel = {
    0x10001040u,
    0x2Cu,
    22u,
    false,
    {
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x027Fu, 0xF800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x000Fu, 0x0001u, 0xFFE8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0007u, 0x0000u, 0xFFF8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x0000u, 0xF800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x87FFu, 0x0000u, 0x7800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0FFFu, 0x0000u, 0xF000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x87FFu, 0x0000u, 0x7800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0003u, 0x0000u, 0xFFFCu, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0007u, 0x0000u, 0xFFF8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0007u, 0x0000u, 0xFFF8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x0000u, 0xF800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x0000u, 0xF800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0007u, 0x0000u, 0xFFF8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0007u, 0x0000u, 0xFFF8u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x003Du, 0x0000u, 0xFFE6u, OtherReset::kRetain, 0x0000u, 0x0000u},
    },
};

struct RopEntry {
    uint16_t code;
    bool     src;
    bool     pat;
    uint32_t (*apply)(uint32_t p, uint32_t s, uint32_t d, uint32_t ones);
};

constexpr RopEntry kRops[] = {
    {0x88u, true, false, [](uint32_t, uint32_t s, uint32_t d, uint32_t) -> uint32_t { return s & d; }},
    {0xCCu, true, false, [](uint32_t, uint32_t s, uint32_t, uint32_t) -> uint32_t { return s; }},
    {0x66u, true, false, [](uint32_t, uint32_t s, uint32_t d, uint32_t) -> uint32_t { return s ^ d; }},
    {0xEEu, true, false, [](uint32_t, uint32_t s, uint32_t d, uint32_t) -> uint32_t { return s | d; }},
    {0xF0u, false, true, [](uint32_t p, uint32_t, uint32_t, uint32_t) -> uint32_t { return p; }},
    {0x5Au, false, true, [](uint32_t p, uint32_t, uint32_t d, uint32_t) -> uint32_t { return p ^ d; }},
    {0x55u, false, false, [](uint32_t, uint32_t, uint32_t d, uint32_t ones) -> uint32_t { return ~d & ones; }},
    {0x00u, false, false, [](uint32_t, uint32_t, uint32_t, uint32_t) -> uint32_t { return 0u; }},
    {0xFFu, false, false, [](uint32_t, uint32_t, uint32_t, uint32_t ones) -> uint32_t { return ones; }},
    {0xAAu, false, false, [](uint32_t, uint32_t, uint32_t d, uint32_t) -> uint32_t { return d; }},
};

constexpr uint32_t kGrdor    = 0u;
constexpr uint32_t kGrscr    = 1u;
constexpr uint32_t kGrcfgr   = 2u;
constexpr uint32_t kLnmdr    = 9u;
constexpr uint32_t kBbtssarh = 10u;
constexpr uint32_t kBbtssarl = 11u;
constexpr uint32_t kBbtdsarh = 12u;
constexpr uint32_t kBbtdsarl = 13u;
constexpr uint32_t kBbtdwr   = 14u;
constexpr uint32_t kBbtdhr   = 15u;
constexpr uint32_t kBbtropr  = 20u;
constexpr uint32_t kBbtmdr   = 21u;

constexpr uint16_t kAccstartMask     = 0x0006u;
constexpr uint16_t kAccstartBitblt   = 0x0006u;
constexpr uint16_t kAccstartLine     = 0x0004u;
constexpr uint16_t kAccstartReserved = 0x0002u;
constexpr uint16_t kColorDepth8      = 0x0001u;
constexpr uint16_t kPatselectSolid   = 0x0010u;
constexpr uint16_t kScandrct         = 0x0001u;

class Hd64461Accelerator : public RegWindow<kModel> {
public:
    using RegWindow::RegWindow;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    void OnReady() override {
        lcdc_ = &emu_.Get<Hd64461Lcdc>();
        RegWindow::OnReady();
    }

protected:
    void BeforeStore(uint32_t i, uint16_t value) override {
        if (i == kGrcfgr && (value & kAccstartMask) == kAccstartReserved) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: GRCFGR write 0x%04X selects reserved ACCSTART 01", value);
        }
    }

    void AfterStore(uint32_t i) override {
        if (i != kGrcfgr) return;
        const uint16_t start = StoredReg(kGrcfgr) & kAccstartMask;
        if (start == kAccstartLine) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: line drawing start, GRCFGR 0x%04X LNMDR 0x%04X",
                                  StoredReg(kGrcfgr), StoredReg(kLnmdr));
        }
        if (start == kAccstartBitblt) BitBlt();
    }

private:
    uint32_t Address(uint32_t high, uint32_t low) const {
        return (static_cast<uint32_t>(StoredReg(high)) << 16) | StoredReg(low);
    }

    void RequireBlockInMemory(const char* what, uint32_t anchor, uint32_t span, uint32_t last_pixel, bool reverse) {
        const uint32_t lo = reverse ? anchor - span : anchor;
        const uint32_t hi = reverse ? anchor + last_pixel : anchor + span + last_pixel;
        if ((reverse && anchor < span) || hi >= Hd64461Lcdc::kDisplayMemoryBytes) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: BitBLT %s block 0x%X..0x%X outside the display memory", what,
                                  lo, hi);
        }
    }

    const RopEntry& LookupRop(uint16_t code) {
        for (const RopEntry& e : kRops) {
            if (e.code == code) return e;
        }
        emu_.Get<Fatal>().Die("Hd64461Accelerator: BitBLT with ROP 0x%04X", code);
    }

    void BitBlt() {
        const Hd64461Lcdc::Format lcd = lcdc_->CurrentFormat();
        if (lcd != Hd64461Lcdc::Format::kIndexed8 && lcd != Hd64461Lcdc::Format::kRgb565) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: BitBLT in LCD format %u", static_cast<uint32_t>(lcd));
        }
        const uint16_t  mode     = StoredReg(kBbtmdr);
        const RopEntry& rop      = LookupRop(StoredReg(kBbtropr));
        const bool      uses_src = rop.src;
        const bool      uses_pat = rop.pat;
        if (uses_pat && (mode & kPatselectSolid) == 0u) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: BitBLT ROP 0x%04X with the 8 x 8 bitmap pattern, BBTMDR 0x%04X",
                                  rop.code, mode);
        }
        if (!lcdc_->DisplayMemoryAccessible()) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: BitBLT while the LCDC display memory is in standby");
        }

        const uint32_t bpp     = (StoredReg(kGrcfgr) & kColorDepth8) != 0u ? 1u : 2u;
        const uint32_t ones    = bpp == 1u ? 0xFFu : 0xFFFFu;
        const uint32_t stride  = ((StoredReg(kGrdor) & 0x7FFu) + 1u) * bpp;
        const uint32_t width   = StoredReg(kBbtdwr) + 1u;
        const uint32_t height  = StoredReg(kBbtdhr) + 1u;
        const bool     reverse = (mode & kScandrct) != 0u;
        const uint32_t dst     = Address(kBbtdsarh, kBbtdsarl);
        const uint32_t src     = uses_src ? Address(kBbtssarh, kBbtssarl) : 0u;
        const uint32_t pat     = StoredReg(kGrscr) & ones;
        if (bpp == 2u && ((dst | src) & 1u) != 0u) {
            emu_.Get<Fatal>().Die("Hd64461Accelerator: 16 bpp BitBLT at odd address, destination 0x%X source 0x%X",
                                  dst, src);
        }
        const uint32_t span = (height - 1u) * stride + (width - 1u) * bpp;
        RequireBlockInMemory("destination", dst, span, bpp - 1u, reverse);
        if (uses_src) RequireBlockInMemory("source", src, span, bpp - 1u, reverse);

        uint8_t* vram = lcdc_->DisplayMemory();
        for (uint32_t r = 0; r < height; ++r) {
            for (uint32_t c = 0; c < width; ++c) {
                const uint32_t step = r * stride + c * bpp;
                uint8_t*       d    = vram + (reverse ? dst - step : dst + step);
                const uint32_t s =
                    uses_src ? static_cast<uint32_t>(cerf::le::UN(vram + (reverse ? src - step : src + step), bpp)) : 0u;
                cerf::le::PutN(d, rop.apply(pat, s, static_cast<uint32_t>(cerf::le::UN(d, bpp)), ones), bpp);
            }
        }
    }

    Hd64461Lcdc* lcdc_ = nullptr;
};

}

REGISTER_SERVICE(Hd64461Accelerator);

}
