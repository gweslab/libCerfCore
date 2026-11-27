#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_fb_regs.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize_regs.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/ga_palette_database.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/hw_screen.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../platform/host_virtual_memory.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtFramebuffer);

CerfVirtFramebuffer::~CerfVirtFramebuffer() {
    if (bytes_) host_platform::Release(bytes_);
}

bool CerfVirtFramebuffer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

static const uint32_t kFullSizeFlipChain = 2u;

static uint64_t PackGeometry(uint32_t w, uint32_t h) {
    return uint64_t{w} | (uint64_t{h} << 32u);
}

uint32_t CerfVirtFramebuffer::Width() const {
    return static_cast<uint32_t>(geometry_.load(std::memory_order_acquire));
}

uint32_t CerfVirtFramebuffer::Height() const {
    return static_cast<uint32_t>(geometry_.load(std::memory_order_acquire) >> 32u);
}

void CerfVirtFramebuffer::Geometry(uint32_t& w, uint32_t& h) const {
    const uint64_t g = geometry_.load(std::memory_order_acquire);
    w = static_cast<uint32_t>(g);
    h = static_cast<uint32_t>(g >> 32u);
}

uint32_t CerfVirtFramebuffer::Stride() const {
    return CerfVirt::FbRowBytes(Width(), Bpp());
}

uint32_t CerfVirtFramebuffer::SizeBytes() const {
    uint32_t w = 0, h = 0;
    Geometry(w, h);
    return h * CerfVirt::FbRowBytes(w, Bpp());
}

void CerfVirtFramebuffer::StoreGeometry(uint32_t w, uint32_t h) {
    geometry_.store(PackGeometry(w, h), std::memory_order_release);
}

uint32_t CerfVirtFramebuffer::ComputeRegionBytes() const {
    const uint32_t window_size = emu_.Get<BoardContext>().GuestAdditionsWindowSize();
    if (window_size <= CerfVirt::kFramebufferMemOffset) {
        emu_.Get<Fatal>().Die("CerfVirtFramebuffer: board GA window 0x%08X B ends before "
                              "the FB region offset 0x%08X", window_size,
                              CerfVirt::kFramebufferMemOffset);
    }
    return RegionBytesIn(window_size);
}

uint32_t CerfVirtFramebuffer::RegionBytesIn(uint32_t window_size) {
    return window_size - CerfVirt::kFramebufferMemOffset;
}

uint64_t CerfVirtFramebuffer::PrimaryBytesAt(uint32_t w, uint32_t h, uint32_t bpp) {
    return uint64_t{h} * CerfVirt::FbRowBytes(w, bpp);
}

bool CerfVirtFramebuffer::PrimaryFitsReserve(uint32_t w, uint32_t h) const {
    return PrimaryBytesAt(w, h, Bpp()) <= PrimaryReserveBytes();
}

bool CerfVirtFramebuffer::CanApply(uint32_t w, uint32_t h, uint32_t bpp) const {
    return PrimaryFitsReserve(w, h) && FrameFitsRegion(0u, PrimaryBytesAt(w, h, bpp));
}

void CerfVirtFramebuffer::ReservePrimary() {
    uint32_t w = 0, h = 0;
    Geometry(w, h);
    const uint64_t configured = PrimaryBytesAt(w, h, Bpp());
    if (!FrameFitsRegion(0u, configured)) {
        emu_.Get<Fatal>().UserError("[CerfVirtFramebuffer] %ux%u at %ubpp needs %llu B, the "
                                    "guest-additions screen of this board holds at most %u B",
                                    w, h, Bpp(), (unsigned long long)configured, region_bytes_);
    }
    primary_reserve_.store(static_cast<uint32_t>(std::max<uint64_t>(
                               configured, region_bytes_ / kFullSizeFlipChain)),
                           std::memory_order_release);
}

bool CerfVirtFramebuffer::FrameFitsRegion(uint32_t offset, uint64_t frame_bytes) const {
    return uint64_t{offset} + frame_bytes <= region_bytes_;
}

