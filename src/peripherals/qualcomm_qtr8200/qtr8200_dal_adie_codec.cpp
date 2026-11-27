#include "../../core/board_parts.h"
#include "../../socs/msm8255/msm8255_dal_remote_device.h"
#include "../../socs/msm8255/msm8255_dal_remote_server.h"
#include "qualcomm_qtr8200_part_id.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kAdieCodecDeviceId = 0x02000065u;
constexpr uint32_t kAdieCodecVersion  = 0x00010000u;

constexpr uint32_t kMethodOpen          = 4u;
constexpr uint32_t kMethodClose         = 5u;
constexpr uint32_t kMethodRegisterCodec = 9u;
constexpr uint32_t kMethodReadSlaveId   = 11u;

constexpr uint32_t kOpenMode              = 1u;
constexpr uint32_t kCloseArgument         = 0u;
constexpr uint32_t kRegisteredCodec       = 3u;
constexpr uint32_t kRegisterCodecBytes    = 4u;
constexpr uint32_t kSlaveIdRequestBytes   = 12u;
constexpr uint32_t kSlaveIdReplyBytes     = 4u;
constexpr uint32_t kSlaveRequestParam     = 255u;

constexpr uint32_t kSlaveIndexCodec   = 2u;
constexpr uint32_t kCodecSlaveAddress = 0x77u;

constexpr uint32_t kOffSlaveIndex  = 0u;
constexpr uint32_t kOffSlaveParam1 = 4u;
constexpr uint32_t kOffSlaveParam2 = 8u;

class Qtr8200DalAdieCodec : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        return emu_.Get<BoardParts>().Has(PartId::QualcommQtr8200);
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kAdieCodecDeviceId; }
    uint32_t InterfaceVersion() const override { return kAdieCodecVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents&) override {
        if (call.shape == kMsm8255DalShapeWord) {
            switch (call.method) {
            case kMethodOpen:  return ExpectWord(call, kOpenMode);
            case kMethodClose: return ExpectWord(call, kCloseArgument);
            default: break;
            }
        } else if (call.shape == kMsm8255DalShapeInput &&
                   call.method == kMethodRegisterCodec && call.in_bytes == kRegisterCodecBytes) {
            return ExpectWord(call, kRegisteredCodec);
        } else if (call.shape == kMsm8255DalShapeBuffers &&
                   call.method == kMethodReadSlaveId) {
            return ReadSlaveId(call);
        }
        emu_.Get<Fatal>().Die(
            "qtr8200 dal adie codec: method %u in argument shape %u with %u input bytes is not modeled",
            call.method, call.shape, call.in_bytes);
    }

private:
    uint32_t ExpectWord(const Msm8255DalRemoteCall& call, uint32_t expected) {
        const uint32_t word = emu_.Get<EmulatedMemory>().ReadWord(call.in_pa);
        if (word != expected) {
            emu_.Get<Fatal>().Die(
                "qtr8200 dal adie codec: method %u with argument %u is not modeled",
                call.method, word);
        }
        return kMsm8255DalSuccess;
    }

    uint32_t ReadSlaveId(const Msm8255DalRemoteCall& call) {
        auto& mem = emu_.Get<EmulatedMemory>();
        if (call.in_bytes != kSlaveIdRequestBytes || call.out_bytes != kSlaveIdReplyBytes) {
            emu_.Get<Fatal>().Die(
                "qtr8200 dal adie codec: a slave id read with %u input and %u output bytes is not modeled",
                call.in_bytes, call.out_bytes);
        }
        const uint32_t index  = mem.ReadWord(call.in_pa + kOffSlaveIndex);
        const uint32_t param1 = mem.ReadWord(call.in_pa + kOffSlaveParam1);
        const uint32_t param2 = mem.ReadWord(call.in_pa + kOffSlaveParam2);
        if (index != kSlaveIndexCodec || param1 != kSlaveRequestParam ||
            param2 != kSlaveRequestParam) {
            emu_.Get<Fatal>().Die(
                "qtr8200 dal adie codec: a slave id read of slave %u (%u, %u) is not modeled",
                index, param1, param2);
        }
        mem.WriteWord(call.out_pa, kCodecSlaveAddress);
        return kMsm8255DalSuccess;
    }
};

}

REGISTER_SERVICE(Qtr8200DalAdieCodec);

}
