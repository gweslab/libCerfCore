#include "msm8255_modem_callbacks.h"

#include "msm8255_modem_peer.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kVoidReplyBytes = kPacmarkBytes + kReplyResultsOff;
constexpr uint32_t kCallsPerKey    = 2u;

}

bool Msm8255ModemCallbacks::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ModemCallbacks::OnReady() {
    release_ = emu_.Get<GuestCycleClock>().Add([this] { Release(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        queue_.clear();
        next_xid_ = 0u;
        emu_.Get<GuestCycleClock>().Disarm(release_);
    });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { ScheduleHeads(); });
}

void Msm8255ModemCallbacks::Queue(Msm8255ModemCallback callback) {
    if (callback.key != 0u) {
        uint32_t same = 0u;
        for (const Entry& e : queue_) {
            if (e.callback.prog == callback.prog && e.callback.key == callback.key) ++same;
        }
        if (same >= kCallsPerKey) return;
    }
    Entry entry;
    entry.callback = std::move(callback);
    queue_.push_back(std::move(entry));
    ScheduleHeads();
}

bool Msm8255ModemCallbacks::Pending(uint32_t prog, uint32_t owner) const {
    for (const Entry& e : queue_) {
        if (e.callback.prog == prog && e.callback.owner == owner) return true;
    }
    return false;
}

void Msm8255ModemCallbacks::CancelUnsentOf(uint32_t prog, uint32_t owner) {
    std::erase_if(queue_, [prog, owner](const Entry& e) {
        return e.sent == 0u && e.callback.prog == prog && e.callback.owner == owner;
    });
    ScheduleHeads();
}

void Msm8255ModemCallbacks::ReleaseClient(const Msm8255RpcEndpoint& client) {
    std::erase_if(queue_, [&client](const Entry& e) {
        return e.callback.dest == client;
    });
    ScheduleHeads();
}

bool Msm8255ModemCallbacks::IsHead(size_t index) const {
    for (size_t i = 0; i < index; ++i) {
        if (queue_[i].callback.prog == queue_[index].callback.prog) return false;
    }
    return true;
}

size_t Msm8255ModemCallbacks::DueHead(uint64_t now) const {
    size_t best = queue_.size();
    for (size_t i = 0; i < queue_.size(); ++i) {
        const Entry& e = queue_[i];
        if (e.sent != 0u || e.timed == 0u || e.due > now || !IsHead(i)) continue;
        if (best == queue_.size() || e.due < queue_[best].due) best = i;
    }
    return best;
}

void Msm8255ModemCallbacks::ScheduleHeads() {
    auto&    clock = emu_.Get<GuestCycleClock>();
    bool     armed = false;
    uint64_t next  = 0u;
    for (size_t i = 0; i < queue_.size(); ++i) {
        Entry& e = queue_[i];
        if (e.sent != 0u || !IsHead(i)) continue;
        if (e.timed == 0u) {
            e.timed = 1u;
            e.due   = clock.CycleAfterNs(e.callback.latency_ns);
        }
        if (!armed || e.due < next) next = e.due;
        armed = true;
    }
    if (armed) {
        clock.Arm(release_, next);
    } else {
        clock.Disarm(release_);
    }
}

void Msm8255ModemCallbacks::Release() {
    const size_t head = DueHead(emu_.Get<GuestCycleClock>().Cycles());
    if (head < queue_.size()) {
        const Msm8255ModemCallback& cb = queue_[head].callback;
        emu_.Get<Msm8255ModemPeer>().PushRouterMessage(
            cb.prog, cb.proc,
            [this](uint32_t out_pa, uint32_t out_cap) { return FlushDue(out_pa, out_cap); });
    }
    ScheduleHeads();
}

uint32_t Msm8255ModemCallbacks::FlushDue(uint32_t out_pa, uint32_t out_cap) {
    const size_t index = DueHead(emu_.Get<GuestCycleClock>().Cycles());
    if (index == queue_.size()) return 0u;
    const uint32_t written = Send(queue_[index], out_pa, out_cap);
    ScheduleHeads();
    return written;
}

uint32_t Msm8255ModemCallbacks::Send(Entry& entry, uint32_t out_pa, uint32_t out_cap) {
    const Msm8255ModemCallback& cb = entry.callback;
    entry.xid  = ++next_xid_;
    entry.sent = 1u;
    LOG(SocModem, "msm8255 modem callbacks: call prog 0x%08X proc %u to pid %u cid %u, xid %u\n",
        cb.prog, cb.proc, cb.dest.pid, cb.dest.cid, entry.xid);
    const Msm8255OncrpcRoute route{out_pa, out_cap, cb.self_pid, cb.client_cid,
                                   cb.dest.pid, cb.dest.cid, entry.xid};
    return emu_.Get<Msm8255OncrpcCodec>().WriteCall(
        route, cb.prog, cb.vers, cb.proc, cb.args.data(), static_cast<uint32_t>(cb.args.size()));
}

