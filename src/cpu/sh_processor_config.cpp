#include "sh_processor_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../core/cpu_config.h"

namespace cerfcore {

REGISTER_SERVICE(ShProcessorConfig);

bool ShProcessorConfig::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh;
}

void ShProcessorConfig::OnReady() {
    dsp_ = emu_.Get<CpuConfig>().Boolean("sh_dsp");
}

}
