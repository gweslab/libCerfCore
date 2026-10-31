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
    bool     HasContent() const { return any_write_; }

    uint32_t RegionBytes() const { return region_bytes_; }

    uint32_t MemBasePa() const;

    uint32_t PrimaryReserveBytes() const { return primary_reserve_.load(std::memory_order_acquire); }

    uint32_t ScanoutOffset() const { return scanout_.load(std::memory_order_acquire); }
    void     SetScanoutOffset(uint32_t offset);
    CerfVirtScanout Scanout() const;

    uint8_t*       Bytes()       { return bytes_; }
    const uint8_t* Bytes() const { return bytes_; }

    void     SetPaletteEntry(uint32_t idx, uint32_t rgb) { if (idx < 256u) palette_[idx] = rgb; }
    uint32_t PaletteEntry(uint32_t idx) const { return idx < 256u ? palette_[idx] : 0u; }
    const uint32_t* Palette() const { return palette_; }

    void MarkDirty() { any_write_ = true; }

    void ClearContentEdge() { any_write_ = false; }

    void ClearContent();

    uint32_t ApplyGuestMode(uint32_t w, uint32_t h, uint32_t quarter);

    bool SetHostMode(uint32_t w, uint32_t h);

    bool FitsPrimary(uint32_t w, uint32_t h, const char* who) const;

    bool CanApply(uint32_t w, uint32_t h, uint32_t bpp) const;

    static uint64_t PrimaryBytesAt(uint32_t w, uint32_t h, uint32_t bpp);
    static uint32_t RegionBytesIn(uint32_t window_size);

    void ReapplyConfiguredDepth();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t ComputeRegionBytes() const;
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
    uint32_t region_bytes_= 0;
    std::atomic<uint32_t> primary_reserve_{0};
    mutable std::mutex    mode_mu_;
    bool     any_write_   = false;
};

}
