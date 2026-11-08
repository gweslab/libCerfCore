#include "s3c2410_pre_kernel_clocks.h"

#include <cerfcore/boards/board_context.h>
#include "s3c2410_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "s3c2410_clocks.h"
#include "s3c2410_watchdog.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kOffMpllCon = 0x04u;
constexpr uint32_t kOffClkDivn = 0x14u;
constexpr uint32_t kOffWtcon   = 0x00u;

class S3C2410PreKernelSetup : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (bd == nullptr || bd->GetSocId() != SocId::S3c2410) return false;
        return emu_.TryGet<S3C2410PreKernelClocks>() != nullptr;
    }

    void OnReady() override {
        Apply();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(); });
    }

private:
    void Apply() {
        auto& board  = emu_.Get<S3C2410PreKernelClocks>();
        auto& clocks = emu_.Get<S3C2410Clocks>();
        emu_.Get<S3C2410Watchdog>().WriteRegister(kOffWtcon, 0u);
        clocks.WriteRegister(kOffMpllCon, board.MpllCon());
        clocks.WriteRegister(kOffClkDivn, board.ClkDivn());
        LOG(Board, "S3C2410PreKernelSetup: WTCON 0, MPLLCON 0x%08X CLKDIVN 0x%08X -> "
                   "FCLK %llu HCLK %llu PCLK %llu Hz\n",
            board.MpllCon(), board.ClkDivn(),
            static_cast<unsigned long long>(clocks.FclkHz()),
            static_cast<unsigned long long>(clocks.HclkHz()),
            static_cast<unsigned long long>(clocks.PclkHz()));
    }
};

}

REGISTER_SERVICE(S3C2410PreKernelSetup);

}
