#pragma once

#include "msm8255_pdapi_types.h"

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255PdapiEngine : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Acknowledge(const Msm8255PdapiCommand& cmd, uint32_t command);
    void StartSession(const Msm8255PdapiCommand& cmd, uint32_t client, uint32_t timeout_s);
    void EndSession(const Msm8255PdapiCommand& cmd, uint32_t client);
    void InjectPosition(const Msm8255PdapiCommand& cmd, uint32_t client);
    void SetParam(const Msm8255PdapiCommand& cmd, uint32_t client, uint32_t type,
                  std::vector<uint32_t> value);
    void GetParam(const Msm8255PdapiCommand& cmd, uint32_t client, uint32_t type);
    void InjectTime(const Msm8255PdapiCommand& cmd, uint32_t client);
    void XtraDownloadRequest(const Msm8255PdapiCommand& cmd, uint32_t client);
    void XtraDataPart(const Msm8255PdapiCommand& cmd, uint32_t client);
    void XtraValidity(const Msm8255PdapiCommand& cmd, uint32_t client);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    struct Session {
        uint32_t client   = 0u;
        uint64_t deadline = 0u;
    };

    static constexpr uint32_t kParamTypes = 32u;

    void Deliver(uint32_t client, Msm8255PdapiKind kind, uint32_t event,
                 const std::vector<uint32_t>& info);
    void DeliverPd(uint32_t client, uint32_t event, uint32_t end_status);
    void DeliverPa(uint32_t client, uint32_t type, const std::vector<uint32_t>& value);
    void DeliverXtraStatus(uint32_t client, uint32_t status);
    void ExpireSessions();
    void Rearm();

    std::vector<Session>                             sessions_;
    std::array<std::vector<uint32_t>, kParamTypes>   params_{};
    GuestCycleClock::Event*                          timer_ = nullptr;
};

}
