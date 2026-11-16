#include "stm32f2_dma.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kLisr = 0x00u;
constexpr uint32_t kHisr = 0x04u;
constexpr uint32_t kLifcr = 0x08u;
constexpr uint32_t kHifcr = 0x0Cu;
constexpr uint32_t kStreamBase = 0x10u;
constexpr uint32_t kStreamStride = 0x18u;

constexpr uint32_t kEn = 1u << 0;
constexpr uint32_t kInterruptEnables = 0x1Eu;
constexpr uint32_t kPfctrl = 1u << 5;
constexpr uint32_t kCirc = 1u << 8;
constexpr uint32_t kPinc = 1u << 9;
constexpr uint32_t kMinc = 1u << 10;
constexpr uint32_t kDbm = 1u << 18;
constexpr uint32_t kPriority = 3u << 16;
constexpr uint32_t kBursts = 0x01E00000u;
constexpr uint32_t kPsize = 3u << 11;
constexpr uint32_t kMsize = 3u << 13;
constexpr uint32_t kPincos = 1u << 15;
constexpr uint32_t kProtectedWhileEnabled = 0x0FEFFEE0u;

constexpr uint32_t kFlagShift[4] = {0u, 6u, 16u, 22u};
constexpr uint32_t kHtif = 4u;
constexpr uint32_t kTcif = 5u;

constexpr uint32_t kDmdis = 1u << 2;

}

uint32_t Dma::Read(uint32_t offset, uint32_t size) {
    if (size != 4u) mcu_.Fault("STM32F2 DMA: sub-word access", base_ + offset);
    if (offset == kLisr) return lisr_;
    if (offset == kHisr) return hisr_;
    if (offset == kLifcr || offset == kHifcr) return 0u;
    if (offset >= kStreamBase && offset < kStreamBase + kStreamStride * kStreams) {
        const Stream& s = streams_[(offset - kStreamBase) / kStreamStride];
        switch ((offset - kStreamBase) % kStreamStride) {
        case 0x00u: return s.cr;
        case 0x04u: return s.ndtr;
        case 0x08u: return s.par;
        case 0x0Cu: return s.m0ar;
        case 0x10u: return s.m1ar;
        case 0x14u: return s.fcr;
        default: break;
        }
    }
    mcu_.Fault("STM32F2 DMA: read of an unmodelled register", base_ + offset);
    CERF_UNREACHABLE();
}

