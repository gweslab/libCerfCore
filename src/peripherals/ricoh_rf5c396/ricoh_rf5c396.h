#pragma once

#include "../pcic82365/isa_exca_socket_pair.h"
#include "ricoh_rf5c396_board.h"
#include "ricoh_rf5c396_chip_registers.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

class RicohRf5c396 : public IsaExcaSocketPair {
public:
    explicit RicohRf5c396(CerfEmulator& emu);

    static bool FittedOnFamily(std::string_view family);

    bool ShouldRegister() override;
    void OnReady() override;

protected:
    const char* ChipName() const override { return "RF5C396"; }
    uint8_t     UnbankedIndexReadLocked(uint32_t port) override;
    uint8_t     UnbankedDataReadLocked(uint32_t port) override;
    void        UnbankedDataWriteLocked(uint8_t) override {}
    bool        ChipOwns(uint8_t sub) const override { return RicohRf5c396ChipRegisters::Owns(sub); }
    bool        ChipReadLocked(int bank, uint8_t sub, uint8_t* value) override;
    void        ChipWriteLocked(int bank, uint8_t sub, uint8_t value, ExcaWriteEffect* effect) override;
    uint8_t     InterfaceStatusLocked(int bank, uint8_t exca_status) override;
    void        ResetChipLocked() override;
    void        CardDetectLocked(int bank) override { TrackGpiLocked(bank); }
    uint32_t    CardAddressLocked(int bank, uint32_t card_addr) const override;
    bool        InterfaceDisabledLocked(int bank) const override { return chip_.InterfaceDisabled(bank); }
    bool        InpackGatedLocked(int bank) const override { return chip_.InpackEnabled(bank); }
    uint32_t    MaskIoReadLocked(int bank, uint32_t off, uint32_t value, unsigned bytes) override;
    bool        Memcs16AssertedLocked(uint32_t off) override;
    void        UpdateIrqLineLocked() override;
    void        SaveChipGlobal(StateWriter& w) const override { chip_.SaveGlobal(w); }
    void        RestoreChipGlobal(StateReader& r) override { chip_.RestoreGlobal(r); }
    void        SaveChipBank(StateWriter& w, int bank) const override { chip_.SaveBank(w, bank); }
    void        RestoreChipBank(StateReader& r, int bank) override { chip_.RestoreBank(r, bank); }
    void        AfterRestoreLocked() override;

private:
    void ApplyChipModesLocked();
    bool GpiLowLocked(int bank);
    void TrackGpiLocked(int bank);
    bool CardInterruptLocked(int bank);

    RicohRf5c396Board* board_ = nullptr;

    RicohRf5c396ChipRegisters chip_;
    bool gpi_low_[2] = { false, false };
};

}