void CerfVirtFramebuffer::MarkWritten(const uint8_t* at) {
    if (visible_written_.load(std::memory_order_relaxed)) return;
    const uintptr_t first = reinterpret_cast<uintptr_t>(bytes_) + ScanoutOffset();
    const uintptr_t byte  = reinterpret_cast<uintptr_t>(at);
    if (byte >= first && byte < first + SizeBytes()) visible_written_.store(true, std::memory_order_release);
}

void CerfVirtFramebuffer::SetScanoutOffset(uint32_t offset) {
    std::lock_guard<std::mutex> lock(mode_mu_);
    if (!FrameFitsRegion(offset, SizeBytes()))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] scanout offset 0x%X with a %u B frame runs past "
                              "the %u B region", offset, SizeBytes(), region_bytes_);
    scanout_.store(offset, std::memory_order_release);
}

void CerfVirtFramebuffer::SetLayer(const CerfVirtLayer& layer) {
    if (layer.pa != 0u) {
        if (layer.bpp != 16u && layer.bpp != 24u && layer.bpp != 32u)
            emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] layer at 0x%08X has %u bpp", layer.pa, layer.bpp);
        const uint64_t row_bytes = (uint64_t{layer.width} * layer.bpp + 7u) >> 3u;
        const uint64_t bytes     = uint64_t{layer.stride} * layer.height;
        if (layer.width == 0u || layer.height == 0u || layer.stride < row_bytes ||
            !emu_.Get<EmulatedMemory>().TryTranslateRange(layer.pa, bytes))
            emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] layer %ux%u, stride %u, at 0x%08X is not "
                                  "backed guest memory", layer.width, layer.height, layer.stride,
                                  layer.pa);
    }
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        layer_ = layer;
    }
    if (layer.pa != 0u) visible_written_.store(true, std::memory_order_release);
}

void CerfVirtFramebuffer::SetOverlay(const CerfVirtOverlay& overlay) {
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        overlay_ = overlay;
    }
    if (overlay.layers != CerfVirt::kFbOverlayLayersBase) visible_written_.store(true, std::memory_order_release);
}

CerfVirtOverlay CerfVirtFramebuffer::Overlay() const {
    std::lock_guard<std::mutex> lock(mode_mu_);
    return overlay_;
}

bool CerfVirtFramebuffer::InRegion(uint32_t pa, uint64_t bytes) const {
    const uint64_t base = MemBasePa();
    return pa >= base && uint64_t{pa} + bytes <= base + region_bytes_;
}

CerfVirtScanout CerfVirtFramebuffer::Scanout() const {
    std::lock_guard<std::mutex> lock(mode_mu_);
    CerfVirtScanout s;
    if (layer_.pa != 0u) {
        s.width    = layer_.width;
        s.height   = layer_.height;
        s.bpp      = layer_.bpp;
        s.offset   = 0u;
        s.stride   = layer_.stride;
        s.guest_pa = layer_.pa;
        return s;
    }
    Geometry(s.width, s.height);
    s.bpp      = Bpp();
    s.offset   = ScanoutOffset();
    s.stride   = CerfVirt::FbRowBytes(s.width, s.bpp);
    s.guest_pa = 0u;
    return s;
}

uint32_t CerfVirtFramebuffer::MemBasePa() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase()
         + CerfVirt::kFramebufferMemOffset;
}

uint32_t CerfVirtFramebuffer::ConfiguredDepth() const {
    const uint32_t bpp = emu_.Get<BoardContext>().ResolveGuestAdditionsColorDepth();
    switch (bpp) {
        case 1u: case 2u: case 4u: case 8u: case 16u: case 24u: case 32u:
            return bpp;
        default:
            emu_.Get<Fatal>().UserError("[CerfVirtFramebuffer] guest-additions colour depth %u bpp; "
                                        "the depth must be 1, 2, 4, 8, 16, 24 or 32", bpp);
    }
}

