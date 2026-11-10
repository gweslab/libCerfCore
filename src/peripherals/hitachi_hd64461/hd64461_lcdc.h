#pragma once

#include <array>
#include <cstdint>
#include <mutex>

#include "../../lcd/display_mode_latch.h"
#include "../../socs/reg_window_impl.h"
#include "hd64461_panel.h"

namespace cerfcore {

class Hd64461SystemControl;

namespace cerf_hd64461_lcdc_detail {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr RegWindowModel kModel = {
    0x10001000u,
    0x38u,
    28u,
    false,
    {
        {ReadKind::kStored, WriteKind::kStored, 0x3FFFu, 0x0000u, 0xC000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x0280u, 0xF800u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kSampled, WriteKind::kStored, 0x01DFu, 0x0001u, 0xF888u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {},
        {},
        {},
        {},
        {},
        {ReadKind::kStored, WriteKind::kStored, 0x0101u, 0x0001u, 0xFEFEu, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x07FFu, 0x0002u, 0xFF38u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x9F9Fu, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x0FFFu, 0x0A80u, 0xF000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x03FFu, 0x00EFu, 0xFC00u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x03FFu, 0x00EFu, 0xFC00u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0x68F0u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x03FFu, 0x0004u, 0xFC10u, OtherReset::kRetain, 0x0000u, 0x0200u},
        {ReadKind::kStored, WriteKind::kStored, 0x03FFu, 0x020Bu, 0xFC00u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kStored, WriteKind::kStored, 0x03FFu, 0x01EAu, 0xFC00u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kFatal, WriteKind::kStored, 0x000Fu, 0x000Cu, 0xFFF0u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {},
        {},
        {},
        {},
        {},
        {ReadKind::kFatal, WriteKind::kStored, 0xFF00u, 0x0000u, 0x00FFu, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kFatal, WriteKind::kStored, 0x003Fu, 0x0000u, 0xFFC0u, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kFatal, WriteKind::kStored, 0xFF00u, 0x0000u, 0x00FFu, OtherReset::kRetain, 0x0000u, 0x0000u},
        {ReadKind::kSampled, WriteKind::kFatal, 0x0000u, 0x0000u, 0x0000u, OtherReset::kRetain, 0x0000u, 0x0000u},
    },
};

}

class Hd64461Lcdc
    : public cerf_reg_window_detail::RegWindow<cerf_hd64461_lcdc_detail::kModel, Peripheral, std::mutex> {
public:
    static constexpr uint32_t kDisplayMemoryBytes = 512u * 1024u;

    enum class Format : uint8_t {
        kUndefined,
        kIndexed8,
        kRgb565,
        kGray64,
        kGray16,
        kGray4,
        kGray2,
    };

    struct Scanout {
        bool                     presenting       = false;
        const uint8_t*           fb               = nullptr;
        uint32_t                 stride           = 0u;
        uint32_t                 line_step        = 1u;
        uint32_t                 dots             = 0u;
        uint32_t                 lines            = 0u;
        uint32_t                 frame_bytes      = 0u;
        uint32_t                 presented_width  = 0u;
        Format                   format           = Format::kUndefined;
        bool                     dinv             = false;
        bool                     palette_complete = false;
        std::array<uint8_t, 768> palette{};
    };

    using RegWindow::RegWindow;

    bool ShouldRegister() override;
    void OnReady() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    Scanout  GetScanout();
    Format   CurrentFormat();
    uint8_t* DisplayMemory() const { return vram_; }
    bool     DisplayMemoryAccessible();
    void     OnModuleClockChanged();

protected:
    uint16_t SampleRegister(uint32_t i, uint32_t addr) override;
    void     BeforeStore(uint32_t i, uint16_t value) override;
    void     AfterStore(uint32_t i) override;

private:
    bool     MemoryInterfaceRunning() const;
    void     OnPanelPowerChanged();
    void     PublishPresentingLocked();
    bool     Presenting() const;
    Format   DecodeFormat() const;
    uint32_t LineStep() const;
    uint32_t Dots() const;
    uint32_t Lines() const;
    uint32_t PresentedWidth() const;
    uint32_t FrameBytes() const;
    void     ApplyStandby();

    uint8_t*                    vram_         = nullptr;
    const Hd64461Panel*         panel_        = nullptr;
    const Hd64461SystemControl* system_       = nullptr;
    bool                        refresh_lost_ = false;
    std::array<uint8_t, 768>    palette_{};
    std::array<uint8_t, 768>    palette_defined_{};
    uint32_t                    write_index_ = 0u;
    uint32_t                    write_phase_ = 0u;
    uint32_t                    read_index_  = 0u;
    uint32_t                    read_phase_  = 0u;
    bool                        epon_        = false;
    DisplayModeLatch            mode_latch_;
};

}
