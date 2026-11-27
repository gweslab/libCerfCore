#pragma once

#include "msm8255_pdapi_types.h"
#include "msm8255_rpc_client_table.h"

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255PdapiClients : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Create(uint32_t type);
    bool Remove(uint32_t id);
    Msm8255PdapiClient* Find(uint32_t id);
    void ReleaseClient(const Msm8255RpcEndpoint& client);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    Msm8255RpcClientTable<Msm8255PdapiClient> clients_{UINT32_MAX};
};

}
