#include "msm8255_dal_remote_server.h"
#include "msm8255_dal_remote_device.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kHdrBytes = 20u;

constexpr uint32_t kLenMask   = 0x0000FFFFu;
constexpr uint32_t kTagShift  = 16u;
constexpr uint32_t kTagMask    = 0x000000FFu;
constexpr uint32_t kFirstFlag = 1u << 24;

constexpr uint32_t kMsgIdShift    = 24u;
constexpr uint32_t kMsgIdKeepMask = 0x00FFFFFFu;
constexpr uint32_t kResponseFlag  = 0x80u;
constexpr uint32_t kEventMsgByte  = 0xC0u;

constexpr uint32_t kMethodMask = 0x0000FFFFu;
constexpr uint32_t kShapeShift = 16u;
constexpr uint32_t kShapeMask  = 0x000000FFu;

constexpr uint32_t kTag = 0x11u;

constexpr uint32_t kMsgIdMethod = 0u;
constexpr uint32_t kMsgIdAttach = 1u;
constexpr uint32_t kMsgIdDetach = 2u;

constexpr uint32_t kAttachRequestBytes = 116u;

constexpr uint32_t kAttachReplyBytes = 84u;
constexpr uint32_t kAttachTailBytes  = kAttachReplyBytes - kHdrBytes;

constexpr char kServingPortName[] = "DAL0";

constexpr uint32_t kMethodInfo        = 6u;
constexpr uint32_t kInfoBytes         = 40u;
constexpr uint32_t kOffInfoDeviceId   = 0u;
constexpr uint32_t kOffInfoVersion    = 4u;

constexpr uint32_t kOffWord0 = 0u;
constexpr uint32_t kOffWord1 = 4u;

constexpr uint32_t kOffCallCallerContext = 8u;
constexpr uint32_t kOffCallTarget        = 12u;
constexpr uint32_t kOffCallArg           = 16u;
constexpr uint32_t kOffCallInBuf         = 20u;

constexpr uint32_t kOffReplyHandle        =  8u;
constexpr uint32_t kOffReplyCallerContext = 12u;
constexpr uint32_t kOffReplyStatus        = 16u;
constexpr uint32_t kOffReplyOutBytes      = 20u;
constexpr uint32_t kOffReplyOutBuf        = 24u;

constexpr uint32_t kOffEventHandle       = 16u;
constexpr uint32_t kOffEventParam        = 20u;
constexpr uint32_t kOffEventPayloadBytes = 24u;
constexpr uint32_t kOffEventPayload      = kMsm8255DalEventHeaderBytes;

constexpr uint32_t kHandleBase = 0x00010000u;
constexpr uint32_t kFreeSlot   = 0u;

constexpr uint32_t Align4(uint32_t bytes) { return (bytes + 3u) & ~3u; }

}

bool Msm8255DalRemoteServer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255DalRemoteServer::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        port_announced_ = false;
        slot_device_.fill(kFreeSlot);
    });
}

void Msm8255DalRemoteServer::RegisterDevice(Msm8255DalRemoteDevice* device) {
    if (FindDevice(device->DeviceId()) != nullptr) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: two devices claim dal device id "
            "0x%08X", device->DeviceId());
    }
    devices_.push_back(device);
}

Msm8255DalRemoteDevice* Msm8255DalRemoteServer::FindDevice(
    uint32_t device_id) const {
    for (Msm8255DalRemoteDevice* device : devices_) {
        if (device->DeviceId() == device_id) return device;
    }
    return nullptr;
}

uint32_t Msm8255DalRemoteServer::RequireSlot(uint32_t handle) const {
    const uint32_t slot = handle - kHandleBase;
    if (handle < kHandleBase || slot >= kHandleSlots ||
        slot_device_[slot] == kFreeSlot) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: the message targets handle 0x%08X, "
            "which no attach issued", handle);
    }
    return slot;
}

