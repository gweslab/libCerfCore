#include "msm8255_dal_remote_device.h"
#include "msm8255_dal_remote_server.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../peripherals/battery_pack.h"

#include <cstdint>
#include <cstring>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kAdcDeviceId = 0x02000067u;
constexpr uint32_t kAdcVersion  = 0x00010001u;

constexpr uint32_t kMethodRequestConversion  = 9u;
constexpr uint32_t kMethodGetInputProperties = 11u;

constexpr uint32_t kNameCapacity      = 16u;
constexpr uint32_t kPropertiesBytes   = 8u;
constexpr uint32_t kRequestBytes      = 12u;
constexpr uint32_t kRequestStatusBytes = 4u;
constexpr uint32_t kResultBytes       = 32u;
constexpr uint32_t kResultPhysicalOff = 0x10u;

constexpr uint32_t kAdcDeviceIndex = 0u;

constexpr uint32_t kChargerInputMv   = 5000u;
constexpr uint32_t kNoChargerInputMv = 0u;

constexpr uint32_t kOffPropsDevice  = 0u;
constexpr uint32_t kOffPropsChannel = 4u;
constexpr uint32_t kOffRequestEvent = 8u;

enum : uint32_t { kChannelVbatt = 0u, kChannelVchg = 1u };

struct AdcChannel {
    const char* name;
    uint32_t    index;
};

constexpr AdcChannel kChannels[] = {
    {"VBATT", kChannelVbatt},
    {"VCHG", kChannelVchg},
};

class Msm8255DalRemoteAdc : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        battery_ = &emu_.Get<BatteryPack>();
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
    }

    uint32_t DeviceId() const override { return kAdcDeviceId; }
    uint32_t InterfaceVersion() const override { return kAdcVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents& events) override {
        if (call.shape != kMsm8255DalShapeBuffers) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: method %u in argument shape %u is not "
                "modeled", call.method, call.shape);
        }
        switch (call.method) {
        case kMethodGetInputProperties: return GetInputProperties(call);
        case kMethodRequestConversion:  return RequestConversion(call, events);
        default:
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: method %u is not modeled", call.method);
        }
    }

private:
    uint32_t GetInputProperties(const Msm8255DalRemoteCall& call) {
        auto& mem = emu_.Get<EmulatedMemory>();
        if (call.in_bytes == 0u || call.in_bytes > kNameCapacity ||
            call.out_bytes != kPropertiesBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: an input lookup with a %u-byte name "
                "and a %u-byte result is not modeled", call.in_bytes,
                call.out_bytes);
        }
        char name[kNameCapacity + 1u] = {};
        for (uint32_t i = 0; i < call.in_bytes; ++i) {
            name[i] = static_cast<char>(mem.ReadByte(call.in_pa + i));
        }
        if (name[call.in_bytes - 1u] != '\0') {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: the %u-byte input name is not "
                "terminated", call.in_bytes);
        }
        for (const AdcChannel& channel : kChannels) {
            if (std::strcmp(name, channel.name) != 0) continue;
            mem.WriteWord(call.out_pa + kOffPropsDevice, kAdcDeviceIndex);
            mem.WriteWord(call.out_pa + kOffPropsChannel, channel.index);
            return kMsm8255DalSuccess;
        }
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote adc: input \"%s\" is not modeled", name);
    }

    uint32_t RequestConversion(const Msm8255DalRemoteCall& call,
                               Msm8255DalRemoteEvents& events) {
        auto& mem = emu_.Get<EmulatedMemory>();
        if (call.in_bytes != kRequestBytes || call.out_bytes != kRequestStatusBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: a conversion request with %u input "
                "and %u output bytes is not modeled", call.in_bytes,
                call.out_bytes);
        }
        const uint32_t device  = mem.ReadWord(call.in_pa + kOffPropsDevice);
        const uint32_t channel = mem.ReadWord(call.in_pa + kOffPropsChannel);
        if (device != kAdcDeviceIndex ||
            (channel != kChannelVbatt && channel != kChannelVchg)) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adc: a conversion of device %u channel %u "
                "is not modeled", device, channel);
        }

        Msm8255DalRemoteEvent event;
        event.order        = Msm8255DalEventOrder::AfterReply;
        event.event_handle = mem.ReadWord(call.in_pa + kOffRequestEvent);
        event.param        = 0u;
        event.payload.assign(kResultBytes, 0u);
        cerf::le::Put32(event.payload.data() + kResultPhysicalOff,
                        ChannelMillivolts(channel));
        events.push_back(std::move(event));
        return kMsm8255DalSuccess;
    }

    uint32_t ChannelMillivolts(uint32_t channel) const {
        if (channel == kChannelVbatt) return battery_->VoltageMv();
        return battery_->ExternalPower() ? kChargerInputMv : kNoChargerInputMv;
    }

    BatteryPack* battery_ = nullptr;
};

}

REGISTER_SERVICE(Msm8255DalRemoteAdc);

}
