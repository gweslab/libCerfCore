#pragma once

#include "siemens_simatic_mobile_panels_gen2_f_module_hardware.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/imx6/imx6_ecspi_endpoint.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class SiemensSimaticMobilePanelsGen2FModuleDevice final : public Imx6EcspiEndpoint, private siemens_simatic_mobile_panels_gen2::FModuleHardwareHost {
public:
    using Imx6EcspiEndpoint::Imx6EcspiEndpoint;
    ~SiemensSimaticMobilePanelsGen2FModuleDevice() override;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;
    uint32_t EcspiBase() const override { return 0x02010000u; }
    void StageDmaTransmit(uint32_t buffer_pa, uint32_t bytes) override;
    void StageDmaReceive(uint32_t buffer_pa, uint32_t bytes) override;
    bool HasStagedTransmit() const override { return dma_tx_ready_; }
    void Exchange(uint32_t conreg, uint32_t configreg) override;

    siemens_simatic_mobile_panels_gen2::FModuleLines Lines() const;
    void ObservePanelAcknowledge(bool driven, bool high);
    void ObserveModuleReset(bool high);

    using LinesChangedFn = void (*)(void*);
    void AddLinesListener(LinesChangedFn fn, void* context);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    static constexpr uint32_t kTransferBytes = 272u;

    [[noreturn]] void FModuleFault(uint32_t channel, const char* what, uint32_t detail, uint32_t pc) override;
    void LinesChanged(const siemens_simatic_mobile_panels_gen2::FModuleLines& lines, int64_t ns) override;
    void NotifyLines();

    void Settle();
    void Resume();
    void OnTick();
    void OnEdge();
    void DeliverDueEdges(int64_t now);
    void ArmAt(GuestCycleClock::Event* event, int64_t ns);
    void FlushDmaReceive();
    uint8_t UndrivenMisoByte();
    std::vector<std::vector<uint8_t>*> FlashRegions();
    void LoadFlashImage();
    void StoreFlashImage();
    void PersistFlash(bool now);

    static constexpr const char* kFlashImageName = "fmodule_flash.img";

    std::unique_ptr<siemens_simatic_mobile_panels_gen2::FModuleHardware> hardware_;
    GuestCycleClock* clock_ = nullptr;
    GuestCycleClock::Event* tick_ = nullptr;
    GuestCycleClock::Event* edge_ = nullptr;
    struct LinesEdge {
        int64_t ns;
        siemens_simatic_mobile_panels_gen2::FModuleLines lines;
    };
    std::deque<LinesEdge> edges_;
    siemens_simatic_mobile_panels_gen2::FModuleHardware::Snapshot snapshot_;
    int64_t window_end_ns_ = 0;
    siemens_simatic_mobile_panels_gen2::FModuleLines delivered_;
    bool speculating_ = false;
    bool replaying_ = false;
    bool faulted_ = false;
    int64_t base_ns_ = 0;
    std::string flash_path_;
    int64_t stored_ns_ = 0;
    bool flash_pending_ = false;

    std::array<uint8_t, kTransferBytes> dma_tx_{};
    std::array<uint8_t, kTransferBytes> dma_rx_{};
    uint32_t dma_rx_buffer_pa_ = 0;
    uint32_t dma_rx_bytes_ = 0;
    bool dma_tx_ready_ = false;
    bool dma_rx_pending_ = false;
    bool dma_response_ready_ = false;
    bool miso_keeper_known_ = false;
    bool panel_ack_known_ = false;
    bool panel_ack_high_ = false;
    bool panel_ack_last_driven_high_ = false;
    bool panel_ack_last_driven_known_ = false;
    bool reset_held_ = false;
    bool first_exchange_logged_ = false;
    uint32_t selected_container_crc32_ = 0;

    std::vector<std::pair<LinesChangedFn, void*>> listeners_;
};

}
