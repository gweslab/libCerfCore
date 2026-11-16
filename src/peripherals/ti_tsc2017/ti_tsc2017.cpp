#include "../../socs/i2c_slave.h"
#include "ti_tsc2017_wiring.h"
#include "tsc2017_host_state.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {


namespace {

class TiTsc2017 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override { return emu_.TryGet<TiTsc2017Wiring>() != nullptr; }
    void OnReady() override { emu_.Get<TiTsc2017Wiring>().Attach(this); }

    bool MatchesAddress(uint8_t slave_addr) const override {
        return slave_addr == emu_.Get<TiTsc2017Wiring>().SlaveAddress();
    }

    void TxnStart(uint8_t) override { start_pending_ = true; }

    void TxnWriteByte(uint8_t, uint8_t value) override {
        if (start_pending_) {
            start_pending_ = false;
            expecting_command_ = true;
            SetPenIrqMode(PenIrqMode::Disabled);
        }
        if (!expecting_command_)
            emu_.Get<Fatal>().Die("TSC2017 unexpected additional write byte 0x%02X", value);
        Command(value);
        expecting_command_ = false;
    }

    uint8_t TxnReadByte(uint8_t) override {
        if (start_pending_) {
            start_pending_ = false;
            expecting_command_ = false;
            read_index_ = 0;
        }
        const bool eight_bit = (command_ & 0x02u) != 0;
        const uint16_t sample = Clamp12(last_value_);
        const uint8_t index = read_index_++;
        const uint8_t result_bytes = eight_bit ? 1u : 2u;
        if (index >= result_bytes) return 0xFFu;

        return index == 0 ? static_cast<uint8_t>(sample >> 4)
                          : static_cast<uint8_t>((sample & 0x000Fu) << 4);
    }

    void SaveState(StateWriter& w) override {
        w.Write("command", command_);
        w.Write("read_index", read_index_);
        w.Write("last_value", last_value_);
        w.Write("setup", setup_);
        w.Write("expecting_command", static_cast<uint8_t>(expecting_command_));
        w.Write("start_pending", static_cast<uint8_t>(start_pending_));
        w.Write("penirq_mode", static_cast<uint8_t>(penirq_mode_));
    }
    void RestoreState(StateReader& r) override {
        r.Read("command", command_);
        r.Read("read_index", read_index_);
        r.Read("last_value", last_value_);
        r.Read("setup", setup_);
        uint8_t expecting_command = 0;
        r.Read("expecting_command", expecting_command);
        expecting_command_ = expecting_command != 0u;
        uint8_t start_pending = 0;
        r.Read("start_pending", start_pending);
        start_pending_ = start_pending != 0u;
        uint8_t penirq_mode = 0;
        r.Read("penirq_mode", penirq_mode);
        penirq_mode_ = static_cast<PenIrqMode>(penirq_mode);
        emu_.Get<Tsc2017HostState>().SetPenIrqMode(penirq_mode_, false);
        emu_.Get<Tsc2017HostState>().ClearHostInputForRestore();
    }

private:
    using PenIrqMode = Tsc2017HostState::PenIrqMode;

    void SetPenIrqMode(PenIrqMode mode) {
        penirq_mode_ = mode;
        emu_.Get<Tsc2017HostState>().SetPenIrqMode(mode, true);
    }

    static uint16_t Clamp12(uint16_t v) { return static_cast<uint16_t>(v & 0x0FFFu); }

    static constexpr uint16_t kTemp0UngroundedStubCode = 803u;
    static constexpr uint16_t kFunction0xAStubValue = 0x000u;
    static constexpr uint16_t kFunction0x8StubValue = 0x000u;
    static constexpr uint16_t kFunction0x9StubValue = 0x000u;

    uint16_t SampleForFunction(uint8_t function) {
        const auto touch = emu_.Get<Tsc2017HostState>().Get();
        switch (function & 0x0Fu) {
        case 0x0u: return kTemp0UngroundedStubCode;
        case 0x2u:
        case 0x4u:
            emu_.Get<Fatal>().Die("TI TSC2017: AUX/TEMP1 conversion (function 0x%X) is not modelled",
                                  static_cast<unsigned>(function & 0x0Fu));
        case 0x8u: return kFunction0x8StubValue;
        case 0x9u: return kFunction0x9StubValue;
        case 0xAu: return kFunction0xAStubValue;
        case 0xCu: return touch.x;
        case 0xDu: return touch.y;
        case 0xEu: return touch.z1;
        case 0xFu: return touch.z2;
        default: emu_.Get<Fatal>().Die("TSC2017 reserved converter function 0x%X", function);
        }
    }

    void Command(uint8_t command) {
        command_ = command;
        read_index_ = 0;
        const uint8_t function = static_cast<uint8_t>(command >> 4);
        if (function == 0x0Bu) {
            if (command & 0x08u)
                emu_.Get<Fatal>().Die("TSC2017 setup command 0x%02X writes reserved D3", command);
            setup_ = static_cast<uint8_t>(command & 0x0Fu);
            if (setup_ & 0x04u) {
                setup_ = 0;
                last_value_ = 0;
                SetPenIrqMode(PenIrqMode::Enabled);
            }
            return;
        }
        if (function == 0x8u || function == 0x9u || function == 0xAu)
            SetPenIrqMode(PenIrqMode::ForcedLow);
        else if ((command & 0x04u) != 0u)
            SetPenIrqMode(PenIrqMode::Disabled);
        else
            SetPenIrqMode(PenIrqMode::Enabled);
        last_value_ = Clamp12(SampleForFunction(function));
    }

    uint8_t command_ = 0, read_index_ = 0, setup_ = 0;
    uint16_t last_value_ = 0;
    bool expecting_command_ = false;
    bool start_pending_ = false;
    PenIrqMode penirq_mode_ = PenIrqMode::Enabled;
};

}

REGISTER_SERVICE(TiTsc2017);

}
