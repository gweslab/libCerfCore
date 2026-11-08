#include "../../socs/i2c_slave.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include "../../socs/imx51/imx51_gpio4.h"
#include "../../execution/engine_bridge.h"
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_translator.h>
#include "../../peripherals/cirrus_cs42448/cs42448_codec.h"
#include "../../peripherals/i2c/i2c_bit_bang_target.h"
#include "../../peripherals/ti_tsc2003/ti_tsc2003_touch.h"

#include <cstdint>
#include <cstdio>

namespace cerfcore {

namespace {

/* Ford SYNC2 "HIC1:" = software-I2C bit-banged over GPIO4 (SCL=16/SDA=17,
   hsi2c.dll), NOT the HS-I2C at 0x70038000; slaves camera 0x20 / DM 0x38 /
   CS42448 0x48 / TSC2003 0x49, decoded off GPIO4's write-observer. */
constexpr uint32_t kScl = 16u;
constexpr uint32_t kSda = 17u;

/* CS42448 audio codec (wavedev2_cs42448.dll CS42448CodecContext). */
constexpr uint8_t kCs42448Addr = 0x48u;

/* Display-module panel-detect slave (DisplayModule.dll DMX_Init sub_C0E326CC ->
   DMI2CXfer sub_C0E31D08: a single 6-byte read from 0x38). */
constexpr uint8_t kDmAddr = 0x38u;

/* TSC2003 touch-screen controller (touch_tsc2003.dll TSCI2CInit). */
constexpr uint8_t kTsc2003Addr = 0x49u;

class FordSyncGen2Hic1I2c : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }
    void OnReady() override {
        gpio_ = &emu_.Get<Imx51Gpio4>();
        tsc2003_ = &emu_.Get<Tsc2003Touch>();
        /* Both lines idle high (open-drain pull-up). SCL especially: the slave
           never clock-stretches, so the master's WaitForSCK readback (hsi2c
           releases GPIO4.16 then waits for it to read high) must read high -
           else it times out (0x102) every clock and the I2C crawls. */
        gpio_->SetInputPin(kScl, true);
        gpio_->SetInputPin(kSda, true);
        gpio_->AddWriteObserver([this] { OnEdge(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            cs42448_.Reset();
            map_set_   = false;
            auto_incr_ = false;
            dm_byte_   = 0;
            reg_       = 0;
        });
        gpio_->SetObserverState([this](StateWriter& w) { SaveBus(w); },
                                [this](StateReader& r) { RestoreBus(r); });
    }

    bool MatchesAddress(uint8_t slave_addr) const override {
        return slave_addr == kCs42448Addr || slave_addr == kDmAddr || slave_addr == kTsc2003Addr;
    }

    void TxnStart(uint8_t) override {
        map_set_ = false;
        dm_byte_ = 0u;
    }

    void TxnWriteByte(uint8_t slave_addr, uint8_t b) override {
        if (slave_addr == kTsc2003Addr) {
            tsc2003_->WriteCommand(b);
        } else if (slave_addr == kDmAddr) {
        } else if (!map_set_) {
            reg_ = b & 0x7Fu;
            auto_incr_ = (b & 0x80u) != 0u;
            map_set_ = true;
        } else {
            cs42448_.WriteReg(reg_, b);
            if (auto_incr_) reg_ = static_cast<uint8_t>((reg_ + 1u) & 0x7Fu);
        }
    }

    uint8_t TxnReadByte(uint8_t slave_addr) override {
        if (slave_addr == kDmAddr) return DmReadByte(++dm_byte_);
        if (slave_addr == kTsc2003Addr) return tsc2003_->ReadResultByte();
        const uint8_t v = cs42448_.ReadReg(reg_);
        if (auto_incr_) reg_ = static_cast<uint8_t>((reg_ + 1u) & 0x7Fu);
        return v;
    }

private:
    void DriveSda(bool level) { gpio_->SetInputPin(kSda, level); }

    void SaveBus(StateWriter& w) {
        target_.SaveState(w);
        w.Write("hic1_map_set", static_cast<uint8_t>(map_set_));
        w.Write("hic1_auto_incr", static_cast<uint8_t>(auto_incr_));
        w.Write("hic1_dm_byte", dm_byte_);
        w.Write("hic1_reg", reg_);
        cs42448_.SaveState(w);
        tsc2003_->SaveState(w);
    }

    void RestoreBus(StateReader& r) {
        uint8_t v = 0;
        target_.RestoreState(r);
        r.Read("hic1_map_set", v);
        map_set_ = v != 0u;
        r.Read("hic1_auto_incr", v);
        auto_incr_ = v != 0u;
        r.Read("hic1_dm_byte", dm_byte_);
        r.Read("hic1_reg", reg_);
        cs42448_.RestoreState(r);
        tsc2003_->RestoreState(r);
    }

    [[noreturn]] void FatalUnmodelledSlave(uint8_t addr, bool read) const {
        cerfengine::Engine& engine = emu_.Get<EngineBridge>().Engine();
        const cerfengine::ArmCpuState* st = engine.Get<cerfengine::ArmCpu>().State();
        const uint32_t sp = st->gprs[cerfengine::ArmGpr::kR13];
        cerfengine::ArmTranslator& probe = engine.Get<cerfengine::ArmTranslator>();
        auto rd = [&](uint32_t va) -> uint32_t {
            uint8_t* p = probe.PeekVaToHost(va);
            return p ? cerf::le::U32(p) : 0u;
        };
        char chain[320];
        int o = 0;
        chain[0] = 0;
        uint32_t logged = 0;
        for (uint32_t i = 0; i < 512u && logged < 32u; ++i) {
            const uint32_t w = rd(sp + i * 4u);
            if (w < 0xC0000000u || w >= 0xC2000000u) continue;
            if ((rd(w - 4u) >> 24) != 0xEBu) continue;
            if (o < static_cast<int>(sizeof(chain)) - 9)
                o += std::snprintf(chain + o, sizeof(chain) - o, "%08X ", w);
            ++logged;
        }
        emu_.Get<Fatal>().Die(
            "[HIC1] unmodelled I2C slave=0x%02X rw=%u pc=0x%08X lr=0x%08X "
            "sp=0x%08X chain: %s",
            addr, static_cast<unsigned>(read), st->gprs[cerfengine::ArmGpr::kR15],
            st->gprs[cerfengine::ArmGpr::kR14], sp, chain);
    }

    /* DisplayModule.dll DMIntHandlerThread sub_C09B2178: byte1/byte3-hi ->
       Buffer[1]=UncalX, byte2/byte3-lo -> Buffer[2]=UncalY (WriteMsgQueue), fed as
       TouchPanelCalibrateAPoint(UncalX,UncalY) so byte1/byte3-hi carries AdcX. byte5
       (v&7)=DM8(2) else DMX_Init raises DisplaySystemError, (v&8)=touch. */
    uint8_t DmReadByte(uint8_t byte) const {
        const bool pen = tsc2003_->PenDown();
        const uint16_t x = tsc2003_->AdcX();
        const uint16_t y = tsc2003_->AdcY();
        switch (byte) {
            case 1u: return pen ? 0x83u : 0x00u;            /* pending|touch|pen */
            case 2u: return pen ? static_cast<uint8_t>(x >> 4) : 0x00u;
            case 3u: return pen ? static_cast<uint8_t>(y >> 4) : 0x00u;
            case 4u: return pen ? static_cast<uint8_t>(((x & 0xFu) << 4) | (y & 0xFu))
                                : 0x00u;
            case 5u: return 0x00u;                          /* error flags */
            case 6u: return 0x0Au;                          /* DM8 + touch */
            default: return 0x00u;
        }
    }

    void OnEdge() {
        /* Open-drain wired-AND: a line is high only if released (input or
           output-high) AND nothing pulls it low; the slave's ACK pull is in the
           same input_level_ the master reads back, so both lines read symmetrically. */
        const bool scl = (!gpio_->PinIsOutput(kScl) || gpio_->PinOutLevel(kScl)) &&
                         gpio_->PinInputLevel(kScl);
        const bool sda = (!gpio_->PinIsOutput(kSda) || gpio_->PinOutLevel(kSda)) &&
                         gpio_->PinInputLevel(kSda);
        target_.OnLines(scl, sda);
    }

    Imx51Gpio4* gpio_ = nullptr;
    Tsc2003Touch* tsc2003_ = nullptr;
    Cs42448Codec cs42448_;
    I2cBitBangTarget target_{
        [this](bool released) { DriveSda(released); },
        { this },
        [this](uint8_t addr, bool read) { FatalUnmodelledSlave(addr, read); },
    };
    bool map_set_ = false;
    bool auto_incr_ = false;
    uint8_t  dm_byte_ = 0;
    uint8_t  reg_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(FordSyncGen2Hic1I2c);

}
