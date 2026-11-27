#include "sony_imx125.h"

namespace cerfcore {

namespace {

constexpr uint16_t kModelId        = 0x4241u;
constexpr uint8_t  kManufacturerId = 0x10u;

}

uint16_t SonyImx125::ModelId() const { return kModelId; }

uint8_t SonyImx125::ManufacturerId() const { return kManufacturerId; }

}
