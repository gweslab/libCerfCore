#pragma once

#include <cerfcore/core/service.h>

#include "msm8255_dal_remote_device.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255DalRemoteServer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterDevice(Msm8255DalRemoteDevice* device);

    uint32_t Answer(uint32_t in_pa, uint32_t in_avail, uint32_t out_pa,
                    uint32_t out_cap, uint32_t& consumed);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kHandleSlots = 16u;

    uint32_t AnswerAttach(uint32_t in_pa, uint32_t len, uint32_t out_pa,
                          uint32_t out_cap);
    uint32_t AnswerMethod(uint32_t in_pa, uint32_t len, uint32_t out_pa,
                          uint32_t out_cap);
    uint32_t AnswerDetach(uint32_t in_pa, uint32_t len, uint32_t out_pa,
                          uint32_t out_cap);
    uint32_t WriteHeaderReply(uint32_t in_pa, uint32_t out_pa, uint32_t bytes,
                              uint32_t status);
    uint32_t PlaceEvents(uint32_t out_pa, uint32_t out_cap, uint32_t reply_bytes,
                         const Msm8255DalRemoteEvents& events);
    uint32_t EventBytes(const Msm8255DalRemoteEvent& event) const;
    uint32_t WriteEvent(uint32_t out_pa, const Msm8255DalRemoteEvent& event);
    Msm8255DalRemoteDevice* FindDevice(uint32_t device_id) const;
    uint32_t RequireSlot(uint32_t handle) const;

    std::vector<Msm8255DalRemoteDevice*>    devices_;
    std::array<uint32_t, kHandleSlots>      slot_device_{};
    bool                                    port_announced_ = false;
};

}
