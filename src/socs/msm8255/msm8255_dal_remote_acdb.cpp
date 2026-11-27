#include "msm8255_dal_remote_device.h"
#include "msm8255_dal_remote_server.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>

namespace cerfcore {

namespace {

constexpr uint32_t kAcdbDeviceId = 0x02000069u;
constexpr uint32_t kAcdbVersion  = 0x00010000u;

constexpr uint32_t kMethodIoctl = 9u;

constexpr uint32_t kCmdGetQdsp5Params     = 0x000111CCu;
constexpr uint32_t kCmdGetAdieProfileSize = 0x000111CFu;
constexpr uint32_t kCmdGetLpaParams       = 0x000111E4u;
constexpr uint32_t kCmdGetDtmfVolMaxStep  = 0x000111E7u;
constexpr uint32_t kCmdGetDtmfVolTableA   = 0x000111E8u;
constexpr uint32_t kCmdGetDtmfVolTableB   = 0x000111E9u;
constexpr uint32_t kCmdGetDevice          = 0x0108BB92u;
constexpr uint32_t kCmdGetDeviceTable     = 0x0108BB97u;
constexpr uint32_t kCmdGetDeviceInfo      = 0x0108F5CBu;
constexpr uint32_t kCmdGetVoiceVolMaxStep = 0x010A190Eu;
constexpr uint32_t kCmdGetAudioVolLevel   = 0x010A1A85u;
constexpr uint32_t kCmdGetAudioVolTable   = 0x010A1A86u;

constexpr uint32_t kCommandBytes = 4u;
constexpr uint32_t kResultBytes  = 16u;

struct RequestShape {
    uint32_t bytes     = 0;
    uint32_t off_total = 0;
    uint32_t off_phys  = 0;
};

constexpr RequestShape kScalarRequest{12u, 4u, 8u};
constexpr RequestShape kSelectorRequest{16u, 8u, 12u};
constexpr RequestShape kTableRequest{24u, 16u, 20u};
constexpr RequestShape kDeviceRequest{32u, 24u, 28u};

constexpr uint32_t kAdieProfileSizeBytes = 4u;
constexpr uint32_t kVolMaxStepBytes      = 2u;
constexpr uint32_t kVolLevelBytes        = 404u;
constexpr uint32_t kDtmfVolTableBytes    = 202u;
constexpr uint32_t kLpaParamsBytes       = 22u;
constexpr uint32_t kDeviceInfoBytes      = 24u;
constexpr uint32_t kQdsp5ParamsBytes     = 8u;
constexpr uint32_t kDeviceTableBytes     = 3256u;
constexpr uint32_t kDeviceBytes          = 4u;

constexpr uint32_t kOffCommand = 0u;
constexpr uint32_t kOffWord1   = 4u;

constexpr uint32_t kOffResultStatus = 12u;

constexpr uint32_t kAcdbResFailure = 0xFFFFFFFFu;

class Msm8255DalRemoteAcdb : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kAcdbDeviceId; }
    uint32_t InterfaceVersion() const override { return kAcdbVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents&) override {
        if (call.shape != kMsm8255DalShapeBuffers || call.method != kMethodIoctl ||
            call.out_bytes != kResultBytes || call.in_bytes < kCommandBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote acdb: method %u in argument shape %u with %u input "
                "and %u output bytes is not modeled",
                call.method, call.shape, call.in_bytes, call.out_bytes);
        }
        const uint32_t command = emu_.Get<EmulatedMemory>().ReadWord(call.in_pa + kOffCommand);
        switch (command) {
        case kCmdGetAdieProfileSize: return Fail(call, command, kScalarRequest, {kAdieProfileSizeBytes});
        case kCmdGetVoiceVolMaxStep: return Fail(call, command, kScalarRequest, {kVolMaxStepBytes});
        case kCmdGetDtmfVolMaxStep:  return Fail(call, command, kScalarRequest, {kVolMaxStepBytes});
        case kCmdGetAudioVolLevel:   return Fail(call, command, kScalarRequest, {kVolMaxStepBytes, kVolLevelBytes});
        case kCmdGetDeviceInfo:      return Fail(call, command, kSelectorRequest, {kDeviceInfoBytes});
        case kCmdGetQdsp5Params:     return Fail(call, command, kSelectorRequest, {kQdsp5ParamsBytes});
        case kCmdGetAudioVolTable:   return Fail(call, command, kSelectorRequest, {kVolLevelBytes});
        case kCmdGetLpaParams:       return Fail(call, command, kSelectorRequest, {kLpaParamsBytes});
        case kCmdGetDtmfVolTableA:
        case kCmdGetDtmfVolTableB:   return Fail(call, command, kSelectorRequest, {kDtmfVolTableBytes});
        case kCmdGetDeviceTable:     return Fail(call, command, kTableRequest, {kDeviceTableBytes});
        case kCmdGetDevice:          return Fail(call, command, kDeviceRequest, {kDeviceBytes});
        default:
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote acdb: command 0x%08X with %u input bytes is not modeled",
                command, call.in_bytes);
        }
    }

private:
    uint32_t Fail(const Msm8255DalRemoteCall& call, uint32_t command, const RequestShape& shape,
                  std::initializer_list<uint32_t> widths) {
        auto& mem = emu_.Get<EmulatedMemory>();
        if (call.in_bytes != shape.bytes) {
            emu_.Get<Fatal>().Die("msm8255 dal remote acdb: command 0x%08X of %u bytes is not modeled", command,
                                  call.in_bytes);
        }
        const uint32_t total = mem.ReadWord(call.in_pa + shape.off_total);
        if (std::find(widths.begin(), widths.end(), total) == widths.end()) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote acdb: command 0x%08X of %u bytes with a %u-byte buffer is not modeled",
                command, call.in_bytes, total);
        }
        LOG(SocModem, "msm8255 dal remote acdb: command 0x%08X, word 1 0x%X, %u bytes at 0x%08X, answered ACDB_RES_FAILURE\n",
            command, mem.ReadWord(call.in_pa + kOffWord1), total, mem.ReadWord(call.in_pa + shape.off_phys));
        mem.WriteWord(call.out_pa + kOffResultStatus, kAcdbResFailure);
        return kMsm8255DalSuccess;
    }
};

}

REGISTER_SERVICE(Msm8255DalRemoteAcdb);

}
