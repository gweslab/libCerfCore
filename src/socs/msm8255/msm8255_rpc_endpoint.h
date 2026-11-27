#pragma once

#include <cstdint>

namespace cerfcore {

struct Msm8255RpcEndpoint {
    uint32_t pid = 0u;
    uint32_t cid = 0u;

    friend bool operator==(const Msm8255RpcEndpoint&, const Msm8255RpcEndpoint&) = default;
};

}
