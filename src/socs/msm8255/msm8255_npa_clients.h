#pragma once

#include "msm8255_rpc_client_table.h"

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct Msm8255NpaClient {
    uint32_t id      = 0u;
    uint32_t name    = 0u;
    uint32_t request = 0u;
    uint32_t issued  = 0u;

    template <typename F>
    static constexpr void Visit(Msm8255NpaClient& c, F& field) {
        field("npa_client_id", c.id);
        field("npa_client_name", c.name);
        field("npa_client_request", c.request);
        field("npa_client_issued", c.issued);
    }
};

class Msm8255NpaClients : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t IssueHandle(const std::string& resource);
    uint32_t ApplyRequest(uint32_t handle, uint32_t request);

    void RegisterResourceDriver(std::string name,
                                std::function<void(uint32_t)> apply_level);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    struct ResourceDriver {
        std::string                   name;
        std::function<void(uint32_t)> apply_level;
    };

    uint32_t NameIndex(const std::string& resource);

    Msm8255RpcClientTable<Msm8255NpaClient> clients_{UINT32_MAX};
    std::vector<std::string>                names_;
    std::vector<ResourceDriver>             drivers_;
};

}
