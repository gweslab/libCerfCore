#pragma once
#include "adreno_a2xx_mmu.h"
#include <cerfcore/core/service.h>
#include "adreno_a2xx_packet.h"
#include <array>
#include <unordered_map>

namespace cerfcore {

class StateWriter;
class StateReader;

class AdrenoA2xxContext : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void Load(const AdrenoA2xxPacket& packet, std::unordered_map<uint32_t, uint32_t>& registers, const AdrenoA2xxMmu& mmu_config);
    void ShadowWrite(uint32_t index, uint32_t value, const AdrenoA2xxMmu& mmu_config);
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);
    void PowerOnReset();
private:
    struct Bank {
        uint32_t address = 0;
        bool     enabled = false;
        uint8_t  pad[3]  = {};

        template <typename F>
        static constexpr void Visit(Bank& b, F& field) {
            field("address", b.address);
            field("enabled", b.enabled);
            field.Skip(b.pad);
        }
    };
    std::array<Bank, 3> banks_{};
};

}
