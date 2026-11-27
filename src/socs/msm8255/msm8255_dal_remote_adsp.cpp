#include "msm8255_adsp.h"
#include "msm8255_adsp_image.h"
#include "msm8255_dal_remote_device.h"
#include "msm8255_dal_remote_server.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kAdspDeviceId = 0x0200009Au;
constexpr uint32_t kAdspVersion  = 0x00010000u;

constexpr uint32_t kMethodAdspCommand = 9u;
constexpr uint32_t kCommandBytes      = 16u;

constexpr uint32_t kOffCmd    = 0u;
constexpr uint32_t kOffProc   = 4u;
constexpr uint32_t kOffModule = 8u;
constexpr uint32_t kOffCookie = 12u;

enum : uint32_t {
    kCmdRegisterApp = 0u,
    kCmdEnable      = 1u,
    kCmdDisable     = 2u,
    kCmdGetInitInfo = 10u,
    kCmdSuspend     = 15u,
    kCmdWakeup      = 16u,
};

enum : uint32_t {
    kEvtModReady     = 0u,
    kEvtInitInfo     = 5u,
    kEvtSuspendDone  = 10u,
    kEvtWakeupDone   = 12u,
};

constexpr uint32_t kProcModem = 1u;
constexpr uint32_t kProcApps  = 2u;

constexpr uint32_t kNoEventHandle = 0u;
constexpr uint32_t kAppsOkToSleep = 0u;

constexpr uint32_t kQueueSlots  = 72u;
constexpr uint32_t kTaskSlots   = 72u;
constexpr uint32_t kModuleSlots = 64u;
constexpr uint32_t kTripleSlots = 36u;

constexpr uint32_t kChunkHeaderBytes = 16u;
constexpr uint32_t kChunkDataMax     = kMsm8255DalEventPayloadMax - kChunkHeaderBytes;

static_assert(kMsm8255AdspQueues.size() * kMsm8255AdspImageCount <= kQueueSlots);
static_assert(kMsm8255AdspModules.size() * kMsm8255AdspImageCount <= kTaskSlots);
static_assert(kMsm8255AdspModules.size() <= kModuleSlots);
static_assert(kMsm8255AdspQueues.size() <= kTripleSlots);
static_assert(kChunkDataMax % 4u == 0u);

using cerf::le::Append32;

std::vector<uint8_t> InitInfoData() {
    std::vector<uint8_t> data;
    Append32(data, kMsm8255AdspImageCount);
    Append32(data, static_cast<uint32_t>(kMsm8255AdspQueues.size()));
    for (const Msm8255AdspQueue& q : kMsm8255AdspQueues) {
        Append32(data, q.queue);
        Append32(data, q.queue);
    }
    for (size_t i = kMsm8255AdspQueues.size(); i < kQueueSlots; ++i) {
        Append32(data, 0u);
        Append32(data, 0u);
    }
    Append32(data, static_cast<uint32_t>(kMsm8255AdspModules.size()));
    for (uint32_t module : kMsm8255AdspModules) Append32(data, module);
    for (size_t i = kMsm8255AdspModules.size(); i < kTaskSlots; ++i) Append32(data, 0u);
    Append32(data, static_cast<uint32_t>(kMsm8255AdspModules.size()));
    for (uint32_t module : kMsm8255AdspModules) Append32(data, module);
    for (size_t i = kMsm8255AdspModules.size(); i < kModuleSlots; ++i) Append32(data, 0u);
    Append32(data, static_cast<uint32_t>(kMsm8255AdspQueues.size()));
    for (const Msm8255AdspQueue& q : kMsm8255AdspQueues) {
        Append32(data, q.module);
        Append32(data, q.queue);
        Append32(data, kMsm8255AdspMaxCommandWords);
    }
    return data;
}

