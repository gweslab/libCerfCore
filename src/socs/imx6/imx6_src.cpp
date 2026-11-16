#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/arm/arm_cpu.h>
#include "imx6_mmio_lane.h"

#include <algorithm>
#include <iterator>
#include "imx6_id.h"
#include "imx6_variant.h"

namespace cerfcore {

namespace {

constexpr uint32_t kScrReset              = 0x00000521u;
constexpr uint32_t kScrWarmResetEnable    = 1u << 0u;
constexpr uint32_t kScrMaskWdogRst        = 0x00000780u;
constexpr uint32_t kScrMaskWdogRstMasked  = 0x00000280u;
constexpr uint32_t kScrMaskWdogRstUnmasked = 0x00000500u;
constexpr uint32_t kScrStoredBits         = 0x020007E1u;

constexpr uint32_t kSrsrIppResetB     = 1u << 0u;
constexpr uint32_t kSrsrIppUserResetB = 1u << 3u;
constexpr uint32_t kSrsrWdogRstB      = 1u << 4u;
constexpr uint32_t kSrsrWarmBoot      = 1u << 16u;
constexpr uint32_t kSrsrW1cBits       = 0x0000007Du;

constexpr uint32_t kScr  = 0x00u;
constexpr uint32_t kSrsr = 0x08u;
constexpr uint32_t kSisr = 0x14u;
constexpr uint32_t kSimr = 0x18u;

constexpr uint32_t kMmdcFirst  = 0x10000000u;
constexpr uint32_t kEimCs0First = 0x08000000u;
constexpr uint32_t kEimCs0Last  = 0x0FFFFFFFu;
constexpr uint32_t kOcramFirst  = 0x00900000u;

class Imx6Src : public Peripheral, public SleepResumeVectorProvider {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        PowerOnReset();
        emu_.Get<GuestDeepSleep>().RegisterResumeVectorProvider(this);
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind kind) {
                if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
                OnSystemReset(kind);
            });
    }

    uint32_t MmioBase() const override { return 0x020D8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (IsReadableRegister(off)) {
            return regs_[off >> 2];
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kScr) {
            WriteScr(value);
            return;
        }
        if (off == kSrsr) {
            regs_[kSrsr >> 2] = (regs_[kSrsr >> 2] & ~(value & kSrsrW1cBits) & ~kSrsrWarmBoot) |
                                (value & kSrsrWarmBoot);
            return;
        }
        if (IsWritableRegister(off)) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("regs", regs_, sizeof(regs_));
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes("regs", regs_, sizeof(regs_));
    }

    void ApplyPendingResume() override {
        const uint32_t entry = regs_[0x20u >> 2];
        if (!IsValidPersistentEntry(entry))
            emu_.Get<Fatal>().Die("imx6-src: invalid PERSISTENT_ENTRY0");
        emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmCpu>().SetPendingResumeVector(entry);
    }

private:
    bool IsValidPersistentEntry(uint32_t entry) const {
        return entry >= kMmdcFirst || (entry >= kEimCs0First && entry <= kEimCs0Last) ||
               (entry >= kOcramFirst && entry <= emu_.Get<Imx6Variant>().OcramLast());
    }

    static bool IsReadableRegister(uint32_t off) {
        return off == kScr || off == kSrsr || off == kSisr || off == kSimr ||
               (off >= 0x20u && off <= 0x44u && (off & 3u) == 0u);
    }

    static bool IsWritableRegister(uint32_t off) {
        return off == kSimr || (off >= 0x20u && off <= 0x44u && (off & 3u) == 0u);
    }

    void PowerOnReset() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        regs_[kScr >> 2]  = kScrReset;
        regs_[kSrsr >> 2] = kSrsrIppResetB;
        regs_[kSimr >> 2] = 0x0000001Fu;
    }

    void OnSystemReset(ResetLineKind kind) {
        switch (kind) {
        case ResetLineKind::Rtc:
            PowerOnReset();
            return;
        case ResetLineKind::Watchdog:
            regs_[kSrsr >> 2] |= kSrsrWdogRstB;
            return;
        case ResetLineKind::Other:
            if ((regs_[kScr >> 2] & kScrWarmResetEnable) == 0u)
                regs_[kSrsr >> 2] |= kSrsrIppUserResetB;
            return;
        }
    }

    void WriteScr(uint32_t value) {
        const uint32_t mask_wdog = value & kScrMaskWdogRst;
        if ((value & ~kScrStoredBits) != 0u || mask_wdog == kScrMaskWdogRstMasked) {
            emu_.Get<Fatal>().Die("Imx6Src: SRC_SCR write 0x%08X asks for a reset, core enable "
                                  "or watchdog mask the model does not carry", value);
        }
        regs_[kScr >> 2] = (value & kScrStoredBits & ~kScrMaskWdogRst) | kScrMaskWdogRstUnmasked;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == kSrsr) {
            WriteWord(lane.address,
                      (regs_[kSrsr >> 2] & kSrsrWarmBoot & ~lane.mask) | (lane.value & lane.mask));
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    uint32_t regs_[0x48u / 4u]{};
};

}

REGISTER_SERVICE(Imx6Src);

}
