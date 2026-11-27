#include "msm8255_rpc_server.h"

namespace cerfcore {

void Msm8255RpcServer::SaveState(StateWriter& w) { (void)w; }

void Msm8255RpcServer::RestoreState(StateReader& r) { (void)r; }

bool Msm8255RpcServer::CallbackClientCid(uint32_t& cid) const {
    (void)cid;
    return false;
}

}
