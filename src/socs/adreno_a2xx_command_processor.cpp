#include "adreno_a2xx_command_processor.h"

#include "adreno_a2xx_blit.h"
#include "adreno_a2xx_context.h"
#include "adreno_a2xx_draw.h"
#include "adreno_a2xx_gpu.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_regs.h"
#include "adreno_a2xx_socs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

using namespace adreno_a2xx_regs;

namespace {

constexpr uint32_t kTimestampHalfRange = 0x80000000u;

}

REGISTER_SERVICE(AdrenoA2xxCommandProcessor);

bool AdrenoA2xxCommandProcessor::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxCommandProcessor::Reject(const char* what, uint32_t address, uint64_t value) const {
    emu_.Get<Fatal>().Die("GPU command processor rejected %s at 0x%08X (value 0x%016llX)", what,
                          address, static_cast<unsigned long long>(value));
}

void AdrenoA2xxCommandProcessor::OnReady() {
    gpu_    = &emu_.Get<AdrenoA2xxGpu>();
    memory_ = &emu_.Get<AdrenoA2xxMemory>();
}

AdrenoA2xxGpu& AdrenoA2xxCommandProcessor::Gpu() { return *gpu_; }

AdrenoA2xxSpan AdrenoA2xxCommandProcessor::ReadSpan(uint64_t address, uint64_t size) {
    return memory_->ReadSpan(address, size, gpu_->MmuConfig());
}
AdrenoA2xxSpan AdrenoA2xxCommandProcessor::WriteSpan(uint64_t address, uint64_t size) {
    return memory_->WriteSpan(address, size, gpu_->MmuConfig());
}
uint32_t AdrenoA2xxCommandProcessor::ReadPa32(uint64_t address) {
    return memory_->ReadPa32(address, gpu_->MmuConfig());
}
void AdrenoA2xxCommandProcessor::WritePa32(uint64_t address, uint32_t value) {
    memory_->WritePa32(address, value, gpu_->MmuConfig());
}

bool AdrenoA2xxCommandProcessor::Owns(uint32_t idx) const {
    return idx == kIdxRbCntl || idx == kIdxRbBase || idx == kIdxRbRptrAddr || idx == kIdxRbWptr;
}

uint32_t AdrenoA2xxCommandProcessor::Read(uint32_t idx) const {
    if (idx == kIdxRbCntl) return rb_cntl_;
    if (idx == kIdxRbWptr) return wptr_;
    Reject("ring register read", idx, 0u);
}

void AdrenoA2xxCommandProcessor::Write(uint32_t idx, uint32_t value) {
    switch (idx) {
        case kIdxRbCntl:     rb_cntl_ = value; return;
        case kIdxRbBase:     rb_base_ = value; rptr_ = 0; wptr_ = 0; return;
        case kIdxRbRptrAddr: rb_rptr_addr_ = value; return;
        case kIdxRbWptr:     HandleRbWptr(value); return;
    }
    Reject("ring register write", idx, value);
}

void AdrenoA2xxCommandProcessor::PowerOnReset() {
    rb_cntl_ = rb_base_ = rb_rptr_addr_ = rptr_ = wptr_ = 0;
    protect_control_ = 0;
    protected_       = kProtectedModeUnknown;
    bin_mask_        = kBinWordReset;
    bin_select_      = kBinWordReset;
}

void AdrenoA2xxCommandProcessor::SaveState(StateWriter& w) const {
    w.Write("rb_cntl", rb_cntl_);
    w.Write("rb_base", rb_base_);
    w.Write("rb_rptr_addr", rb_rptr_addr_);
    w.Write("rptr", rptr_);
    w.Write("wptr", wptr_);
    w.Write("cp_protect_control", protect_control_);
    w.Write("cp_protected_mode", protected_);
    w.Write("cp_bin_mask", bin_mask_);
    w.Write("cp_bin_select", bin_select_);
}

void AdrenoA2xxCommandProcessor::RestoreState(StateReader& r) {
    r.Read("rb_cntl", rb_cntl_);
    r.Read("rb_base", rb_base_);
    r.Read("rb_rptr_addr", rb_rptr_addr_);
    r.Read("rptr", rptr_);
    r.Read("wptr", wptr_);
    r.Read("cp_protect_control", protect_control_);
    r.Read("cp_protected_mode", protected_);
    r.Read("cp_bin_mask", bin_mask_);
    r.Read("cp_bin_select", bin_select_);
}

