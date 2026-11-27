#include "msm8255_dal_remote_device.h"
#include "msm8255_dal_remote_server.h"
#include "msm8255_gpio_banks.h"
#include "msm8255_gpio_bus.h"
#include "msm8255_gpio_pin_mux.h"
#include "msm8255_smem.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kTlmmDeviceId     = 0x0200002Bu;
constexpr uint32_t kTlmmVersion      = 0x00010001u;
constexpr uint32_t kMethodConfigGpio = 22u;
constexpr uint32_t kMethodBspOwner   = 23u;

constexpr char     kTlmmRegionName[] = "DALTLMM";
constexpr uint32_t kRegionTableOff  = 40u;
constexpr uint32_t kRecordBytes     = 16u;
constexpr uint32_t kRecordOwnerOff  = 4u;
constexpr uint32_t kBspOwner        = 0u;

constexpr uint32_t kPackedFormat = 0x20000000u;
constexpr uint32_t kFuncMask     = 0xFu;

constexpr uint32_t kPackedGpioShift  = 4u;
constexpr uint32_t kPackedGpioMask   = 0x3FFu;
constexpr uint32_t kPackedDirShift   = 14u;
constexpr uint32_t kPackedPullShift  = 15u;
constexpr uint32_t kPackedDriveShift = 17u;
constexpr uint32_t kPackedDriveMask  = 0xFu;

constexpr uint32_t kLegacyGpioShift  = 8u;
constexpr uint32_t kLegacyGpioMask   = 0xFFu;
constexpr uint32_t kLegacyDirShift   = 16u;
constexpr uint32_t kLegacyPullShift  = 17u;
constexpr uint32_t kLegacyDriveShift = 19u;
constexpr uint32_t kLegacyDriveMask  = 0x7u;

constexpr uint32_t kPullMask = 0x3u;

class Msm8255DalRemoteTlmm : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kTlmmDeviceId; }
    uint32_t InterfaceVersion() const override { return kTlmmVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents&) override {
        if (call.shape != kMsm8255DalShapeWord ||
            (call.method != kMethodConfigGpio && call.method != kMethodBspOwner)) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote tlmm: method %u in argument shape %u is "
                "not modeled", call.method, call.shape);
        }
        const uint32_t cfg    = emu_.Get<EmulatedMemory>().ReadWord(call.in_pa);
        const bool     packed = (cfg & kPackedFormat) != 0u;
        const uint32_t gpio   = packed ? (cfg >> kPackedGpioShift) & kPackedGpioMask
                                       : (cfg >> kLegacyGpioShift) & kLegacyGpioMask;
        if (gpio >= kMsm8255GpioPinCount) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote tlmm: config word 0x%08X names gpio %u, "
                "past the %u pins the controller addresses", cfg, gpio,
                kMsm8255GpioPinCount);
        }
        if (call.method == kMethodBspOwner) {
            SetOwner(gpio, kBspOwner);
            return kMsm8255DalSuccess;
        }
        const uint32_t dir   = (cfg >> (packed ? kPackedDirShift : kLegacyDirShift)) & 1u;
        const uint32_t pull  = (cfg >> (packed ? kPackedPullShift : kLegacyPullShift)) & kPullMask;
        const uint32_t drive = packed ? (cfg >> kPackedDriveShift) & kPackedDriveMask
                                      : (cfg >> kLegacyDriveShift) & kLegacyDriveMask;
        const uint32_t mux =
            pull | ((cfg & kFuncMask) << kMsm8255PinFuncShift) | (drive << kMsm8255PinDriveShift);
        emu_.Get<Msm8255GpioBus>().ApplyModemPinConfig(gpio, mux, dir != 0u);
        return kMsm8255DalSuccess;
    }

private:
    void SetOwner(uint32_t gpio, uint32_t owner) {
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t region = emu_.Get<Msm8255Smem>().DalRecordPa(kTlmmRegionName);
        if (region == 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote tlmm: the dal arena carries no %s region for "
                "the ownership change of gpio %u", kTlmmRegionName, gpio);
        }
        const uint32_t len    = mem.ReadWord(region);
        const uint32_t record = kRegionTableOff + kRecordBytes * gpio;
        if (record + kRecordOwnerOff + 4u > len) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote tlmm: the %u-byte %s region holds no record "
                "for gpio %u", len, kTlmmRegionName, gpio);
        }
        mem.WriteWord(region + record + kRecordOwnerOff, owner);
    }
};

}

REGISTER_SERVICE(Msm8255DalRemoteTlmm);

}
