#include "vr4121_dram_decode.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/page_table_builder.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "vr4121_bcu_board.h"
#include "vr4121_bcu_regs.h"
#include "vr4121_id.h"

#include <cerfcore/cpu/emulated_memory.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace cerfcore {

REGISTER_SERVICE(Vr4121DramDecode);

namespace {

using namespace vr4121_bcu;

constexpr uint32_t kBanks = 4u;

constexpr uint32_t kMb2  = 0x00200000u;
constexpr uint32_t kMb4  = 0x00400000u;
constexpr uint32_t kMb8  = 0x00800000u;
constexpr uint32_t kMb16 = 0x01000000u;
constexpr uint32_t kMb32 = 0x02000000u;

bool ExpansionBank(bool dbus32, uint32_t bank) { return dbus32 && bank >= 2u; }

std::string Format(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

}

bool Vr4121DramDecode::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Vr4121;
}

void Vr4121DramDecode::OnReady() {
    const auto wiring = emu_.Get<Vr4121BcuBoard>().DramWiring();
    if (!wiring) return;

    const auto regions = emu_.Get<PageTableBuilder>().CachedDramRegions();
    size_t k = 0;
    for (uint32_t bank = 0; bank < kBanks; ++bank) {
        const uint32_t bytes = wiring->bank_chip_bytes[bank];
        if (bytes == 0u) continue;
        if (k >= regions.size() || regions[k].size != bytes) {
            emu_.Get<Fatal>().Die("Vr4121DramDecode: bank %u chip 0x%X has no matching "
                                  "cached DRAM region", bank, bytes);
        }
        chips_.push_back({ bank, regions[k].pa_base, bytes });
        ++k;
    }
    if (k != regions.size()) {
        emu_.Get<Fatal>().Die("Vr4121DramDecode: %zu cached DRAM regions, %zu populated "
                              "banks", regions.size(), k);
    }
}

uint64_t Vr4121DramDecode::Signature(uint16_t cnt1, uint16_t cnt3, uint16_t ramsize) {
    const bool rd64d = (cnt1 & kCnt1Rd64d) != 0u;
    return (uint64_t{cnt1} & (kCnt1Dram64 | kCnt1Rd64d)) |
           (uint64_t{static_cast<uint16_t>(cnt3 & kCnt3ExtFields)} << 16) |
           (uint64_t{rd64d ? static_cast<uint16_t>(ramsize & kRamSizeWmask)
                           : uint16_t{0}} << 32);
}

void Vr4121DramDecode::Begin(uint16_t cnt1, uint16_t cnt3, uint16_t ramsize) {
    if (!emu_.Get<Vr4121BcuBoard>().DramWiring()) {
        signature_ = Signature(cnt1, cnt3, ramsize);
        return;
    }
    Check(cnt1, cnt3, ramsize);
}

void Vr4121DramDecode::Check(uint16_t cnt1, uint16_t cnt3, uint16_t ramsize) const {
    const std::string why = Mismatch(cnt1, cnt3, ramsize);
    if (!why.empty()) emu_.Get<Fatal>().Die("Vr4121DramDecode: %s", why.c_str());
}

std::string Vr4121DramDecode::Mismatch(uint16_t cnt1, uint16_t cnt3,
                                       uint16_t ramsize) const {
    const auto wiring = emu_.Get<Vr4121BcuBoard>().DramWiring();
    if (!wiring) {
        if (Signature(cnt1, cnt3, ramsize) != signature_) {
            return Format("DRAM decode changed (BCUCNTREG1 0x%04X BCUCNTREG3 0x%04X "
                          "RAMSIZEREG 0x%04X) on a board whose DRAM wiring is not modeled",
                          cnt1, cnt3, ramsize);
        }
        return {};
    }
    if (cnt1 & kCnt1Rd64d) {
        for (uint32_t bank = 0; bank < kBanks; ++bank) {
            const uint32_t code = RamSizeCode(ramsize, bank);
            if (SizeCodeBytes(wiring->dbus32, code) == 0u) {
                return Format("RAMSIZEREG 0x%04X SIZE%u code %u is RFU for this bus",
                              ramsize, bank, code);
            }
        }
    }
    for (const Chip& chip : chips_) {
        if (ExpansionBank(wiring->dbus32, chip.bank) && (cnt3 & kCnt3ExtMem) == 0u) continue;
        std::string why;
        uint32_t base = 0;
        for (uint32_t lower = 0; lower < chip.bank; ++lower) {
            base += BankCapacity(wiring->dbus32, lower, cnt1, cnt3, ramsize, why);
            if (!why.empty()) return why;
        }
        const uint32_t span = BankCapacity(wiring->dbus32, chip.bank, cnt1, cnt3, ramsize, why);
        if (!why.empty()) return why;
        if (base != chip.base || span != chip.bytes) {
            return Format("bank %u decodes 0x%08X..0x%08X, not its 0x%X chip at 0x%08X "
                          "(BCUCNTREG1 0x%04X RAMSIZEREG 0x%04X)", chip.bank, base,
                          base + span, chip.bytes, chip.base, cnt1, ramsize);
        }
    }
    return {};
}

