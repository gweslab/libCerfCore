#include "small_page_nand_flash.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kCmdRead1A     = 0x00u;
constexpr uint8_t kCmdRead1B     = 0x01u;
constexpr uint8_t kCmdRead2      = 0x50u;
constexpr uint8_t kCmdReadId     = 0x90u;
constexpr uint8_t kCmdReset      = 0xFFu;
constexpr uint8_t kCmdDataInput  = 0x80u;
constexpr uint8_t kCmdProgram    = 0x10u;
constexpr uint8_t kCmdEraseSetup = 0x60u;
constexpr uint8_t kCmdErase      = 0xD0u;
constexpr uint8_t kCmdStatus     = 0x70u;

constexpr uint8_t kStatusNotProtected = 0x80u;
constexpr uint8_t kStatusReady        = 0x40u;

constexpr uint32_t kSpareColumnMask = 0x0Fu;
constexpr uint8_t  kReadIdAddress   = 0x00u;

}

SmallPageNandFlash::SmallPageNandFlash(CerfEmulator& emu, std::string name,
                                       const SmallPageNandGeometry& geometry,
                                       SmallPageNandStore& store)
    : emu_(emu), name_(std::move(name)), geometry_(geometry), store_(store) {}

void SmallPageNandFlash::Die(const char* what, uint32_t value) const {
    emu_.Get<Fatal>().Die("%s: %s (0x%X)", name_.c_str(), what, value);
}

void SmallPageNandFlash::PowerUp() {
    mode_          = Mode::Read;
    pointer_       = Pointer::A;
    page_loaded_   = false;
    sequential_ended_ = false;
    address_open_  = false;
    addr_cycle_    = 0;
    column_        = 0;
    row_           = 0;
    page_          = 0;
    data_column_   = 0;
    id_index_      = 0;
}

uint32_t SmallPageNandFlash::PointerColumn() const {
    if (pointer_ == Pointer::B) return kHalfPage + column_;
    if (pointer_ == Pointer::C) return kSpareColumn + (column_ & kSpareColumnMask);
    return column_;
}

void SmallPageNandFlash::SetWriteProtect(bool active) {
    write_protect_known_ = true;
    write_protect_       = active;
}

void SmallPageNandFlash::RequireWriteProtectLevel(const char* what) const {
    if (!write_protect_known_) Die(what, page_);
}

void SmallPageNandFlash::RequireWritesEnabled(const char* op, uint32_t value) const {
    if (!write_protect_known_)
        emu_.Get<Fatal>().Die("%s: %s with the WP# line level not established (0x%X)",
                              name_.c_str(), op, value);
    if (write_protect_)
        emu_.Get<Fatal>().Die("%s: %s with write protect asserted (0x%X)",
                              name_.c_str(), op, value);
}

void SmallPageNandFlash::ConsumeOneShotPointer() {
    if (pointer_ == Pointer::B) pointer_ = Pointer::A;
}

void SmallPageNandFlash::StartAddressPhase(Mode mode) {
    mode_         = mode;
    page_loaded_  = false;
    sequential_ended_ = false;
    address_open_ = true;
    addr_cycle_   = 0;
    column_       = 0;
    row_          = 0;
}

void SmallPageNandFlash::Command(uint8_t value) {
    if (mode_ == Mode::DataInput && value != kCmdProgram && value != kCmdReset)
        Die("command during serial data input", value);
    if (mode_ == Mode::EraseSetup && value != kCmdErase && value != kCmdReset)
        Die("command between erase setup and confirm", value);
    switch (value) {
        case kCmdRead1A: pointer_ = Pointer::A; StartAddressPhase(Mode::Read); return;
        case kCmdRead1B: pointer_ = Pointer::B; StartAddressPhase(Mode::Read); return;
        case kCmdRead2:  pointer_ = Pointer::C; StartAddressPhase(Mode::Read); return;
        case kCmdDataInput:
            RequireWritesEnabled("serial data input command", value);
            std::memset(page_buf_, SmallPageNandStore::kErased, sizeof(page_buf_));
            StartAddressPhase(Mode::DataInput);
            return;
        case kCmdEraseSetup:
            RequireWritesEnabled("block erase setup command", value);
            StartAddressPhase(Mode::EraseSetup);
            return;
        case kCmdReadId:
            id_index_ = 0;
            StartAddressPhase(Mode::ReadId);
            return;
        case kCmdProgram:
            if (mode_ != Mode::DataInput || !page_loaded_)
                Die("page program confirm without a serial data input address", value);
            Program();
            break;
        case kCmdErase:
            if (mode_ != Mode::EraseSetup || !page_loaded_)
                Die("block erase confirm without a block address", value);
            Erase();
            break;
        case kCmdStatus:
            mode_         = Mode::Status;
            address_open_ = false;
            return;
        case kCmdReset:
            mode_         = Mode::Waiting;
            page_loaded_  = false;
            address_open_ = false;
            ConsumeOneShotPointer();
            return;
        default:
            Die("command not modeled", value);
    }
    mode_         = Mode::Status;
    page_loaded_  = false;
    address_open_ = false;
    ConsumeOneShotPointer();
}

