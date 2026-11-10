#pragma once

#include <cerfcore/core/service.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

class HpDebugBoardPlacement : public Service {
public:
    using Service::Service;

    static bool FittedOnFamily(std::string_view family) {
        return family == BoardFamilyId::Hp600 || family == BoardFamilyId::HpJornada700;
    }

    virtual uint32_t BasePa() const = 0;
};

}
