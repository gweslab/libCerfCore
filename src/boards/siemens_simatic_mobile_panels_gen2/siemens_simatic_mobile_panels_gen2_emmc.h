#pragma once

#include "../../peripherals/mmc/emmc_card_base.h"
#include "siemens_simatic_mobile_panels_gen2_card_store.h"

#include <span>

namespace cerfcore {

class SiemensSimaticMobilePanelsGen2Emmc final : public EmmcCardBase {
public:
    using EmmcCardBase::EmmcCardBase;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t SlotIndex() const override;

protected:
    SdCardCid                       Cid() const override;
    EmmcCsdFields                   Csd() const override;
    std::span<const EmmcExtCsdByte> ExtCsdProperties() const override;
    uint32_t                        SectorCount() const override;
    uint8_t                         ErasedMemCont() const override;

    void Reset() override;
    void ReadBlock(uint32_t sector, uint8_t* out) override;
    void WriteBlock(uint32_t sector, const uint8_t* data) override;

private:
    SiemensSimaticMobilePanelsGen2CardStore* card_ = nullptr;
};

}
