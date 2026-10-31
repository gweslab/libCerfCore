#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cerfcore {

class CerfEmulator;
class StateWriter;
class StateReader;

class SmallPageNandStore {
public:
    static constexpr uint32_t kPageBytes     = 528u;
    static constexpr uint32_t kPagesPerBlock = 32u;
    static constexpr uint8_t  kErased        = 0xFFu;

    virtual void ReadPage  (uint32_t page,  uint8_t* dst)       = 0;
    virtual void WritePage (uint32_t page,  const uint8_t* src) = 0;
    virtual void EraseBlock(uint32_t block)                     = 0;

protected:
    ~SmallPageNandStore() = default;
};

struct SmallPageNandGeometry {
    uint32_t       blocks;
    uint32_t       row_cycles;
    const uint8_t* id;
    uint32_t       id_bytes;
    bool           sequential_row_read;
};

class SmallPageNandFlash {
public:
    SmallPageNandFlash(CerfEmulator& emu, std::string name, const SmallPageNandGeometry& geometry,
                       SmallPageNandStore& store);

    void    Command  (uint8_t value);
    void    Address  (uint8_t value);
    void    WriteData(uint8_t value);
    uint8_t ReadData ();

    void Deselect();
    void SetWriteProtect(bool active);

    void PowerUp();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum class Mode : uint8_t { Waiting, Read, ReadId, Status, DataInput, EraseSetup };
    enum class Pointer : uint8_t { A, B, C };

    static constexpr uint32_t kHalfPage    = 256u;
    static constexpr uint32_t kSpareColumn = 512u;

    [[noreturn]] void Die(const char* what, uint32_t value) const;

    uint32_t PageCount() const { return geometry_.blocks * SmallPageNandStore::kPagesPerBlock; }
    uint32_t PointerColumn() const;
    void     RequireWriteProtectLevel(const char* what) const;
    void     RequireWritesEnabled(const char* op, uint32_t value) const;
    void     ConsumeOneShotPointer();
    void     StartAddressPhase(Mode mode);
    void     CompleteRowAddress();
    void     LoadPage(uint32_t page);
    void     Program();
    void     Erase();

    CerfEmulator&         emu_;
    std::string           name_;
    SmallPageNandGeometry geometry_;
    SmallPageNandStore&   store_;

    Mode     mode_          = Mode::Read;
    Pointer  pointer_       = Pointer::A;
    bool     write_protect_known_ = false;
    bool     write_protect_       = false;
    bool     page_loaded_   = false;
    bool     sequential_ended_ = false;
    bool     address_open_  = false;
    uint32_t addr_cycle_    = 0;
    uint32_t column_        = 0;
    uint32_t row_           = 0;
    uint32_t page_          = 0;
    uint32_t data_column_   = 0;
    uint32_t id_index_      = 0;
    uint8_t  page_buf_[SmallPageNandStore::kPageBytes] = {};
};

}