void CerfVirtFramebuffer::LoadConfiguredPalette() {
    const uint32_t               bpp        = Bpp();
    const bool                   own_depth  = bpp == ConfiguredDepth();
    const auto                   live       = emu_.Get<DeviceConfig>().Live();
    const std::vector<uint32_t>& configured = live->guest_additions_palette;
    if (bpp > 8u) {
        if (own_depth && !configured.empty())
            emu_.Get<Fatal>().UserError("[CerfVirtFramebuffer] an indexed palette is set for the %u bpp "
                                        "guest-additions colour depth; only 1, 2, 4 and 8 bpp use a palette", bpp);
        return;
    }
    const DbGaPalette* const     preset = (own_depth && !configured.empty())
                                              ? nullptr
                                              : emu_.Get<GaPaletteDatabase>().DefaultFor(bpp);
    const std::vector<uint32_t>& colors = preset ? preset->colors : configured;
    const size_t                 entries = size_t{1} << bpp;
    if (colors.size() != entries)
        emu_.Get<Fatal>().UserError("[CerfVirtFramebuffer] the indexed palette has %zu colours; %u bpp needs %zu",
                                    colors.size(), bpp, entries);
    std::copy(colors.begin(), colors.end(), palette_);
    LOG(Periph, "[CerfVirtFramebuffer] %u bpp palette '%s' with %zu colours\n", bpp,
        preset ? preset->id.c_str() : "configured", entries);
}

void CerfVirtFramebuffer::OnReady() {
    const auto live = emu_.Get<DeviceConfig>().Live();
    StoreGeometry(live->board_configurable_screen_width, live->board_configurable_screen_height);
    bpp_.store(ConfiguredDepth(), std::memory_order_release);
    LoadConfiguredPalette();
    region_bytes_ = ComputeRegionBytes();
    if (emu_.Get<EmulatedMemory>().OverlapsRegion(MemBasePa(), region_bytes_))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] FB region at 0x%08X, %u B, overlaps backed "
                              "memory of this board", MemBasePa(), region_bytes_);
    ReservePrimary();
    bytes_ = host_platform::AllocateZeroedTracked(region_bytes_);
    if (!bytes_)
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] allocation of the %u B FB region failed",
                              region_bytes_);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        {
            std::lock_guard<std::mutex> lock(mode_mu_);
            scanout_.store(0u, std::memory_order_release);
            layer_   = CerfVirtLayer{};
            overlay_ = CerfVirtOverlay{};
        }
        backlight_percent_.store(0u, std::memory_order_release);
        ReapplyConfiguredSize();
        ReapplyConfiguredDepth();
        LoadConfiguredPalette();
        ClearContent();
        TurnBackForReset();
    });
    LOG(Periph, "[CerfVirtFramebuffer] %ux%u %ubpp stride=%u "
                "fb_size=%u region=%u bytes (offscreen=%u bytes)\n",
        Width(), Height(), Bpp(), Stride(), SizeBytes(),
        region_bytes_, region_bytes_ - PrimaryReserveBytes());
}

void CerfVirtFramebuffer::SaveState(StateWriter& w) {
    static_assert(StateVisitCoversAllBytes<CerfVirtOverlay>(
                      [](CerfVirtOverlay& s, StateFieldBytes& f) { CerfVirtOverlay::Visit(s, f); }),
                  "CerfVirtOverlay::Visit must name every field of CerfVirtOverlay");
    uint32_t gw = 0, gh = 0, bpp = 0, scanout = 0;
    CerfVirtLayer   layer{};
    CerfVirtOverlay overlay{};
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        Geometry(gw, gh);
        bpp     = Bpp();
        scanout = ScanoutOffset();
        layer   = layer_;
        overlay = overlay_;
    }
    w.Write("bpp", bpp);
    w.Write("width", gw);
    w.Write("height", gh);
    w.Write("quarter", Quarter());
    w.Write("primary_reserve", PrimaryReserveBytes());
    w.Write("scanout_offset", scanout);
    w.Write("layer_pa", layer.pa);
    w.Write("layer_width", layer.width);
    w.Write("layer_height", layer.height);
    w.Write("layer_stride", layer.stride);
    w.Write("layer_bpp", layer.bpp);
    StateWriteField overlay_field(w);
    CerfVirtOverlay::Visit(overlay, overlay_field);
    w.Write("backlight_percent", BacklightPercent());
    w.Write<uint8_t>("visible_written", HasContent() ? 1u : 0u);
    for (uint32_t i = 0; i < 256u; ++i) w.Write("palette", palette_[i]);
    std::vector<uint8_t> pages;
    TouchedPages(pages);
    w.WriteBytes("saved_pages", pages.data(), pages.size());
    for (uint32_t page = 0; page < PageCount(); ++page)
        if ((pages[page >> 3u] >> (page & 7u)) & 1u)
            w.WriteBytes("page", bytes_ + (size_t{page} << kPageShift), kPageBytes);
}