uint32_t Msm8255DalRemoteServer::Answer(uint32_t in_pa, uint32_t in_avail,
                                        uint32_t out_pa, uint32_t out_cap,
                                        uint32_t& consumed) {
    auto& mem = emu_.Get<EmulatedMemory>();

    if (in_avail < kHdrBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: %u bytes are queued, which is short of "
            "the %u-byte message header", in_avail, kHdrBytes);
    }

    const uint32_t word0 = mem.ReadWord(in_pa + kOffWord0);
    const uint32_t word1 = mem.ReadWord(in_pa + kOffWord1);
    const uint32_t len   = word0 & kLenMask;
    const uint32_t tag   = (word0 >> kTagShift) & kTagMask;
    const uint32_t msg   = word1 >> kMsgIdShift;

    if (tag != kTag) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: the message carries tag 0x%02X, and "
            "only 0x%02X is modeled", tag, kTag);
    }
    if (len < kHdrBytes || len > in_avail) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: the message declares %u bytes and %u "
            "are queued", len, in_avail);
    }
    consumed = len;

    switch (msg) {
    case kMsgIdAttach: return AnswerAttach(in_pa, len, out_pa, out_cap);
    case kMsgIdMethod: return AnswerMethod(in_pa, len, out_pa, out_cap);
    case kMsgIdDetach: return AnswerDetach(in_pa, len, out_pa, out_cap);
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: message id %u is not modeled", msg);
    }
}

uint32_t Msm8255DalRemoteServer::AnswerAttach(uint32_t in_pa, uint32_t len,
                                              uint32_t out_pa,
                                              uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (len != kAttachRequestBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: an attach declares %u bytes, and only "
            "the %u-byte form is modeled", len, kAttachRequestBytes);
    }
    if (out_cap < kAttachReplyBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: the reply needs %u bytes and the "
            "window at 0x%08X has %u", kAttachReplyBytes, out_pa, out_cap);
    }

    const uint32_t device_id = mem.ReadWord(in_pa + kOffCallArg);
    if (FindDevice(device_id) == nullptr) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: an attach of dal device 0x%08X is not "
            "modeled", device_id);
    }
    uint32_t slot = 0;
    while (slot < kHandleSlots && slot_device_[slot] != kFreeSlot) ++slot;
    if (slot == kHandleSlots) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: an attach of dal device 0x%08X "
            "with all %u handles open is not modeled", device_id,
            kHandleSlots);
    }
    slot_device_[slot]    = device_id;
    const uint32_t handle = kHandleBase + slot;

    uint32_t reply0 = kAttachReplyBytes | (kTag << kTagShift);
    if (!port_announced_) {
        port_announced_ = true;
        reply0 |= kFirstFlag;
    }
    const uint32_t word1 = mem.ReadWord(in_pa + kOffWord1);
    mem.WriteWord(out_pa + kOffWord0, reply0);
    mem.WriteWord(out_pa + kOffWord1,
                  (word1 & kMsgIdKeepMask) |
                      ((kMsgIdAttach | kResponseFlag) << kMsgIdShift));
    mem.WriteWord(out_pa + kOffReplyHandle, handle);
    mem.WriteWord(out_pa + kOffReplyCallerContext,
                  mem.ReadWord(in_pa + kOffCallCallerContext));
    mem.WriteWord(out_pa + kOffReplyStatus, kMsm8255DalSuccess);
    for (uint32_t off = 0; off < kAttachTailBytes; off += 4u) {
        mem.WriteWord(out_pa + kHdrBytes + off, 0u);
    }
    for (uint32_t i = 0; i + 1u < sizeof(kServingPortName); ++i) {
        mem.WriteByte(out_pa + kHdrBytes + i,
                      static_cast<uint8_t>(kServingPortName[i]));
    }
    return kAttachReplyBytes;
}

