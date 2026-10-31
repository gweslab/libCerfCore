#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "s3c2410_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../peripherals/nand/small_page_nand_flash.h"
#include "s3c2410_nand_part.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <optional>

namespace cerfcore {

namespace {

/* S3C2410A UM pp. 6-6..6-8 "SPECIAL FUNCTION REGISTERS": NFCONF at 0x4E000000,
   NFCMD 0x4E000004, NFADDR 0x4E000008, NFDATA 0x4E00000C, NFSTAT 0x4E000010,
   NFECC 0x4E000014. */
constexpr uint32_t kBase = 0x4E000000u;
constexpr uint32_t kSpan = 0x18u;

constexpr uint32_t kOffConf = 0x00u;
constexpr uint32_t kOffCmd  = 0x04u;
constexpr uint32_t kOffAddr = 0x08u;
constexpr uint32_t kOffData = 0x0Cu;
constexpr uint32_t kOffStat = 0x10u;

/* UM p. 6-6 NFCONF: [15] NAND flash controller enable, [12] initialize ECC,
   [11] nFCE (0 = L active), [10:8] TACLS, [6:4] TWRPH0, [2:0] TWRPH1;
   [14:13], [7] and [3] Reserved. */
constexpr uint32_t kConfWritable = 0x00009F77u;
constexpr uint32_t kConfNfce     = 1u << 11;
constexpr uint32_t kConfEnable   = 1u << 15;

/* UM p. 6-8 NFSTAT: [0] RnB, "1 = NAND flash memory ready to operate". */
constexpr uint32_t kStatReady = 1u << 0;

class S3C2410NandFlash : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::S3c2410;
    }

    void OnReady() override {
        if (auto* part = emu_.TryGet<S3c2410NandPart>())
            chip_.emplace(emu_, "S3C2410 NAND", part->Geometry(), part->Store());
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            conf_ &= kConfNfce;
            if (chip_ && kind == ResetLineKind::Rtc) chip_->PowerUp();
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSpan; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("conf", conf_);
        if (chip_) chip_->SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("conf", conf_);
        if (chip_) chip_->RestoreState(r);
    }

private:
    bool ChipSelected(const char* op, uint32_t addr) {
        if (!chip_)
            emu_.Get<Fatal>().Die("S3C2410 NAND: %s at 0x%08X with no NAND part on this board", op, addr);
        if (!(conf_ & kConfEnable))
            emu_.Get<Fatal>().Die("S3C2410 NAND: %s at 0x%08X with NFCONF enable clear (0x%04X)",
                                  op, addr, conf_);
        return !(conf_ & kConfNfce);
    }

    uint32_t                          conf_ = 0;
    std::optional<SmallPageNandFlash> chip_;
};

void S3C2410NandFlash::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
    if (off != kOffConf)
        HaltUnsupportedAccess("WriteWord", addr, value);
    const bool ce_rises = !(conf_ & kConfNfce) && (value & kConfNfce);
    conf_ = value & kConfWritable;
    if (chip_ && ce_rises) chip_->Deselect();
}

uint32_t S3C2410NandFlash::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
    switch (off) {
        case kOffConf: return conf_;
        case kOffStat:
            if (!chip_)
                emu_.Get<Fatal>().Die("S3C2410 NAND: NFSTAT read with no NAND part on this board");
            return kStatReady;
        default:
            HaltUnsupportedAccess("ReadWord", addr, 0);
    }
}

void S3C2410NandFlash::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    if (off != kOffCmd && off != kOffAddr && off != kOffData)
        HaltUnsupportedAccess("WriteByte", addr, value);
    if (!ChipSelected("WriteByte", addr)) return;
    switch (off) {
        case kOffCmd:  chip_->Command(value);   return;
        case kOffAddr: chip_->Address(value);   return;
        default:       chip_->WriteData(value); return;
    }
}

uint8_t S3C2410NandFlash::ReadByte(uint32_t addr) {
    if (addr - kBase != kOffData)
        HaltUnsupportedAccess("ReadByte", addr, 0);
    if (!ChipSelected("ReadByte", addr))
        emu_.Get<Fatal>().Die("S3C2410 NAND: NFDATA read with nFCE inactive (NFCONF 0x%04X)", conf_);
    return chip_->ReadData();
}

}  /* namespace */

REGISTER_SERVICE(S3C2410NandFlash);

}
