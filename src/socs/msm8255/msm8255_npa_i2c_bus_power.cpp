#include "msm8255_npa_clients.h"
#include "msm8255_smem.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

struct I2cBusResource {
    const char* resource;
    const char* lock;
};

constexpr I2cBusResource kI2cBusResources[] = {
    {"/bus/i2c1", kMsm8255DalI2c1Lock},
    {"/bus/i2c2", kMsm8255DalI2c2Lock},
};

constexpr uint32_t kLockPowerStateOff = 40u;
constexpr uint32_t kPowerActive       = 2u;
constexpr uint32_t kPowerManagementOff = 4096u;

class Msm8255NpaI2cBusPower : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        auto& clients = emu_.Get<Msm8255NpaClients>();
        for (const I2cBusResource& bus : kI2cBusResources) {
            clients.RegisterResourceDriver(
                bus.resource, [this, bus](uint32_t level) { ApplyLevel(bus, level); });
        }
    }

private:
    void ApplyLevel(const I2cBusResource& bus, uint32_t level) {
        if (level > kPowerActive) {
            emu_.Get<Fatal>().Die(
                "msm8255 npa i2c bus power: %s was requested at level %u, past "
                "the %u the bus power events reach", bus.resource, level,
                kPowerActive);
        }
        const uint32_t lock = emu_.Get<Msm8255Smem>().DalRecordPa(bus.lock);
        if (lock == 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 npa i2c bus power: the dal arena carries no %s record "
                "for the level %u request on %s", bus.lock, level, bus.resource);
        }
        auto& mem = emu_.Get<EmulatedMemory>();
        if (mem.ReadWord(lock + kLockPowerStateOff) == kPowerManagementOff) {
            return;
        }
        mem.WriteWord(lock + kLockPowerStateOff, level);
    }
};

}

REGISTER_SERVICE(Msm8255NpaI2cBusPower);

}
