#include "stm32f2_flash_interface.h"
#include "stm32f2_register.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kAcr = 0x00u;
constexpr uint32_t kKeyr = 0x04u;
constexpr uint32_t kOptkeyr = 0x08u;
constexpr uint32_t kSr = 0x0Cu;
constexpr uint32_t kCr = 0x10u;
constexpr uint32_t kOptcr = 0x14u;

constexpr uint32_t kAcrReadWrite = 0x00000707u;
constexpr uint32_t kIcen = 1u << 9;
constexpr uint32_t kDcen = 1u << 10;
constexpr uint32_t kIcrst = 1u << 11;
constexpr uint32_t kDcrst = 1u << 12;

constexpr uint32_t kKey1 = 0x45670123u;
constexpr uint32_t kKey2 = 0xCDEF89ABu;
constexpr uint32_t kOptKey1 = 0x08192A3Bu;
constexpr uint32_t kOptKey2 = 0x4C5D6E7Fu;

constexpr uint32_t kSrClearable = 0x000000F3u;
constexpr uint32_t kPgaerr = 1u << 5;
constexpr uint32_t kPgperr = 1u << 6;
constexpr uint32_t kPgserr = 1u << 7;

constexpr uint32_t kPg = 1u << 0;
constexpr uint32_t kSer = 1u << 1;
constexpr uint32_t kMer = 1u << 2;
constexpr uint32_t kStrt = 1u << 16;
constexpr uint32_t kEopie = 1u << 24;
constexpr uint32_t kErrie = 1u << 25;
constexpr uint32_t kLock = 1u << 31;
constexpr uint32_t kCrWritable = 0x8301037Fu;

constexpr uint32_t kOptLock = 1u << 0;
constexpr uint32_t kOptStrt = 1u << 1;

constexpr uint32_t kSectorOffset[6] = {0x00000u, 0x04000u, 0x08000u, 0x0C000u, 0x10000u, 0x20000u};
constexpr uint32_t kSectorBytes[6] = {0x04000u, 0x04000u, 0x04000u, 0x04000u, 0x10000u, 0x20000u};

}

uint32_t FlashInterface::Read(uint32_t offset, uint32_t) {
    uint32_t value = 0u;
    switch (offset & ~3u) {
    case kAcr: value = acr_; break;
    case kKeyr: case kOptkeyr: value = 0u; break;
    case kSr: value = sr_; break;
    case kCr: value = cr_; break;
    case kOptcr: value = optcr_; break;
    default: mcu_.Fault("STM32F2 FLASH: read of an unmodelled register", kBase + offset);
    }
    return value >> ((offset & 3u) * 8u);
}

void FlashInterface::Write(uint32_t offset, uint32_t size, uint32_t value) {
    switch (offset & ~3u) {
    case kAcr: {
        const uint32_t next = MergeRegisterWrite(acr_, offset, size, value);
        if ((next & ~(kAcrReadWrite | kIcrst | kDcrst)) != 0u)
            mcu_.Fault("STM32F2 FLASH: reserved ACR bits are set", next);
        if (((next & kIcrst) != 0u && (next & kIcen) != 0u) || ((next & kDcrst) != 0u && (next & kDcen) != 0u))
            mcu_.Fault("STM32F2 FLASH: cache reset while the cache is enabled", next);
        acr_ = next & kAcrReadWrite;
        CheckLatency();
        return;
    }
    case kKeyr:
        if (size != 4u) mcu_.Fault("STM32F2 FLASH: FLASH_KEYR takes word access", kBase + offset);
        WriteKey(value);
        return;
    case kOptkeyr:
        if (size != 4u) mcu_.Fault("STM32F2 FLASH: FLASH_OPTKEYR takes word access", kBase + offset);
        WriteOptionKey(value);
        return;
    case kSr: sr_ &= ~(MergeRegisterWrite(0u, offset, size, value) & kSrClearable); return;
    case kCr: {
        if ((cr_ & kLock) != 0u) return;
        const uint32_t merged = MergeRegisterWrite(cr_, offset, size, value);
        if ((merged & ~kCrWritable) != 0u) mcu_.Fault("STM32F2 FLASH: reserved CR bits are set", merged);
        const uint32_t next = merged & kCrWritable;
        if ((next & (kEopie | kErrie)) != 0u)
            mcu_.Fault("STM32F2 FLASH: end of operation and error interrupts are not modelled", next);
        cr_ = next;
        if ((cr_ & kLock) != 0u) key_stage_ = 0u;
        if ((cr_ & kStrt) != 0u) Start();
        return;
    }
    case kOptcr: {
        if ((optcr_ & kOptLock) != 0u) return;
        const uint32_t merged = MergeRegisterWrite(optcr_, offset, size, value);
        if ((merged & ~0x0FFFFFEFu) != 0u) mcu_.Fault("STM32F2 FLASH: reserved OPTCR bits are set", merged);
        optcr_ = merged & 0x0FFFFFEFu;
        if ((optcr_ & kOptStrt) != 0u)
            mcu_.Fault("STM32F2 FLASH: option byte programming is not modelled", optcr_);
        if ((optcr_ & kOptLock) != 0u) option_key_stage_ = 0u;
        return;
    }
    default: mcu_.Fault("STM32F2 FLASH: write of an unmodelled register", kBase + offset);
    }
}