void Dma::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size != 4u) mcu_.Fault("STM32F2 DMA: sub-word access", base_ + offset);
    if (offset == kLifcr) {
        lisr_ &= ~value;
        return;
    }
    if (offset == kHifcr) {
        hisr_ &= ~value;
        return;
    }
    if (offset < kStreamBase || offset >= kStreamBase + kStreamStride * kStreams)
        mcu_.Fault("STM32F2 DMA: write of an unmodelled register", base_ + offset);
    const uint32_t index = (offset - kStreamBase) / kStreamStride;
    Stream& s = streams_[index];
    const uint32_t reg = (offset - kStreamBase) % kStreamStride;
    if (reg == 0x00u) {
        if ((value & ~0x0FEFFFFFu) != 0u) mcu_.Fault("STM32F2 DMA: reserved SxCR bits are set", value);
        const bool was_enabled = (s.cr & kEn) != 0u;
        const bool starting = !was_enabled && (value & kEn) != 0u;
        const uint32_t writable = 0x0FEFFFFFu & (was_enabled ? ~kProtectedWhileEnabled : ~0u);
        s.cr = (s.cr & ~writable) | (value & writable);
        if (was_enabled && (value & kEn) == 0u) SetFlag(index, kTcif);
        if ((s.cr & kEn) != 0u && (s.fcr & kDmdis) == 0u)
            s.cr = (s.cr & ~(kMsize | kBursts | kPincos)) | ((s.cr & kPsize) << 2);
        if ((s.cr & kEn) != 0u && (s.cr & (kInterruptEnables | kPfctrl | kCirc | kPinc | kDbm)) != 0u)
            mcu_.Fault("STM32F2 DMA: interrupts, flow control, circular, peripheral increment or double buffer", s.cr);
        if (starting) {
            if ((s.fcr & kDmdis) != 0u) mcu_.Fault("STM32F2 DMA: FIFO mode is not modelled", s.fcr);
            s.initial_ndtr = s.ndtr;
            s.item = 0u;
            if (!servicing_) ServiceAll();
        }
        return;
    }
    if (reg == 0x14u && (value & 0xFFFFFF40u) != 0u)
        mcu_.Fault("STM32F2 DMA: reserved SxFCR bits are set", value);
    if (reg == 0x14u && (value & 0x80u) != 0u) mcu_.Fault("STM32F2 DMA: FIFO error interrupt is not modelled", value);
    if ((s.cr & kEn) != 0u) return;
    switch (reg) {
    case 0x04u:
        if ((value & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 DMA: reserved SxNDTR bits are set", value);
        s.ndtr = value & 0xFFFFu;
        return;
    case 0x08u: s.par = value; return;
    case 0x0Cu: s.m0ar = value; return;
    case 0x10u: s.m1ar = value; return;
    case 0x14u:
        s.fcr = (s.fcr & ~0x87u) | (value & 0x87u);
        return;
    default: mcu_.Fault("STM32F2 DMA: write of an unmodelled register", base_ + offset);
    }
}

void Dma::SetRequest(uint32_t stream, uint32_t channel, bool asserted) {
    const uint8_t bit = static_cast<uint8_t>(1u << channel);
    requests_[stream] = asserted ? (requests_[stream] | bit) : (requests_[stream] & ~bit);
    if (asserted && !servicing_) ServiceAll();
}

bool Dma::Requested(uint32_t index) const {
    const Stream& s = streams_[index];
    return (s.cr & kEn) != 0u && s.ndtr != 0u && (requests_[index] & (1u << ((s.cr >> 25) & 7u))) != 0u;
}

void Dma::ServiceAll() {
    servicing_ = true;
    for (;;) {
        uint32_t selected = kStreams;
        uint32_t priority = 0u;
        for (uint32_t i = 0u; i < kStreams; ++i) {
            if (!Requested(i)) continue;
            const uint32_t candidate_priority = (streams_[i].cr & kPriority) >> 16;
            if (selected == kStreams || candidate_priority > priority) {
                selected = i;
                priority = candidate_priority;
            }
        }
        if (selected == kStreams) break;
        Transfer(selected);
    }
    servicing_ = false;
}

void Dma::Transfer(uint32_t index) {
    Stream& s = streams_[index];
    const uint32_t dir = (s.cr >> 6) & 3u;
    const uint32_t psize = 1u << ((s.cr >> 11) & 3u);
    if (psize > 4u) mcu_.Fault("STM32F2 DMA: reserved PSIZE", s.cr);
    const uint32_t memory = s.m0ar + ((s.cr & kMinc) != 0u ? s.item * psize : 0u);
    if (dir == 0u) {
        mcu_.Write(memory, psize, mcu_.Read(s.par, psize));
    } else if (dir == 1u) {
        mcu_.Write(s.par, psize, mcu_.Read(memory, psize));
    } else {
        mcu_.Fault("STM32F2 DMA: memory-to-memory transfer is not modelled", s.cr);
    }
    ++s.item;
    --s.ndtr;
    if (s.ndtr == s.initial_ndtr / 2u) SetFlag(index, kHtif);
    if (s.ndtr == 0u) {
        SetFlag(index, kTcif);
        s.cr &= ~kEn;
    }
}

void Dma::SetFlag(uint32_t stream, uint32_t flag) {
    const uint32_t bit = 1u << (kFlagShift[stream & 3u] + flag);
    if (stream < 4u) {
        lisr_ |= bit;
    } else {
        hisr_ |= bit;
    }
}

void Dma::SaveState(StateWriter& writer) const {
    for (const Stream& s : streams_) {
        writer.Write("dma_cr", s.cr);
        writer.Write("dma_ndtr", s.ndtr);
        writer.Write("dma_par", s.par);
        writer.Write("dma_m0ar", s.m0ar);
        writer.Write("dma_m1ar", s.m1ar);
        writer.Write("dma_fcr", s.fcr);
        writer.Write("dma_initial_ndtr", s.initial_ndtr);
        writer.Write("dma_item", s.item);
    }
    writer.Write("dma_lisr", lisr_);
    writer.Write("dma_hisr", hisr_);
    writer.Write("dma_requests", requests_);
}

void Dma::RestoreState(StateReader& reader) {
    for (Stream& s : streams_) {
        reader.Read("dma_cr", s.cr);
        reader.Read("dma_ndtr", s.ndtr);
        reader.Read("dma_par", s.par);
        reader.Read("dma_m0ar", s.m0ar);
        reader.Read("dma_m1ar", s.m1ar);
        reader.Read("dma_fcr", s.fcr);
        reader.Read("dma_initial_ndtr", s.initial_ndtr);
        reader.Read("dma_item", s.item);
    }
    reader.Read("dma_lisr", lisr_);
    reader.Read("dma_hisr", hisr_);
    reader.Read("dma_requests", requests_);
}

}

}