uint32_t Msm8255ModemCallbacks::ConsumeReply(const Msm8255RpcServer& server,
                                             const uint8_t* payload,
                                             uint32_t size, uint32_t out_pa,
                                             uint32_t out_cap) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcReply reply = codec.ParseAcceptedReply(server, payload, size);
    uint32_t cid = 0u;
    server.CallbackClientCid(cid);
    size_t index = 0;
    while (index < queue_.size() &&
           (queue_[index].sent == 0u || queue_[index].xid != reply.xid)) {
        ++index;
    }
    if (index == queue_.size() || queue_[index].callback.client_cid != cid) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem callbacks: the guest answered xid %u at cid %u, which no outstanding "
            "callback to that cid holds", reply.xid, cid);
    }
    const Msm8255ModemCallback& cb = queue_[index].callback;
    const uint32_t want_bytes =
        kVoidReplyBytes + 4u * static_cast<uint32_t>(cb.reply_results.size());
    if (size != want_bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem callbacks: the reply to callback proc %u of prog 0x%08X is %u bytes, "
            "and the modeled reply is %u", cb.proc, cb.prog, size, want_bytes);
    }
    for (uint32_t i = 0; i < cb.reply_results.size(); ++i) {
        const uint32_t got = codec.ReplyWord(reply, 4u * i);
        if (got != cb.reply_results[i]) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem callbacks: the reply to callback proc %u of prog 0x%08X carries "
                "result %u = %u, and the modeled result is %u", cb.proc, cb.prog, i, got,
                cb.reply_results[i]);
        }
    }
    LOG(SocModem, "msm8255 modem callbacks: reply to xid %u\n", reply.xid);
    const uint32_t prog = cb.prog;
    queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(index));
    size_t next = index;
    while (next < queue_.size() && queue_[next].callback.prog != prog) ++next;
    if (next == queue_.size()) return 0u;
    return Send(queue_[next], out_pa, out_cap);
}

void Msm8255ModemCallbacks::SaveState(StateWriter& w) {
    w.Write("modem_cb_next_xid", next_xid_);
    w.Write("modem_cb_count", static_cast<uint32_t>(queue_.size()));
    for (size_t i = 0; i < queue_.size(); ++i) {
        const std::string p = "modem_cb" + std::to_string(i) + "_";
        const Entry& e = queue_[i];
        const Msm8255ModemCallback& cb = e.callback;
        w.Write((p + "prog").c_str(), cb.prog);
        w.Write((p + "vers").c_str(), cb.vers);
        w.Write((p + "client_cid").c_str(), cb.client_cid);
        w.Write((p + "proc").c_str(), cb.proc);
        w.Write((p + "self_pid").c_str(), cb.self_pid);
        w.Write((p + "dest_pid").c_str(), cb.dest.pid);
        w.Write((p + "dest_cid").c_str(), cb.dest.cid);
        w.Write((p + "arg_count").c_str(), static_cast<uint32_t>(cb.args.size()));
        w.WriteBytes((p + "args").c_str(), cb.args.data(), cb.args.size() * sizeof(uint32_t));
        w.Write((p + "result_count").c_str(), static_cast<uint32_t>(cb.reply_results.size()));
        w.WriteBytes((p + "results").c_str(), cb.reply_results.data(),
                     cb.reply_results.size() * sizeof(uint32_t));
        w.Write((p + "key").c_str(), cb.key);
        w.Write((p + "latency_ns").c_str(), cb.latency_ns);
        w.Write((p + "owner").c_str(), cb.owner);
        w.Write((p + "xid").c_str(), e.xid);
        w.Write((p + "sent").c_str(), e.sent);
        w.Write((p + "timed").c_str(), e.timed);
        w.Write((p + "due").c_str(), e.due);
    }
}

void Msm8255ModemCallbacks::RestoreState(StateReader& r) {
    uint32_t count = 0u;
    r.Read("modem_cb_next_xid", next_xid_);
    r.Read("modem_cb_count", count);
    queue_.assign(count, Entry{});
    for (size_t i = 0; i < queue_.size(); ++i) {
        const std::string p = "modem_cb" + std::to_string(i) + "_";
        Entry& e = queue_[i];
        Msm8255ModemCallback& cb = e.callback;
        uint32_t args    = 0u;
        uint32_t results = 0u;
        r.Read((p + "prog").c_str(), cb.prog);
        r.Read((p + "vers").c_str(), cb.vers);
        r.Read((p + "client_cid").c_str(), cb.client_cid);
        r.Read((p + "proc").c_str(), cb.proc);
        r.Read((p + "self_pid").c_str(), cb.self_pid);
        r.Read((p + "dest_pid").c_str(), cb.dest.pid);
        r.Read((p + "dest_cid").c_str(), cb.dest.cid);
        r.Read((p + "arg_count").c_str(), args);
        cb.args.resize(args);
        r.ReadBytes((p + "args").c_str(), cb.args.data(), cb.args.size() * sizeof(uint32_t));
        r.Read((p + "result_count").c_str(), results);
        cb.reply_results.resize(results);
        r.ReadBytes((p + "results").c_str(), cb.reply_results.data(),
                    cb.reply_results.size() * sizeof(uint32_t));
        r.Read((p + "key").c_str(), cb.key);
        r.Read((p + "latency_ns").c_str(), cb.latency_ns);
        r.Read((p + "owner").c_str(), cb.owner);
        r.Read((p + "xid").c_str(), e.xid);
        r.Read((p + "sent").c_str(), e.sent);
        r.Read((p + "timed").c_str(), e.timed);
        r.Read((p + "due").c_str(), e.due);
    }
}

REGISTER_SERVICE(Msm8255ModemCallbacks);

}
