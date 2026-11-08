#include "eeprom_24cxx.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

void Eeprom24cxx::OnReady() {
    FillCells();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc) return;
        page_.fill(0);
        page_dirty_   = 0;
        addr_         = 0;
        addr_valid_   = false;
        addr_pending_ = false;
    });
}

bool Eeprom24cxx::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == DeviceAddress();
}

void Eeprom24cxx::TxnStart(uint8_t) {
    addr_pending_ = true;
}

void Eeprom24cxx::TxnWriteByte(uint8_t, uint8_t byte) {
    if (addr_pending_) {
        addr_         = byte;
        addr_valid_   = true;
        addr_pending_ = false;
        return;
    }
    const uint16_t page_bytes = PageBytes();
    if (page_bytes == 0u) Refuse("data write to a part whose page size is not established");
    const uint16_t in_page = static_cast<uint16_t>(page_bytes - 1u);
    const uint16_t slot    = addr_ & in_page;
    page_[slot] = byte;
    page_dirty_ = static_cast<uint16_t>(page_dirty_ | (1u << slot));
    addr_       = static_cast<uint16_t>((addr_ & ~in_page) | ((addr_ + 1u) & in_page));
}

uint8_t Eeprom24cxx::TxnReadByte(uint8_t) {
    addr_pending_ = false;
    if (!addr_valid_) Refuse("current-address read before any word address");
    if (addr_ >= kBlockBytes) Refuse("sequential read past word 0xFF");
    return cells_[addr_++];
}

void Eeprom24cxx::BusStart() {
    page_dirty_ = 0;
}

void Eeprom24cxx::BusStop() {
    if (page_dirty_ == 0u) return;
    const uint16_t page_bytes = PageBytes();
    const uint16_t base = static_cast<uint16_t>(addr_ & ~(page_bytes - 1u));
    for (uint16_t i = 0; i < page_bytes; ++i) {
        if (page_dirty_ & (1u << i)) cells_[base | i] = page_[i];
    }
    page_dirty_ = 0;
}

void Eeprom24cxx::SaveState(StateWriter& w) {
    w.WriteBytes("eeprom_cells", cells_.data(), cells_.size());
    w.WriteBytes("eeprom_page", page_.data(), page_.size());
    w.Write("eeprom_page_dirty", page_dirty_);
    w.Write("eeprom_addr", addr_);
    w.Write("eeprom_addr_valid", static_cast<uint8_t>(addr_valid_));
    w.Write("eeprom_addr_pending", static_cast<uint8_t>(addr_pending_));
}

void Eeprom24cxx::RestoreState(StateReader& r) {
    uint8_t v = 0;
    r.ReadBytes("eeprom_cells", cells_.data(), cells_.size());
    r.ReadBytes("eeprom_page", page_.data(), page_.size());
    r.Read("eeprom_page_dirty", page_dirty_);
    r.Read("eeprom_addr", addr_);
    r.Read("eeprom_addr_valid", v);
    addr_valid_ = v != 0u;
    r.Read("eeprom_addr_pending", v);
    addr_pending_ = v != 0u;
}

void Eeprom24cxx::Refuse(const char* what) const {
    emu_.Get<Fatal>().Die("%s: %s (word 0x%02X)", PartName(), what,
                          static_cast<unsigned>(addr_));
}

}
