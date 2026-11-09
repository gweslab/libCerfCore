#pragma once

namespace cerfcore {

enum class ShutdownChoice { Cancel, Exit, ExitSave, SoftReset, HardReset, Dismissed };

enum class ShutdownTrigger { WindowClose, DeepSleep };

}
