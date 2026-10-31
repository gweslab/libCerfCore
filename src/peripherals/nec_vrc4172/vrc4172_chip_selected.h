#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../floating_bus.h"
#include "vrc4172_pcs.h"

#include <cstdint>

namespace cerfcore {

template <class Base>
class Vrc4172ChipSelected : public Base {
public:
    using Base::Base;

    void OnReady() final {
        pcs_ = &this->emu_.template Get<Vrc4172Pcs>();
        OnChipSelectReady();
    }

    uint8_t ReadByte(uint32_t addr) final {
        return Selected(addr) ? SelectedReadByte(addr) : bus_.Read8(kUnselectedTag, addr);
    }
    uint16_t ReadHalf(uint32_t addr) final {
        return Selected(addr) ? SelectedReadHalf(addr) : bus_.Read16(kUnselectedTag, addr);
    }
    uint32_t ReadWord(uint32_t addr) final {
        return Selected(addr) ? SelectedReadWord(addr) : bus_.Read32(kUnselectedTag, addr);
    }
    uint64_t ReadDword(uint32_t addr) final {
        return Selected(addr) ? SelectedReadDword(addr) : bus_.Read64(kUnselectedTag, addr);
    }
    void WriteByte(uint32_t addr, uint8_t value) final {
        if (Selected(addr)) SelectedWriteByte(addr, value);
        else bus_.Write(kUnselectedTag, 8u, addr);
    }
    void WriteHalf(uint32_t addr, uint16_t value) final {
        if (Selected(addr)) SelectedWriteHalf(addr, value);
        else bus_.Write(kUnselectedTag, 16u, addr);
    }
    void WriteWord(uint32_t addr, uint32_t value) final {
        if (Selected(addr)) SelectedWriteWord(addr, value);
        else bus_.Write(kUnselectedTag, 32u, addr);
    }
    void WriteDword(uint32_t addr, uint64_t value) final {
        if (Selected(addr)) SelectedWriteDword(addr, value);
        else bus_.Write(kUnselectedTag, 64u, addr);
    }

    Peripheral::FastReadFn  FastReader() final { return Peripheral::FastReader(); }
    Peripheral::FastWriteFn FastWriter() final { return Peripheral::FastWriter(); }

protected:
    virtual uint32_t ChipSelect() const = 0;

    virtual void OnChipSelectReady() = 0;

    virtual uint8_t  SelectedReadByte (uint32_t addr) { return Base::ReadByte(addr); }
    virtual uint16_t SelectedReadHalf (uint32_t addr) { return Base::ReadHalf(addr); }
    virtual uint32_t SelectedReadWord (uint32_t addr) { return Base::ReadWord(addr); }
    virtual uint64_t SelectedReadDword(uint32_t addr) { return Base::ReadDword(addr); }
    virtual void SelectedWriteByte (uint32_t addr, uint8_t  value) { Base::WriteByte(addr, value); }
    virtual void SelectedWriteHalf (uint32_t addr, uint16_t value) { Base::WriteHalf(addr, value); }
    virtual void SelectedWriteWord (uint32_t addr, uint32_t value) { Base::WriteWord(addr, value); }
    virtual void SelectedWriteDword(uint32_t addr, uint64_t value) { Base::WriteDword(addr, value); }

private:
    static constexpr const char* kUnselectedTag = "VRC4172 chip select inactive";

    bool Selected(uint32_t addr) const { return pcs_->Selects(ChipSelect(), addr); }

    Vrc4172Pcs* pcs_ = nullptr;
    FloatingBus bus_;
};

}
