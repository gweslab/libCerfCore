#pragma once
#include "adreno_a2xx_mmu.h"
#include <cerfcore/core/service.h>
#include "adreno_a2xx_packet.h"
#include <array>
#include <span>
#include <unordered_map>

namespace cerfcore {

class StateWriter;
class StateReader;
struct AdrenoA2xxShaderState;

class AdrenoA2xxDraw : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void Packet(const AdrenoA2xxPacket& packet,
                std::unordered_map<uint32_t, uint32_t>& registers, const AdrenoA2xxMmu& mmu_config);
    void SetShaderBases(uint32_t value, std::unordered_map<uint32_t, uint32_t>& registers);
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);
    void PowerOnReset();

private:
    uint32_t Operand(const AdrenoA2xxPacket& packet, uint32_t index, const AdrenoA2xxMmu& mmu);
    uint32_t InstructionOffset(uint32_t stage, uint32_t start, uint32_t count);
    std::span<const uint32_t> Program(uint32_t stage);
    void Load(const AdrenoA2xxPacket& packet, const AdrenoA2xxMmu& mmu);
    void Store(const AdrenoA2xxPacket& packet, const AdrenoA2xxMmu& mmu);
    void Export(const AdrenoA2xxShaderState& state, const AdrenoA2xxMmu& mmu);
    void Draw(const AdrenoA2xxPacket& packet,
              const std::unordered_map<uint32_t, uint32_t>& registers, const AdrenoA2xxMmu& mmu);
    [[noreturn]] void Reject(const char* reason, uint64_t value);
    std::array<std::array<uint32_t, 1536>, 2> instructions_{};
    std::array<std::array<uint8_t, 1536>, 2> valid_{};
    std::array<uint32_t, 3> start_size_{};
    uint32_t bases_ = 0;
    uint32_t bin_base_ = 0;
};

}
