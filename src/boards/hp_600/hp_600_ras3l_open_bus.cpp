#include "hp_600_dram_bank_open_bus.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_area3_dram.h"

namespace cerfcore {

namespace {

class Hp600Ras3lOpenBus : public Hp600DramBankOpenBus {
public:
    using Hp600DramBankOpenBus::Hp600DramBankOpenBus;

protected:
    uint32_t         BankPaBase() const override { return kSh77xxRas3lPaBase; }
    std::string_view ProbingSocId() const override { return SocId::Sh7709; }
    const char*      WindowTag() const override { return "Hp600Ras3lOpenBus"; }
};

}

REGISTER_SERVICE(Hp600Ras3lOpenBus);

}