void FlashInterface::WriteKey(uint32_t value) {
    if ((cr_ & kLock) == 0u || (key_stage_ == 0u && value != kKey1) || (key_stage_ == 1u && value != kKey2))
        mcu_.Fault("STM32F2 FLASH: wrong FLASH_KEYR sequence (bus error)", value);
    if (key_stage_++ == 1u) {
        cr_ &= ~kLock;
        key_stage_ = 0u;
    }
}

void FlashInterface::WriteOptionKey(uint32_t value) {
    if ((optcr_ & kOptLock) == 0u || (option_key_stage_ == 0u && value != kOptKey1) ||
        (option_key_stage_ == 1u && value != kOptKey2))
        mcu_.Fault("STM32F2 FLASH: wrong FLASH_OPTKEYR sequence (bus error)", value);
    if (option_key_stage_++ == 1u) {
        optcr_ &= ~kOptLock;
        option_key_stage_ = 0u;
    }
}

void FlashInterface::Start() {
    cr_ &= ~kStrt;
    const uint32_t psize = (cr_ >> 8) & 3u;
    if (psize == 3u) mcu_.Fault("STM32F2 FLASH: x64 erase needs VPP, which is not modelled", cr_);
    if ((cr_ & kMer) != 0u) {
        EraseRange(0u, Mcu::kFlashBytes);
        return;
    }
    if ((cr_ & kSer) == 0u) mcu_.Fault("STM32F2 FLASH: STRT with neither SER nor MER", cr_);
    const uint32_t sector = (cr_ >> 3) & 0xFu;
    if (sector >= 6u) mcu_.Fault("STM32F2 FLASH: erase of a sector beyond the 256 Kbyte main memory", sector);
    EraseRange(kSectorOffset[sector], kSectorBytes[sector]);
}

void FlashInterface::ClockChanged() {
    CheckLatency();
}

void FlashInterface::CheckLatency() {
    constexpr uint64_t kStepHz = 30000000u;
    const uint64_t hclk = rcc_.HclkHz();
    if (hclk > 4u * kStepHz) mcu_.Fault("STM32F2 FLASH: HCLK above 120 MHz", static_cast<uint32_t>(hclk));
    if ((acr_ & 7u) < (hclk - 1u) / kStepHz) mcu_.Fault("STM32F2 FLASH: LATENCY below Table 3 for HCLK", acr_);
}

void FlashInterface::EraseRange(uint32_t offset, uint32_t bytes) {
    auto& flash = mcu_.Flash();
    std::fill(flash.begin() + offset, flash.begin() + offset + bytes, uint8_t{0xFFu});
    mcu_.InvalidateCode(Mcu::kFlashBase + offset, bytes);
    modified_ = true;
}

void FlashInterface::Program(uint32_t address, uint32_t size, uint32_t value) {
    if ((cr_ & kLock) != 0u || (cr_ & kPg) == 0u) {
        sr_ |= kPgserr;
        return;
    }
    const uint32_t psize = (cr_ >> 8) & 3u;
    if (psize == 3u) mcu_.Fault("STM32F2 FLASH: x64 programming is not modelled", address);
    if (size != (1u << psize)) {
        sr_ |= kPgperr;
        return;
    }
    if ((address & 15u) + size > 16u) {
        sr_ |= kPgaerr;
        return;
    }
    uint8_t* target = mcu_.ProgramTarget(address, size);
    for (uint32_t i = 0u; i < size; ++i) target[i] &= static_cast<uint8_t>(value >> (8u * i));
    mcu_.InvalidateCode(address, size);
    modified_ = true;
}

void FlashInterface::SaveState(StateWriter& writer) const {
    writer.Write("stm32f2_flash_acr", acr_);
    writer.Write("stm32f2_flash_sr", sr_);
    writer.Write("stm32f2_flash_cr", cr_);
    writer.Write("stm32f2_flash_optcr", optcr_);
    writer.Write("stm32f2_flash_key_stage", key_stage_);
    writer.Write("stm32f2_flash_option_key_stage", option_key_stage_);
}

void FlashInterface::RestoreState(StateReader& reader) {
    reader.Read("stm32f2_flash_acr", acr_);
    reader.Read("stm32f2_flash_sr", sr_);
    reader.Read("stm32f2_flash_cr", cr_);
    reader.Read("stm32f2_flash_optcr", optcr_);
    reader.Read("stm32f2_flash_key_stage", key_stage_);
    reader.Read("stm32f2_flash_option_key_stage", option_key_stage_);
}

}

}
