#pragma once

#include "../../core/service.h"
#include "../../state/state_stream.h"
#include "cerf_virt_addr_map.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class CerfVirtCustomizationsReset : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void OnCustomizationsApplied();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    struct RegistryCustomizations {
        uint32_t                                        scheme_count = 0;
        std::array<uint32_t, CerfVirt::kColorSchemeMax> scheme_entries{};
        uint32_t                                        font_set    = 0;
        int32_t                                         font_height = 0;

        bool operator==(const RegistryCustomizations&) const = default;
    };

    RegistryCustomizations Published();

    mutable std::mutex     mutex_;
    bool                   registry_known_ = false;
    RegistryCustomizations registry_;
};

}
