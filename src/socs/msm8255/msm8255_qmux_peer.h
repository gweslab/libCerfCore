#pragma once

#include "msm8255_rpc_client_table.h"

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class Msm8255QmiService;
class StateReader;
class StateWriter;

inline constexpr uint32_t kQmiBroadcastClient = 0xFFu;

inline constexpr const char* kMsm8255QmuxControlPorts[] = {"DATA5_CNTL", "DATA6_CNTL",
                                                           "DATA7_CNTL"};
inline constexpr uint32_t kMsm8255QmuxPortCount =
    sizeof(kMsm8255QmuxControlPorts) / sizeof(kMsm8255QmuxControlPorts[0]);

inline constexpr uint32_t kQmiPowerSaveStates = 2u;
inline constexpr uint32_t kQmiServiceWords    = 256u / 32u;

struct Msm8255QmuxPort {
    uint32_t data_format_set = 0u;
    uint32_t qos             = 0u;
    uint32_t link_protocol   = 0u;
    uint32_t power_save_services[kQmiPowerSaveStates * kQmiServiceWords] = {};

    template <typename F>
    static constexpr void Visit(Msm8255QmuxPort& p, F& field) {
        field("qmux_port_data_format_set", p.data_format_set);
        field("qmux_port_qos", p.qos);
        field("qmux_port_link_protocol", p.link_protocol);
        field("qmux_port_power_save_services", p.power_save_services);
    }
};

struct Msm8255QmiClient {
    uint32_t id      = 0u;
    uint32_t port    = 0u;
    uint32_t service = 0u;

    template <typename F>
    static constexpr void Visit(Msm8255QmiClient& c, F& field) {
        field("qmi_client_id", c.id);
        field("qmi_client_port", c.port);
        field("qmi_client_service", c.service);
    }
};

class Msm8255QmuxPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Answer(uint32_t port, uint32_t in_pa, uint32_t in_bytes, uint32_t out_pa,
                    uint32_t out_cap);

    void RegisterService(Msm8255QmiService* service);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint32_t AnswerService(uint32_t port, uint8_t service, uint8_t client, uint32_t in_pa,
                           uint32_t in_bytes, uint32_t out_pa, uint32_t out_cap);
    std::vector<uint8_t> AllocateClient(uint32_t port, uint32_t in_pa, uint32_t tlv_len);
    uint16_t SetDataFormat(uint32_t port, uint32_t in_pa, uint32_t tlv_len);
    void SetPowerSaveConfig(uint32_t port, uint32_t in_pa, uint32_t tlv_len);
    uint32_t WriteFrame(uint32_t out_pa, uint32_t out_cap, uint8_t service, uint8_t client,
                        const std::vector<uint8_t>& sdu);

    Msm8255RpcClientTable<Msm8255QmiClient> clients_{kQmiBroadcastClient - 1u};
    Msm8255QmuxPort                         ports_[kMsm8255QmuxPortCount] = {};
    std::vector<Msm8255QmiService*>         services_;
};

}
