#pragma once

#include <cerfcore/core/service.h>

#include "../../storage/disk_image.h"
#include "siemens_simatic_mobile_panels_gen2_emmc_store.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class SiemensSimaticMobilePanelsGen2CardStore : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    DiskImage& Card() { return card_; }
    void ReadSector(uint32_t sector, uint8_t* out);
    void WriteSector(uint32_t sector, const uint8_t* data);

private:
    void RequireSector(uint32_t sector, const char* what) const;
    void SeedCardFile(const siemens_simatic_mobile_panels_gen2_emmc::CardInputs& inputs);

    DiskImage   card_;
    std::string path_;
};

}