void CerfVirtFramebuffer::RestoreState(StateReader& r) {
    uint32_t bpp = 0;
    r.Read("bpp", bpp);
    if (bpp != Bpp())
        r.Reject("the guest display ran at %u bpp, this machine is at %u bpp", bpp, Bpp());
    uint32_t w = 0, h = 0, quarter = 0;
    r.Read("width", w);
    r.Read("height", h);
    r.Read("quarter", quarter);
    uint32_t reserve = 0;
    r.Read("primary_reserve", reserve);
    const bool turned = TurnTo(quarter) != 0u;
    primary_reserve_.store(reserve, std::memory_order_release);
    uint32_t scanout = 0;
    r.Read("scanout_offset", scanout);
    CerfVirtLayer layer{};
    r.Read("layer_pa", layer.pa);
    r.Read("layer_width", layer.width);
    r.Read("layer_height", layer.height);
    r.Read("layer_stride", layer.stride);
    r.Read("layer_bpp", layer.bpp);
    CerfVirtOverlay overlay{};
    StateReadField  overlay_field(r);
    CerfVirtOverlay::Visit(overlay, overlay_field);
    uint32_t backlight = 0;
    r.Read("backlight_percent", backlight);
    backlight_percent_.store(backlight, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        StoreGeometry(w, h);
        scanout_.store(scanout, std::memory_order_release);
        layer_   = layer;
        overlay_ = overlay;
    }
    uint8_t written = 0;
    r.Read("visible_written", written);
    visible_written_.store(written != 0, std::memory_order_release);
    for (uint32_t i = 0; i < 256u; ++i) r.Read("palette", palette_[i]);
    std::vector<uint8_t> saved((PageCount() + 7u) >> 3u, 0u);
    r.ReadBytes("saved_pages", saved.data(), saved.size());
    std::vector<uint8_t> stale;
    TouchedPages(stale);
    for (size_t i = 0; i < stale.size(); ++i) stale[i] &= static_cast<uint8_t>(~saved[i]);
    ZeroPages(stale);
    for (uint32_t page = 0; page < PageCount(); ++page)
        if ((saved[page >> 3u] >> (page & 7u)) & 1u)
            r.ReadBytes("page", bytes_ + (size_t{page} << kPageShift), kPageBytes);
    if (turned) emu_.Get<HostPresentation>().FrameTurned();
}

void CerfVirtFramebuffer::TouchedPages(std::vector<uint8_t>& bitmap) {
    bitmap.assign((PageCount() + 7u) >> 3u, 0u);
    if (!host_platform::TouchedPages(bytes_, region_bytes_, kPageBytes, bitmap))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] the host did not report the touched pages of the %u B "
                              "FB region", region_bytes_);
}

void CerfVirtFramebuffer::ZeroPages(const std::vector<uint8_t>& bitmap) {
    if (!host_platform::ZeroPages(bytes_, region_bytes_, kPageBytes, bitmap))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] the host did not zero pages of the %u B FB region",
                              region_bytes_);
}

void CerfVirtFramebuffer::ClearContent() {
    std::vector<uint8_t> pages;
    TouchedPages(pages);
    ZeroPages(pages);
    visible_written_.store(false, std::memory_order_release);
}

void CerfVirtFramebuffer::ReapplyConfiguredSize() {
    const DeviceConfig& config = emu_.Get<DeviceConfig>();
    if (config.adopt_guest_additions_resolution_for_host_screen) return;
    const auto     live   = config.Live();
    const uint32_t want_w = live->board_configurable_screen_width;
    const uint32_t want_h = live->board_configurable_screen_height;
    uint32_t       was_w = 0, was_h = 0;
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        Geometry(was_w, was_h);
        if (want_w == was_w && want_h == was_h) return;
        StoreGeometry(want_w, want_h);
    }
    ReservePrimary();
    LOG(Periph, "[CerfVirtFramebuffer] reset to the configured %ux%u from %ux%u primary reserve=%u bytes\n",
        want_w, want_h, was_w, was_h, PrimaryReserveBytes());
    ShowHostMode(want_w, want_h);
}

