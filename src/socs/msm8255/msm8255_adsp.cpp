#include "msm8255_adsp.h"
#include "msm8255_adsp_task.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../irq_controller.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kAd5Base = 0xA7000000u;
constexpr uint32_t kAd5Size = 0x00D00000u;

constexpr uint32_t kRegWriteCtrl = 0x00400034u;
constexpr uint32_t kRegReadCtrl  = 0x00400038u;
constexpr uint32_t kRegDspStatus = 0x00C0000Cu;
constexpr uint32_t kRegSendIrq   = 0x00C00200u;

constexpr uint32_t kDspStatusIdle = 0u;
constexpr uint32_t kSendIrqValue  = 1u;

constexpr uint32_t kWriteMutexMask   = 0x80000000u;
constexpr uint32_t kWriteCmdMask     = 0x70000000u;
constexpr uint32_t kWriteCmdRequest  = 0x00000000u;
constexpr uint32_t kWriteCmdDone     = 0x10000000u;
constexpr uint32_t kWriteAddrMask    = 0x00FFFFFFu;
constexpr uint32_t kWriteCtrlReady   = 0x70000000u;

constexpr uint32_t kReadReadyMask   = 0xE0000000u;
constexpr uint32_t kReadCtrlPosted  = 0xA0000000u;
constexpr uint32_t kReadCtrlDone    = 0xE0000000u;
constexpr uint32_t kReadKeepMask    = 0x1FFFFFFFu;

constexpr uint32_t kCmd16Base   = 0x00010000u;
constexpr uint32_t kCmd16Stride = 0x00000400u;
constexpr uint32_t kMsg16Base   = 0x00020000u;
constexpr uint32_t kCmd32Base   = 0x00410000u;
constexpr uint32_t kCmd32Stride = 0x00000800u;
constexpr uint32_t kMsg32Base   = 0x00420000u;

constexpr uint32_t kTaskShift    = 8u;
constexpr uint32_t kLengthShift  = 16u;

constexpr int kVicLine = 20;

constexpr uint32_t kNoQueue = 0xFFFFFFFFu;

}

bool Msm8255Adsp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255Adsp::OnReady() {
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Msm8255Adsp::MmioBase() const { return kAd5Base; }
uint32_t Msm8255Adsp::MmioSize() const { return kAd5Size; }

void Msm8255Adsp::Reset() {
    enabled_.fill(false);
    std::fill(cmd16_.begin(), cmd16_.end(), uint16_t{0});
    std::fill(cmd32_.begin(), cmd32_.end(), 0u);
    std::fill(msg16_.begin(), msg16_.end(), uint16_t{0});
    std::fill(msg32_.begin(), msg32_.end(), 0u);
    outbox_.clear();
    write_ctrl_    = kWriteCtrlReady;
    read_ctrl_     = kReadCtrlDone;
    pending_queue_ = kNoQueue;
    in_flight_     = false;
    for (Msm8255AdspTask* task : tasks_) {
        if (task != nullptr) task->Reset();
    }
}

uint32_t Msm8255Adsp::TaskIndex(uint32_t module) const {
    return static_cast<uint32_t>(Msm8255AdspModuleIndex(module));
}

uint32_t Msm8255Adsp::QueueSlot(uint32_t queue) const {
    for (uint32_t i = 0; i < kQueues; ++i) {
        if (kMsm8255AdspQueues[i].queue == queue) return i;
    }
    emu_.Get<Fatal>().Die("msm8255 adsp: queue %u is not in the image", queue);
}

void Msm8255Adsp::RegisterTask(Msm8255AdspTask* task) { tasks_[TaskIndex(task->Module())] = task; }

void Msm8255Adsp::SetModuleEnabled(uint32_t module, bool enabled) { enabled_[TaskIndex(module)] = enabled; }

uint16_t Msm8255Adsp::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kAd5Base;
    if (off >= kMsg16Base && off < kMsg16Base + 2u * msg16_.size()) {
        return msg16_[(off - kMsg16Base) / 2u];
    }
    HaltUnsupportedAccess("ReadHalf", addr, 0u);
}

uint32_t Msm8255Adsp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kAd5Base;
    if (off == kRegWriteCtrl) return write_ctrl_;
    if (off == kRegReadCtrl) return read_ctrl_;
    if (off == kRegDspStatus) return kDspStatusIdle;
    if (off >= kMsg32Base && off < kMsg32Base + 4u * msg32_.size() && off % 4u == 0u) {
        return msg32_[(off - kMsg32Base) / 4u];
    }
    HaltUnsupportedAccess("ReadWord", addr, 0u);
}

void Msm8255Adsp::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kAd5Base;
    if (off >= kCmd16Base && off < kCmd16Base + kQueues * kCmd16Stride && off % 2u == 0u) {
        const uint32_t slot = (off - kCmd16Base) / kCmd16Stride;
        if (kMsm8255AdspQueues[slot].width == kMsm8255AdspWidth16) {
            cmd16_[slot * kWords + (off - kCmd16Base - slot * kCmd16Stride) / 2u] = value;
            return;
        }
    }
    HaltUnsupportedAccess("WriteHalf", addr, value);
}

