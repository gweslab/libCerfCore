#pragma once
#include <array>
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxDepthStencil {
public:
    AdrenoA2xxDepthStencil(uint32_t control, uint32_t info, uint32_t front_ref_mask, uint32_t back_ref_mask);

    template <typename ReadRegister>
    static AdrenoA2xxDepthStencil Read(ReadRegister&& reg) {
        const uint32_t control = reg(kRegDepthControl);
        const bool     stencil = (control & kStencilEnable) != 0u;
        return { control, (control & (kZEnable | kStencilEnable)) != 0u ? reg(kRegDepthInfo) : 0u,
                 stencil ? reg(kRegStencilRefMask) : 0u,
                 stencil && (control & kBackfaceEnable) != 0u ? reg(kRegStencilRefMaskBack) : 0u };
    }

    static uint32_t InfoBase(uint32_t info) { return info & 0xFFFFF000u; }
    static uint32_t InfoBytes(uint32_t info) { return (info & kFormat24_8) != 0u ? 4u : 2u; }
    static bool     InfoReserved(uint32_t info) { return (info & kInfoReservedMask) != 0u; }

    uint32_t    Control() const { return control_; }
    uint32_t    Info() const { return info_; }
    bool        Attached() const { return depth_test_ || stencil_test_; }
    bool        DepthTest() const { return depth_test_; }
    bool        MayWrite() const;
    uint32_t    Base() const { return InfoBase(info_); }
    uint32_t    Bytes() const { return InfoBytes(info_); }
    const char* Unsupported() const;

    struct Outcome {
        bool passes;
        bool writes;
    };
    Outcome Test(double depth, bool front, const uint8_t* stored, std::array<uint8_t, 4>& updated) const;

private:
    static constexpr uint32_t kRegDepthInfo          = 0x2002u;
    static constexpr uint32_t kRegStencilRefMaskBack = 0x210Cu;
    static constexpr uint32_t kRegStencilRefMask     = 0x210Du;
    static constexpr uint32_t kRegDepthControl       = 0x2200u;

    static constexpr uint32_t kStencilEnable  = 0x1u;
    static constexpr uint32_t kZEnable        = 0x2u;
    static constexpr uint32_t kZWriteEnable   = 0x4u;
    static constexpr uint32_t kBackfaceEnable = 0x80u;

    static constexpr uint32_t kFormat24_8       = 0x1u;
    static constexpr uint32_t kInfoReservedMask = 0xFFEu;

    struct Face {
        uint32_t func;
        uint32_t fail;
        uint32_t zpass;
        uint32_t zfail;
        uint32_t ref;
        uint32_t mask;
        uint32_t write_mask;
    };
    Face FaceFor(bool front) const;

    uint32_t control_;
    uint32_t info_;
    uint32_t front_ref_mask_;
    uint32_t back_ref_mask_;
    bool     depth_test_;
    bool     depth_write_;
    bool     stencil_test_;
};

}
