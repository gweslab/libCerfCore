#include "msm8255_oncrpc_codec.h"

#include "msm8255_rpc_router_peer.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <algorithm>
#include <cstdint>
#include <typeinfo>

namespace cerfcore {

namespace {

constexpr uint32_t kReplyCommonBytes   = kReplyStatOff + 4u;
constexpr uint32_t kReplyAcceptedBytes = kReplyAcceptStatOff + 4u;

}

bool Msm8255OncrpcCodec::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

Msm8255OncrpcCall Msm8255OncrpcCodec::ParseCall(const Msm8255RpcServer& server,
                                                const uint8_t* payload, uint32_t size) {
    if (size < kPacmarkBytes + kCallArgsOff) {
        emu_.Get<Fatal>().Die(
            "Service '%s': rpc payload is %u bytes, which is short of the "
            "%u-byte call header",
            typeid(server).name(), size, kPacmarkBytes + kCallArgsOff);
    }

    const uint8_t* body       = payload + kPacmarkBytes;
    const uint32_t body_bytes = size - kPacmarkBytes;
    const uint32_t xid  = BodyWord(body, body_bytes, kCallXidOff);
    const uint32_t type = BodyWord(body, body_bytes, kCallTypeOff);
    const uint32_t rpcv = BodyWord(body, body_bytes, kCallRpcVersOff);
    const uint32_t prog = BodyWord(body, body_bytes, kCallProgOff);
    const uint32_t vers = BodyWord(body, body_bytes, kCallVersOff);
    const uint32_t proc = BodyWord(body, body_bytes, kCallProcOff);

    if (type != kOncrpcCall || rpcv != kOncrpcVersion) {
        emu_.Get<Fatal>().Die(
            "Service '%s': rpc message type %u version %u is not modeled",
            typeid(server).name(), type, rpcv);
    }
    if (prog != server.ServerProg() || vers != server.ServerVers()) {
        emu_.Get<Fatal>().Die(
            "Service '%s': rpc call prog 0x%08X vers 0x%08X is not modeled",
            typeid(server).name(), prog, vers);
    }

    const uint32_t cred_flavor = BodyWord(body, body_bytes, kCallCredFlavorOff);
    const uint32_t cred_len    = BodyWord(body, body_bytes, kCallCredLenOff);
    const uint32_t verf_flavor = BodyWord(body, body_bytes, kCallVerfFlavorOff);
    const uint32_t verf_len    = BodyWord(body, body_bytes, kCallVerfLenOff);
    if (cred_flavor != kAuthNone || cred_len != 0u ||
        verf_flavor != kAuthNone || verf_len != 0u) {
        emu_.Get<Fatal>().Die(
            "Service '%s': rpc call carries cred flavor %u length %u and verf "
            "flavor %u length %u, and only an unauthenticated call is modeled",
            typeid(server).name(), cred_flavor, cred_len, verf_flavor,
            verf_len);
    }

    return {body, body_bytes, xid, proc};
}

std::vector<uint32_t> Msm8255OncrpcCodec::CallArgs(const Msm8255RpcServer& server,
                                                   const Msm8255OncrpcCall& call, uint32_t size) {
    const uint32_t bytes = size - (kPacmarkBytes + kCallArgsOff);
    if (bytes % 4u != 0u) {
        emu_.Get<Fatal>().Die("Service '%s': procedure %u carries %u argument bytes",
                              typeid(server).name(), call.proc, bytes);
    }
    std::vector<uint32_t> words(bytes / 4u);
    for (uint32_t i = 0; i < words.size(); ++i) {
        words[i] = CallWord(call, kCallArgsOff + 4u * i);
    }
    return words;
}

void Msm8255OncrpcCodec::RequireArgWords(const Msm8255RpcServer& server, uint32_t proc,
                                         const std::vector<uint32_t>& args, uint32_t want) {
    if (args.size() == want) return;
    emu_.Get<Fatal>().Die(
        "Service '%s': procedure %u carries %u argument words and the modeled form has %u",
        typeid(server).name(), proc, static_cast<uint32_t>(args.size()), want);
}

void Msm8255OncrpcCodec::RequireArgWordsAtLeast(const Msm8255RpcServer& server, uint32_t proc,
                                                const std::vector<uint32_t>& args,
                                                uint32_t want) {
    if (args.size() >= want) return;
    emu_.Get<Fatal>().Die(
        "Service '%s': procedure %u carries %u argument words, short of the modeled %u",
        typeid(server).name(), proc, static_cast<uint32_t>(args.size()), want);
}

void Msm8255OncrpcCodec::RequireBoolean(const Msm8255RpcServer& server, uint32_t proc,
                                        uint32_t word) {
    if (word == kXdrFalse || word == kXdrTrue) return;
    emu_.Get<Fatal>().Die("Service '%s': procedure %u carries boolean word %u",
                          typeid(server).name(), proc, word);
}

void Msm8255OncrpcCodec::RequireNoCallback(const Msm8255RpcServer& server, uint32_t proc,
                                           uint32_t callback) {
    if (callback == kRpcNoCallback) return;
    emu_.Get<Fatal>().Die(
        "Service '%s': procedure %u carries callback %u, and that callback is not modeled",
        typeid(server).name(), proc, callback);
}

void Msm8255OncrpcCodec::RequireCallBytes(const Msm8255RpcServer& server,
                                          uint32_t proc, uint32_t size,
                                          uint32_t want) {
    if (size == want) {
        return;
    }
    emu_.Get<Fatal>().Die(
        "Service '%s': procedure %u carries %u payload bytes and the modeled "
        "argument list is %u",
        typeid(server).name(), proc, size, want);
}

/* RFC 4506 section 4.11: a string is its byte count as an unsigned integer,
   then that many bytes, then 0 to 3 zero bytes so the total is a multiple of
   four. */
uint32_t Msm8255OncrpcCodec::ConsumeXdrString(const uint8_t* body, uint32_t size,
                                              uint32_t off, uint32_t which,
                                              std::string* text) {
    if (size < kPacmarkBytes + off + 4u) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: the rpc call is %u bytes, which is short of "
            "the %u that carry the length of its string argument %u", size,
            kPacmarkBytes + off + 4u, which);
    }

    const uint32_t len = BodyWord(body, size - kPacmarkBytes, off);
    if (len == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: string argument %u of the rpc call is empty, "
            "and only a named one is modeled", which);
    }
    if (len > kRouterMsgSizeMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: string argument %u of the rpc call is %u "
            "bytes, which does not fit a %u-byte router message", which, len,
            kRouterMsgSizeMax);
    }
    if (size < kPacmarkBytes + off + 4u + len) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: the rpc call is %u bytes, which is short of "
            "the %u that carry its string argument %u", size,
            kPacmarkBytes + off + 4u + len, which);
    }

    if (text != nullptr) {
        text->clear();
        for (uint32_t i = 0; i < len; ++i) {
            const uint8_t byte = body[off + 4u + i];
            if (byte == 0u) break;
            text->push_back(static_cast<char>(byte));
        }
    }
    return off + 4u + ((len + 3u) & ~3u);
}

