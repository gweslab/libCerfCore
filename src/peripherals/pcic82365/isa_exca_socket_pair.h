#pragma once

#include <cerfcore/core/service.h>
#include "../floating_bus.h"
#include "../pcmcia/pcmcia_slot.h"
#include "exca_socket_sequencer.h"
#include "pcic82365.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

class StateReader;
class StateWriter;

class IsaExcaSocketPair : public Service, public PcmciaSlotHost {
public:
    IsaExcaSocketPair(CerfEmulator& emu, ExcaPartRules rules, const char* slot0_name, const char* slot1_name);

    void OnShutdown() override;

    uint8_t ReadPcicByte (uint32_t port);
    void    WritePcicByte(uint32_t port, uint8_t value);

    uint8_t  ReadCardMem8 (uint32_t off);
    uint16_t ReadCardMem16(uint32_t off);
    void     WriteCardMem8 (uint32_t off, uint8_t  value);
    void     WriteCardMem16(uint32_t off, uint16_t value);

    uint8_t  ReadCardIo8 (uint32_t off);
    uint16_t ReadCardIo16(uint32_t off);
    void     WriteCardIo8 (uint32_t off, uint8_t  value);
    void     WriteCardIo16(uint32_t off, uint16_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

    void OnCardDetectChanged(PcmciaSlot& slot) override;
    void OnReadyIreqLow     (PcmciaSlot& slot) override;
    void OnReadyIreqHigh    (PcmciaSlot& slot) override;
    uint8_t UndrivenDataLane() const override { return 0u; }

protected:
    static constexpr int kSockets = 2;

    void StartSockets();
    void ResetController();

    int         BankOf(const PcmciaSlot& slot) const { return &slot == &slot1_ ? 1 : 0; }
    PcmciaSlot& Slot(int bank) { return bank ? slot1_ : slot0_; }
    Pcic82365&  Exca(int bank) { return bank ? exca1_ : exca0_; }
    uint8_t     IndexLocked() const { return index_; }
    FloatingBus& Floating() { return floating_; }

    virtual const char* ChipName() const = 0;
    virtual uint8_t     UnbankedIndexReadLocked(uint32_t port) = 0;
    virtual uint8_t     UnbankedDataReadLocked(uint32_t port) = 0;
    virtual void        UnbankedDataWriteLocked(uint8_t value) = 0;
    virtual bool        ChipOwns(uint8_t sub) const = 0;
    virtual bool        ChipReadLocked(int bank, uint8_t sub, uint8_t* value) = 0;
    virtual void        ChipWriteLocked(int bank, uint8_t sub, uint8_t value, ExcaWriteEffect* effect) = 0;
    virtual uint8_t     InterfaceStatusLocked(int bank, uint8_t exca_status) = 0;
    virtual void        ResetChipLocked() = 0;
    virtual void        CardDetectLocked(int bank) = 0;
    virtual uint32_t    CardAddressLocked(int bank, uint32_t card_addr) const = 0;
    virtual bool        InterfaceDisabledLocked(int bank) const = 0;
    virtual bool        InpackGatedLocked(int bank) const = 0;
    virtual uint32_t    MaskIoReadLocked(int bank, uint32_t off, uint32_t value, unsigned bytes) = 0;
    virtual bool        Memcs16AssertedLocked(uint32_t off) = 0;
    virtual void        UpdateIrqLineLocked() = 0;
    virtual void        SaveChipGlobal(StateWriter& w) const = 0;
    virtual void        RestoreChipGlobal(StateReader& r) = 0;
    virtual void        SaveChipBank(StateWriter& w, int bank) const = 0;
    virtual void        RestoreChipBank(StateReader& r, int bank) = 0;
    virtual void        AfterRestoreLocked() = 0;

    mutable std::mutex state_mutex_;

private:
    struct CardMemHit {
        int      bank        = 0;
        uint32_t addr        = 0u;
        bool     attribute   = false;
        bool     writable    = false;
        bool     sixteen_bit = false;
        bool     isolated    = false;
    };
    struct CardIoHit {
        int           bank         = 0;
        uint32_t      io           = 0u;
        PcmciaIoWidth width        = PcmciaIoWidth::kEightBit;
        bool          isolated     = false;
        bool          inpack_gated = false;
    };

    uint8_t ReadDataLocked(uint32_t port);
    void    WriteDataLocked(int bank, uint8_t value, ExcaWriteEffect* effect);
    bool    DecodeMem(uint32_t off, CardMemHit* hit);
    bool    DecodeIo (uint32_t off, CardIoHit* hit);
    bool    Memcs16Asserted(uint32_t off);
    uint32_t MaskIoRead(int bank, uint32_t off, uint32_t value, unsigned bytes);

    PcmciaSlot slot0_;
    PcmciaSlot slot1_;
    Pcic82365  exca0_;
    Pcic82365  exca1_;
    ExcaSocketSequencer sequencer_;
    uint8_t     index_ = 0u;
    FloatingBus floating_;
};

}
