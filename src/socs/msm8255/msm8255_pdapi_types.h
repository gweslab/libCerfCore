#pragma once

#include "msm8255_rpc_endpoint.h"
#include "msm8255_rpcrouter_wire.h"

#include <array>
#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kPdapiCbProg      = 0x3100005Bu;
inline constexpr uint32_t kPdapiCbVers      = 0x00100003u;
inline constexpr uint32_t kPdapiCbClientCid = 14u;

enum class Msm8255PdapiKind : uint32_t { Pd = 0, Pa, Lcs, Xtra, Ext, Count };

inline constexpr uint32_t kPdapiKinds = static_cast<uint32_t>(Msm8255PdapiKind::Count);

struct Msm8255PdapiCommand {
    uint32_t           callback = kRpcNoCallback;
    uint32_t           data     = 0u;
    uint32_t           self_pid = 0u;
    Msm8255RpcEndpoint source;
};

struct Msm8255PdapiRegistration {
    uint32_t           set      = 0u;
    uint32_t           callback = kRpcNoCallback;
    uint32_t           data     = 0u;
    uint32_t           mask     = 0u;
    uint32_t           self_pid = 0u;
    Msm8255RpcEndpoint dest;
};

struct Msm8255PdapiClient {
    uint32_t id     = 0u;
    uint32_t type   = 0u;
    uint32_t active = 0u;
    std::array<Msm8255PdapiRegistration, kPdapiKinds> regs{};

    template <typename F>
    static constexpr void Visit(Msm8255PdapiClient& c, F& field) {
        field("pdapi_client_id", c.id);
        field("pdapi_client_type", c.type);
        field("pdapi_client_active", c.active);
        for (Msm8255PdapiRegistration& reg : c.regs) {
            field("pdapi_reg_set", reg.set);
            field("pdapi_reg_callback", reg.callback);
            field("pdapi_reg_data", reg.data);
            field("pdapi_reg_mask", reg.mask);
            field("pdapi_reg_self_pid", reg.self_pid);
            field("pdapi_reg_dest_pid", reg.dest.pid);
            field("pdapi_reg_dest_cid", reg.dest.cid);
        }
    }
};

}
