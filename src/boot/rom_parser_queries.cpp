#include <cerfcore/boot/rom_parser_queries.h>

#include "rom_toc_lookup.h"

#include <cerfcore/boards/board_context.h>
#include "flat_container_placing_mode_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>

namespace cerfcore {

REGISTER_SERVICE(RomParserQueries);

namespace {

constexpr const char* kKernelModule = "nk.exe";

}  /* namespace */

bool RomParserQueries::ShouldRegister() {
    return emu_.Get<BoardContext>().GetRomPlacingMode()
        == RomPlacingModeId::FlatContainer;
}

void RomParserQueries::OnReady() {
    uint16_t major = 0, minor = 0;
    if (KernelSubsystemVersion(major, minor))
        LOG(Boot, "RomParser: kernel subsystem version %u.%u\n", major, minor);
}

const std::vector<ParsedRom>& RomParserQueries::Loaded() const {
    return emu_.Get<RomParserService>().Loaded();
}

const ParsedTOCentry* RomParserQueries::FindModule(const char* name) const {
    const auto& loaded = Loaded();
    if (loaded.empty()) return nullptr;
    for (const auto& xip : loaded[0].xips)
        if (const ParsedTOCentry* m = cerf::rom_toc::FindModule(xip.toc, name)) return m;
    return nullptr;
}

const ParsedTOCentry* RomParserQueries::KernelModule() const { return FindModule(kKernelModule); }

std::span<const uint8_t>
RomParserQueries::ReadVa(uint32_t va, uint32_t len) const {
    for (const auto& rom : Loaded()) {
        for (const auto& xip : rom.xips) {
            if (va < xip.load_offset) continue;
            const size_t off = size_t(va - xip.load_offset);
            if (off + len <= rom.flat.size())
                return rom.flat.subspan(off, len);
        }
    }
    return {};
}

bool RomParserQueries::KernelSubsystemVersion(uint16_t& major,
                                              uint16_t& minor) const {
    for (const auto& rom : Loaded()) {
        if (rom.is_ce1) return false;
        for (const auto& xip : rom.xips) {
            if (const ParsedTOCentry* nk = cerf::rom_toc::FindModule(xip.toc, kKernelModule))
                return cerf::rom_toc::ReadSubsystemVersion(rom.flat, xip, *nk, major, minor);
        }
    }
    return false;
}

}
