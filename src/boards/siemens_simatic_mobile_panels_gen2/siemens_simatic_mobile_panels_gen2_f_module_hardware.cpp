#include "siemens_simatic_mobile_panels_gen2_f_module_hardware.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_channel.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_firmware.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <random>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

namespace {

constexpr uint32_t kPortA = 0u;
constexpr uint32_t kPortB = 1u;
constexpr uint32_t kPortC = 2u;
constexpr uint32_t kPortD = 3u;

constexpr uint64_t kSliceNs = 500u; /* Sync step between the two MCUs and the panel. */
constexpr uint64_t kNsPerSecond = 1000000000u;

}

class FModuleHardware::ChannelHost : public stm32f2::McuHost {
public:
    ChannelHost(FModuleHardwareHost& host, uint32_t channel) : host_(host), channel_(channel) {}
    [[noreturn]] void McuFault(const char* what, uint32_t detail, uint32_t pc) override {
        host_.FModuleFault(channel_, what, detail, pc);
        CERF_UNREACHABLE();
    }

private:
    FModuleHardwareHost& host_;
    uint32_t channel_;
};

class FModuleHardware::ChannelPins : public stm32f2::GpioPins {
public:
    ChannelPins(FModuleHardware& hw, uint32_t channel) : hw_(hw), channel_(channel) {}
    bool ExternalLevel(uint32_t port, uint32_t pin, bool* level) override {
        return hw_.NetLevel(channel_, port, pin, level);
    }
    bool AlternateLevel(uint32_t port, uint32_t pin, uint32_t af, bool* level) override {
        return hw_.AlternateLevel(channel_, port, pin, af, level);
    }
    void OutputChanged(uint32_t port, uint32_t pin, bool driven, bool level) override {
        hw_.OutputChanged(channel_, port, pin, driven, level);
    }

private:
    FModuleHardware& hw_;
    uint32_t channel_;
};

FModuleHardware::FModuleHardware(CerfEmulator& emu, FModuleHardwareHost& host)
    : emu_(emu), host_(host) {
    std::random_device entropy;
    rng_seed_ = (static_cast<uint64_t>(entropy()) << 32) | entropy();
    Build();
}