void SmallPageNandFlash::Address(uint8_t value) {
    if (!address_open_) {
        if (mode_ != Mode::Read) Die("address cycle with no command that takes one", value);
        StartAddressPhase(Mode::Read);
    }
    switch (mode_) {
        case Mode::ReadId:
            if (addr_cycle_++ != 0) return;
            if (value != kReadIdAddress) Die("READ ID address", value);
            page_loaded_ = true;
            return;
        case Mode::EraseSetup:
            RequireWritesEnabled("block erase address cycle", value);
            if (addr_cycle_ >= geometry_.row_cycles) return;
            row_ |= static_cast<uint32_t>(value) << (8u * addr_cycle_);
            if (++addr_cycle_ == geometry_.row_cycles) CompleteRowAddress();
            return;
        case Mode::Read:
        case Mode::DataInput:
            if (mode_ == Mode::DataInput) RequireWritesEnabled("serial data input address cycle", value);
            if (addr_cycle_ > geometry_.row_cycles) return;
            if (addr_cycle_ == 0) column_ = value;
            else row_ |= static_cast<uint32_t>(value) << (8u * (addr_cycle_ - 1u));
            if (addr_cycle_++ == geometry_.row_cycles) CompleteRowAddress();
            return;
        case Mode::Waiting:
        case Mode::Status:
            break;
    }
    Die("address cycle with no command that takes one", value);
}

void SmallPageNandFlash::CompleteRowAddress() {
    if (row_ >= PageCount()) Die("row address past the device", row_);
    page_ = row_;
    if (mode_ == Mode::EraseSetup) {
        page_loaded_ = true;
        return;
    }
    data_column_ = PointerColumn();
    ConsumeOneShotPointer();
    if (mode_ == Mode::Read) LoadPage(page_);
    else page_loaded_ = true;
}

void SmallPageNandFlash::LoadPage(uint32_t page) {
    store_.ReadPage(page, page_buf_);
    page_        = page;
    page_loaded_ = true;
}

uint8_t SmallPageNandFlash::ReadData() {
    address_open_ = false;
    switch (mode_) {
        case Mode::Status:
            RequireWriteProtectLevel("status read with the WP# line level not established");
            return static_cast<uint8_t>((write_protect_ ? 0u : kStatusNotProtected) | kStatusReady);
        case Mode::ReadId:
            if (!page_loaded_) Die("READ ID data before its address", id_index_);
            if (id_index_ >= geometry_.id_bytes) Die("READ ID byte past the modeled ID bytes", id_index_);
            return geometry_.id[id_index_++];
        case Mode::Read:
            if (!page_loaded_) Die("data output with no page address", data_column_);
            if (data_column_ >= SmallPageNandStore::kPageBytes) {
                if (!geometry_.sequential_row_read) Die("data output past the page", page_);
                if (sequential_ended_)
                    Die("data output past the page after chip enable ended the sequential row read", page_);
                const uint32_t next = page_ + 1u;
                if (next % SmallPageNandStore::kPagesPerBlock == 0)
                    Die("sequential row read past the last page of a block", page_);
                LoadPage(next);
                data_column_ = pointer_ == Pointer::C ? kSpareColumn : 0u;
            }
            return page_buf_[data_column_++];
        case Mode::Waiting:
        case Mode::DataInput:
        case Mode::EraseSetup:
            break;
    }
    Die("data output with no read operation", static_cast<uint32_t>(mode_));
}

void SmallPageNandFlash::WriteData(uint8_t value) {
    address_open_ = false;
    if (mode_ != Mode::DataInput || !page_loaded_)
        Die("data input with no serial data input address", value);
    RequireWritesEnabled("serial data input", value);
    if (data_column_ >= SmallPageNandStore::kPageBytes)
        Die("serial data input past the page", data_column_);
    page_buf_[data_column_++] = value;
}

void SmallPageNandFlash::Deselect() {
    switch (mode_) {
        case Mode::Read:
            sequential_ended_ = true;
            return;
        case Mode::EraseSetup:
            Die("chip deselected between erase setup and confirm", page_);
        case Mode::Waiting:
        case Mode::ReadId:
        case Mode::Status:
        case Mode::DataInput:
            return;
    }
}

void SmallPageNandFlash::Program() {
    RequireWritesEnabled("page program", page_);
    uint8_t cells[SmallPageNandStore::kPageBytes];
    store_.ReadPage(page_, cells);
    for (uint32_t i = 0; i < SmallPageNandStore::kPageBytes; ++i)
        cells[i] = static_cast<uint8_t>(cells[i] & page_buf_[i]);
    store_.WritePage(page_, cells);
}

void SmallPageNandFlash::Erase() {
    RequireWritesEnabled("block erase", page_);
    store_.EraseBlock(page_ / SmallPageNandStore::kPagesPerBlock);
}

void SmallPageNandFlash::SaveState(StateWriter& w) {
    w.Write("nand_mode", mode_);
    w.Write("nand_pointer", pointer_);
    w.Write("nand_write_protect_known", write_protect_known_);
    w.Write("nand_write_protect", write_protect_);
    w.Write("nand_page_loaded", page_loaded_);
    w.Write("nand_sequential_ended", sequential_ended_);
    w.Write("nand_address_open", address_open_);
    w.Write("nand_addr_cycle", addr_cycle_);
    w.Write("nand_column", column_);
    w.Write("nand_row", row_);
    w.Write("nand_page", page_);
    w.Write("nand_data_column", data_column_);
    w.Write("nand_id_index", id_index_);
    w.Write("nand_page_buf", page_buf_);
}

void SmallPageNandFlash::RestoreState(StateReader& r) {
    r.Read("nand_mode", mode_);
    r.Read("nand_pointer", pointer_);
    r.Read("nand_write_protect_known", write_protect_known_);
    r.Read("nand_write_protect", write_protect_);
    r.Read("nand_page_loaded", page_loaded_);
    r.Read("nand_sequential_ended", sequential_ended_);
    r.Read("nand_address_open", address_open_);
    r.Read("nand_addr_cycle", addr_cycle_);
    r.Read("nand_column", column_);
    r.Read("nand_row", row_);
    r.Read("nand_page", page_);
    r.Read("nand_data_column", data_column_);
    r.Read("nand_id_index", id_index_);
    r.Read("nand_page_buf", page_buf_);
}

}