void CerfVirtFramebuffer::ReapplyConfiguredDepth() {
    const uint32_t want = ConfiguredDepth();
    const uint32_t was  = Bpp();
    if (want == was) return;
    uint32_t w = 0, h = 0;
    bool     fits = false;
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        Geometry(w, h);
        fits = FrameFitsRegion(ScanoutOffset(), PrimaryBytesAt(w, h, want));
        if (fits) bpp_.store(want, std::memory_order_release);
    }
    if (!fits) {
        char msg[192];
        snprintf(msg, sizeof(msg), "Colour depth %u bpp at %ux%u needs %llu B; this "
                 "board's display memory holds %u B. Staying at %u bpp.",
                 want, w, h, (unsigned long long)PrimaryBytesAt(w, h, want),
                 region_bytes_, was);
        LOG(Caution, "[CerfVirtFramebuffer] %s\n", msg);
        emu_.Get<HwScreen>().AddLine(msg);
        return;
    }
    ReservePrimary();
    LOG(Periph, "[CerfVirtFramebuffer] colour depth %ubpp -> %ubpp "
                "stride=%u region=%u bytes primary reserve=%u bytes\n",
        was, want, Stride(), region_bytes_, PrimaryReserveBytes());
}

bool CerfVirtFramebuffer::FitsPrimary(uint32_t w, uint32_t h, const char* who) const {
    if (PrimaryFitsReserve(w, h)) return true;
    LOG(Caution, "[CerfVirtFramebuffer] %s %ux%u (%llu B) does not fit the primary "
                 "reserve %u B; ignoring\n", who, w, h, (unsigned long long)PrimaryBytesAt(w, h, Bpp()),
        PrimaryReserveBytes());
    return false;
}

uint32_t CerfVirtFramebuffer::ApplyGuestMode(uint32_t w, uint32_t h, uint32_t quarter) {
    if (quarter >= CerfVirt::kRszQuarterTurns)
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] guest applied %u quarter turns", quarter);
    if (!PrimaryFitsReserve(w, h))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] guest applied %ux%u past the primary reserve %u B",
                              w, h, PrimaryReserveBytes());
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        if (!FrameFitsRegion(ScanoutOffset(), PrimaryBytesAt(w, h, Bpp())))
            emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] guest applied %ux%u with the scanout at 0x%X "
                                  "of the %u B region", w, h, ScanoutOffset(), region_bytes_);
        StoreGeometry(w, h);
    }
    const uint32_t turn = TurnTo(quarter);
    LOG(Periph, "[CerfVirtFramebuffer] guest re-moded to %ux%u stride=%u quarter=%u\n",
        w, h, Stride(), quarter);
    return turn;
}

uint32_t CerfVirtFramebuffer::TurnTo(uint32_t quarter) {
    return CerfVirt::QuarterTurnsFrom(quarter_.exchange(quarter, std::memory_order_acq_rel), quarter);
}

bool CerfVirtFramebuffer::SetHostMode(uint32_t w, uint32_t h) {
    if (!FitsPrimary(w, h, "host mode")) return false;
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        scanout_.store(0u, std::memory_order_release);
        StoreGeometry(w, h);
    }
    LOG(Periph, "[CerfVirtFramebuffer] host mode %ux%u stride=%u\n", w, h, Stride());
    emu_.Get<HostPresentation>().GuestRemoded(w, h);
    return true;
}

void CerfVirtFramebuffer::ShowHostMode(uint32_t w, uint32_t h) {
    if (SetHostMode(w, h)) emu_.Get<HostPresentation>().HostModeSet(w, h);
}

void CerfVirtFramebuffer::TurnBackForReset() {
    const uint32_t turn = TurnTo(0u);
    if (turn == 0u) return;
    LOG(Periph, "[CerfVirtFramebuffer] reset turned the display back by %u quarter turns\n", turn);
    emu_.Get<HostPresentation>().FrameTurned();
}

}
