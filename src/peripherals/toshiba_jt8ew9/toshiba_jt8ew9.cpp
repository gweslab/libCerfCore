#include "toshiba_jt8ew9.h"

namespace cerfcore {

namespace {

constexpr uint16_t kModelId        = 0x560Fu;
constexpr uint8_t  kManufacturerId = 0x0Cu;

}

uint16_t ToshibaJt8ew9::ModelId() const { return kModelId; }

uint8_t ToshibaJt8ew9::ManufacturerId() const { return kManufacturerId; }

}
