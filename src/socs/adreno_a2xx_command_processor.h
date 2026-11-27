#pragma once

#include "adreno_a2xx_packet.h"
#include "adreno_a2xx_regs.h"
#include "adreno_a2xx_span.h"

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class AdrenoA2xxGpu;
class AdrenoA2xxMemory;
class StateReader;
class StateWriter;

class AdrenoA2xxCommandProcessor : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool     Owns(uint32_t idx) const;
    uint32_t Read(uint32_t idx) const;
    void     Write(uint32_t idx, uint32_t value);

    void PowerOnReset();
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    [[noreturn]] void Reject(const char* what, uint32_t address, uint64_t value) const;

    AdrenoA2xxGpu& Gpu();
    AdrenoA2xxSpan ReadSpan(uint64_t address, uint64_t size);
    AdrenoA2xxSpan WriteSpan(uint64_t address, uint64_t size);
    uint32_t       ReadPa32(uint64_t address);
    void           WritePa32(uint64_t address, uint32_t value);

    AdrenoA2xxPacket DecodePacket(uint64_t address, uint32_t available,
                                  AdrenoA2xxPacketSource source);
    uint32_t ReadOperand(const AdrenoA2xxPacket& packet, uint32_t index);

    void     StoreType0(const AdrenoA2xxPacket& packet);
    void     HandleMeInit(const AdrenoA2xxPacket& packet);
    void     HandleSetProtectedMode(const AdrenoA2xxPacket& packet);
    uint64_t ReadBinWord(const AdrenoA2xxPacket& packet);
    bool     PredicateFails(const AdrenoA2xxPacket& packet) const;
    void     RequireRegisterCondition(const AdrenoA2xxPacket& packet, uint32_t function, uint32_t reg,
                                      uint32_t ref, uint32_t mask);
    void     HandleWaitRegMem(const AdrenoA2xxPacket& packet);
    void     StoreSetConstant(const AdrenoA2xxPacket& packet);
    void     ScanIb(uint32_t ibaddr, uint32_t sizedwords, uint32_t depth);
    uint32_t ExecuteType3(const AdrenoA2xxPacket& packet, uint32_t ib_depth, uint32_t following);
    void     HandleRegRmw(const AdrenoA2xxPacket& packet);
    void     HandleMemWrite(const AdrenoA2xxPacket& packet);
    void     HandleEventWrite(const AdrenoA2xxPacket& packet);
    void     HandleRegToMem(const AdrenoA2xxPacket& packet);
    void     HandleDrawIndx(const AdrenoA2xxPacket& packet);
    void     HandleInterrupt(const AdrenoA2xxPacket& packet);
    uint32_t CondExecSkip(const AdrenoA2xxPacket& packet, uint32_t following);
    void     HandleRbWptr(uint32_t wptr);
    void     ScanRing(uint32_t size);

    AdrenoA2xxGpu*    gpu_    = nullptr;
    AdrenoA2xxMemory* memory_ = nullptr;

    uint32_t rb_cntl_      = 0;
    uint32_t rb_base_      = 0;
    uint32_t rb_rptr_addr_ = 0;
    uint32_t rptr_         = 0;
    uint32_t wptr_         = 0;
    uint32_t protect_control_ = 0;
    uint32_t protected_       = adreno_a2xx_regs::kProtectedModeUnknown;
    uint64_t bin_mask_        = adreno_a2xx_regs::kBinWordReset;
    uint64_t bin_select_      = adreno_a2xx_regs::kBinWordReset;
};

}
