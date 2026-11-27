#include "ti_bq27520_rom_mode.h"

#include "../../core/board_parts.h"
#include "ti_bq27520_flash.h"
#include "ti_bq27520_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <span>

namespace cerfcore {

namespace {

constexpr uint8_t kRegCommand       = 0x00u;
constexpr uint8_t kRegAddressLow    = 0x01u;
constexpr uint8_t kRegAddressHigh   = 0x02u;
constexpr uint8_t kRegData          = 0x04u;
constexpr uint8_t kRegChecksumLow   = 0x64u;
constexpr uint8_t kRegChecksumHigh  = 0x65u;
constexpr uint32_t kAddressBytes    = 2u;
constexpr uint32_t kInstructionRowBytes = 96u;

constexpr uint8_t kCmdProgramInstructionRow = 0x02u;
constexpr uint8_t kCmdEraseInstructionRow   = 0x03u;
constexpr uint8_t kCmdFirmwareSignature     = 0x05u;
constexpr uint8_t kCmdDataFlashSum          = 0x08u;
constexpr uint8_t kCmdProgramDataFlashRow   = 0x0Au;
constexpr uint8_t kCmdEraseDataFlash        = 0x0Cu;
constexpr uint8_t kCmdExit                  = 0x0Fu;

constexpr uint8_t kEraseKeyLow  = 0x83u;
constexpr uint8_t kEraseKeyHigh = 0xDEu;

constexpr uint8_t kStatusSuccess = 0x00u;

}

bool TiBq27520RomMode::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::TiBq27520);
}

void TiBq27520RomMode::OnReady() {
    active_ = !emu_.Get<TiBq27520Flash>().FirmwareValid();
}

void TiBq27520RomMode::Enter() {
    active_       = true;
    pointer_.EndSelect();
    written_.fill(0u);
    status_valid_ = false;
}

void TiBq27520RomMode::TxnStart() {
    pointer_.Start();
}

void TiBq27520RomMode::WriteByte(uint8_t byte) {
    if (pointer_.Select(byte)) {
        if (byte > kStatusRegister) {
            emu_.Get<Fatal>().Die("ti bq27520: the host addressed ROM-mode register 0x%02X", byte);
        }
        return;
    }
    const uint16_t reg = pointer_.Register();
    if (reg >= kStatusRegister) {
        emu_.Get<Fatal>().Die("ti bq27520: the host wrote 0x%02X to ROM-mode register 0x%02X", byte, reg);
    }
    regs_[reg]    = byte;
    written_[reg] = 1u;
    if (pointer_.Advance() == kRegChecksumHigh) Execute();
}

uint8_t TiBq27520RomMode::ReadByte() {
    const uint16_t reg = pointer_.Register();
    if (reg == kStatusRegister) {
        if (!status_valid_) {
            emu_.Get<Fatal>().Die("ti bq27520: the host read the ROM-mode status before any command");
        }
        pointer_.Advance();
        return status_;
    }
    if (reg > kStatusRegister || written_[reg] == 0u) {
        emu_.Get<Fatal>().Die("ti bq27520: the host read ROM-mode register 0x%02X, which holds no "
                              "written or computed value", reg);
    }
    return regs_[pointer_.Advance()];
}

uint16_t TiBq27520RomMode::SumRegisters(uint8_t first, uint32_t count) const {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t reg = first + i;
        if (written_[reg] == 0u) {
            emu_.Get<Fatal>().Die("ti bq27520: ROM command 0x%02X takes register 0x%02X, which the host "
                                  "never wrote", regs_[kRegCommand], reg);
        }
        sum += regs_[reg];
    }
    return static_cast<uint16_t>(sum);
}

void TiBq27520RomMode::VerifyChecksum(uint8_t command, uint16_t operand_sum) const {
    const uint16_t expected = static_cast<uint16_t>(command + operand_sum);
    const uint16_t sent     = static_cast<uint16_t>(regs_[kRegChecksumLow] | (regs_[kRegChecksumHigh] << 8));
    if (sent != expected) {
        emu_.Get<Fatal>().Die("ti bq27520: ROM command 0x%02X carries checksum 0x%04X over operands "
                              "that sum to 0x%04X, and a failed command status is not modeled",
                              command, sent, expected);
    }
}

void TiBq27520RomMode::SetResult(uint8_t reg, uint8_t value) {
    regs_[reg]    = value;
    written_[reg] = 1u;
}

