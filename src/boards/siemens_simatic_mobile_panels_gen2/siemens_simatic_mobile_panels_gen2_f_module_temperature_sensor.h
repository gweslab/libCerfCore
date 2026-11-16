#pragma once

#include "../../socs/stm32f2/stm32f2_i2c.h"
#include "../../socs/stm32f2/stm32f2_mcu.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

class FModuleTemperatureSensor : public stm32f2::I2cDevice {
public:
    explicit FModuleTemperatureSensor(stm32f2::Mcu& mcu) : mcu_(mcu) {}

    bool Start(uint32_t address7, bool read) override {
        index_ = 0u;
        expect_pointer_ = !read;
        if (address7 != 0x48u) mcu_.Fault("F-module I2C1: no target is modelled at this address", address7);
        return true;
    }
    bool WriteByte(uint8_t value) override {
        if (!expect_pointer_) mcu_.Fault("F-module temperature sensor: a register write is not modelled", value);
        if (value != 0u) mcu_.Fault("F-module temperature sensor: only the temperature register is modelled", value);
        expect_pointer_ = false;
        return true;
    }
    uint8_t ReadByte() override {
        if (index_ > 1u)
            mcu_.Fault("F-module temperature sensor: read past the two temperature bytes", index_);
        return index_++ == 0u ? kBoardTemperatureStubC : 0u;
    }
    void Stop() override {}

    void SaveState(StateWriter& writer) const {
        writer.Write("fmodule_sensor_index", index_);
        writer.Write("fmodule_sensor_expect_pointer", expect_pointer_);
    }
    void RestoreState(StateReader& reader) {
        reader.Read("fmodule_sensor_index", index_);
        reader.Read("fmodule_sensor_expect_pointer", expect_pointer_);
    }

private:
    static constexpr uint8_t kBoardTemperatureStubC = 25u;

    stm32f2::Mcu& mcu_;
    uint8_t index_ = 0u;
    bool expect_pointer_ = false;
};

}

}
