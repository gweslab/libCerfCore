#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "s3c2410_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kSlotBwscon     = 0;
constexpr uint32_t kBwsconDw0      = 0x00000006u;
constexpr uint32_t kSlotBankcon0   = 1;
constexpr uint32_t kSlotBankcon6   = 7;
constexpr uint32_t kSlotRefresh    = 9;
constexpr uint32_t kSlotBanksize   = 10;
constexpr uint32_t kBankconReset   = 0x00000700u;
constexpr uint32_t kBankcon67Reset = 0x00018008u;
constexpr uint32_t kRefreshReset   = 0x00AC0000u;
constexpr uint32_t kBanksizeReset  = 0x00000002u;

class S3C2410MemCtrl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::S3c2410;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x48000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes("storage", storage_, sizeof(storage_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("storage", storage_, sizeof(storage_)); }

private:
    void ResetRegisters() {
        storage_[kSlotBwscon] &= kBwsconDw0;
        for (uint32_t s = kSlotBankcon0; s < kSlotBankcon6; ++s) storage_[s] = kBankconReset;
        for (uint32_t s = kSlotBankcon6; s < kSlotRefresh; ++s) storage_[s] = kBankcon67Reset;
        storage_[kSlotRefresh]  = kRefreshReset;
        storage_[kSlotBanksize] = kBanksizeReset;
    }

    static constexpr size_t kSlotCount = 13;  /* BWSCON..MRSRB7 */
    uint32_t storage_[kSlotCount] = {};
};

uint32_t S3C2410MemCtrl::ReadWord(uint32_t addr) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }
    const uint32_t value = storage_[slot];
    LOG(SocMemc, "read  +0x%02X -> 0x%08X\n",
        addr - MmioBase(), value);
    return value;
}

void S3C2410MemCtrl::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t slot = (addr - MmioBase()) / 4u;
    if (slot >= kSlotCount) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    LOG(SocMemc, "write +0x%02X = 0x%08X\n",
        addr - MmioBase(), value);
    storage_[slot] = slot == kSlotBwscon ? (value & ~kBwsconDw0) | (storage_[slot] & kBwsconDw0) : value;
}

}  /* namespace */

REGISTER_SERVICE(S3C2410MemCtrl);

}
