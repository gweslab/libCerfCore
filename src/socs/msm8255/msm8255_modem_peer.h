#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class IrqController;

class Msm8255ModemPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RingDoorbell(uint32_t mask);
    void RaiseM2a6();
    void PushRouterMessage(
        uint32_t prog, uint32_t proc,
        const std::function<uint32_t(uint32_t out_pa, uint32_t out_cap)>& produce);

private:
    void SeedProcCommReady();
    void PublishModemState();
    void NotifySmd();
    uint32_t ChannelRecordPa(uint32_t cid);
    void ServiceSmdChannel(uint32_t cid, uint32_t rec, uint32_t item);
    void ConsumeAppsSmdFlags(uint32_t cid, uint32_t rec, uint32_t apps_half_pa);
    bool ChannelNameIs(uint32_t rec, const char* name, uint32_t bytes);
    uint32_t QmuxControlPort(uint32_t rec);
    [[noreturn]] void HaltUnroutedSmdChannel(uint32_t cid, uint32_t rec);
    void ServiceSmdData(uint32_t cid, uint32_t rec, uint32_t apps_half_pa);
    void ResolveFifo(uint32_t cid, uint32_t& fifo_pa, uint32_t& half);
    void ReadHalfIndices(uint32_t half_pa, uint32_t half, uint32_t& head,
                         uint32_t& tail);
    void PublishModemHead(uint32_t modem_half_pa, uint32_t head);
    void OpenModemSmdHalf(uint32_t modem_half_pa);
    void RunProcComm();

    IrqController* irq_ = nullptr;
};

}
