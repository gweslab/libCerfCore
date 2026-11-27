#include "msm8255_rpc_router_peer.h"

#include "msm8255_modem_callbacks.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <algorithm>
#include <cstdint>

namespace cerfcore {

bool Msm8255RpcRouterPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255RpcRouterPeer::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        next_mid_ = 1;
        incomplete_.clear();
    });
}

uint32_t Msm8255RpcRouterPeer::Answer(uint32_t in_pa, uint32_t in_avail,
                                      uint32_t out_pa, uint32_t out_cap,
                                      uint32_t& consumed) {
    auto& mem      = emu_.Get<EmulatedMemory>();
    auto& registry = emu_.Get<Msm8255RpcServerRegistry>();

    if (in_avail < kHdrBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: %u bytes are queued, which is short of "
            "the %u-byte router header", in_avail, kHdrBytes);
    }

    /* Linux arch/arm/mach-msm smd_rpcrouter.c do_read_data tests the header
       version before it reads any other header field, and bounds the declared
       size against RPCROUTER_MSGSIZE_MAX after that. */
    const uint32_t version = mem.ReadWord(in_pa + kHdrVersionOff);
    if (version != kRouterVersion) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: router version %u is not modeled",
            version);
    }

    const uint32_t size = mem.ReadWord(in_pa + kHdrSizeOff);
    if (size > kRouterMsgSizeMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the header declares %u payload bytes and "
            "the router carries at most %u", size, kRouterMsgSizeMax);
    }
    if (in_avail < kHdrBytes + size) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the header declares %u payload bytes and "
            "only %u are queued", size, in_avail - kHdrBytes);
    }
    consumed = kHdrBytes + size;

    const uint32_t dst_cid = mem.ReadWord(in_pa + kHdrDstCidOff);
    if (dst_cid != kRouterAddress) {
        return AnswerData(in_pa, size, out_pa, out_cap);
    }

    if (size != kCtrlMsgBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: router-addressed payload is %u bytes, and "
            "only the %u-byte control message is modeled", size, kCtrlMsgBytes);
    }

    const uint32_t confirm_rx = mem.ReadWord(in_pa + kHdrConfirmRxOff);
    if (confirm_rx != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the guest asked for receive confirmation "
            "on a control message, which the router model never does");
    }

    const uint32_t type = mem.ReadWord(in_pa + kHdrTypeOff);
    const uint32_t cmd  = mem.ReadWord(in_pa + kHdrBytes);
    if (type != cmd) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: header type %u disagrees with payload "
            "command %u", type, cmd);
    }

    if (type == kCtrlCmdNewServer) {
        const uint32_t vers = mem.ReadWord(in_pa + kHdrBytes + kSrvVersOff);
        if (vers == 0u) {
            LOG(SocModem, "msm8255 rpc router peer: dropped the announcement of prog 0x%08X with version 0\n",
                mem.ReadWord(in_pa + kHdrBytes + kSrvProgOff));
            return 0u;
        }
        LOG(SocModem, "msm8255 rpc router peer: the guest announced prog 0x%08X vers 0x%08X at pid %u cid 0x%08X\n",
            mem.ReadWord(in_pa + kHdrBytes + kSrvProgOff), vers,
            mem.ReadWord(in_pa + kHdrBytes + kSrvPidOff),
            mem.ReadWord(in_pa + kHdrBytes + kSrvCidOff));
        return 0u;
    }

    if (type == kCtrlCmdRemoveClient) {
        const Msm8255RpcEndpoint client{mem.ReadWord(in_pa + kHdrBytes + kCliPidOff),
                                        mem.ReadWord(in_pa + kHdrBytes + kCliCidOff)};
        LOG(SocModem, "msm8255 rpc router peer: the guest removed client pid %u cid 0x%08X\n",
            client.pid, client.cid);
        std::erase_if(incomplete_, [&client](const Msm8255RpcIncompletePacket& p) {
            return Msm8255RpcEndpoint{p.src_pid, p.src_cid} == client;
        });
        for (auto* server : registry.Servers()) server->ReleaseClient(client);
        emu_.Get<Msm8255ModemCallbacks>().ReleaseClient(client);
        return 0u;
    }

    if (type != kCtrlCmdHello) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: router control command %u (words %08X %08X) is not modeled",
            type, mem.ReadWord(in_pa + kHdrBytes + kCliPidOff),
            mem.ReadWord(in_pa + kHdrBytes + kCliCidOff));
    }

    const uint32_t msg_bytes = kHdrBytes + kCtrlMsgBytes;
    const uint32_t announced = (uint32_t)registry.Servers().size();
    const uint32_t reply_bytes = (1u + announced) * msg_bytes;
    if (out_cap < reply_bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the modem fifo has %u bytes free, and "
            "the hello reply plus %u server announcements need %u",
            out_cap, announced, reply_bytes);
    }

    const uint32_t src_pid = mem.ReadWord(in_pa + kHdrSrcPidOff);
    const uint32_t dst_pid = mem.ReadWord(in_pa + kHdrDstPidOff);

    /* Linux arch/arm/mach-msm smd_rpcrouter.c: the hello arm answers with a
       hello of its own, then announces one new-server message per server the
       answering processor hosts. */
    WriteCtrlMsg(out_pa, kCtrlCmdHello, dst_pid, src_pid, 0u, 0u, 0u, 0u);
    uint32_t written = msg_bytes;
    for (auto* server : registry.Servers()) {
        WriteCtrlMsg(out_pa + written, kCtrlCmdNewServer, dst_pid, src_pid,
                     server->ServerProg(), server->ServerVers(), dst_pid,
                     server->ServerCid());
        written += msg_bytes;
    }
    return written;
}

