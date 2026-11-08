#pragma once

#include "../pcic82365/isa_exca_socket_pair.h"
#include "nec_vrc4171a_board.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

class NecVrc4171a : public IsaExcaSocketPair {
public:
    explicit NecVrc4171a(CerfEmulator& emu);

    static bool FittedOnFamily(std::string_view family);
    static bool IsTopLevelPort(uint32_t port);

    bool ShouldRegister() override;
    void OnReady() override;

    bool     IoUpperDecode();
    uint16_t ReadTopLevel(uint32_t port);
    void     WriteTopLevel(uint32_t port, uint16_t value);

protected:
    const char* ChipName() const override { return "VRC4171A"; }
    uint8_t     UnbankedIndexReadLocked(uint32_t port) override;
    uint8_t     UnbankedDataReadLocked(uint32_t port) override;
    void        UnbankedDataWriteLocked(uint8_t value) override;
    bool        ChipOwns(uint8_t sub) const override;
    bool        ChipReadLocked(int bank, uint8_t sub, uint8_t* value) override;
    void        ChipWriteLocked(int bank, uint8_t sub, uint8_t value, ExcaWriteEffect* effect) override;
    uint8_t     InterfaceStatusLocked(int bank, uint8_t exca_status) override;
    void        ResetChipLocked() override;
    void        CardDetectLocked(int bank) override;
    uint32_t    CardAddressLocked(int, uint32_t card_addr) const override { return card_addr; }
    bool        InterfaceDisabledLocked(int) const override { return false; }
    bool        InpackGatedLocked(int) const override { return false; }
    uint32_t    MaskIoReadLocked(int, uint32_t, uint32_t value, unsigned) override { return value; }
    bool        Memcs16AssertedLocked(uint32_t off) override;
    void        UpdateIrqLineLocked() override;
    void        SaveChipGlobal(StateWriter& w) const override;
    void        RestoreChipGlobal(StateReader& r) override;
    void        SaveChipBank(StateWriter& w, int bank) const override;
    void        RestoreChipBank(StateReader& r, int bank) override;
    void        AfterRestoreLocked() override;

private:
    void     ApplyChipModesLocked();
    uint16_t InterruptStatusLocked();

    NecVrc4171aBoard* board_ = nullptr;

    uint16_t config1_ = 0u;
    uint16_t config2_ = 0u;
    uint8_t  card_detect_control_[2] = { 0u, 0u };
    uint8_t  global_control_[2]      = { 0u, 0u };
    uint8_t  voltage_select_[2]      = { 0u, 0u };
};

}
