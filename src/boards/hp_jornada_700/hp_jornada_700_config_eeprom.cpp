#include "../../peripherals/intel_sa1111/sa1111_ssp_device.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/host_file_read.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

class HpJornada700ConfigEeprom : public Sa1111SspDevice {
public:
    using Sa1111SspDevice::Sa1111SspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind != ResetLineKind::Rtc) return;
            phase_     = Phase::Cmd;
            addr_      = 0;
            wel_       = false;
            pending_   = 0u;
            page_mask_ = 0u;
            bit_       = 0u;
            in_byte_   = 0u;
            out_byte_  = 0xFF;
        });
        const auto& cfg = emu_.Get<DeviceConfig>();
        if (cfg.rom_eeprom.empty()) return;
        const std::string  path = ResolveDeviceFile(cfg.device_name, cfg.rom_eeprom);
        const std::string  key  = CfgKeyPath({config_key::kRom, config_key::kEeprom});
        const HostFileRead read = ReadHostFileIfPresent(path, eeprom_);
        if (read == HostFileRead::kUnreadable)
            emu_.Get<Fatal>().Die("HpJornada700ConfigEeprom: %s %s cannot be read", key.c_str(), path.c_str());
        if (read == HostFileRead::kAbsent) {
            LOG(Caution, "HpJornada700ConfigEeprom: %s '%s' not found at %s - EEPROM reads "
                "float 0xFF (OAL uses default config)\n", key.c_str(), cfg.rom_eeprom.c_str(), path.c_str());
            return;
        }
        LOG(Boot, "HpJornada700ConfigEeprom: loaded config EEPROM %s (%lld bytes)\n",
            cfg.rom_eeprom.c_str(), static_cast<long long>(eeprom_.size()));
    }

    uint32_t Shift(uint32_t out, uint32_t bits) override {
        uint32_t in = 0u;
        for (uint32_t i = bits; i-- > 0u;) {
            if (bit_ == 0u) out_byte_ = OutputByte();
            in       = (in << 1) | ((out_byte_ >> (7u - bit_)) & 1u);
            in_byte_ = static_cast<uint8_t>((in_byte_ << 1) | ((out >> i) & 1u));
            if (++bit_ == 8u) {
                InputByte(in_byte_);
                bit_     = 0u;
                in_byte_ = 0u;
            }
        }
        return in;
    }

    void EndTransaction() override {
        if (bit_ == 0u) Complete();
        phase_     = Phase::Cmd;
        page_mask_ = 0u;
        bit_       = 0u;
        in_byte_   = 0u;
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint64_t>("eeprom_count", eeprom_.size());
        if (!eeprom_.empty()) w.WriteBytes("eeprom", eeprom_.data(), eeprom_.size());
        w.Write("phase", phase_);
        w.Write("addr", addr_);
        w.Write("block_protect", block_protect_);
        w.Write("wel", wel_);
        w.Write("pending", pending_);
        w.WriteBytes("page_data", page_data_, sizeof(page_data_));
        w.Write("page_mask", page_mask_);
        w.Write("ee_bit", bit_);
        w.Write("ee_in_byte", in_byte_);
        w.Write("ee_out_byte", out_byte_);
    }

    void RestoreState(StateReader& r) override {
        uint64_t n = 0;
        r.Read("eeprom_count", n);
        eeprom_.resize(static_cast<size_t>(n));
        if (n) r.ReadBytes("eeprom", eeprom_.data(), static_cast<size_t>(n));
        r.Read("phase", phase_);
        r.Read("addr", addr_);
        r.Read("block_protect", block_protect_);
        r.Read("wel", wel_);
        r.Read("pending", pending_);
        r.ReadBytes("page_data", page_data_, sizeof(page_data_));
        r.Read("page_mask", page_mask_);
        r.Read("ee_bit", bit_);
        r.Read("ee_in_byte", in_byte_);
        r.Read("ee_out_byte", out_byte_);
    }

