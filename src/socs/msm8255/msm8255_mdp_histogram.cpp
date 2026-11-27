#include "msm8255_mdp_histogram.h"

#include "msm8255_id.h"
#include "msm8255_mdp_regs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

using namespace msm8255_mdp;

constexpr uint32_t kStart      = 0x00u;
constexpr uint32_t kFrameCount = 0x04u;
constexpr uint32_t kBitMask    = 0x08u;
constexpr uint32_t kReset      = 0x0Cu;
constexpr uint32_t kWord10     = 0x10u;
constexpr uint32_t kStatus     = 0x14u;
constexpr uint32_t kClear      = 0x18u;
constexpr uint32_t kEnable     = 0x1Cu;
constexpr uint32_t kBinsRed    = 0x100u;
constexpr uint32_t kBinStride  = 0x100u;

constexpr uint32_t kResetSeqDone = 1u << 0;
constexpr uint32_t kHistDone     = 1u << 1;
constexpr uint32_t kStatusBits   = kResetSeqDone | kHistDone;

constexpr uint32_t kBitMaskGuest = 0x80000000u;
constexpr uint32_t kWord10Bits   = 1u;
constexpr uint32_t kTrigger      = 1u;

constexpr uint32_t kPixelBits = 12u;
constexpr uint32_t kBinBits   = 5u;

}

bool Msm8255MdpHistogram::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255MdpHistogram::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
}

bool Msm8255MdpHistogram::Owns(uint32_t off) {
    return off >= kHistogram && off - kHistogram < kHistogramSize;
}

bool Msm8255MdpHistogram::BinSlot(uint32_t off, uint32_t& channel, uint32_t& bin) const {
    if (off < kBinsRed || (off & 3u) != 0u) return false;
    channel = (off - kBinsRed) / kBinStride;
    bin     = ((off - kBinsRed) % kBinStride) / 4u;
    return channel < kChannels && bin < kBins;
}

uint32_t Msm8255MdpHistogram::Read(uint32_t off) {
    const uint32_t reg = off - kHistogram;
    uint32_t channel = 0u, bin = 0u;
    if (BinSlot(reg, channel, bin)) return bins_[channel][bin];
    switch (reg) {
    case kWord10: return word10_;
    case kStatus: return status_;
    case kEnable: return enable_;
    default:      Halt("read", off, 0u);
    }
}

void Msm8255MdpHistogram::Write(uint32_t off, uint32_t value) {
    const uint32_t reg = off - kHistogram;
    uint32_t channel = 0u, bin = 0u;
    if (BinSlot(reg, channel, bin)) {
        bins_[channel][bin] = value;
        return;
    }
    switch (reg) {
    case kStart:
        if ((value & kTrigger) == 0u || frame_count_ == 0u) Halt("write", off, value);
        for (auto& channel_bins : bins_) channel_bins.fill(0u);
        frames_left_ = frame_count_;
        return;
    case kFrameCount:
        frame_count_ = value;
        return;
    case kBitMask:
        if (value != 0u && value != kBitMaskGuest) Halt("write", off, value);
        return;
    case kReset:
        if ((value & kTrigger) == 0u) Halt("write", off, value);
        for (auto& channel_bins : bins_) channel_bins.fill(0u);
        frames_left_ = 0u;
        Raise(kResetSeqDone);
        return;
    case kWord10:
        if ((value & ~kWord10Bits) != 0u) Halt("write", off, value);
        word10_ = value;
        return;
    case kClear:
        if ((value & ~kStatusBits) != 0u) Halt("write", off, value);
        status_ &= ~value;
        return;
    case kEnable:
        if ((value & ~kStatusBits) != 0u) Halt("write", off, value);
        enable_ = value;
        return;
    default:
        Halt("write", off, value);
    }
}

void Msm8255MdpHistogram::CountFrame(const Msm8255MdpPixel* pixels, size_t count) {
    if (frames_left_ == 0u) return;
    constexpr uint32_t shift = kPixelBits - kBinBits;
    auto& red = bins_[0];
    auto& green = bins_[1];
    auto& blue = bins_[2];
    for (size_t i = 0; i < count; ++i) {
        ++red[pixels[i].r >> shift];
        ++green[pixels[i].g >> shift];
        ++blue[pixels[i].b >> shift];
    }
    if (--frames_left_ == 0u) Raise(kHistDone);
}

bool Msm8255MdpHistogram::TakeInterrupt() {
    const bool pending = interrupt_;
    interrupt_ = false;
    return pending;
}

void Msm8255MdpHistogram::Raise(uint32_t bits) {
    status_ |= bits;
    if ((bits & enable_) != 0u) interrupt_ = true;
}

void Msm8255MdpHistogram::Reset() {
    frame_count_ = 0u;
    word10_      = 0u;
    status_      = 0u;
    enable_      = 0u;
    frames_left_ = 0u;
    interrupt_   = false;
    for (auto& channel_bins : bins_) channel_bins.fill(0u);
}

void Msm8255MdpHistogram::Halt(const char* access, uint32_t off, uint32_t value) {
    emu_.Get<Fatal>().Die("msm8255 mdp histogram: %s of %05X value %08X is not modeled", access, off,
                          value);
}

void Msm8255MdpHistogram::SaveState(StateWriter& w) const {
    w.Write("hist_frame_count", frame_count_);
    w.Write("hist_word10", word10_);
    w.Write("hist_status", status_);
    w.Write("hist_enable", enable_);
    w.Write("hist_frames_left", frames_left_);
    for (const auto& channel_bins : bins_) {
        w.WriteBytes("hist_bins", channel_bins.data(), sizeof(channel_bins));
    }
}

void Msm8255MdpHistogram::RestoreState(StateReader& r) {
    r.Read("hist_frame_count", frame_count_);
    r.Read("hist_word10", word10_);
    r.Read("hist_status", status_);
    r.Read("hist_enable", enable_);
    r.Read("hist_frames_left", frames_left_);
    for (auto& channel_bins : bins_) {
        r.ReadBytes("hist_bins", channel_bins.data(), sizeof(channel_bins));
    }
}

REGISTER_SERVICE(Msm8255MdpHistogram);

}