uint32_t Msm8255DalRemoteServer::AnswerMethod(uint32_t in_pa, uint32_t len,
                                              uint32_t out_pa,
                                              uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t word1  = mem.ReadWord(in_pa + kOffWord1);
    const uint32_t handle = mem.ReadWord(in_pa + kOffCallTarget);
    const uint32_t slot   = RequireSlot(handle);

    Msm8255DalRemoteCall call;
    call.method = word1 & kMethodMask;
    call.shape  = (word1 >> kShapeShift) & kShapeMask;

    uint32_t reply_bytes = kHdrBytes;
    uint32_t out_bytes   = 0u;
    if (call.shape == kMsm8255DalShapeWord) {
        if (len != kHdrBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: a one-word call of method %u "
                "declares %u bytes", call.method, len);
        }
        call.in_pa    = in_pa + kOffCallArg;
        call.in_bytes = 4u;
    } else if (call.shape == kMsm8255DalShapeInput) {
        const uint32_t in_bytes = mem.ReadWord(in_pa + kOffCallArg);
        if (in_bytes > len || len != kOffCallInBuf + Align4(in_bytes)) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: an input call of method %u carries "
                "%u input bytes in a %u-byte message", call.method, in_bytes,
                len);
        }
        call.in_pa    = in_pa + kOffCallInBuf;
        call.in_bytes = in_bytes;
    } else if (call.shape == kMsm8255DalShapeBuffers) {
        const uint32_t in_bytes = mem.ReadWord(in_pa + kOffCallArg);
        if (in_bytes > len || len != kOffCallInBuf + Align4(in_bytes) + 4u) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: a buffer call of method %u carries "
                "%u input bytes in a %u-byte message", call.method, in_bytes,
                len);
        }
        out_bytes      = mem.ReadWord(in_pa + kOffCallInBuf + Align4(in_bytes));
        call.in_pa     = in_pa + kOffCallInBuf;
        call.in_bytes  = in_bytes;
    } else if (call.shape == kMsm8255DalShapeOutput) {
        if (len != kHdrBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: an output-only call of method %u "
                "declares %u bytes", call.method, len);
        }
        out_bytes = mem.ReadWord(in_pa + kOffCallArg);
    } else {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: method %u of dal device 0x%08X uses "
            "argument shape %u, which is not modeled", call.method,
            slot_device_[slot], call.shape);
    }

    if (call.shape == kMsm8255DalShapeBuffers || call.shape == kMsm8255DalShapeOutput) {
        if (out_bytes > out_cap || kOffReplyOutBuf + Align4(out_bytes) > out_cap) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: method %u asks for %u output bytes "
                "and the window at 0x%08X has %u", call.method, out_bytes,
                out_pa, out_cap);
        }
        call.out_pa    = out_pa + kOffReplyOutBuf;
        call.out_bytes = out_bytes;
        reply_bytes    = kOffReplyOutBuf + Align4(out_bytes);
        for (uint32_t off = 0; off < Align4(out_bytes); off += 4u) {
            mem.WriteWord(call.out_pa + off, 0u);
        }
        mem.WriteWord(out_pa + kOffReplyOutBytes, out_bytes);
    }

    Msm8255DalRemoteDevice* device = FindDevice(slot_device_[slot]);
    Msm8255DalRemoteEvents events;
    uint32_t status = kMsm8255DalSuccess;
    if (call.method == kMethodInfo && call.shape == kMsm8255DalShapeOutput) {
        if (call.out_bytes != kInfoBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 dal remote server: an info query of dal device 0x%08X "
                "asks for %u bytes", device->DeviceId(), call.out_bytes);
        }
        mem.WriteWord(call.out_pa + kOffInfoDeviceId, device->DeviceId());
        mem.WriteWord(call.out_pa + kOffInfoVersion, device->InterfaceVersion());
    } else {
        status = device->Invoke(call, events);
    }
    WriteHeaderReply(in_pa, out_pa, reply_bytes, status);
    return PlaceEvents(out_pa, out_cap, reply_bytes, events);
}

uint32_t Msm8255DalRemoteServer::EventBytes(const Msm8255DalRemoteEvent& event) const {
    return kOffEventPayload + Align4(static_cast<uint32_t>(event.payload.size()));
}

uint32_t Msm8255DalRemoteServer::PlaceEvents(uint32_t out_pa, uint32_t out_cap,
                                             uint32_t reply_bytes,
                                             const Msm8255DalRemoteEvents& events) {
    auto& mem = emu_.Get<EmulatedMemory>();
    uint32_t before_bytes = 0u;
    uint32_t after_bytes  = 0u;
    for (const Msm8255DalRemoteEvent& event : events) {
        const uint32_t bytes = EventBytes(event);
        if (event.order == Msm8255DalEventOrder::BeforeReply) {
            before_bytes += bytes;
        } else {
            after_bytes += bytes;
        }
    }
    const uint64_t total = uint64_t{before_bytes} + reply_bytes + after_bytes;
    if (total > out_cap) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: a %u-byte reply with %u event bytes "
            "does not fit the %u-byte window at 0x%08X", reply_bytes,
            before_bytes + after_bytes, out_cap, out_pa);
    }
    if (before_bytes != 0u) {
        std::vector<uint32_t> reply(reply_bytes / 4u);
        for (uint32_t i = 0; i < reply.size(); ++i) {
            reply[i] = mem.ReadWord(out_pa + 4u * i);
        }
        for (uint32_t i = 0; i < reply.size(); ++i) {
            mem.WriteWord(out_pa + before_bytes + 4u * i, reply[i]);
        }
    }
    uint32_t before_at = out_pa;
    uint32_t after_at  = out_pa + before_bytes + reply_bytes;
    for (const Msm8255DalRemoteEvent& event : events) {
        uint32_t& at = event.order == Msm8255DalEventOrder::BeforeReply ? before_at
                                                                        : after_at;
        at += WriteEvent(at, event);
    }
    return static_cast<uint32_t>(total);
}

