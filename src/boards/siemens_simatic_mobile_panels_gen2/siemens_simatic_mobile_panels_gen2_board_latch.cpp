#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/host/machine_object_tree.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"
#include "siemens_simatic_mobile_panels_gen2_key_led.h"

namespace cerfcore {

namespace {

constexpr ObjectRgb kFunctionKeyLedColor{80, 220, 80};

struct KeyLed {
    uint8_t number;
    uint8_t bit;
};

std::vector<KeyLed> KeyLedsFor(SiemensSimaticMobilePanelsGen2OpType op_type) {
    switch (op_type) {
    case SiemensSimaticMobilePanelsGen2OpType::Ktp400F: 
	    return {{0, 0}, {1, 2}, {2, 1}, {3, 3}};
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FHw:
        return {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700:
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700F:
    case SiemensSimaticMobilePanelsGen2OpType::Ktp700FArctic:
        return {{0, 0}, {1, 8}, {2, 1}, {3, 9}, {4, 2}, {5, 10}, {6, 3}, {7, 11}};
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900:
    case SiemensSimaticMobilePanelsGen2OpType::Ktp900F:
        return {{0, 0}, {1, 8}, {2, 1}, {3, 9}, {4, 2}, {5, 10}, {6, 3}, {7, 11}, {22, 4}, {23, 12}};
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000F:
    case SiemensSimaticMobilePanelsGen2OpType::Tp1000FRo: 
	    return {};
    }
    return {};
}

class SiemensSimaticMobilePanelsGen2BoardLatch : public Peripheral, public SiemensSimaticMobilePanelsGen2KeyLedOwner {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        auto& tree = emu_.Get<MachineObjectTree>();
        for (const KeyLed& led : KeyLedsFor(SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId()).op_type)) {
            lamps_.push_back(std::make_unique<SiemensSimaticMobilePanelsGen2KeyLed>(*this, led.bit, led.number, kFunctionKeyLedColor));
            tree.Add(lamps_.back().get());
        }
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                for (uint16_t& value : values_) value = 0u;
            }
            RefreshLamps();
        });
    }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 8u; }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u || off >= MmioSize())
            HaltUnsupportedAccess("write-half", addr, value);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            values_[off >> 1] = value;
        }
        RefreshLamps();
    }

    bool KeyLedLit(uint32_t line) const override {
        std::lock_guard<std::mutex> lk(mutex_);
        return ((values_[0] >> line) & 1u) != 0u;
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mutex_);
        w.WriteBytes("values", values_, sizeof(values_));
    }
    void RestoreState(StateReader& r) override {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            r.ReadBytes("values", values_, sizeof(values_));
        }
        RefreshLamps();
    }

private:
    void RefreshLamps() {
        for (const auto& lamp : lamps_) lamp->Refresh();
    }

    mutable std::mutex mutex_;
    uint16_t values_[4] = {};
    std::vector<std::unique_ptr<SiemensSimaticMobilePanelsGen2KeyLed>> lamps_;
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2BoardLatch);

}
