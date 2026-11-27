#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class Msm8255RpcServer;
class StateReader;
class StateWriter;

struct Msm8255RpcIncompletePacket {
    uint32_t             dst_cid = 0;
    uint32_t             mid     = 0;
    uint32_t             src_pid = 0;
    uint32_t             src_cid = 0;
    uint32_t             dst_pid = 0;
    std::vector<uint8_t> body;
};

class Msm8255RpcRouterPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Answer(uint32_t in_pa, uint32_t in_avail, uint32_t out_pa,
                    uint32_t out_cap, uint32_t& consumed);

    void WriteHeader(uint32_t out_pa, uint32_t type, uint32_t src_pid,
                     uint32_t src_cid, uint32_t size, uint32_t dst_pid,
                     uint32_t dst_cid);
    uint32_t NextMid();
    static uint32_t Pacmark(uint32_t bytes, uint32_t mid, bool first, bool last);
    uint32_t AnswerConfirmRx(uint32_t in_pa, uint32_t out_pa, uint32_t out_cap,
                             uint32_t reserved);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    void ValidatePacmark(uint32_t pacmark, uint32_t body_bytes);
    uint32_t AnswerData(uint32_t in_pa, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap);
    Msm8255RpcServer* Claimant(uint32_t dst_cid, bool& reply);
    void WriteCtrlHeader(uint32_t out_pa, uint32_t cmd, uint32_t self_pid,
                         uint32_t peer_pid);
    void WriteCtrlMsg(uint32_t out_pa, uint32_t cmd, uint32_t self_pid,
                      uint32_t peer_pid, uint32_t prog, uint32_t vers,
                      uint32_t srv_pid, uint32_t srv_cid);
    void WriteResumeTx(uint32_t out_pa, uint32_t self_pid, uint32_t peer_pid,
                       uint32_t cli_pid, uint32_t cli_cid);

    uint32_t next_mid_ = 1;
    std::vector<Msm8255RpcIncompletePacket> incomplete_;
};

}
