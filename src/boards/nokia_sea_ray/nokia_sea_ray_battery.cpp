#include "../../peripherals/battery_pack.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/battery.h>
#include <cerfcore/host/machine_object_tree.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kVoltageMinDesignMv = 3200u;
constexpr uint32_t kVoltageMaxDesignMv = 4300u;
constexpr int      kCellCelsius        = 25;
constexpr uint32_t kDesignCapacityMah  = 1450u;

class NokiaSeaRayBattery : public BatteryPack {
public:
    using BatteryPack::BatteryPack;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override { emu_.Get<MachineObjectTree>().Add(&battery_); }

    int  ChargePercent() const override { return battery_.ChargePercent(); }
    bool ExternalPower() const override { return battery_.OnExternalPower(); }
    bool Charging() const override { return battery_.Charging(); }

    uint32_t VoltageMv() const override {
        const uint32_t span = kVoltageMaxDesignMv - kVoltageMinDesignMv;
        return kVoltageMinDesignMv +
               span * static_cast<uint32_t>(battery_.ChargePercent()) /
                   static_cast<uint32_t>(Battery::kFullPercent);
    }

    int TemperatureCelsius() const override { return kCellCelsius; }

    uint32_t DesignCapacityMah() const override { return kDesignCapacityMah; }

    void AddChangeListener(std::function<void()> fn) override {
        battery_.AddChangeListener(std::move(fn));
    }

private:
    Battery battery_{emu_};
};

}

REGISTER_SERVICE_AS(NokiaSeaRayBattery, BatteryPack);

}