void TiBq27520RomMode::Execute() {
    if (written_[kRegCommand] == 0u || written_[kRegChecksumLow] == 0u) {
        emu_.Get<Fatal>().Die("ti bq27520: the host completed a ROM command without its command or "
                              "checksum low byte");
    }
    auto&         flash   = emu_.Get<TiBq27520Flash>();
    const uint8_t command = regs_[kRegCommand];
    switch (command) {
    case kCmdEraseInstructionRow: {
        VerifyChecksum(command, SumRegisters(kRegAddressLow, kAddressBytes));
        flash.EraseInstructionRow(regs_[kRegAddressLow] | (regs_[kRegAddressHigh] << 8));
        break;
    }
    case kCmdEraseDataFlash: {
        VerifyChecksum(command, SumRegisters(kRegData, kAddressBytes));
        if (regs_[kRegData] != kEraseKeyLow || regs_[kRegData + 1u] != kEraseKeyHigh) {
            emu_.Get<Fatal>().Die("ti bq27520: data flash erase with key 0x%02X%02X", regs_[kRegData],
                                  regs_[kRegData + 1u]);
        }
        flash.EraseDataFlash();
        break;
    }
    case kCmdProgramDataFlashRow: {
        const uint16_t sum = static_cast<uint16_t>(SumRegisters(kRegAddressLow, 1u) +
                                                   SumRegisters(kRegData, kBq27520DataFlashRowBytes));
        VerifyChecksum(command, sum);
        flash.ProgramDataFlashRow(regs_[kRegAddressLow],
                                  std::span<const uint8_t, kBq27520DataFlashRowBytes>(
                                      regs_.data() + kRegData, kBq27520DataFlashRowBytes));
        break;
    }
    case kCmdDataFlashSum: {
        VerifyChecksum(command, 0u);
        const uint16_t sum = flash.DataFlashSum();
        SetResult(kRegData, static_cast<uint8_t>(sum));
        SetResult(kRegData + 1u, static_cast<uint8_t>(sum >> 8));
        break;
    }
    case kCmdProgramInstructionRow: {
        const uint16_t sum = static_cast<uint16_t>(SumRegisters(kRegAddressLow, kAddressBytes) +
                                                   SumRegisters(kRegData, kInstructionRowBytes));
        VerifyChecksum(command, sum);
        flash.ProgramInstructionRow(regs_[kRegAddressLow] | (regs_[kRegAddressHigh] << 8));
        break;
    }
    case kCmdFirmwareSignature: {
        VerifyChecksum(command, 0u);
        const auto signature = flash.FirmwareSignature();
        for (uint32_t i = 0; i < kBq27520SignatureBytes; ++i) {
            SetResult(static_cast<uint8_t>(kRegData + i), signature[i]);
        }
        break;
    }
    case kCmdExit:
        VerifyChecksum(command, 0u);
        if (!flash.FirmwareValid()) {
            emu_.Get<Fatal>().Die("ti bq27520: the host left ROM mode with instruction flash row 0 erased");
        }
        active_ = false;
        break;
    default:
        emu_.Get<Fatal>().Die("ti bq27520: ROM command 0x%02X is not modeled", command);
    }
    status_       = kStatusSuccess;
    status_valid_ = true;
}

void TiBq27520RomMode::SaveState(StateWriter& w) {
    w.Write<uint8_t>("bq27520_rom_active", active_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_rom_pointer_pending", pointer_.Pending() ? 1u : 0u);
    w.Write<uint8_t>("bq27520_rom_pointer", static_cast<uint8_t>(pointer_.Register()));
    w.WriteBytes("bq27520_rom_regs", regs_.data(), regs_.size());
    w.WriteBytes("bq27520_rom_written", written_.data(), written_.size());
    w.Write<uint8_t>("bq27520_rom_status_valid", status_valid_ ? 1u : 0u);
    w.Write<uint8_t>("bq27520_rom_status", status_);
}

void TiBq27520RomMode::RestoreState(StateReader& r) {
    uint8_t active = 0u, pending = 0u, pointer = 0u, valid = 0u;
    r.Read("bq27520_rom_active", active);
    r.Read("bq27520_rom_pointer_pending", pending);
    r.Read("bq27520_rom_pointer", pointer);
    r.ReadBytes("bq27520_rom_regs", regs_.data(), regs_.size());
    r.ReadBytes("bq27520_rom_written", written_.data(), written_.size());
    r.Read("bq27520_rom_status_valid", valid);
    r.Read("bq27520_rom_status", status_);
    active_       = active != 0u;
    status_valid_ = valid != 0u;
    pointer_.Restore(pointer, true, pending != 0u);
}

REGISTER_SERVICE(TiBq27520RomMode);

}