void Msm8255RpcRouterPeer::ValidatePacmark(uint32_t pacmark,
                                           uint32_t body_bytes) {
    if ((pacmark & kPacmarkLenMask) != body_bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: pacmark 0x%08X declares %u payload bytes "
            "and the router header declares %u", pacmark,
            pacmark & kPacmarkLenMask, body_bytes);
    }
}

Msm8255RpcServer* Msm8255RpcRouterPeer::Claimant(uint32_t dst_cid, bool& reply) {
    auto& registry = emu_.Get<Msm8255RpcServerRegistry>();
    for (auto* server : registry.Servers()) {
        uint32_t cb_cid = 0;
        if (server->CallbackClientCid(cb_cid) && dst_cid == cb_cid) {
            reply = true;
            return server;
        }
        if (dst_cid == server->ServerCid()) {
            reply = false;
            return server;
        }
    }
    emu_.Get<Fatal>().Die(
        "msm8255 rpc router peer: the guest addressed cid 0x%08X, and none "
        "of the %u announced endpoints claims it",
        dst_cid, (uint32_t)registry.Servers().size());
}

uint32_t Msm8255RpcRouterPeer::AnswerData(uint32_t in_pa, uint32_t size,
                                          uint32_t out_pa, uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();

    if (size < kPacmarkBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: a %u-byte data packet carries no pacmark",
            size);
    }
    const uint32_t pacmark = mem.ReadWord(in_pa + kHdrBytes);
    const uint32_t bytes   = size - kPacmarkBytes;
    ValidatePacmark(pacmark, bytes);

    const uint32_t dst_cid = mem.ReadWord(in_pa + kHdrDstCidOff);
    bool reply = false;
    Msm8255RpcServer* server = Claimant(dst_cid, reply);

    const uint32_t mid = (pacmark >> kPacmarkMidShift) & kPacmarkMidMask;
    auto it = std::find_if(incomplete_.begin(), incomplete_.end(),
                           [&](const Msm8255RpcIncompletePacket& p) {
                               return p.dst_cid == dst_cid && p.mid == mid;
                           });
    if (it == incomplete_.end()) {
        incomplete_.push_back({dst_cid, mid, mem.ReadWord(in_pa + kHdrSrcPidOff),
                               mem.ReadWord(in_pa + kHdrSrcCidOff),
                               mem.ReadWord(in_pa + kHdrDstPidOff), {}});
        it = incomplete_.end() - 1;
    }
    const size_t held = it->body.size();
    it->body.resize(held + bytes);
    mem.CopyOut(in_pa + kHdrBytes + kPacmarkBytes, it->body.data() + held, bytes);
    if ((pacmark & kPacmarkLast) == 0u) {
        return AnswerConfirmRx(in_pa, out_pa, out_cap, 0u);
    }

    const Msm8255RpcIncompletePacket packet = std::move(*it);
    incomplete_.erase(it);

    const uint32_t total = kPacmarkBytes + static_cast<uint32_t>(packet.body.size());
    std::vector<uint8_t> payload(total);
    cerf::le::Put32(payload.data(), Pacmark(total - kPacmarkBytes, mid, true, true));
    std::copy(packet.body.begin(), packet.body.end(), payload.begin() + kPacmarkBytes);

    auto& callbacks = emu_.Get<Msm8255ModemCallbacks>();
    uint32_t written = reply
        ? callbacks.ConsumeReply(*server, payload.data(), total, out_pa, out_cap)
        : server->AnswerCall(payload.data(), total, out_pa, out_cap,
                             packet.dst_pid, packet.src_pid, packet.src_cid);
    written += callbacks.FlushDue(out_pa + written, out_cap - written);
    return written + AnswerConfirmRx(in_pa, out_pa, out_cap, written);
}