uint32_t Msm8255DalRemoteServer::AnswerDetach(uint32_t in_pa, uint32_t len,
                                              uint32_t out_pa,
                                              uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (len != kHdrBytes || out_cap < kHdrBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 dal remote server: a detach declares %u bytes with a "
            "%u-byte reply window", len, out_cap);
    }
    const uint32_t slot = RequireSlot(mem.ReadWord(in_pa + kOffCallTarget));
    slot_device_[slot]  = kFreeSlot;
    return WriteHeaderReply(in_pa, out_pa, kHdrBytes, kMsm8255DalSuccess);
}

uint32_t Msm8255DalRemoteServer::WriteHeaderReply(uint32_t in_pa,
                                                  uint32_t out_pa,
                                                  uint32_t bytes,
                                                  uint32_t status) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t word1 = mem.ReadWord(in_pa + kOffWord1);
    mem.WriteWord(out_pa + kOffWord0, bytes | (kTag << kTagShift));
    mem.WriteWord(out_pa + kOffWord1, word1 | (kResponseFlag << kMsgIdShift));
    mem.WriteWord(out_pa + kOffReplyHandle, mem.ReadWord(in_pa + kOffCallTarget));
    mem.WriteWord(out_pa + kOffReplyCallerContext,
                  mem.ReadWord(in_pa + kOffCallCallerContext));
    mem.WriteWord(out_pa + kOffReplyStatus, status);
    return bytes;
}

uint32_t Msm8255DalRemoteServer::WriteEvent(uint32_t out_pa,
                                            const Msm8255DalRemoteEvent& event) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t payload_bytes = static_cast<uint32_t>(event.payload.size());
    const uint32_t bytes         = EventBytes(event);
    mem.WriteWord(out_pa + kOffWord0, bytes | (kTag << kTagShift));
    mem.WriteWord(out_pa + kOffWord1, kEventMsgByte << kMsgIdShift);
    mem.WriteWord(out_pa + kOffCallCallerContext, 0u);
    mem.WriteWord(out_pa + kOffCallTarget, 0u);
    mem.WriteWord(out_pa + kOffEventHandle, event.event_handle);
    mem.WriteWord(out_pa + kOffEventParam, event.param);
    mem.WriteWord(out_pa + kOffEventPayloadBytes, payload_bytes);
    for (uint32_t off = 0; off < Align4(payload_bytes); ++off) {
        const uint8_t b = off < payload_bytes ? event.payload[off] : 0u;
        mem.WriteByte(out_pa + kOffEventPayload + off, b);
    }
    return bytes;
}

void Msm8255DalRemoteServer::SaveState(StateWriter& w) {
    w.Write<uint32_t>("port_announced", port_announced_ ? 1u : 0u);
    for (uint32_t slot = 0; slot < kHandleSlots; ++slot) {
        w.Write<uint32_t>("dal_slot_device", slot_device_[slot]);
    }
    for (Msm8255DalRemoteDevice* device : devices_) {
        w.BeginFrame(device->DeviceId());
        device->SaveState(w);
        w.EndFrame();
    }
}

void Msm8255DalRemoteServer::RestoreState(StateReader& r) {
    uint32_t announced = 0;
    r.Read("port_announced", announced);
    port_announced_ = announced != 0u;
    for (uint32_t slot = 0; slot < kHandleSlots; ++slot) {
        uint32_t device_id = kFreeSlot;
        r.Read("dal_slot_device", device_id);
        if (device_id != kFreeSlot && FindDevice(device_id) == nullptr) {
            r.Reject("msm8255 dal remote server: restored handle %u names dal "
                     "device 0x%08X, which no device serves", slot, device_id);
        }
        slot_device_[slot] = device_id;
    }
    for (Msm8255DalRemoteDevice* device : devices_) {
        const uint32_t id = r.EnterFrame();
        if (id != device->DeviceId()) {
            r.Reject("msm8255 dal remote server: the image holds dal device "
                     "0x%08X where this build serves 0x%08X", id,
                     device->DeviceId());
        }
        device->RestoreState(r);
        r.LeaveFrame();
    }
}

REGISTER_SERVICE(Msm8255DalRemoteServer);

}
