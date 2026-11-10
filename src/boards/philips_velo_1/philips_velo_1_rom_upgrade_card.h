#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cerfcore {

struct Velo1CardWindow {
    uint32_t                 pa = 0;
    std::span<const uint8_t> bytes;
};

class PhilipsVelo1RomUpgradeCard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool            Fitted() const { return !bytes_.empty(); }
    Velo1CardWindow KsegWindow() const;
    Velo1CardWindow KusegWindow() const;

private:
    std::vector<uint8_t> bytes_;
};

}