/* Linux arch/arm/mach-msm smd_rpcrouter.c msm_rpc_write: the sender allocates a
   fresh pacmark mid per message, from one counter masked to eight bits. */
uint32_t Msm8255RpcRouterPeer::NextMid() {
    return ++next_mid_ & kPacmarkMidMask;
}

uint32_t Msm8255RpcRouterPeer::Pacmark(uint32_t bytes, uint32_t mid, bool first,
                                       bool last) {
    return (bytes & kPacmarkLenMask) | (mid << kPacmarkMidShift) |
           (first ? kPacmarkFirst : 0u) | (last ? kPacmarkLast : 0u);
}

/* Linux arch/arm/mach-msm smd_rpcrouter.c do_read_data: every data packet that
   asks for confirmation is answered with a resume-tx naming that packet's own
   destination, not only the ones the receiver replies to. */
uint32_t Msm8255RpcRouterPeer::AnswerConfirmRx(uint32_t in_pa, uint32_t out_pa,
                                               uint32_t out_cap,
                                               uint32_t reserved) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const uint32_t confirm_rx = mem.ReadWord(in_pa + kHdrConfirmRxOff);
    if (confirm_rx == 0u) {
        return 0u;
    }
    if (confirm_rx != 1u) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the message carries confirm_rx %u, and "
            "only the cleared and set forms are modeled", confirm_rx);
    }

    const uint32_t bytes = kHdrBytes + kCtrlMsgBytes;
    if (out_cap < reserved + bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 rpc router peer: the modem fifo has %u bytes free, and "
            "the resume-tx needs %u after the %u already reserved",
            out_cap, bytes, reserved);
    }
    const uint32_t self_pid = mem.ReadWord(in_pa + kHdrDstPidOff);
    WriteResumeTx(out_pa + reserved, self_pid,
                  mem.ReadWord(in_pa + kHdrSrcPidOff), self_pid,
                  mem.ReadWord(in_pa + kHdrDstCidOff));
    return bytes;
}

void Msm8255RpcRouterPeer::WriteHeader(uint32_t out_pa, uint32_t type,
                                       uint32_t src_pid, uint32_t src_cid,
                                       uint32_t size, uint32_t dst_pid,
                                       uint32_t dst_cid) {
    auto& mem = emu_.Get<EmulatedMemory>();

    mem.WriteWord(out_pa + kHdrVersionOff,   kRouterVersion);
    mem.WriteWord(out_pa + kHdrTypeOff,      type);
    mem.WriteWord(out_pa + kHdrSrcPidOff,    src_pid);
    mem.WriteWord(out_pa + kHdrSrcCidOff,    src_cid);
    mem.WriteWord(out_pa + kHdrConfirmRxOff, 0u);
    mem.WriteWord(out_pa + kHdrSizeOff,      size);
    mem.WriteWord(out_pa + kHdrDstPidOff,    dst_pid);
    mem.WriteWord(out_pa + kHdrDstCidOff,    dst_cid);
}

