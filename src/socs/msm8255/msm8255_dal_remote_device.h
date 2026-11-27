#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255DalShapeWord    = 0u;
inline constexpr uint32_t kMsm8255DalShapeInput   = 5u;
inline constexpr uint32_t kMsm8255DalShapeBuffers = 8u;
inline constexpr uint32_t kMsm8255DalShapeOutput  = 9u;
inline constexpr uint32_t kMsm8255DalSuccess      = 0u;

inline constexpr uint32_t kMsm8255DalEventHeaderBytes = 28u;
inline constexpr uint32_t kMsm8255DalEventMessageMax  = 1024u;
inline constexpr uint32_t kMsm8255DalEventPayloadMax =
    kMsm8255DalEventMessageMax - kMsm8255DalEventHeaderBytes;

struct Msm8255DalRemoteCall {
    uint32_t method    = 0;
    uint32_t shape     = 0;
    uint32_t in_pa     = 0;
    uint32_t in_bytes  = 0;
    uint32_t out_pa    = 0;
    uint32_t out_bytes = 0;
};

enum class Msm8255DalEventOrder : uint8_t { BeforeReply, AfterReply };

struct Msm8255DalRemoteEvent {
    Msm8255DalEventOrder order        = Msm8255DalEventOrder::AfterReply;
    uint32_t             event_handle = 0;
    uint32_t             param        = 0;
    std::vector<uint8_t> payload;
};

using Msm8255DalRemoteEvents = std::vector<Msm8255DalRemoteEvent>;

class Msm8255DalRemoteDevice : public Service {
public:
    using Service::Service;

    virtual uint32_t DeviceId() const = 0;
    virtual uint32_t InterfaceVersion() const = 0;
    virtual uint32_t Invoke(const Msm8255DalRemoteCall& call,
                            Msm8255DalRemoteEvents& events) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};

}