AdrenoA2xxPacket AdrenoA2xxCommandProcessor::DecodePacket(uint64_t address, uint32_t available,
                                                          AdrenoA2xxPacketSource source) {
    if (address > UINT32_MAX || (address & 3u) != 0 || available == 0)
        Reject("PM4 malformed header address/extent", static_cast<uint32_t>(address), available);
    const uint32_t header = ReadPa32(address);
    AdrenoA2xxPacket packet;
    if (const char* error = AdrenoA2xxPacket::Decode(header, address, available, source, packet))
        Reject(error, static_cast<uint32_t>(address), header);
    return packet;
}

uint32_t AdrenoA2xxCommandProcessor::ReadOperand(const AdrenoA2xxPacket& packet, uint32_t index) {
    uint64_t address = 0;
    if (!packet.OperandAddress(index, address))
        Reject("PM4 malformed operand index", static_cast<uint32_t>(packet.address), index);
    return ReadPa32(address);
}

void AdrenoA2xxCommandProcessor::StoreType0(const AdrenoA2xxPacket& packet) {
    const uint32_t regindx = packet.header & 0x7FFFu;
    for (uint32_t k = 0; k < packet.payload_count; ++k) {
        const uint32_t idx = packet.same_register ? regindx : regindx + k;
        if ((protect_control_ & kProtectCheckEnable) != 0u && idx < (protect_control_ & kProtectLimitMask)) {
            if (protected_ == kProtectedModeOn)
                Reject("CP protected-mode register write", static_cast<uint32_t>(packet.address), idx);
            if (protected_ == kProtectedModeUnknown)
                Reject("CP register write before SET_PROTECTED_MODE", static_cast<uint32_t>(packet.address), idx);
        }
        Gpu().WriteRegister(idx, ReadOperand(packet, k));
    }
}

void AdrenoA2xxCommandProcessor::HandleMeInit(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count == 0u) Reject("ME_INIT payload length", pa, 0u);
    const uint32_t fields = ReadOperand(packet, 0u);
    if ((fields & ~kMeInitFieldMask) != 0u) Reject("ME_INIT field mask", pa, fields);
    uint32_t operand = 1u;
    for (uint32_t field = 0; field < kMeInitFieldCount; ++field) {
        if ((fields & (1u << field)) == 0u) continue;
        const uint32_t dwords = field == kMeInitFieldRegisterOffsets ? kMeInitRegisterOffsetDwords : 1u;
        if (operand + dwords > packet.payload_count) Reject("ME_INIT payload length", pa, packet.payload_count);
        const uint32_t value = ReadOperand(packet, operand);
        switch (field) {
            case kMeInitFieldRealTimeStream:
            case kMeInitFieldImplicitSync:
            case kMeInitFieldWriteConfirm:
                break;
            case kMeInitFieldRegisterOffsets:
                for (uint32_t k = 0; k < kMeInitRegisterOffsetDwords; ++k) {
                    const uint32_t block_offset = ReadOperand(packet, operand + k);
                    if (block_offset != k * kMeInitRegisterOffsetStride)
                        Reject("ME_INIT register offset", pa, block_offset);
                }
                break;
            case kMeInitFieldShaderBases:
                emu_.Get<AdrenoA2xxDraw>().SetShaderBases(value, Gpu().RegisterFile());
                break;
            case kMeInitFieldMaxContexts:
                if (value != 1u) Reject("ME_INIT maximum contexts", pa, value);
                break;
            case kMeInitFieldMemorySwap:
            case kMeInitFieldHeaderDump:
            case kMeInitFieldHeaderDumpSize:
                if (value != 0u) Reject("ME_INIT swap / header dump field", pa, value);
                break;
            case kMeInitFieldProtectedMode:
                if ((value & ~(kProtectCheckEnable | kProtectLimitMask)) != 0u)
                    Reject("ME_INIT protected-mode word", pa, value);
                protect_control_ = value;
                break;
        }
        operand += dwords;
    }
    if (operand != packet.payload_count) Reject("ME_INIT payload length", pa, packet.payload_count);
}