void Msm8255Adsp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kAd5Base;
    if (off == kRegWriteCtrl) {
        const uint32_t top = value & (kWriteMutexMask | kWriteCmdMask);
        if ((value & ~(kWriteMutexMask | kWriteCmdMask | kWriteAddrMask)) == 0u &&
            (top == (kWriteMutexMask | kWriteCmdRequest) || top == (kWriteMutexMask | kWriteCmdDone))) {
            write_ctrl_ = value;
            return;
        }
    } else if (off == kRegReadCtrl) {
        if (in_flight_ && (value & kReadReadyMask) == 0u &&
            (value & kReadKeepMask) == (read_ctrl_ & kReadKeepMask)) {
            read_ctrl_ = value;
            return;
        }
    } else if (off == kRegSendIrq) {
        if (value == kSendIrqValue) {
            SendIrq();
            return;
        }
    } else if (off >= kCmd32Base && off < kCmd32Base + kQueues * kCmd32Stride && off % 4u == 0u) {
        const uint32_t slot = (off - kCmd32Base) / kCmd32Stride;
        if (kMsm8255AdspQueues[slot].width == kMsm8255AdspWidth32) {
            cmd32_[slot * kWords + (off - kCmd32Base - slot * kCmd32Stride) / 4u] = value;
            return;
        }
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Msm8255Adsp::SendIrq() {
    if ((write_ctrl_ & kWriteMutexMask) != 0u) {
        if ((write_ctrl_ & kWriteCmdMask) == kWriteCmdRequest) {
            AcceptWriteRequest();
        } else {
            AcceptWriteDone();
        }
        return;
    }
    if (in_flight_ && (read_ctrl_ & kReadReadyMask) == 0u) {
        in_flight_ = false;
        if (outbox_.empty()) {
            read_ctrl_ = kReadCtrlDone;
        } else {
            DeliverNext();
        }
        return;
    }
    emu_.Get<Fatal>().Die(
        "msm8255 adsp: an interrupt to the dsp with write control 0x%08X and read control 0x%08X is not modeled",
        write_ctrl_, read_ctrl_);
}

void Msm8255Adsp::AcceptWriteRequest() {
    const uint32_t queue = write_ctrl_ & kWriteAddrMask;
    const uint32_t slot  = QueueSlot(queue);
    const Msm8255AdspQueue& q = kMsm8255AdspQueues[slot];
    if (!enabled_[TaskIndex(q.module)] || pending_queue_ != kNoQueue) {
        emu_.Get<Fatal>().Die(
            "msm8255 adsp: a command request on queue %u of module 0x%08X while it is %s is not modeled",
            queue, q.module, pending_queue_ != kNoQueue ? "busy" : "disabled");
    }
    pending_queue_ = queue;
    write_ctrl_ = q.width == kMsm8255AdspWidth16 ? kCmd16Base + slot * kCmd16Stride
                                                 : kCmd32Base + slot * kCmd32Stride;
}

void Msm8255Adsp::AcceptWriteDone() {
    const uint32_t queue = write_ctrl_ & kWriteAddrMask;
    if (queue != pending_queue_) {
        emu_.Get<Fatal>().Die(
            "msm8255 adsp: a command on queue %u completes while queue %u holds the buffer",
            queue, pending_queue_);
    }
    const uint32_t slot = QueueSlot(queue);
    const Msm8255AdspQueue& q = kMsm8255AdspQueues[slot];
    Msm8255AdspCommand command;
    command.queue = queue;
    command.words.resize(kWords);
    for (uint32_t i = 0; i < kWords; ++i) {
        command.words[i] = q.width == kMsm8255AdspWidth16 ? cmd16_[slot * kWords + i]
                                                          : cmd32_[slot * kWords + i];
    }
    Msm8255AdspTask* task = tasks_[TaskIndex(q.module)];
    if (task == nullptr) {
        emu_.Get<Fatal>().Die("msm8255 adsp: no task models module 0x%08X (queue %u, command 0x%04X)",
                              q.module, queue, command.words[0]);
    }
    task->Command(command);
    pending_queue_ = kNoQueue;
    write_ctrl_    = kWriteCtrlReady;
}

void Msm8255Adsp::PostMessage(uint32_t module, uint32_t msg_id, uint32_t width,
                              const std::vector<uint32_t>& payload) {
    Message message;
    message.task    = TaskIndex(module);
    message.msg_id  = msg_id;
    message.width   = width;
    message.payload = payload;
    outbox_.push_back(std::move(message));
    if (in_flight_) return;
    DeliverNext();
    emu_.Get<IrqController>().PulseIrq(kVicLine);
}

void Msm8255Adsp::DeliverNext() {
    const Message message = std::move(outbox_.front());
    outbox_.pop_front();
    const uint32_t length = static_cast<uint32_t>(message.payload.size());
    if (message.width == kMsm8255AdspWidth16) {
        msg16_[0] = static_cast<uint16_t>((message.task << kTaskShift) | message.msg_id);
        msg16_[1] = static_cast<uint16_t>(length);
        for (uint32_t i = 0; i < length; ++i) {
            msg16_[2u + i] = static_cast<uint16_t>(message.payload[i]);
        }
        read_ctrl_ = kReadCtrlPosted | kMsg16Base;
    } else {
        msg32_[0] = message.msg_id | (message.task << kTaskShift) | (length << kLengthShift);
        for (uint32_t i = 0; i < length; ++i) msg32_[1u + i] = message.payload[i];
        read_ctrl_ = kReadCtrlPosted | kMsg32Base;
    }
    in_flight_ = true;
}

void Msm8255Adsp::SaveState(StateWriter& w) {
    for (uint32_t i = 0; i < kModules; ++i) {
        w.Write<uint32_t>("adsp_enabled", enabled_[i] ? 1u : 0u);
    }
    w.WriteBytes("adsp_cmd16", cmd16_.data(), cmd16_.size() * sizeof(uint16_t));
    w.WriteBytes("adsp_cmd32", cmd32_.data(), cmd32_.size() * sizeof(uint32_t));
    w.WriteBytes("adsp_msg16", msg16_.data(), msg16_.size() * sizeof(uint16_t));
    w.WriteBytes("adsp_msg32", msg32_.data(), msg32_.size() * sizeof(uint32_t));
    w.Write<uint32_t>("adsp_write_ctrl", write_ctrl_);
    w.Write<uint32_t>("adsp_read_ctrl", read_ctrl_);
    w.Write<uint32_t>("adsp_pending_queue", pending_queue_);
    w.Write<uint32_t>("adsp_in_flight", in_flight_ ? 1u : 0u);
    w.Write<uint32_t>("adsp_outbox", static_cast<uint32_t>(outbox_.size()));
    for (const Message& message : outbox_) {
        w.Write<uint32_t>("adsp_msg_task", message.task);
        w.Write<uint32_t>("adsp_msg_id", message.msg_id);
        w.Write<uint32_t>("adsp_msg_width", message.width);
        w.Write<uint32_t>("adsp_msg_length", static_cast<uint32_t>(message.payload.size()));
        w.WriteBytes("adsp_msg_payload", message.payload.data(),
                     message.payload.size() * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < kModules; ++i) {
        if (tasks_[i] == nullptr) continue;
        w.BeginFrame(kMsm8255AdspModules[i]);
        tasks_[i]->SaveState(w);
        w.EndFrame();
    }
}

void Msm8255Adsp::RestoreState(StateReader& r) {
    for (uint32_t i = 0; i < kModules; ++i) {
        uint32_t enabled = 0;
        r.Read("adsp_enabled", enabled);
        enabled_[i] = enabled != 0u;
    }
    r.ReadBytes("adsp_cmd16", cmd16_.data(), cmd16_.size() * sizeof(uint16_t));
    r.ReadBytes("adsp_cmd32", cmd32_.data(), cmd32_.size() * sizeof(uint32_t));
    r.ReadBytes("adsp_msg16", msg16_.data(), msg16_.size() * sizeof(uint16_t));
    r.ReadBytes("adsp_msg32", msg32_.data(), msg32_.size() * sizeof(uint32_t));
    r.Read("adsp_write_ctrl", write_ctrl_);
    r.Read("adsp_read_ctrl", read_ctrl_);
    r.Read("adsp_pending_queue", pending_queue_);
    uint32_t in_flight = 0;
    r.Read("adsp_in_flight", in_flight);
    in_flight_ = in_flight != 0u;
    uint32_t count = 0;
    r.Read("adsp_outbox", count);
    outbox_.clear();
    for (uint32_t m = 0; m < count; ++m) {
        Message message;
        uint32_t length = 0;
        r.Read("adsp_msg_task", message.task);
        r.Read("adsp_msg_id", message.msg_id);
        r.Read("adsp_msg_width", message.width);
        r.Read("adsp_msg_length", length);
        message.payload.resize(length);
        r.ReadBytes("adsp_msg_payload", message.payload.data(),
                    message.payload.size() * sizeof(uint32_t));
        outbox_.push_back(std::move(message));
    }
    for (uint32_t i = 0; i < kModules; ++i) {
        if (tasks_[i] == nullptr) continue;
        const uint32_t id = r.EnterFrame();
        if (id != kMsm8255AdspModules[i]) {
            r.Reject("msm8255 adsp: the image holds task 0x%08X where this build models 0x%08X",
                     id, kMsm8255AdspModules[i]);
        }
        tasks_[i]->RestoreState(r);
        r.LeaveFrame();
    }
}

void Msm8255Adsp::PostRestore() {
    for (Msm8255AdspTask* task : tasks_) {
        if (task != nullptr) task->PostRestore();
    }
}

REGISTER_SERVICE(Msm8255Adsp);

}
