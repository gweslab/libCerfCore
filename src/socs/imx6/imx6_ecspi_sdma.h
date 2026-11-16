#pragma once

#include "../freescale_sdma_bd_ring.h"
#include "../freescale_sdma_bus.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class Imx6EcspiEndpoint;
class StateReader;
class StateWriter;

class Imx6EcspiSdma : public FreescaleSdmaChannelSink {
public:
    explicit Imx6EcspiSdma(CerfEmulator& emu) : emu_(emu), tx_(emu), rx_(emu) {}

    void Bind(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event,
              std::function<void()> on_claim);
    void Service(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words);
    void Exchange(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words, uint32_t words,
                  uint32_t conreg, uint32_t configreg);
    void FlushFifos();

    uint32_t TxLevel() const { return tx_level_; }
    uint32_t RxLevel() const { return rx_level_; }

    bool HandlesEvent(int event) const override;
    bool ClaimChannel(const FreescaleSdmaChannelStart& start) override;
    void ReleaseChannel(uint32_t channel) override;
    void OnSdmaClock(bool running) override;
    void SaveSinkState(StateWriter&) override {}
    void RestoreSinkState(StateReader&) override {}
    void PostRestoreSink() override {}

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

    static constexpr uint32_t kFifoWords = 64u;

private:
    struct Direction {
        explicit Direction(CerfEmulator& emu) : ring(emu) {}
        FreescaleSdmaBdRing ring;
        bool     active = false;
        bool     open   = false;
        uint32_t burst  = 1u;
        uint32_t buffer = 0u;
        uint32_t count  = 0u;
        uint32_t moved  = 0u;
        bool     cont   = false;
    };

    bool Open(Direction& d);
    void Retire(Direction& d);
    void ServiceTx(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words);
    void ServiceRx(Imx6EcspiEndpoint* endpoint, uint32_t dmareg);
    void SaveDirection(StateWriter& w, const Direction& d, const char* prefix) const;
    void RestoreDirection(StateReader& r, Direction& d, const char* prefix);

    static constexpr uint32_t kDmaTeden = 1u << 7;
    static constexpr uint32_t kDmaRxden = 1u << 23;

    CerfEmulator&             emu_;
    FreescaleSdmaChannelHost* host_     = nullptr;
    uint32_t                  tx_event_ = 0;
    uint32_t                  rx_event_ = 0;
    std::function<void()>     on_claim_;
    bool                      sdma_clocked_ = true;

    Direction tx_;
    Direction rx_;
    uint32_t  tx_level_      = 0u;
    uint32_t  rx_level_      = 0u;
    bool      rx_data_ready_ = false;
};

}