uint64_t AdrenoA2xxCommandProcessor::ReadBinWord(const AdrenoA2xxPacket& packet) {
    if (packet.payload_count != 2u) {
        Reject("SET_BIN_MASK / SET_BIN_SELECT payload length", static_cast<uint32_t>(packet.address),
               packet.payload_count);
    }
    return uint64_t(ReadOperand(packet, 0u)) | (uint64_t(ReadOperand(packet, 1u)) << 32);
}

bool AdrenoA2xxCommandProcessor::PredicateFails(const AdrenoA2xxPacket& packet) const {
    return packet.predicate && (bin_mask_ & bin_select_) == 0u;
}

void AdrenoA2xxCommandProcessor::RequireRegisterCondition(const AdrenoA2xxPacket& packet, uint32_t function,
                                                          uint32_t reg, uint32_t ref, uint32_t mask) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    const uint32_t value = Gpu().ReadRegister(reg) & mask;
    bool met = false;
    switch (function) {
        case kCpCondAlways:       met = true;         break;
        case kCpCondLess:         met = value < ref;  break;
        case kCpCondLessEqual:    met = value <= ref; break;
        case kCpCondEqual:        met = value == ref; break;
        case kCpCondNotEqual:     met = value != ref; break;
        case kCpCondGreaterEqual: met = value >= ref; break;
        case kCpCondGreater:      met = value > ref;  break;
        default: Reject("CP wait compare function", pa, function);
    }
    if (!met) Reject("CP wait condition unmet", pa, reg);
}

void AdrenoA2xxCommandProcessor::HandleWaitRegMem(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count != kWaitRegMemPayloadDwords) Reject("WAIT_REG_MEM payload length", pa, packet.payload_count);
    const uint32_t control = ReadOperand(packet, 0u);
    if ((control & ~kCpCondFunctionMask) != 0u) Reject("WAIT_REG_MEM poll/compare flags", pa, control);
    RequireRegisterCondition(packet, control & kCpCondFunctionMask, ReadOperand(packet, 1u),
                             ReadOperand(packet, 2u), ReadOperand(packet, 3u));
}

void AdrenoA2xxCommandProcessor::HandleSetProtectedMode(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count != 1u) Reject("SET_PROTECTED_MODE payload length", pa, packet.payload_count);
    const uint32_t mode = ReadOperand(packet, 0u);
    if (mode > 1u) Reject("SET_PROTECTED_MODE mode", pa, mode);
    protected_ = mode != 0u ? kProtectedModeOn : kProtectedModeOff;
}

void AdrenoA2xxCommandProcessor::StoreSetConstant(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address), cnt = packet.payload_count;
    const uint32_t tgt    = ReadOperand(packet, 0u);
    const uint32_t offset = tgt & 0xFFFFu;
    uint32_t base = 0u;
    switch ((tgt >> 16) & 0x7u) {
        case 0u: base = kScBaseAlu;   break;
        case 1u: base = kScBaseFetch; break;
        case 2u: base = kScBaseBool;  break;
        case 3u: base = kScBaseLoop;  break;
        case 4u: base = kScBaseReg;   break;
        default: Reject("SET_CONSTANT type", pa, tgt);
    }
    for (uint32_t j = 0; j + 1u < cnt; ++j)
        Gpu().WriteRegister(base + offset + j, ReadOperand(packet, j + 1u));
}

void AdrenoA2xxCommandProcessor::ScanIb(uint32_t ibaddr, uint32_t sizedwords, uint32_t depth) {
    if ((ibaddr & 3u) != 0 || sizedwords == 0 || sizedwords > 0xFFFFFu)
        Reject("CP IB address/size", ibaddr, sizedwords);
    memory_->RequireReadable(ibaddr, uint64_t(sizedwords) * 4u, gpu_->MmuConfig());
    for (uint32_t i = 0; i < sizedwords; ) {
        const auto packet = DecodePacket(uint64_t(ibaddr) + uint64_t(i) * 4u,
                                         sizedwords - i, AdrenoA2xxPacketSource::IndirectBuffer);
        const uint32_t cnt = packet.payload_count;
        if (packet.type == kPm4Type0) { StoreType0(packet); i += 1u + cnt; continue; }
        if (packet.type == kPm4Type2) { i += 1u; continue; }
        if (PredicateFails(packet)) { i += 1u + cnt; continue; }
        i += 1u + cnt + ExecuteType3(packet, depth + 1u, sizedwords - i - 1u - cnt);
    }
}

