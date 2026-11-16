#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/calling_convention.h>

namespace cerfcore {

class ArmCp15Registers;
class ArmPageWalker;
class EngineBridge;

class ArmAddressTranslation : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static void CERFENGINE_FASTCALL Translate(void* self, uint32_t opc2, uint32_t va);

private:
    uint32_t Par(uint32_t opc2, uint32_t va);

    ArmCp15Registers* cp15_   = nullptr;
    ArmPageWalker*    walker_ = nullptr;
    EngineBridge*     bridge_ = nullptr;
};

}
