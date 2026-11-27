#include "nokia_sea_ray_lens_driver.h"

#include "../../core/board_parts.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_lens_driver_part_id.h"

namespace cerfcore {

REGISTER_SERVICE(NokiaSeaRayLensDriver);

namespace {

constexpr uint8_t kDeviceAddress = 0x0Eu;

constexpr uint8_t kRegId     = 0x00u;
constexpr uint8_t kRegStatus = 0x14u;

constexpr uint8_t kIdStub     = 0x23u;
constexpr uint8_t kStatusStub = 0x00u;

constexpr uint32_t kWritableRegisters = (1u << 0x03) | (1u << 0x04) | (1u << 0x05) | (1u << 0x06) |
                                        (1u << 0x0D) | (1u << 0x0E) | (1u << 0x0F) | (1u << 0x10) |
                                        (1u << 0x11) | (1u << 0x12) | (1u << 0x13);

}

bool NokiaSeaRayLensDriver::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay &&
           emu_.Get<BoardParts>().Has(PartId::NokiaSeaRayLensDriver);
}

void NokiaSeaRayLensDriver::OnReady() { PowerOn(); }

void NokiaSeaRayLensDriver::PowerOn() {
    index_.Reset(0u, false);
    registers_.fill(0u);
    written_.fill(0u);
}

bool NokiaSeaRayLensDriver::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == kDeviceAddress;
}

void NokiaSeaRayLensDriver::TxnStart(uint8_t) { index_.Start(); }

bool NokiaSeaRayLensDriver::TxnWriteByte(uint8_t, uint8_t byte) {
    if (index_.Select(byte)) return true;
    WriteRegister(static_cast<uint8_t>(index_.Advance()), byte);
    return true;
}

uint8_t NokiaSeaRayLensDriver::TxnReadByte(uint8_t) {
    if (!index_.Selected()) {
        emu_.Get<Fatal>().Die("camera lens driver: register read with no index written");
    }
    return ReadRegister(static_cast<uint8_t>(index_.Advance()));
}

uint8_t NokiaSeaRayLensDriver::ReadRegister(uint8_t reg) {
    if (reg == kRegId) return kIdStub;
    if (reg == kRegStatus) return kStatusStub;
    if (reg < kRegisterCount && written_[reg] != 0u) return registers_[reg];
    emu_.Get<Fatal>().Die("camera lens driver: read of register 0x%02X is not modeled", reg);
}

void NokiaSeaRayLensDriver::WriteRegister(uint8_t reg, uint8_t value) {
    if (reg >= kRegisterCount || (kWritableRegisters & (1u << reg)) == 0u) {
        emu_.Get<Fatal>().Die("camera lens driver: write of 0x%02X to register 0x%02X is not modeled",
                              value, reg);
    }
    registers_[reg] = value;
    written_[reg]   = 1u;
}

void NokiaSeaRayLensDriver::SaveState(StateWriter& w) {
    w.Write<uint8_t>("lens_index", static_cast<uint8_t>(index_.Register()));
    w.Write<uint8_t>("lens_index_valid", index_.Selected() ? 1u : 0u);
    w.Write<uint8_t>("lens_index_next", index_.Pending() ? 1u : 0u);
    w.Write("lens_registers", registers_);
    w.Write("lens_written", written_);
}

void NokiaSeaRayLensDriver::RestoreState(StateReader& r) {
    uint8_t index = 0u, valid = 0u, next = 0u;
    r.Read("lens_index", index);
    r.Read("lens_index_valid", valid);
    r.Read("lens_index_next", next);
    r.Read("lens_registers", registers_);
    r.Read("lens_written", written_);
    index_.Restore(index, valid != 0u, next != 0u);
}

}