uint32_t AdrenoA2xxCommandProcessor::ExecuteType3(const AdrenoA2xxPacket& packet, uint32_t ib_depth,
                                                  uint32_t following) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    auto& gpu = Gpu();
    switch (packet.opcode) {
        case kPm4OpSetBinMask: bin_mask_ = ReadBinWord(packet); break;
        case kPm4OpSetBinSelect: bin_select_ = ReadBinWord(packet); break;
        case kPm4OpMeInit: HandleMeInit(packet); break;
        case kPm4OpSetProtectedMode: HandleSetProtectedMode(packet); break;
        case kPm4OpIndirectBuffer:
        case kPm4OpIndirectBufferPfd:
            if (ib_depth > 2u) Reject("CP unsupported IB nesting depth", pa, ib_depth);
            if (packet.payload_count != 2u) Reject("CP IB packet length", pa, packet.payload_count);
            ScanIb(ReadOperand(packet, 0u), ReadOperand(packet, 1u), ib_depth);
            break;
        case kPm4OpNop:
        case kPm4OpWaitForIdle:
        case kPm4OpInvalidateState: break;
        case kPm4OpLoadConstantContext:
            emu_.Get<AdrenoA2xxContext>().Load(packet, gpu.RegisterFile(), gpu.MmuConfig());
            break;
        case kPm4OpImStore: case kPm4OpImLoad: case kPm4OpImLoadImmediate:
        case kPm4OpSetShaderBases: case 0x4Bu: case 0x34u:
            emu_.Get<AdrenoA2xxDraw>().Packet(packet, gpu.RegisterFile(), gpu.MmuConfig());
            break;
        case kPm4OpRegRmw: HandleRegRmw(packet); break;
        case kPm4OpWaitRegEq:
            RequireRegisterCondition(packet, kCpCondEqual, ReadOperand(packet, 0u), ReadOperand(packet, 1u),
                                     ReadOperand(packet, 2u));
            break;
        case kPm4OpWaitRegMem: HandleWaitRegMem(packet); break;
        case kPm4OpSetConstant: StoreSetConstant(packet); break;
        case kPm4OpMemWrite: HandleMemWrite(packet); break;
        case kPm4OpRegToMem: HandleRegToMem(packet); break;
        case kPm4OpEventWrite: HandleEventWrite(packet); break;
        case kPm4OpInterrupt: HandleInterrupt(packet); break;
        case kPm4OpDrawIndx: HandleDrawIndx(packet); break;
        case kPm4OpCondExec: return CondExecSkip(packet, following);
        default: Reject("CP type-3 opcode", pa, packet.header);
    }
    return 0u;
}

void AdrenoA2xxCommandProcessor::HandleRegRmw(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count != 3u) Reject("REG_RMW payload length", pa, packet.payload_count);
    const uint32_t target = ReadOperand(packet, 0u);
    if (target != kIdxScratchReg2) Reject("REG_RMW unsupported target/flags", pa, target);
    const uint32_t and_mask = ReadOperand(packet, 1u), or_mask = ReadOperand(packet, 2u);
    Gpu().WriteRegister(target, (Gpu().ReadRegister(target) & and_mask) | or_mask);
}

void AdrenoA2xxCommandProcessor::HandleMemWrite(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count < 2u) Reject("PM4 malformed MEM_WRITE payload", pa, packet.header);
    const uint32_t destination = ReadOperand(packet, 0u);
    if ((destination & 3u) != 0) Reject("MEM_WRITE unsupported address low bits", pa, destination);
    const uint32_t packet_bytes = (packet.payload_count + 1u) * 4u;
    const uint32_t data_bytes = (packet.payload_count - 1u) * 4u;
    const AdrenoA2xxSpan source_span = ReadSpan(packet.address, packet_bytes);
    AdrenoA2xxSpan target_span = WriteSpan(destination, data_bytes);
    if (source_span.Overlaps(target_span)) Reject("MEM_WRITE unsupported packet overlap", pa, destination);
    std::memcpy(target_span.data(), source_span.data() + 8u, data_bytes);
}

void AdrenoA2xxCommandProcessor::HandleEventWrite(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    const uint32_t event = ReadOperand(packet, 0u);
    if (event == kEventCacheFlush) return;
    if (event != kEventCacheFlushTs) Reject("CP EVENT_WRITE event", pa, event);
    WritePa32(ReadOperand(packet, 1u), ReadOperand(packet, 2u));
}

