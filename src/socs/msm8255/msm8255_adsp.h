#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "msm8255_adsp_image.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class Msm8255AdspTask;

class Msm8255Adsp : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void RegisterTask(Msm8255AdspTask* task);
    void SetModuleEnabled(uint32_t module, bool enabled);
    void PostMessage(uint32_t module, uint32_t msg_id, uint32_t width,
                     const std::vector<uint32_t>& payload);

private:
    static constexpr size_t kModules = kMsm8255AdspModules.size();
    static constexpr size_t kQueues  = kMsm8255AdspQueues.size();
    static constexpr size_t kWords   = kMsm8255AdspMaxCommandWords;

    struct Message {
        uint32_t              task   = 0;
        uint32_t              msg_id = 0;
        uint32_t              width  = 0;
        std::vector<uint32_t> payload;
    };

    void SendIrq();
    void AcceptWriteRequest();
    void AcceptWriteDone();
    void DeliverNext();
    void Reset();

    uint32_t TaskIndex(uint32_t module) const;
    uint32_t QueueSlot(uint32_t queue) const;

    std::array<Msm8255AdspTask*, kModules> tasks_{};
    std::array<bool, kModules>             enabled_{};

    std::vector<uint16_t> cmd16_ = std::vector<uint16_t>(kQueues * kWords);
    std::vector<uint32_t> cmd32_ = std::vector<uint32_t>(kQueues * kWords);
    std::vector<uint16_t> msg16_ = std::vector<uint16_t>(2u + kWords);
    std::vector<uint32_t> msg32_ = std::vector<uint32_t>(1u + kWords);

    std::deque<Message> outbox_;

    uint32_t write_ctrl_    = 0;
    uint32_t read_ctrl_     = 0;
    uint32_t pending_queue_ = 0;
    bool     in_flight_     = false;
};

}