class Msm8255DalRemoteAdsp : public Msm8255DalRemoteDevice {
public:
    using Msm8255DalRemoteDevice::Msm8255DalRemoteDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255DalRemoteServer>().RegisterDevice(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { event_handle_ = kNoEventHandle; });
    }

    uint32_t DeviceId() const override { return kAdspDeviceId; }
    uint32_t InterfaceVersion() const override { return kAdspVersion; }

    uint32_t Invoke(const Msm8255DalRemoteCall& call,
                    Msm8255DalRemoteEvents& events) override {
        if (call.shape != kMsm8255DalShapeInput || call.method != kMethodAdspCommand ||
            call.in_bytes != kCommandBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adsp: method %u in argument shape %u with %u input bytes is not modeled",
                call.method, call.shape, call.in_bytes);
        }
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t cmd    = mem.ReadWord(call.in_pa + kOffCmd);
        const uint32_t proc   = mem.ReadWord(call.in_pa + kOffProc);
        const uint32_t module = mem.ReadWord(call.in_pa + kOffModule);
        if (cmd == kCmdGetInitInfo) {
            if (proc != kProcModem) {
                emu_.Get<Fatal>().Die(
                    "msm8255 dal remote adsp: an init info request for processor %u is not modeled", proc);
            }
            event_handle_ = mem.ReadWord(call.in_pa + kOffCookie);
            PushInitInfo(proc, events);
            return kMsm8255DalSuccess;
        }
        if (proc != kProcApps || Msm8255AdspModuleIndex(module) == kMsm8255AdspNoModule ||
            event_handle_ == kNoEventHandle) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adsp: command %u for module 0x%08X from processor %u is not modeled",
                cmd, module, proc);
        }
        switch (cmd) {
        case kCmdRegisterApp:
            return kMsm8255DalSuccess;
        case kCmdDisable:
            emu_.Get<Msm8255Adsp>().SetModuleEnabled(module, false);
            return kMsm8255DalSuccess;
        case kCmdEnable:
            emu_.Get<Msm8255Adsp>().SetModuleEnabled(module, true);
            PushModuleEvent(kEvtModReady, proc, module, events);
            return kMsm8255DalSuccess;
        case kCmdSuspend:
            PushModuleEvent(kEvtSuspendDone, proc, module, events);
            return kMsm8255DalSuccess;
        case kCmdWakeup:
            PushModuleEvent(kEvtWakeupDone, proc, module, events);
            return kMsm8255DalSuccess;
        default:
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote adsp: command %u for module 0x%08X is not modeled", cmd, module);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("adsp_event_handle", event_handle_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("adsp_event_handle", event_handle_);
    }

private:
    Msm8255DalRemoteEvent NewEvent() const {
        Msm8255DalRemoteEvent event;
        event.order        = Msm8255DalEventOrder::BeforeReply;
        event.event_handle = event_handle_;
        event.param        = 0u;
        return event;
    }

    void PushModuleEvent(uint32_t evt, uint32_t proc, uint32_t module,
                         Msm8255DalRemoteEvents& events) const {
        Msm8255DalRemoteEvent event = NewEvent();
        Append32(event.payload, evt);
        Append32(event.payload, proc);
        Append32(event.payload, evt);
        Append32(event.payload, module);
        Append32(event.payload, kMsm8255AdspImage);
        Append32(event.payload, kAppsOkToSleep);
        events.push_back(std::move(event));
    }

    void PushInitInfo(uint32_t proc, Msm8255DalRemoteEvents& events) const {
        const std::vector<uint8_t> data = InitInfoData();
        size_t at = 0;
        while (at < data.size()) {
            const size_t chunk     = std::min<size_t>(kChunkDataMax, data.size() - at);
            const size_t remaining = data.size() - at - chunk;
            Msm8255DalRemoteEvent event = NewEvent();
            Append32(event.payload, kEvtInitInfo);
            Append32(event.payload, proc);
            Append32(event.payload, kEvtInitInfo);
            Append32(event.payload, static_cast<uint32_t>(remaining));
            event.payload.insert(event.payload.end(), data.begin() + at,
                                 data.begin() + at + chunk);
            events.push_back(std::move(event));
            at += chunk;
        }
    }

    uint32_t event_handle_ = kNoEventHandle;
};

}

REGISTER_SERVICE(Msm8255DalRemoteAdsp);

}