uint32_t Msm8255OncrpcCodec::BodyWord(const uint8_t* body, uint32_t body_bytes,
                                      uint32_t offset) {
    if (offset > body_bytes || body_bytes - offset < 4u) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: a read of the word at byte %u runs past the "
            "%u-byte rpc message", offset, body_bytes);
    }
    return cerf::be::U32(body, offset);
}

uint32_t Msm8255OncrpcCodec::CallWord(const Msm8255OncrpcCall& call, uint32_t offset) {
    return BodyWord(call.body, call.body_bytes, offset);
}

uint32_t Msm8255OncrpcCodec::ReplyWord(const Msm8255OncrpcReply& reply, uint32_t offset) {
    return BodyWord(reply.results, reply.result_bytes, offset);
}

/* RFC 5531 section 9: an accepted reply is xid, msg_type, reply_stat, the verf
   opaque_auth pair, accept_stat, then the procedure results. */
uint32_t Msm8255OncrpcCodec::WriteAcceptedReply(const Msm8255OncrpcRoute& route,
                                                const uint32_t* results,
                                                uint32_t result_words) {
    return WriteReply(route, kAcceptSuccess, results, result_words);
}

uint32_t Msm8255OncrpcCodec::WriteOptionalReply(const Msm8255OncrpcRoute& route,
                                                uint32_t status, uint32_t present,
                                                uint32_t value) {
    if (present == kXdrFalse) {
        const uint32_t results[2] = {status, kXdrFalse};
        return WriteAcceptedReply(route, results, 2u);
    }
    const uint32_t results[3] = {status, kXdrTrue, value};
    return WriteAcceptedReply(route, results, 3u);
}

uint32_t Msm8255OncrpcCodec::WriteMessage(const Msm8255OncrpcRoute& route,
                                          const std::vector<uint32_t>& body) {
    auto& router = emu_.Get<Msm8255RpcRouterPeer>();
    auto& mem    = emu_.Get<EmulatedMemory>();

    const uint32_t body_bytes = 4u * static_cast<uint32_t>(body.size());
    const uint32_t fragments =
        (body_bytes + kRouterFragmentDataMax - 1u) / kRouterFragmentDataMax;
    const uint32_t total = fragments * (kHdrBytes + kPacmarkBytes) + body_bytes;
    if (route.out_cap < total) {
        emu_.Get<Fatal>().Die(
            "msm8255 oncrpc codec: a %u-byte rpc body in %u fragments needs %u "
            "bytes and the window at 0x%08X has %u", body_bytes, fragments, total,
            route.out_pa, route.out_cap);
    }

    const uint32_t mid = router.NextMid();
    uint32_t out = route.out_pa;
    for (uint32_t off = 0; off < body_bytes; off += kRouterFragmentDataMax) {
        const uint32_t bytes = std::min(kRouterFragmentDataMax, body_bytes - off);
        router.WriteHeader(out, kCtrlCmdData, route.self_pid, route.self_cid,
                           kPacmarkBytes + bytes, route.peer_pid, route.peer_cid);
        mem.WriteWord(out + kHdrBytes,
                      Msm8255RpcRouterPeer::Pacmark(bytes, mid, off == 0u,
                                                    off + bytes == body_bytes));
        for (uint32_t i = 0; i < bytes; i += 4u) {
            mem.WriteWord(out + kHdrBytes + kPacmarkBytes + i, Be32(body[(off + i) / 4u]));
        }
        out += kHdrBytes + kPacmarkBytes + bytes;
    }
    return total;
}