void AdrenoA2xxCommandProcessor::HandleRegToMem(const AdrenoA2xxPacket& packet) {
    const uint32_t src = ReadOperand(packet, 0u) & ~kRegToMemShadowFlag;
    const uint32_t dst = ReadOperand(packet, 1u);
    WritePa32(dst, Gpu().ReadRegister(src));
}

void AdrenoA2xxCommandProcessor::HandleDrawIndx(const AdrenoA2xxPacket& packet) {
    auto& gpu = Gpu();
    const uint32_t control = ReadOperand(packet, 1u);
    if (control == 0x00040086u)
        emu_.Get<AdrenoA2xxBlit>().Draw(control, packet.address, gpu.RegisterFile(), gpu.MmuConfig());
    else emu_.Get<AdrenoA2xxDraw>().Packet(packet, gpu.RegisterFile(), gpu.MmuConfig());
}

void AdrenoA2xxCommandProcessor::HandleInterrupt(const AdrenoA2xxPacket& packet) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count != 1u) Reject("CP interrupt packet length", pa, packet.payload_count);
    Gpu().RaiseCpInterrupt(ReadOperand(packet, 0u), pa);
}

uint32_t AdrenoA2xxCommandProcessor::CondExecSkip(const AdrenoA2xxPacket& packet, uint32_t following) {
    const uint32_t pa = static_cast<uint32_t>(packet.address);
    if (packet.payload_count != 4u) Reject("COND_EXEC payload length", pa, packet.payload_count);
    const uint32_t dwords = ReadOperand(packet, 3u);
    if (dwords > following) Reject("COND_EXEC skips past its command buffer", pa, dwords);
    const bool     enabled   = ReadPa32(uint64_t(ReadOperand(packet, 0u)) << 2) != 0u;
    const uint32_t reference = ReadPa32(uint64_t(ReadOperand(packet, 1u)) << 2);
    const bool     ahead     = ReadOperand(packet, 2u) - reference < kTimestampHalfRange;
    return enabled && ahead ? 0u : dwords;
}

void AdrenoA2xxCommandProcessor::HandleRbWptr(uint32_t wptr) {
    Gpu().RequireCoreRunning("runs its command ring");
    const uint32_t shift = rb_cntl_ & 0x3Fu;
    if (shift >= 20u || (rb_base_ & 31u) != 0 || (rb_cntl_ & 0x30000u) != 0)
        Reject("CP ring geometry/swap", rb_base_, rb_cntl_);
    const uint32_t size = 2u << shift;
    if (wptr >= size || rptr_ >= size) Reject("CP ring cursor", rb_base_, wptr);
    memory_->RequireReadable(rb_base_, uint64_t(size) * 4u, gpu_->MmuConfig());
    if ((rb_cntl_ & 0x08000000u) == 0 && (rb_rptr_addr_ & 3u) != 0)
        Reject("CP RPTR unsupported swap", rb_rptr_addr_, rb_cntl_);
    wptr_ = wptr;
    ScanRing(size);
}

void AdrenoA2xxCommandProcessor::ScanRing(uint32_t size) {
    while (rptr_ != wptr_) {
        const uint32_t available = (wptr_ + size - rptr_) % size;
        const uint32_t tail = size - rptr_;
        const auto packet = DecodePacket(uint64_t(rb_base_) + uint64_t(rptr_) * 4u,
                                         available < tail ? available : tail,
                                         AdrenoA2xxPacketSource::Ring);
        const uint32_t pa = static_cast<uint32_t>(packet.address), hdr = packet.header;
        const uint32_t count = 1u + packet.payload_count;
        if (count > tail) Reject("CP ring packet crosses tail", pa, hdr);
        if (count > available) {
            if (packet.type == kPm4Type3 && packet.opcode == kPm4OpNop && count == tail) return;
            Reject("CP ring truncated packet", pa, hdr);
        }
        uint32_t skip = 0u;
        if (packet.type == kPm4Type0) StoreType0(packet);
        if (packet.type == kPm4Type3 && !PredicateFails(packet)) skip = ExecuteType3(packet, 1u, available - count);
        rptr_ = (rptr_ + count + skip) % size;
        if ((rb_cntl_ & 0x08000000u) == 0) WritePa32(rb_rptr_addr_, rptr_);
    }
}

}
