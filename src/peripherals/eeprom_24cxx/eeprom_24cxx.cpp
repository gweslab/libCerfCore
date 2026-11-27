#include "eeprom_24cxx.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_error.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

void Eeprom24cxx::OnReady() {
    const std::string path = StoragePath();
    if (path.empty()) {
        FillCells();
    } else {
        OpenStorage(path);
    }
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (!ResetsWithLine(DeviceAddress(), kind)) return;
        page_.fill(0);
        page_dirty_ = 0;
        addr_.Reset(0u, false);
    });
}

bool Eeprom24cxx::MatchesAddress(uint8_t slave_addr) const {
    return slave_addr == DeviceAddress();
}

void Eeprom24cxx::TxnStart(uint8_t) {
    addr_.Start();
}

bool Eeprom24cxx::TxnWriteByte(uint8_t, uint8_t byte) {
    if (addr_.Select(byte)) return true;
    const uint16_t page_bytes = PageBytes();
    if (page_bytes == 0u) {
        if (page_dirty_ != 0u) Refuse("second data byte to a part whose page size is not established");
        page_[0]    = byte;
        page_dirty_ = 1u;
        return true;
    }
    const uint16_t in_page = static_cast<uint16_t>(page_bytes - 1u);
    const uint16_t addr    = addr_.Register();
    const uint16_t slot    = addr & in_page;
    page_[slot] = byte;
    page_dirty_ = static_cast<uint16_t>(page_dirty_ | (1u << slot));
    addr_.Point(static_cast<uint16_t>((addr & ~in_page) | ((addr + 1u) & in_page)));
    return true;
}

uint8_t Eeprom24cxx::TxnReadByte(uint8_t) {
    addr_.EndSelect();
    if (!addr_.Selected()) Refuse("current-address read before any word address");
    if (addr_.Register() >= kBlockBytes) Refuse("sequential read past word 0xFF");
    return cells_[addr_.Advance()];
}

void Eeprom24cxx::BusStart() {
    page_dirty_ = 0;
}

void Eeprom24cxx::BusStop() {
    if (page_dirty_ == 0u) return;
    const uint16_t page_bytes = PageBytes();
    if (page_bytes == 0u) {
        cells_[addr_.Register()] = page_[0];
        page_dirty_              = 0;
        addr_.Clear();
        if (!path_.empty()) StoreCells(addr_.Register(), 1u);
        return;
    }
    const uint16_t base = static_cast<uint16_t>(addr_.Register() & ~(page_bytes - 1u));
    for (uint16_t i = 0; i < page_bytes; ++i) {
        if (page_dirty_ & (1u << i)) cells_[base | i] = page_[i];
    }
    page_dirty_ = 0;
    if (!path_.empty()) StoreCells(base, page_bytes);
}

void Eeprom24cxx::OpenStorage(const std::string& path) {
    path_ = path;
    if (!file_.Open(path, host_platform::FileMode::kReadWriteAlways)) {
        emu_.Get<Fatal>().UserError("%s: cannot open '%s' (error %u)", PartName(), path.c_str(),
                                    host_platform::LastErrorCode());
    }
    uint64_t size = 0;
    if (!file_.Size(size)) emu_.Get<Fatal>().UserError("%s: cannot read the size of '%s'", PartName(), path.c_str());
    if (size == 0u) {
        FillCells();
        StoreCells(0u, static_cast<uint16_t>(kBlockBytes));
        LOG(Boot, "%s: created '%s'\n", PartName(), path.c_str());
        return;
    }
    if (size != kBlockBytes) {
        emu_.Get<Fatal>().UserError("%s: '%s' holds %llu bytes; the part holds %u", PartName(), path.c_str(),
                                    static_cast<unsigned long long>(size), static_cast<unsigned>(kBlockBytes));
    }
    uint32_t done = 0u;
    if (!file_.SeekTo(0u) || !file_.Read(cells_.data(), static_cast<uint32_t>(kBlockBytes), done) ||
        done != kBlockBytes) {
        emu_.Get<Fatal>().UserError("%s: read of '%s' failed", PartName(), path.c_str());
    }
    LOG(Boot, "%s: using '%s'\n", PartName(), path.c_str());
}

void Eeprom24cxx::StoreCells(uint16_t first, uint16_t count) {
    uint32_t done = 0u;
    if (!file_.SeekTo(first) || !file_.Write(cells_.data() + first, count, done) || done != count) {
        emu_.Get<Fatal>().UserError("%s: write to '%s' failed", PartName(), path_.c_str());
    }
}

void Eeprom24cxx::SaveState(StateWriter& w) {
    if (path_.empty()) w.WriteBytes("eeprom_cells", cells_.data(), cells_.size());
    w.WriteBytes("eeprom_page", page_.data(), page_.size());
    w.Write("eeprom_page_dirty", page_dirty_);
    w.Write("eeprom_addr", addr_.Register());
    w.Write("eeprom_addr_valid", static_cast<uint8_t>(addr_.Selected()));
    w.Write("eeprom_addr_pending", static_cast<uint8_t>(addr_.Pending()));
}

void Eeprom24cxx::RestoreState(StateReader& r) {
    uint16_t addr = 0;
    uint8_t  valid = 0, pending = 0;
    if (path_.empty()) r.ReadBytes("eeprom_cells", cells_.data(), cells_.size());
    r.ReadBytes("eeprom_page", page_.data(), page_.size());
    r.Read("eeprom_page_dirty", page_dirty_);
    r.Read("eeprom_addr", addr);
    r.Read("eeprom_addr_valid", valid);
    r.Read("eeprom_addr_pending", pending);
    addr_.Restore(addr, valid != 0u, pending != 0u);
}

void Eeprom24cxx::Refuse(const char* what) const {
    emu_.Get<Fatal>().Die("%s: %s (word 0x%02X)", PartName(), what,
                          static_cast<unsigned>(addr_.Register()));
}

}
