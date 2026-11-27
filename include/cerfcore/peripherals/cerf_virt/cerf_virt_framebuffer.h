#pragma once

#include "../../core/service.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

struct CerfVirtScanout {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t offset;
    uint32_t stride;
    uint32_t guest_pa;
};

struct CerfVirtLayer {
    uint32_t pa;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp;
};

struct CerfVirtOverlay {
    uint32_t layers        = 0;
    uint32_t format        = 0;
    uint32_t width         = 0;
    uint32_t height        = 0;
    uint32_t pa            = 0;
    uint32_t stride        = 0;
    uint32_t cb_offset     = 0;
    uint32_t cr_offset     = 0;
    uint32_t chroma_stride = 0;
    uint32_t src_left      = 0;
    uint32_t src_top       = 0;
    uint32_t src_right     = 0;
    uint32_t src_bottom    = 0;
    uint32_t dst_left      = 0;
    uint32_t dst_top       = 0;
    uint32_t dst_right     = 0;
    uint32_t dst_bottom    = 0;
    uint32_t clockwise     = 0;
    uint32_t select_x      = 0;
    uint32_t select_y      = 0;

    template <typename Self, typename Field>
    static constexpr void Visit(Self& s, Field& f) {
        f("overlay_layers", s.layers);
        f("overlay_format", s.format);
        f("overlay_width", s.width);
        f("overlay_height", s.height);
        f("overlay_pa", s.pa);
        f("overlay_stride", s.stride);
        f("overlay_cb_offset", s.cb_offset);
        f("overlay_cr_offset", s.cr_offset);
        f("overlay_chroma_stride", s.chroma_stride);
        f("overlay_src_left", s.src_left);
        f("overlay_src_top", s.src_top);
        f("overlay_src_right", s.src_right);
        f("overlay_src_bottom", s.src_bottom);
        f("overlay_dst_left", s.dst_left);
        f("overlay_dst_top", s.dst_top);
        f("overlay_dst_right", s.dst_right);
        f("overlay_dst_bottom", s.dst_bottom);
        f("overlay_clockwise", s.clockwise);
        f("overlay_select_x", s.select_x);
        f("overlay_select_y", s.select_y);
    }
};

class CerfVirtFramebuffer : public Service {
public:
    using Service::Service;
    ~CerfVirtFramebuffer() override;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Width()    const;
    uint32_t Height()   const;
    void     Geometry(uint32_t& w, uint32_t& h) const;
    uint32_t Bpp()      const { return bpp_.load(std::memory_order_acquire); }
    uint32_t Stride()   const;
    uint32_t SizeBytes()const;
    uint32_t Quarter()  const { return quarter_.load(std::memory_order_acquire); }
    bool     HasContent() const { return visible_written_.load(std::memory_order_acquire); }

    uint32_t RegionBytes() const { return region_bytes_; }

    uint32_t MemBasePa() const;

    uint32_t PrimaryReserveBytes() const { return primary_reserve_.load(std::memory_order_acquire); }

    uint32_t ScanoutOffset() const { return scanout_.load(std::memory_order_acquire); }
    void     SetScanoutOffset(uint32_t offset);
    void     SetLayer(const CerfVirtLayer& layer);
    CerfVirtScanout Scanout() const;
    void     SetOverlay(const CerfVirtOverlay& overlay);
    CerfVirtOverlay Overlay() const;
    void     SetBacklightPercent(uint32_t percent) { backlight_percent_.store(percent, std::memory_order_release); }
    uint32_t BacklightPercent() const { return backlight_percent_.load(std::memory_order_acquire); }
    bool     InRegion(uint32_t pa, uint64_t bytes) const;

    uint8_t*       Bytes()       { return bytes_; }
    const uint8_t* Bytes() const { return bytes_; }

    void     SetPaletteEntry(uint32_t idx, uint32_t rgb) { if (idx < 256u) palette_[idx] = rgb; }
    uint32_t PaletteEntry(uint32_t idx) const { return idx < 256u ? palette_[idx] : 0u; }
    const uint32_t* Palette() const { return palette_; }

    void MarkWritten(const uint8_t* at);

    void ClearContent();

    uint32_t ApplyGuestMode(uint32_t w, uint32_t h, uint32_t quarter);

    bool SetHostMode(uint32_t w, uint32_t h);
    void ShowHostMode(uint32_t w, uint32_t h);

    bool FitsPrimary(uint32_t w, uint32_t h, const char* who) const;

    bool CanApply(uint32_t w, uint32_t h, uint32_t bpp) const;

    static uint64_t PrimaryBytesAt(uint32_t w, uint32_t h, uint32_t bpp);
    static uint32_t RegionBytesIn(uint32_t window_size);

    void ReapplyConfiguredSize();
    void ReapplyConfiguredDepth();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t ComputeRegionBytes() const;
    uint32_t ConfiguredDepth() const;
    void     LoadConfiguredPalette();
    void     ReservePrimary();
    void     StoreGeometry(uint32_t w, uint32_t h);
    bool     PrimaryFitsReserve(uint32_t w, uint32_t h) const;
    bool     FrameFitsRegion(uint32_t offset, uint64_t frame_bytes) const;
    uint32_t TurnTo(uint32_t quarter);
    void     TurnBackForReset();

    static constexpr uint32_t kPageShift = 12u;
    static constexpr uint32_t kPageBytes = 1u << kPageShift;

    uint32_t PageCount() const { return region_bytes_ >> kPageShift; }
    void     TouchedPages(std::vector<uint8_t>& bitmap);
    void     ZeroPages(const std::vector<uint8_t>& bitmap);

    uint8_t* bytes_ = nullptr;
    uint32_t palette_[256] = { 0 };
    std::atomic<uint64_t> geometry_{0};
    std::atomic<uint32_t> bpp_{32};
    std::atomic<uint32_t> quarter_{0};
    std::atomic<uint32_t> scanout_{0};
    std::atomic<uint32_t> backlight_percent_{0};
    CerfVirtLayer         layer_{};
    CerfVirtOverlay       overlay_{};
    uint32_t region_bytes_= 0;
    std::atomic<uint32_t> primary_reserve_{0};
    mutable std::mutex    mode_mu_;
    std::atomic<bool>     visible_written_{false};
};

}