private:
    enum class Phase { Cmd, ReadAddr, ReadData, WriteAddr, WriteData, RdsrData, WrsrData, WrsrDone,
                       Wren, Wrdi, Drain };

    static constexpr uint8_t kBlockProtectMask = 0x0Cu;
    static constexpr uint8_t kStatusWel        = 0x02u;
    static constexpr uint8_t kPageMask         = 0x0Fu;

    uint8_t EepromByte(uint32_t a) const { return a < eeprom_.size() ? eeprom_[a] : 0xFFu; }

    uint8_t OutputByte() const {
        if (phase_ == Phase::ReadData) return EepromByte(addr_ & 0xFFu);
        if (phase_ == Phase::RdsrData) return block_protect_ | (wel_ ? kStatusWel : 0u);
        return 0xFFu;
    }

    uint32_t FirstProtected() const {
        switch (block_protect_ >> 2) {
            case 0u: return 0x100u;
            case 1u: return 0xC0u;
            case 2u: return 0x80u;
        }
        return 0x00u;
    }

    void Complete() {
        switch (phase_) {
            case Phase::Wren: wel_ = true; break;
            case Phase::Wrdi: wel_ = false; break;
            case Phase::WrsrDone:
                if (!wel_) break;
                block_protect_ = pending_ & kBlockProtectMask;
                wel_           = false;
                break;
            case Phase::WriteData:
                if (!wel_ || page_mask_ == 0u) break;
                CommitPage();
                wel_ = false;
                break;
            default: break;
        }
    }

    void CommitPage() {
        const uint32_t page = addr_ & 0xF0u;
        for (uint32_t i = 0; i <= kPageMask; ++i) {
            const uint32_t a = page | i;
            if ((page_mask_ & (1u << i)) == 0u || a >= FirstProtected() || a >= eeprom_.size()) continue;
            eeprom_[a] = page_data_[i];
            LOG(Periph, "[HpJornada700ConfigEeprom] EEPROM write [0x%02X]=0x%02X\n", a, page_data_[i]);
        }
    }

    /* jornada720 jornada720.bin hplib.dll sub_EA1C40: 1 WRSR, 2 WRITE, 3 READ, 4 WRDI, 5 RDSR,
       6 WREN. */
    void InputByte(uint8_t tx) {
        switch (phase_) {
            case Phase::Cmd:
                if ((tx & 0xF0u) != 0u) Die(tx);
                switch (tx & 0x07u) {
                    case 0x01: phase_ = Phase::WrsrData; break;
                    case 0x02: phase_ = Phase::WriteAddr; break;
                    case 0x03: phase_ = Phase::ReadAddr; break;
                    case 0x04: phase_ = Phase::Wrdi; break;
                    case 0x05: phase_ = Phase::RdsrData; break;
                    case 0x06: phase_ = Phase::Wren; break;
                    default: Die(tx);
                }
                break;
            case Phase::ReadAddr:
                addr_  = tx;
                phase_ = Phase::ReadData;
                break;
            case Phase::ReadData:
                addr_++;
                break;
            case Phase::WriteAddr:
                addr_  = tx;
                phase_ = Phase::WriteData;
                break;
            case Phase::WriteData:
                page_data_[addr_ & kPageMask] = tx;
                page_mask_ = static_cast<uint16_t>(page_mask_ | (1u << (addr_ & kPageMask)));
                addr_      = (addr_ & ~kPageMask) | ((addr_ + 1u) & kPageMask);
                break;
            case Phase::WrsrData:
                pending_ = tx;
                phase_   = Phase::WrsrDone;
                break;
            case Phase::WrsrDone:
            case Phase::Wren:
            case Phase::Wrdi:
                phase_ = Phase::Drain;
                break;
            case Phase::RdsrData:
            case Phase::Drain:
                break;
        }
    }

    [[noreturn]] void Die(uint8_t tx) {
        emu_.Get<Fatal>().Die("HpJornada700ConfigEeprom: byte 0x%02X is not a 25xx instruction", tx);
    }

    std::vector<uint8_t> eeprom_;
    Phase                phase_         = Phase::Cmd;
    uint32_t             addr_          = 0;
    uint8_t              block_protect_ = 0;
    bool                 wel_           = false;
    uint8_t              pending_       = 0;
    uint8_t              page_data_[16] = {};
    uint16_t             page_mask_     = 0;
    uint32_t             bit_           = 0;
    uint8_t              in_byte_       = 0;
    uint8_t              out_byte_      = 0xFF;
};

}

REGISTER_SERVICE_AS(HpJornada700ConfigEeprom, Sa1111SspDevice);

}