/* Linux arch/arm/mach-msm smd_rpcrouter.c rpcrouter_send_control_msg: a control
   message is addressed router to router and carries its command word twice, in
   the header type and in the first payload word. */
void Msm8255RpcRouterPeer::WriteCtrlHeader(uint32_t out_pa, uint32_t cmd,
                                           uint32_t self_pid,
                                           uint32_t peer_pid) {
    WriteHeader(out_pa, cmd, self_pid, kRouterAddress, kCtrlMsgBytes, peer_pid,
                kRouterAddress);
    emu_.Get<EmulatedMemory>().WriteWord(out_pa + kHdrBytes, cmd);
}

void Msm8255RpcRouterPeer::WriteResumeTx(uint32_t out_pa, uint32_t self_pid,
                                         uint32_t peer_pid, uint32_t cli_pid,
                                         uint32_t cli_cid) {
    auto& mem = emu_.Get<EmulatedMemory>();

    WriteCtrlHeader(out_pa, kCtrlCmdResumeTx, self_pid, peer_pid);

    mem.WriteWord(out_pa + kHdrBytes + kCliPidOff, cli_pid);
    mem.WriteWord(out_pa + kHdrBytes + kCliCidOff, cli_cid);
    for (uint32_t off = kCliCidOff + 4u; off < kCtrlMsgBytes; off += 4u) {
        mem.WriteWord(out_pa + kHdrBytes + off, 0u);
    }
}

void Msm8255RpcRouterPeer::WriteCtrlMsg(uint32_t out_pa, uint32_t cmd,
                                        uint32_t self_pid, uint32_t peer_pid,
                                        uint32_t prog, uint32_t vers,
                                        uint32_t srv_pid, uint32_t srv_cid) {
    auto& mem = emu_.Get<EmulatedMemory>();

    WriteCtrlHeader(out_pa, cmd, self_pid, peer_pid);

    mem.WriteWord(out_pa + kHdrBytes + kSrvProgOff, prog);
    mem.WriteWord(out_pa + kHdrBytes + kSrvVersOff, vers);
    mem.WriteWord(out_pa + kHdrBytes + kSrvPidOff,  srv_pid);
    mem.WriteWord(out_pa + kHdrBytes + kSrvCidOff,  srv_cid);
}

void Msm8255RpcRouterPeer::SaveState(StateWriter& w) {
    w.Write<uint32_t>("next_mid", next_mid_);
    w.Write<uint32_t>("incomplete_count", static_cast<uint32_t>(incomplete_.size()));
    for (const Msm8255RpcIncompletePacket& p : incomplete_) {
        w.Write<uint32_t>("incomplete_dst_cid", p.dst_cid);
        w.Write<uint32_t>("incomplete_mid", p.mid);
        w.Write<uint32_t>("incomplete_src_pid", p.src_pid);
        w.Write<uint32_t>("incomplete_src_cid", p.src_cid);
        w.Write<uint32_t>("incomplete_dst_pid", p.dst_pid);
        w.Write<uint32_t>("incomplete_bytes", static_cast<uint32_t>(p.body.size()));
        w.WriteBytes("incomplete_body", p.body.data(), p.body.size());
    }
    emu_.Get<Msm8255ModemCallbacks>().SaveState(w);
}

void Msm8255RpcRouterPeer::RestoreState(StateReader& r) {
    r.Read("next_mid", next_mid_);
    uint32_t count = 0;
    r.Read("incomplete_count", count);
    incomplete_.assign(count, {});
    for (Msm8255RpcIncompletePacket& p : incomplete_) {
        r.Read("incomplete_dst_cid", p.dst_cid);
        r.Read("incomplete_mid", p.mid);
        r.Read("incomplete_src_pid", p.src_pid);
        r.Read("incomplete_src_cid", p.src_cid);
        r.Read("incomplete_dst_pid", p.dst_pid);
        uint32_t bytes = 0;
        r.Read("incomplete_bytes", bytes);
        p.body.assign(bytes, 0u);
        r.ReadBytes("incomplete_body", p.body.data(), p.body.size());
    }
    emu_.Get<Msm8255ModemCallbacks>().RestoreState(r);
}

REGISTER_SERVICE(Msm8255RpcRouterPeer);

}
