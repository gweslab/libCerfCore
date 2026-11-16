#pragma once

#include "siemens_simatic_mobile_panels_gen2_f_module_bootloader.h"
#include "../../socs/stm32f2/stm32f2_dma.h"
#include "../../socs/stm32f2/stm32f2_exti.h"
#include "../../socs/stm32f2/stm32f2_flash_interface.h"
#include "../../socs/stm32f2/stm32f2_gpio.h"
#include "../../socs/stm32f2/stm32f2_i2c.h"
#include "../../socs/stm32f2/stm32f2_iwdg.h"
#include "../../socs/stm32f2/stm32f2_mcu.h"
#include "../../socs/stm32f2/stm32f2_rcc.h"
#include "../../socs/stm32f2/stm32f2_rng.h"
#include "../../socs/stm32f2/stm32f2_spi.h"
#include "../../socs/stm32f2/stm32f2_timer.h"
#include "../../socs/stm32f2/stm32f2_usart.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

namespace siemens_simatic_mobile_panels_gen2 {

struct FModuleChannel;

struct FModuleLines {
    bool ready_driven = false;
    bool ready = false;
    bool operator==(const FModuleLines& o) const { return ready_driven == o.ready_driven && ready == o.ready; }
    bool operator!=(const FModuleLines& o) const { return !(*this == o); }
};

class FModuleHardwareHost {
public:
    virtual ~FModuleHardwareHost() = default;
    [[noreturn]] virtual void FModuleFault(uint32_t channel, const char* what, uint32_t detail, uint32_t pc) = 0;
    virtual void LinesChanged(const FModuleLines& lines, int64_t ns) = 0;
};

class FModuleHardware {
public:
    static constexpr uint32_t kChannels = 2u;
    static constexpr uint32_t kHseHz = 24000000u;
    static constexpr bool kSeparateNrstNets = true;
    static constexpr uint32_t kApplicationOffset = 0x10000u;

    FModuleHardware(CerfEmulator& emu, FModuleHardwareHost& host);
    ~FModuleHardware();

    bool LoadApplication(const uint8_t* image, size_t bytes);
    std::vector<uint8_t>& Flash(uint32_t channel);
    std::vector<uint8_t>& Otp(uint32_t channel);

    bool TakeFlashModified();

    void Reset();

    void RunUntilNs(int64_t ns);
    int64_t NowNs() const { return now_ns_; }

    void SetAck(bool known, bool level) {
        ack_known_ = known;
        ack_ = level;
    }
    const FModuleLines& Lines() const { return lines_; }
    uint8_t SpiExchange(uint32_t channel, uint8_t mosi);
    bool SpiEnabled(uint32_t channel) const;
    void RequireMisoReleased(uint32_t channel);
    const cortex_m3::Core& Core(uint32_t channel) const;

    void SaveState(StateWriter& writer) const { SaveState(writer, true); }
    void RestoreState(StateReader& reader) { RestoreState(reader, true); }

    struct Snapshot {
        std::vector<uint8_t> state;
        std::array<stm32f2::Mcu::MemorySnapshot, kChannels> memory;
        std::array<uint64_t, kChannels> generation{};
    };
    void SaveSnapshot(Snapshot& snapshot);
    void RestoreSnapshot(Snapshot& snapshot);

    uint64_t NextRngSeed();

private:
    using Channel = FModuleChannel;
    class ChannelPins;
    class ChannelHost;

    void Build();
    void Connect();
    void SaveState(StateWriter& writer, bool memory) const;
    void RestoreState(StateReader& reader, bool memory);
    void DropStaleMemory(Snapshot& snapshot) const;
    void ResetChannels(const std::array<bool, kChannels>& reset, const std::array<uint32_t, kChannels>& reset_flags);
    void UngroundedBootloaderImageSync(uint32_t channel, std::vector<uint8_t>& flash);
    static void SealBootloaderRegion(std::vector<uint8_t>& flash);
    bool NetLevel(uint32_t channel, uint32_t port, uint32_t pin, bool* level);
    bool SharedNet(uint32_t channel, uint32_t port, uint32_t pin, stm32f2::Gpio& source, uint32_t source_pin, bool* level);
    bool AlternateLevel(uint32_t channel, uint32_t port, uint32_t pin, uint32_t af, bool* level);
    void OutputChanged(uint32_t channel, uint32_t port, uint32_t pin, bool driven, bool level);
    void SetLines(const FModuleLines& lines, int64_t ns);
    uint64_t StepNs(int64_t ns) const;

    CerfEmulator& emu_;
    FModuleHardwareHost& host_;
    std::array<std::unique_ptr<ChannelHost>, kChannels> channel_hosts_;
    std::array<std::unique_ptr<ChannelPins>, kChannels> channel_pins_;
    std::array<std::unique_ptr<Channel>, kChannels> channels_;
    bool ack_known_ = false;
    bool ack_ = false;
    int64_t now_ns_ = 0;
    bool flash_modified_ = false;
    uint64_t rng_seed_ = 0u;
    FModuleLines lines_;
    std::array<uint64_t, kChannels> generation_{};
    uint64_t next_generation_ = 0u;
};

}

}
