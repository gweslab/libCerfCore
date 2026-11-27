#include "msm8255_dal_remote_device.h"
#include "msm8255_dal_remote_server.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kAxiDeviceId            = 0x02000053u;
constexpr uint32_t kAxiVersion             = 0x00010004u;
constexpr uint32_t kMethodConfigureBridge  = 20u;
constexpr uint32_t kCfg3dGrpSyncMode  = 19u;
constexpr uint32_t kCfg3dGrpAsyncMode = 20u;

class Msm8255DalRemoteAxi : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kAxiDeviceId; }
    uint32_t InterfaceVersion() const override { return kAxiVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents&) override {
        if (call.shape != kMsm8255DalShapeWord || call.method != kMethodConfigureBridge) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote axi: method %u in argument shape %u is not modeled",
                call.method, call.shape);
        }
        const uint32_t cfg = emu_.Get<EmulatedMemory>().ReadWord(call.in_pa);
        if (cfg != kCfg3dGrpSyncMode && cfg != kCfg3dGrpAsyncMode) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote axi: bridge configuration %u is not modeled", cfg);
        }
        return kMsm8255DalSuccess;
    }
};

}

REGISTER_SERVICE(Msm8255DalRemoteAxi);

}
