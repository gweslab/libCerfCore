#pragma once

#include "../smia_camera_sensor/smia_camera_sensor.h"

#include <cstdint>

namespace cerfcore {

class ToshibaJt8ew9 : public SmiaCameraSensor {
public:
    using SmiaCameraSensor::SmiaCameraSensor;

protected:
    uint16_t ModelId() const override;
    uint8_t  ManufacturerId() const override;
};

}