uint64_t FModuleHardware::NextRngSeed() {
    rng_seed_ += 0x9E3779B97F4A7C15ULL;
    uint64_t z = rng_seed_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void FModuleHardware::Build() {
    for (uint32_t c = 0u; c < kChannels; ++c) {
        channel_hosts_[c] = std::make_unique<ChannelHost>(host_, c);
        channel_pins_[c] = std::make_unique<ChannelPins>(*this, c);
        channels_[c] = std::make_unique<Channel>(*channel_hosts_[c], *channel_pins_[c], NextRngSeed());
    }
    Connect();
}

void FModuleHardware::Connect() {
    for (uint32_t c = 0u; c < kChannels; ++c) {
        Channel& other = *channels_[c ^ 1u];
        channels_[c]->wire.Connect(&channels_[c]->usart1, &other.usart1);
        channels_[c]->bootloader.Connect(other.gpio[kPortB].get());
    }
}

FModuleHardware::~FModuleHardware() = default;

bool FModuleHardware::LoadApplication(const uint8_t* image, size_t bytes) {
    for (auto& channel : channels_) {
        auto& flash = channel->mcu.Flash();
        if (bytes > flash.size() - kApplicationOffset) return false;
        std::copy(image, image + bytes, flash.begin() + kApplicationOffset);
    }
    return true;
}

void FModuleHardware::SealBootloaderRegion(std::vector<uint8_t>& flash) {
    constexpr size_t kCrcOffset = FModuleBootloader::kRegionBytes - 4u;
    const uint32_t crc = Komp2Crc(flash.data(), kCrcOffset);
    for (uint32_t b = 0u; b < 4u; ++b) flash[kCrcOffset + b] = static_cast<uint8_t>(crc >> (8u * b));
}

void FModuleHardware::Reset() {
    ResetChannels({true, true}, {stm32f2::Rcc::kPinResetFlag, stm32f2::Rcc::kPinResetFlag});
    now_ns_ = 0;
    lines_ = FModuleLines{};
}

void FModuleHardware::ResetChannels(const std::array<bool, kChannels>& reset, const std::array<uint32_t, kChannels>& reset_flags) {
    flash_modified_ = TakeFlashModified();
    std::array<std::vector<uint8_t>, kChannels> flash;
    std::array<std::vector<uint8_t>, kChannels> otp;
    std::array<std::vector<uint8_t>, kChannels> sram;
    std::array<uint32_t, kChannels> flags{};
    for (uint32_t c = 0u; c < kChannels; ++c) {
        flash[c] = channels_[c]->mcu.Flash();
        otp[c] = channels_[c]->mcu.Otp();
        sram[c] = channels_[c]->mcu.Sram();
        flags[c] = channels_[c]->rcc.ResetFlags() | reset_flags[c];
    }
    for (uint32_t c = 0u; c < kChannels; ++c) {
        if (!reset[c]) continue;
        const FModuleBootloader& own = channels_[c]->bootloader;
        if (own.Erased() && !own.Updated())
            channels_[c]->mcu.Fault("F-module: reset of an MCU whose bootloader erased the application without completing the image is not modelled", c);
        if (!own.Active()) UngroundedBootloaderImageSync(c, flash[c]);
    }
    for (uint32_t c = 0u; c < kChannels; ++c) {
        if (!reset[c]) continue;
        channels_[c] = std::make_unique<Channel>(*channel_hosts_[c], *channel_pins_[c], NextRngSeed());
        generation_[c] = ++next_generation_;
    }
    Connect();
    for (uint32_t c = 0u; c < kChannels; ++c) {
        if (!reset[c]) continue;
        auto& target = channels_[c]->mcu.Flash();
        std::copy(flash[c].begin(), flash[c].end(), target.begin());
        FModuleBootloader::Install(target);
        SealBootloaderRegion(target);
        std::copy(otp[c].begin(), otp[c].end(), channels_[c]->mcu.Otp().begin());
        channels_[c]->mcu.Start(stm32f2::Mcu::kFlashBase + kApplicationOffset);
        std::copy(sram[c].begin(), sram[c].end(), channels_[c]->mcu.Sram().begin());
        channels_[c]->rcc.SetResetFlags(flags[c]);
    }
}

void FModuleHardware::UngroundedBootloaderImageSync(uint32_t channel, std::vector<uint8_t>& flash) {
    FModuleBootloader& partner = channels_[channel ^ 1u]->bootloader;
    if (!partner.Active() || !partner.Updated()) return;
    const std::vector<uint8_t>& source = channels_[channel ^ 1u]->mcu.Flash();
    std::copy(source.begin() + kApplicationOffset, source.end(), flash.begin() + kApplicationOffset);
    flash_modified_ = true;
    partner.StartApplication();
}

uint64_t FModuleHardware::StepNs(int64_t ns) const {
    uint64_t step = std::min(static_cast<uint64_t>(ns - now_ns_), kSliceNs);
    for (const auto& channel : channels_) {
        const Channel& ch = *channel;
        for (uint32_t base : {0x40010000u, 0x40011000u}) {
            const uint64_t at = ch.mcu.EventCycle(base);
            if (at == UINT64_MAX) continue;
            const uint64_t ahead = at > ch.target ? at - ch.target : 0u;
            const uint64_t hz = ch.rcc.HclkHz();
            step = std::min(step, std::max<uint64_t>((ahead * kNsPerSecond + hz - 1u) / hz, 1u));
        }
    }
    return step;
}

void FModuleHardware::RunUntilNs(int64_t ns) {
    while (now_ns_ < ns) {
        const uint64_t step = StepNs(ns);
        now_ns_ += static_cast<int64_t>(step);
        for (auto& channel : channels_) {
            Channel& ch = *channel;
            const uint64_t hz = ch.rcc.HclkHz();
            if (step == kSliceNs) {
                if (hz != ch.slice_hz) {
                    ch.slice_hz = hz;
                    ch.slice_cycles = kSliceNs * hz / kNsPerSecond;
                    ch.slice_fraction = kSliceNs * hz % kNsPerSecond;
                }
                ch.target += ch.slice_cycles;
                ch.fraction += ch.slice_fraction;
            } else {
                ch.fraction += static_cast<uint64_t>(step) * hz;
            }
            if (ch.fraction >= kNsPerSecond) {
                ch.target += ch.fraction / kNsPerSecond;
                ch.fraction %= kNsPerSecond;
            }
            ch.mcu.RunUntil(ch.target);
            if (ch.bootloader.Active()) {
                ch.bootloader.Run(now_ns_);
            } else if (ch.mcu.ProgramCounter() == stm32f2::Mcu::kFlashBase + FModuleBootloader::kBootloaderStubOffset) {
                ch.bootloader.Enter(now_ns_);
                ch.bootloader.Run(now_ns_);
            }
        }
        for (auto& channel : channels_) channel->wire.Flush();
        for (auto& channel : channels_) channel->exti->Sample();
        std::array<bool, kChannels> reset{};
        std::array<uint32_t, kChannels> causes{};
        for (uint32_t c = 0u; c < kChannels; ++c) {
            causes[c] = channels_[c]->mcu.TakeSystemReset();
            reset[c] = causes[c] != 0u;
        }
        if (!reset[0] && !reset[1]) continue;
        const uint32_t pin = stm32f2::Rcc::kPinResetFlag;
        if constexpr (!kSeparateNrstNets) reset = {true, true};
        ResetChannels(reset, {reset[0] ? pin | causes[0] : 0u, reset[1] ? pin | causes[1] : 0u});
        if (reset[0]) SetLines(FModuleLines{}, now_ns_);
    }
}

void FModuleHardware::SetLines(const FModuleLines& lines, int64_t ns) {
    if (lines == lines_) return;
    lines_ = lines;
    host_.LinesChanged(lines_, ns);
}

bool FModuleHardware::NetLevel(uint32_t channel, uint32_t port, uint32_t pin, bool* level) {
    Channel& other = *channels_[channel ^ 1u];
    if (port == kPortA && pin == 8u) {
        *level = channel == 1u;
        return true;
    }
    if (port == kPortC && pin == 8u) {
        *level = ack_;
        return ack_known_;
    }
    if (port == kPortA && pin == 10u) return SharedNet(channel, port, pin, *other.gpio[kPortA], 9u, level);
    if (port == kPortC && pin == 1u) {
        if (channel == 0u) {
            *level = false;
            return true;
        }
        return SharedNet(channel, port, pin, *other.gpio[kPortC], 0u, level);
    }
    if (port == kPortC && pin == 11u) return SharedNet(channel, port, pin, *other.gpio[kPortA], 11u, level);
    if (port == kPortD && pin <= 3u) return SharedNet(channel, port, pin, *other.gpio[kPortB], pin + 8u, level);
    const bool paired = (port == kPortA && pin >= 2u && pin <= 7u) || (port == kPortD && (pin == 7u || pin == 8u));
    return paired && SharedNet(channel, port, pin, *other.gpio[port], pin, level);
}

bool FModuleHardware::SharedNet(uint32_t channel, uint32_t port, uint32_t pin, stm32f2::Gpio& source, uint32_t source_pin,
                                bool* level) {
    if (source.DrivenLevel(source_pin, level)) return true;
    const uint32_t far = source.Pull(source_pin);
    if (far == 0u) return false;
    const uint32_t near = channels_[channel]->gpio[port]->Pull(pin);
    if (far == 3u || (near != 0u && near != far))
        channels_[channel]->mcu.Fault("F-module: opposite or reserved pulls on the two ends of a net", (far << 2) | near);
    *level = far == 1u;
    return true;
}

bool FModuleHardware::AlternateLevel(uint32_t channel, uint32_t port, uint32_t pin, uint32_t af, bool* level) {
    Channel& ch = *channels_[channel];
    constexpr bool kUnmappedAlternateFunctionDrives = false;
    if (port == kPortA && pin == 11u && af == 0u) return kUnmappedAlternateFunctionDrives;
    if (port == kPortA && pin == 11u && af == 1u) {
        if (!ch.tim1.OutputEnabled(3u)) return false;
        *level = ch.tim1.OutputLevel(3u);
        return true;
    }
    if (port == kPortA && pin == 9u && af == 7u) {
        ch.mcu.FlushTicks();
        return ch.usart1.TxLevel(level);
    }
    if ((port == kPortA && pin == 10u && af == 7u) || (port == kPortB && pin >= 12u && pin != 14u && af == 5u))
        return false;
    if (af == 0u && ((port == kPortA && pin >= 13u) || (port == kPortB && pin == 4u))) return false;
    ch.mcu.Fault("F-module: the pin level of this alternate function is not modelled", (port << 8) | (pin << 4) | af);
    CERF_UNREACHABLE();
}

void FModuleHardware::OutputChanged(uint32_t channel, uint32_t port, uint32_t pin, bool driven, bool level) {
    if (channel != 0u || port != kPortC || pin != 9u) return;
    Channel& ch = *channels_[channel];
    if (!driven && ch.gpio[kPortC]->Mode(9u) == 2u)
        ch.mcu.Fault("F-module: PC9 in alternate function mode is not modelled", ch.gpio[kPortC]->AlternateFunction(9u));
    if (!driven && ch.gpio[kPortC]->Pull(9u) != 0u)
        ch.mcu.Fault("F-module: a pull on the undriven READY line (PC9) is not modelled", ch.gpio[kPortC]->Pull(9u));
    const int64_t behind = static_cast<int64_t>(ch.target) - static_cast<int64_t>(ch.mcu.Now());
    SetLines(FModuleLines{driven, driven && level},
             now_ns_ - behind * static_cast<int64_t>(kNsPerSecond) / static_cast<int64_t>(ch.rcc.HclkHz()));
}

uint8_t FModuleHardware::SpiExchange(uint32_t channel, uint8_t mosi) {
    Channel& ch = *channels_[channel];
    const stm32f2::Gpio& port_b = *ch.gpio[kPortB];
    for (uint32_t pin = 12u; pin <= 15u; ++pin) {
        if (port_b.Mode(pin) != 2u || port_b.AlternateFunction(pin) != 5u)
            ch.mcu.Fault("F-module: an ECSPI3 frame with PB12-PB15 not on SPI2 (AF5)", pin);
    }
    ch.mcu.FlushTicks();
    return ch.spi2.SlaveExchange(mosi);
}

bool FModuleHardware::SpiEnabled(uint32_t channel) const {
    return channels_[channel]->spi2.Enabled();
}

void FModuleHardware::RequireMisoReleased(uint32_t channel) {
    Channel& ch = *channels_[channel];
    const stm32f2::Gpio& port_b = *ch.gpio[kPortB];
    if (ch.spi2.Enabled() || port_b.Mode(14u) != 2u || port_b.Pull(14u) != 0u)
        ch.mcu.Fault("F-module: PB14 neither driven by an enabled SPI2 nor an alternate function pin without pull",
                     port_b.Mode(14u) | (port_b.Pull(14u) << 2u));
}

std::vector<uint8_t>& FModuleHardware::Flash(uint32_t channel) {
    return channels_[channel]->mcu.Flash();
}

std::vector<uint8_t>& FModuleHardware::Otp(uint32_t channel) {
    return channels_[channel]->mcu.Otp();
}

bool FModuleHardware::TakeFlashModified() {
    bool modified = flash_modified_;
    flash_modified_ = false;
    for (auto& channel : channels_) modified = channel->flash.TakeModified() || modified;
    return modified;
}

const cortex_m3::Core& FModuleHardware::Core(uint32_t channel) const {
    return channels_[channel]->mcu.Core();
}

void FModuleHardware::DropStaleMemory(Snapshot& snapshot) const {
    for (uint32_t c = 0u; c < kChannels; ++c) {
        if (snapshot.generation[c] != generation_[c]) snapshot.memory[c].synced = false;
    }
}

void FModuleHardware::SaveSnapshot(Snapshot& snapshot) {
    StateWriter writer(emu_, snapshot.state);
    SaveState(writer, false);
    DropStaleMemory(snapshot);
    for (uint32_t c = 0u; c < kChannels; ++c) {
        snapshot.generation[c] = generation_[c];
        channels_[c]->mcu.SnapshotMemory(snapshot.memory[c]);
    }
}

void FModuleHardware::RestoreSnapshot(Snapshot& snapshot) {
    StateReader reader(emu_, snapshot.state);
    RestoreState(reader, false);
    DropStaleMemory(snapshot);
    for (uint32_t c = 0u; c < kChannels; ++c) {
        channels_[c]->mcu.RestoreMemory(snapshot.memory[c]);
        generation_[c] = snapshot.generation[c];
    }
}

void FModuleHardware::SaveState(StateWriter& writer, bool memory) const {
    for (const auto& channel : channels_) {
        channel->mcu.SaveState(writer, memory);
        channel->bootloader.SaveState(writer);
        channel->sensor.SaveState(writer);
        writer.Write("fmodule_target", channel->target);
        writer.Write("fmodule_fraction", channel->fraction);
    }
    writer.Write("fmodule_ack_known", ack_known_);
    writer.Write("fmodule_ack", ack_);
    writer.Write("fmodule_now_ns", now_ns_);
    writer.Write("fmodule_rng_seed", rng_seed_);
    writer.Write("fmodule_ready_driven", lines_.ready_driven);
    writer.Write("fmodule_ready", lines_.ready);
}

void FModuleHardware::RestoreState(StateReader& reader, bool memory) {
    for (auto& channel : channels_) {
        channel->mcu.RestoreState(reader, memory);
        channel->bootloader.RestoreState(reader);
        channel->sensor.RestoreState(reader);
        reader.Read("fmodule_target", channel->target);
        reader.Read("fmodule_fraction", channel->fraction);
        channel->slice_hz = 0u;
    }
    if (memory) {
        for (uint64_t& generation : generation_) generation = ++next_generation_;
    }
    reader.Read("fmodule_ack_known", ack_known_);
    reader.Read("fmodule_ack", ack_);
    reader.Read("fmodule_now_ns", now_ns_);
    reader.Read("fmodule_rng_seed", rng_seed_);
    reader.Read("fmodule_ready_driven", lines_.ready_driven);
    reader.Read("fmodule_ready", lines_.ready);
}

}

}
