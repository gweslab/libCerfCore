#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace cerf_imx6_ipu_mem_detail {

constexpr uint32_t kSize = 0x00100000u;

template <uint32_t kBase> class Imx6IpuInternalMem : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
    }

    void ClearMemory(uint32_t offset, uint32_t length) {
        const uint32_t first = offset >> 2;
        const uint32_t words = (length + (offset & 3u) + 3u) >> 2;
        for (uint32_t i = 0; i < words; ++i) regs_[first + i] = 0u;
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        EnsureModelled("read8", addr);
        return static_cast<uint8_t>(regs_[off >> 2] >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        EnsureModelled("read16", addr);
        return static_cast<uint16_t>(regs_[off >> 2] >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        EnsureModelled("read32", addr);
        return regs_[(addr - kBase) >> 2];
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - kBase, sh = (off & 3u) * 8u;
        EnsureModelled("write8", addr);
        uint32_t& w = regs_[off >> 2];
        w = (w & ~(0xFFu << sh)) | (static_cast<uint32_t>(value) << sh);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase, sh = (off & 2u) * 8u;
        EnsureModelled("write16", addr);
        uint32_t& w = regs_[off >> 2];
        w = (w & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(value) << sh);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        EnsureModelled("write32", addr);
        regs_[(addr - kBase) >> 2] = value;
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t)); }

protected:
    void Reset() { regs_.fill(0u); }

    void EnsureModelled(const char* op, uint32_t addr) const {
        const uint32_t off = addr - kBase;
        for (const auto& window : kModelledWindows)
            if (off >= window[0] && off <= window[1]) return;
        HaltUnsupportedAccess(op, addr, 0);
    }

    static constexpr uint32_t kModelledWindows[4][2] = {
        {0x00000u, 0x013FFu}, {0x40000u, 0x40117u}, {0x60000u, 0x6FFFFu}, {0x80000u, 0x807FFu},
    };

    std::array<uint32_t, kSize / 4> regs_{};
};

}

}
