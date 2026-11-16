#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "imx6_vivante_blit.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace cerfcore {

namespace imx6_vivante {

class VivanteFe {
public:
    VivanteFe(VivanteState& s, VivanteMem& mem, VivanteBlit& blit) : s_(s), mem_(mem), blit_(blit) {}

    void AdvanceFrontendRing();
    bool AdvanceSemaphoreStall();
    bool Busy() const { return s_.fe_live_ && !s_.fe_idle_ring_; }
    uint64_t FrontendWaitCycles();
    bool PrepareFrontendFetch();
    bool ResumeParkedWord();
    void RunFrontend(uint32_t control);
    bool CacheParkedWord();
    bool ParkedWordUnchanged() const;
    void SetParked(bool parked) { parked_ = parked; }
    void ResetParkedWord() {
        parked_ = false;
        parked_word_host_ = parked_pte_host_ = nullptr;
    }

private:
    struct FeControlState {
        uint32_t pc = 0u;
        uint32_t window_words = 0u;
        uint32_t call_depth = 0u;
        FeCallFrame call_stack[kFeCallStackDepth]{};
    };

    class FeLoopDetector {
    public:
        enum class Result {
            Inserted,
            Repeated,
        };

        Result Insert(const FeControlState& state);
        void Reset() { entries_.clear(); }

    private:
        static bool Equal(const FeControlState& lhs, const FeControlState& rhs);

        std::vector<FeControlState> entries_;
    };

    void CommandFits(uint32_t command_words, uint32_t window_words, uint32_t pc);
    void FetchWords(uint32_t pc, uint32_t* words, uint32_t count);
    void ConsumeWords(uint32_t words, uint32_t& pc, uint32_t& window_words) const;
    bool TryParkIdleRing();
    void ParkIdleRing(const IdleRingInfo& idle);
    uint32_t ExecuteCommandStream(uint32_t address, FeStats& stats);
    void ApplyRunResult(const FeStats& stats, uint32_t final_pc, uint32_t fallback_pc);
    void ApplyLoadState(const uint32_t* values, uint32_t state_word, uint32_t count, bool fixp);

    VivanteState& s_;
    VivanteMem& mem_;
    VivanteBlit& blit_;
    std::array<uint32_t, kMaxFeCommandWords> packet_buffer_{};
    FeLoopDetector loop_detector_;
    bool parked_ = false;
    const uint8_t* parked_word_host_ = nullptr;
    uint32_t parked_word_ = 0u;
    const uint8_t* parked_pte_host_ = nullptr;
    uint32_t parked_pte_ = 0u;
};

}

}
