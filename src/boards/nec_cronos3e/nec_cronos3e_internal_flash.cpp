#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/nand/small_page_nand_flash.h"
#include "../../peripherals/nand/small_page_nand_image.h"
#include "../../peripherals/nec_vrc4172/vrc4172_chip_selected.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "nec_mobilegear2_r450_id.h"
#include "nec_mobilegear2_r550_id.h"
#include "nec_mobilepro_790_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kWindowBase = 0x11C00000u;
constexpr uint32_t kWindowSize = 4u;
constexpr uint32_t kChipSelect = 1u;
constexpr uint32_t kOffData    = 0u;
constexpr uint32_t kOffControl = 2u;

constexpr uint8_t kControlCeHigh = 0x01u;
constexpr uint8_t kControlAle    = 0x02u;
constexpr uint8_t kControlCle    = 0x04u;
constexpr uint8_t kControlWpHigh = 0x80u;
constexpr uint8_t kControlKnown  = kControlCeHigh | kControlAle | kControlCle | kControlWpHigh;

constexpr const char* kName = "CRONOS3E internal flash";

constexpr uint8_t kNandId[] = {0xECu, 0x73u};

constexpr SmallPageNandGeometry kNandGeometry = {
    1024u, 2u, kNandId, sizeof(kNandId), true,
};

class NecCronos3eInternalFlash : public Vrc4172ChipSelected<Peripheral> {
public:
    using Vrc4172ChipSelected<Peripheral>::Vrc4172ChipSelected;

    bool ShouldRegister() override {
        const auto board = emu_.Get<BoardContext>().GetBoardId();
        return board == BoardId::NecMobilepro790 || board == BoardId::NecMobilegear2R550 ||
               board == BoardId::NecMobilegear2R450;
    }

    uint32_t MmioBase() const override { return kWindowBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    void SaveState(StateWriter& w) override {
        w.Write("control", control_);
        w.Write("control_written", control_written_);
        chip_.SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("control", control_);
        r.Read("control_written", control_written_);
        chip_.RestoreState(r);
    }

protected:
    uint32_t ChipSelect() const override { return kChipSelect; }

    void OnChipSelectReady() override {
        const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
        image_.Open(ResolveDeviceFile(cfg.device_name, cfg.storage_nand));
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            control_written_ = false;
            if (kind == ResetLineKind::Rtc) chip_.PowerUp();
        });
    }

    uint8_t SelectedReadByte(uint32_t addr) override {
        if (addr - kWindowBase != kOffData) HaltUnsupportedAccess("ReadByte", addr, 0);
        RequireSelectedChip("data read", addr);
        if (control_ & (kControlAle | kControlCle))
            emu_.Get<Fatal>().Die("%s: data read with the control latch at 0x%02X", kName, control_);
        return chip_.ReadData();
    }

    void SelectedWriteByte(uint32_t addr, uint8_t value) override {
        switch (addr - kWindowBase) {
            case kOffControl: WriteControl(addr, value); return;
            case kOffData:    break;
            default:          HaltUnsupportedAccess("WriteByte", addr, value);
        }
        if (!control_written_)
            emu_.Get<Fatal>().Die("%s: data write 0x%02X before the control latch is written",
                                  kName, value);
        if (control_ & kControlCeHigh) return;
        switch (control_ & (kControlAle | kControlCle)) {
            case kControlCle: chip_.Command(value);   return;
            case kControlAle: chip_.Address(value);   return;
            case 0u:          chip_.WriteData(value); return;
            default:
                emu_.Get<Fatal>().Die("%s: data write 0x%02X with CLE and ALE both set", kName, value);
        }
    }

private:
    void RequireSelectedChip(const char* op, uint32_t addr) {
        if (!control_written_ || (control_ & kControlCeHigh))
            emu_.Get<Fatal>().Die("%s: %s at 0x%08X with chip enable not asserted", kName, op, addr);
    }

    void WriteControl(uint32_t addr, uint8_t value) {
        if (value & ~kControlKnown) HaltUnsupportedAccess("WriteByte", addr, value);
        const bool was_selected = control_written_ && !(control_ & kControlCeHigh);
        control_         = value;
        control_written_ = true;
        chip_.SetWriteProtect(!(value & kControlWpHigh));
        if (was_selected && (value & kControlCeHigh)) chip_.Deselect();
    }

    SmallPageNandImage image_{emu_, kName, kNandGeometry.blocks};
    SmallPageNandFlash chip_{emu_, kName, kNandGeometry, image_};
    uint8_t            control_         = 0;
    bool               control_written_ = false;
};

}

REGISTER_SERVICE(NecCronos3eInternalFlash);

}
