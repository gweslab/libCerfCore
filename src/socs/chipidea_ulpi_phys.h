#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class UlpiPhy;

class ChipIdeaUlpiPhys : public Service {
public:
    using Service::Service;

    virtual UlpiPhy* Phy(uint32_t core) = 0;
};

}
