#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_resize_regs.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
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
    return Width() * (Bpp() >> 3u);
}

uint32_t CerfVirtFramebuffer::SizeBytes() const {
    uint32_t w = 0, h = 0;
    Geometry(w, h);
    return h * w * (Bpp() >> 3u);
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
    return uint64_t{h} * w * (bpp >> 3u);
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

void CerfVirtFramebuffer::SetScanoutOffset(uint32_t offset) {
    std::lock_guard<std::mutex> lock(mode_mu_);
    if (!FrameFitsRegion(offset, SizeBytes()))
        emu_.Get<Fatal>().Die("[CerfVirtFramebuffer] scanout offset 0x%X with a %u B frame runs past "
                              "the %u B region", offset, SizeBytes(), region_bytes_);
    scanout_.store(offset, std::memory_order_release);
}

CerfVirtScanout CerfVirtFramebuffer::Scanout() const {
    std::lock_guard<std::mutex> lock(mode_mu_);
    CerfVirtScanout s;
    Geometry(s.width, s.height);
    s.bpp    = Bpp();
    s.offset = ScanoutOffset();
    return s;
}

uint32_t CerfVirtFramebuffer::MemBasePa() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase()
         + CerfVirt::kFramebufferMemOffset;
}

void CerfVirtFramebuffer::OnReady() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    StoreGeometry(cfg.board_configurable_screen_width, cfg.board_configurable_screen_height);
    bpp_.store(emu_.Get<BoardContext>().ResolveGuestAdditionsColorDepth(),
               std::memory_order_release);
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
        }
        ReapplyConfiguredDepth();
        ClearContent();
        TurnBackForReset();
    });
    LOG(Periph, "[CerfVirtFramebuffer] %ux%u %ubpp stride=%u "
                "fb_size=%u region=%u bytes (offscreen=%u bytes)\n",
        Width(), Height(), Bpp(), Stride(), SizeBytes(),
        region_bytes_, region_bytes_ - PrimaryReserveBytes());
}

void CerfVirtFramebuffer::SaveState(StateWriter& w) {
    uint32_t gw = 0, gh = 0;
    Geometry(gw, gh);
    w.Write("bpp", Bpp());
    w.Write("width", gw);
    w.Write("height", gh);
    w.Write("quarter", Quarter());
    w.Write("primary_reserve", PrimaryReserveBytes());
    w.Write("scanout_offset", ScanoutOffset());
    w.Write<uint8_t>("any_write", any_write_ ? 1u : 0u);
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
    quarter_.store(quarter, std::memory_order_release);
    primary_reserve_.store(reserve, std::memory_order_release);
    uint32_t scanout = 0;
    r.Read("scanout_offset", scanout);
    {
        std::lock_guard<std::mutex> lock(mode_mu_);
        StoreGeometry(w, h);
        scanout_.store(scanout, std::memory_order_release);
    }
    uint8_t aw = 0;
    r.Read("any_write", aw);
    any_write_ = (aw != 0);
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
    any_write_ = false;
}

void CerfVirtFramebuffer::ReapplyConfiguredDepth() {
    const uint32_t want = emu_.Get<BoardContext>().ResolveGuestAdditionsColorDepth();
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
    return true;
}

void CerfVirtFramebuffer::TurnBackForReset() {
    const uint32_t turn = TurnTo(0u);
    if (turn == 0u) return;
    LOG(Periph, "[CerfVirtFramebuffer] reset turned the display back by %u quarter turns\n", turn);
    emu_.Get<HostPresentation>().GuestTurned(turn);
}

}
