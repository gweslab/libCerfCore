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

constexpr uint32_t kVoiceDeviceId = 0x02000075u;
constexpr uint32_t kVoiceVersion  = 0x00010000u;

constexpr uint32_t kMethodVoiceCmd = 9u;

constexpr uint32_t kCmdVoiceInit = 1u;

constexpr uint32_t kHeaderBytes       = 8u;
constexpr uint32_t kInitCommandBytes  = 40u;
constexpr uint32_t kInitPayloadBytes  = 4u;

constexpr uint32_t kOffId      = 0u;
constexpr uint32_t kOffDataLen = 4u;

class Msm8255DalRemoteVoice : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kVoiceDeviceId; }
    uint32_t InterfaceVersion() const override { return kVoiceVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents&) override {
        if (call.shape != kMsm8255DalShapeInput || call.method != kMethodVoiceCmd ||
            call.in_bytes < kHeaderBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote voice: method %u in argument shape %u with %u input bytes is not modeled",
                call.method, call.shape, call.in_bytes);
        }
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t id       = mem.ReadWord(call.in_pa + kOffId);
        const uint32_t data_len = mem.ReadWord(call.in_pa + kOffDataLen);
        if (id != kCmdVoiceInit || data_len != kInitPayloadBytes ||
            call.in_bytes != kInitCommandBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote voice: command %u with a %u-byte payload in %u input bytes is not modeled",
                id, data_len, call.in_bytes);
        }
        return kMsm8255DalSuccess;
    }
};

}

REGISTER_SERVICE(Msm8255DalRemoteVoice);

}
