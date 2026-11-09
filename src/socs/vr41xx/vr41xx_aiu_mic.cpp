#include "vr41xx_aiu_mic.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "../../host/audio_activity_widget.h"
#include "vr41xx_audio_switch.h"
#include "vr41xx_dcu.h"

namespace cerfcore {

namespace {

constexpr uint16_t kMstIntr    = 0x0100u;
constexpr uint16_t kMidleIntr  = 0x0200u;
constexpr uint16_t kMIntr      = 0x0400u;
constexpr uint16_t kMendIntr   = 0x0800u;
constexpr uint16_t kMidatv     = 0x0002u;
constexpr uint16_t kMdmav      = 0x0001u;
constexpr uint16_t kDataMask   = 0x03FFu;
constexpr uint16_t kDataReset  = 0x0200u;
constexpr size_t   kFifoBytes  = 65536u;

constexpr Vr41xxAiuPathConfig kConfig{Vr41xxAiuChannel::In, kDmaMskAin, kMIntr, kMendIntr, "mic"};

}

Vr41xxAiuMic::Vr41xxAiuMic(CerfEmulator& emu, Vr41xxAiuPathHost& host) : Vr41xxAiuPath(emu, host, kConfig) {}

void Vr41xxAiuMic::AttachHost() {
    Attach();
    switch_ = &emu_.Get<Vr41xxAudioSwitch>();
    mic_on_ = switch_->MicOn();
    mic_.Start(emu_.Get<InstanceThreads>(), "Vr41xxAiuMic", kFifoBytes);
}

void Vr41xxAiuMic::StopHost() { mic_.Stop(); }

void Vr41xxAiuMic::SwitchChanged() {
    CatchUp();
    mic_on_ = switch_->MicOn();
}

uint16_t Vr41xxAiuMic::ValidBits() const {
    return static_cast<uint16_t>((midatv_ ? kMidatv : 0u) | (mdmav_ ? kMdmav : 0u));
}

void Vr41xxAiuMic::WriteMidat(uint16_t value) { midat_ = static_cast<uint16_t>(value & kDataMask); }

void Vr41xxAiuMic::WriteMdmadat(uint16_t value) { mdmadat_ = static_cast<uint16_t>(value & kDataMask); }

void Vr41xxAiuMic::WriteValidBits(uint16_t bits) {
    midatv_ = (bits & kMidatv) != 0u;
    mdmav_  = (bits & kMdmav) != 0u;
}

uint16_t Vr41xxAiuMic::NextSample() {
    uint8_t pcm[2] = {};
    const bool heard = mic_.Take(pcm, sizeof pcm) != 0u;
    if (!mic_on_) return kDataReset;
    if (heard) emu_.Get<AudioActivityWidget>().MarkRx();
    const int sample = static_cast<int16_t>(cerf::le::U16(pcm, 0));
    return static_cast<uint16_t>((sample + 0x8000) >> 6);
}

void Vr41xxAiuMic::MoveToDma() {
    if (mdmav_ || !midatv_) return;
    mdmadat_ = midat_;
    mdmav_   = true;
    midatv_  = false;
    host_.RaiseAiuInt(kMstIntr);
}

void Vr41xxAiuMic::Convert() {
    if (!host_.ConverterEnabled(Vr41xxAiuChannel::In))
        emu_.Get<Fatal>().Die("Vr41xxAiu: mic A/D conversion with ADENAIU 0");
    const uint16_t sample = NextSample();
    if (midatv_) host_.RaiseAiuInt(kMidleIntr);
    midat_  = sample;
    midatv_ = true;
    MoveToDma();
    Serve();
}

void Vr41xxAiuMic::Serve() {
    while (mdmav_ && DmaAllowed()) {
        mem_->WriteHalf(TakeDmaAddress(), mdmadat_);
        mdmav_ = false;
        MoveToDma();
    }
}

uint64_t Vr41xxAiuMic::ConversionsToNextChange() const {
    if (!host_.AiuIntPending(kMstIntr) && (!mdmav_ || DmaAllowed())) return 1u;
    if (DmaAllowed()) return HalfwordsToBoundary();
    if (host_.AiuIntPending(kMidleIntr)) return 0u;
    return midatv_ ? 1u : 2u;
}

void Vr41xxAiuMic::ClearPipeline() {
    midat_   = kDataReset;
    mdmadat_ = kDataReset;
    midatv_  = false;
    mdmav_   = false;
}

void Vr41xxAiuMic::OnStart() {
    mic_.Clear();
    mic_.Request(RateHz(), 1, 16);
}

void Vr41xxAiuMic::OnStop() { mic_.Clear(); }

void Vr41xxAiuMic::OnRateChanged(uint32_t hz) {
    mic_.Clear();
    mic_.Request(hz, 1, 16);
}

void Vr41xxAiuMic::SavePipeline(StateWriter& w) {
    w.Write("mic_midat", midat_);
    w.Write("mic_mdmadat", mdmadat_);
    w.Write<uint8_t>("mic_midatv", midatv_ ? 1u : 0u);
    w.Write<uint8_t>("mic_mdmav", mdmav_ ? 1u : 0u);
    w.Write<uint8_t>("mic_switch_on", mic_on_ ? 1u : 0u);
}

void Vr41xxAiuMic::RestorePipeline(StateReader& r) {
    uint8_t midatv = 0u, mdmav = 0u, switch_on = 0u;
    r.Read("mic_midat", midat_);
    r.Read("mic_mdmadat", mdmadat_);
    r.Read("mic_midatv", midatv);
    r.Read("mic_mdmav", mdmav);
    r.Read("mic_switch_on", switch_on);
    midatv_ = midatv != 0u;
    mdmav_  = mdmav != 0u;
    mic_on_ = switch_on != 0u;
}

void Vr41xxAiuMic::ResumeHost() {
    mic_.Clear();
    if (Enabled()) mic_.Request(RateHz(), 1, 16);
}

}