uint32_t Msm8255OncrpcCodec::WriteReply(const Msm8255OncrpcRoute& route,
                                        uint32_t accept_stat,
                                        const uint32_t* results,
                                        uint32_t result_words) {
    std::vector<uint32_t> body(kReplyResultsOff / 4u + result_words, 0u);
    body[kReplyXidOff / 4u]        = route.xid;
    body[kReplyTypeOff / 4u]       = kOncrpcReply;
    body[kReplyStatOff / 4u]       = kMsgAccepted;
    body[kReplyVerfFlavorOff / 4u] = kAuthNone;
    body[kReplyVerfLenOff / 4u]    = 0u;
    body[kReplyAcceptStatOff / 4u] = accept_stat;
    for (uint32_t i = 0; i < result_words; ++i) {
        body[kReplyResultsOff / 4u + i] = results[i];
    }
    return WriteMessage(route, body);
}

uint32_t Msm8255OncrpcCodec::WriteCall(const Msm8255OncrpcRoute& route, uint32_t prog,
                                       uint32_t vers, uint32_t proc,
                                       const uint32_t* args, uint32_t arg_words) {
    std::vector<uint32_t> body(kCallArgsOff / 4u + arg_words, 0u);
    body[kCallXidOff / 4u]        = route.xid;
    body[kCallTypeOff / 4u]       = kOncrpcCall;
    body[kCallRpcVersOff / 4u]    = kOncrpcVersion;
    body[kCallProgOff / 4u]       = prog;
    body[kCallVersOff / 4u]       = vers;
    body[kCallProcOff / 4u]       = proc;
    body[kCallCredFlavorOff / 4u] = kAuthNone;
    body[kCallCredLenOff / 4u]    = 0u;
    body[kCallVerfFlavorOff / 4u] = kAuthNone;
    body[kCallVerfLenOff / 4u]    = 0u;
    for (uint32_t i = 0; i < arg_words; ++i) {
        body[kCallArgsOff / 4u + i] = args[i];
    }
    return WriteMessage(route, body);
}

Msm8255OncrpcReply Msm8255OncrpcCodec::ParseAcceptedReply(
    const Msm8255RpcServer& server, const uint8_t* payload, uint32_t size) {
    if (size < kPacmarkBytes + kReplyCommonBytes) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the callback reply is %u bytes, which is short of "
            "the %u bytes every reply carries",
            typeid(server).name(), size, kPacmarkBytes + kReplyCommonBytes);
    }

    const uint8_t* body       = payload + kPacmarkBytes;
    const uint32_t body_bytes = size - kPacmarkBytes;
    const uint32_t xid  = BodyWord(body, body_bytes, kReplyXidOff);
    const uint32_t type = BodyWord(body, body_bytes, kReplyTypeOff);
    const uint32_t stat = BodyWord(body, body_bytes, kReplyStatOff);

    if (type != kOncrpcReply) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the guest sent rpc message type %u to the callback "
            "client, and only a reply is modeled", typeid(server).name(), type);
    }
    if (stat != kMsgAccepted) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the callback reply carries reply_stat %u, and only "
            "an accepted reply is modeled", typeid(server).name(), stat);
    }
    if (size < kPacmarkBytes + kReplyAcceptedBytes) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the accepted callback reply is %u bytes, which is "
            "short of the %u that carry its verifier and accept_stat",
            typeid(server).name(), size, kPacmarkBytes + kReplyAcceptedBytes);
    }

    const uint32_t vfl  = BodyWord(body, body_bytes, kReplyVerfFlavorOff);
    const uint32_t vlen = BodyWord(body, body_bytes, kReplyVerfLenOff);
    const uint32_t acc  = BodyWord(body, body_bytes, kReplyAcceptStatOff);
    if (vfl != kAuthNone || vlen != 0u) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the callback reply verifier is flavor %u length %u, "
            "and only the null verifier is modeled", typeid(server).name(), vfl,
            vlen);
    }
    if (acc != kAcceptSuccess) {
        emu_.Get<Fatal>().Die(
            "Service '%s': the guest answered a callback with accept_stat %u, "
            "and only success is modeled", typeid(server).name(), acc);
    }
    return {xid, body + kReplyResultsOff, body_bytes - kReplyResultsOff};
}

REGISTER_SERVICE(Msm8255OncrpcCodec);

}
