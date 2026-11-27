#pragma once

#include "imx51/imx51_id.h"
#include "msm8255/msm8255_id.h"

#include <string_view>

namespace cerfcore {

inline bool SocHasAdrenoA2xx(std::string_view soc) {
    return soc == SocId::Imx51 || soc == SocId::Msm8255;
}

}