std::vector<Vr4121DramDecode::Chip> Vr4121DramDecode::ExpansionChips() const {
    std::vector<Chip> out;
    const auto wiring = emu_.Get<Vr4121BcuBoard>().DramWiring();
    if (!wiring) return out;
    for (const Chip& chip : chips_) {
        if (ExpansionBank(wiring->dbus32, chip.bank)) out.push_back(chip);
    }
    return out;
}

bool Vr4121DramDecode::ExpansionChipFitted() const {
    return !ExpansionChips().empty();
}

uint32_t Vr4121DramDecode::ExpansionBase() const {
    uint32_t base = 0xFFFFFFFFu;
    for (const Chip& chip : ExpansionChips()) base = std::min(base, chip.base);
    return base;
}

uint32_t Vr4121DramDecode::ExpansionEnd() const {
    uint32_t end = 0u;
    for (const Chip& chip : ExpansionChips()) end = std::max(end, chip.base + chip.bytes);
    return end;
}

void Vr4121DramDecode::ApplyExpansionAccess(uint16_t cnt3) const {
    auto& memory = emu_.Get<EmulatedMemory>();
    for (const Chip& chip : ExpansionChips()) memory.SetBusAccess(chip.base, (cnt3 & kCnt3ExtMem) != 0u);
}

uint32_t Vr4121DramDecode::SizeCodeBytes(bool dbus32, uint32_t code) const {
    const bool sdram = emu_.Get<Vr4121BcuBoard>().Sdram();
    if (!dbus32 && sdram) {
        if (code == 3u) return kMb16;
        if (code == 2u) return kMb8;
    } else if (!dbus32) {
        if (code == 2u) return kMb8;
        if (code == 0u) return kMb2;
    } else if (sdram) {
        if (code == 4u) return kMb32;
        if (code == 3u) return kMb16;
        if (code == 2u) return kMb8;
    } else {
        if (code == 3u) return kMb16;
        if (code == 1u) return kMb4;
    }
    return 0u;
}

uint32_t Vr4121DramDecode::BankCapacity(bool dbus32, uint32_t bank, uint16_t cnt1,
                                        uint16_t cnt3, uint16_t ramsize,
                                        std::string& why) const {
    const bool expansion = ExpansionBank(dbus32, bank);
    if (expansion && (cnt3 & kCnt3ExtMem) == 0u) {
        why = Format("bank %u is expansion space and BCUCNTREG3 0x%04X leaves EXT_MEM clear",
                     bank, cnt3);
        return 0u;
    }
    const uint16_t romcs = static_cast<uint16_t>(cnt3 & kCnt3ExtRomcsMask);
    if (expansion && romcs == kCnt3ExtRomcsRfu) {
        why = Format("BCUCNTREG3 0x%04X sets the reserved EXT_ROMCS(3:2) code 01", cnt3);
        return 0u;
    }
    if (expansion && (romcs == kCnt3ExtRomcsBothRom || (romcs == kCnt3ExtRomcsBank3Rom && bank == 3u))) {
        why = Format("BCUCNTREG3 0x%04X assigns bank %u to ROM", cnt3, bank);
        return 0u;
    }
    if (cnt1 & kCnt1Rd64d) return SizeCodeBytes(dbus32, RamSizeCode(ramsize, bank));
    const bool dram64 = expansion ? (cnt3 & kCnt3ExtDram64) != 0u : (cnt1 & kCnt1Dram64) != 0u;
    if (!dram64 && emu_.Get<Vr4121BcuBoard>().Sdram()) {
        why = expansion
            ? Format("BCUCNTREG3 0x%04X selects 16-Mbit expansion DRAM on an SDRAM board", cnt3)
            : Format("BCUCNTREG1 0x%04X selects 16-Mbit DRAM on an SDRAM board", cnt1);
        return 0u;
    }
    return dram64 ? (dbus32 ? kMb16 : kMb8) : (dbus32 ? kMb4 : kMb2);
}

}
