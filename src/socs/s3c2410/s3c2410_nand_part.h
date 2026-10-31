#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/nand/small_page_nand_flash.h"

namespace cerfcore {

class S3c2410NandPart : public Service {
public:
    using Service::Service;

    virtual const SmallPageNandGeometry& Geometry() const = 0;
    virtual SmallPageNandStore&          Store()          = 0;
};

}
