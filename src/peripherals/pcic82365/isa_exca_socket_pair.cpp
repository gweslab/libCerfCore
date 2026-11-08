#include "isa_exca_socket_pair.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../pcmcia/pcmcia_host_cycles.h"

namespace cerfcore {

namespace {

constexpr uint32_t kPortIndex      = 0x0u;
constexpr unsigned kIndexBankShift = 6u;
constexpr uint8_t  kIndexSubMask   = 0x3Fu;
constexpr uint8_t  kRegInterfaceStatus = 0x01u;

}

IsaExcaSocketPair::IsaExcaSocketPair(CerfEmulator& emu, ExcaPartRules rules, const char* slot0_name,
                                     const char* slot1_name)
    : Service(emu),
      slot0_(emu, *this, slot0_name),
      slot1_(emu, *this, slot1_name),
      exca0_(rules),
      exca1_(rules) {}

void IsaExcaSocketPair::OnShutdown() {
    slot0_.OnShutdown();
    slot1_.OnShutdown();
}

void IsaExcaSocketPair::StartSockets() {
    auto& reg = emu_.Get<HostWidgetRegistry>();
    reg.Register(&slot0_);
    reg.Register(&slot1_);
    ResetController();
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { ResetController(); });
    reset.RegisterResetReleaseListener([this] {
        std::lock_guard<std::mutex> lk(state_mutex_);
        UpdateIrqLineLocked();
    });
}

void IsaExcaSocketPair::ResetController() {
    sequencer_.Run([this] {
        std::lock_guard<std::mutex> lk(state_mutex_);
        index_ = 0u;
        exca0_.Reset();
        exca1_.Reset();
        ResetChipLocked();
        return ExcaSocketUpdates{{ {&slot0_, ExcaWriteEffect::SocketOff()},
                                   {&slot1_, ExcaWriteEffect::SocketOff()} }};
    });
}

uint8_t IsaExcaSocketPair::ReadPcicByte(uint32_t port) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (port == kPortIndex) return IndexReadLocked(port);
    const uint8_t result = ReadDataLocked(port);
    UpdateIrqLineLocked();
    return result;
}

void IsaExcaSocketPair::WritePcicByte(uint32_t port, uint8_t value) {
    if (port == kPortIndex) {
        std::lock_guard<std::mutex> lk(state_mutex_);
        index_ = value;
        return;
    }
    sequencer_.Run([&] {
        ExcaSocketUpdates updates{};
        std::lock_guard<std::mutex> lk(state_mutex_);
        const int bank = index_ >> kIndexBankShift;
        if (bank >= kSockets) {
            UnbankedDataWriteLocked(value);
            return updates;
        }
        WriteDataLocked(bank, value, &updates[0].effect);
        updates[0].slot = &Slot(bank);
        UpdateIrqLineLocked();
        return updates;
    });
}

uint8_t IsaExcaSocketPair::ReadDataLocked(uint32_t port) {
    const int bank = index_ >> kIndexBankShift;
    if (bank >= kSockets)
        return UnbankedDataReadLocked(port);
    const uint8_t sub = index_ & kIndexSubMask;
    uint8_t v = 0u;
    if (ChipReadLocked(bank, sub, &v))
        return v;
    if (!Exca(bank).ReadReg(sub, &v))
        emu_.Get<Fatal>().Die("[%s] read of INDEX 0x%02X, a register the part does not document", ChipName(), index_);
    if (sub == kRegInterfaceStatus)
        v = InterfaceStatusLocked(bank, v);
    return v;
}

void IsaExcaSocketPair::WriteDataLocked(int bank, uint8_t value, ExcaWriteEffect* effect) {
    const uint8_t sub = index_ & kIndexSubMask;
    if (ChipOwns(sub)) {
        ChipWriteLocked(bank, sub, value, effect);
        return;
    }
    if (!Exca(bank).Owns(sub))
        emu_.Get<Fatal>().Die("[%s] write 0x%02X to INDEX 0x%02X, a register the part does not document",
                              ChipName(), value, index_);
    if (!Exca(bank).WriteReg(sub, value, effect))
        emu_.Get<Fatal>().Die("[%s] INDEX 0x%02X value 0x%02X sets reserved bits or selects a reserved code",
                              ChipName(), index_, value);
}

bool IsaExcaSocketPair::DecodeMem(uint32_t off, CardMemHit* hit) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    for (int b = 0; b < kSockets; ++b) {
        if (Exca(b).MapMem(off, &hit->addr, &hit->attribute, &hit->writable, &hit->sixteen_bit)) {
            hit->bank = b;
            hit->addr = CardAddressLocked(b, hit->addr);
            hit->isolated = InterfaceDisabledLocked(b);
            return true;
        }
    }
    return false;
}

bool IsaExcaSocketPair::DecodeIo(uint32_t off, CardIoHit* hit) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    for (int b = 0; b < kSockets; ++b) {
        if (Exca(b).MapIo(off, &hit->io, &hit->width)) {
            hit->bank = b;
            hit->isolated = InterfaceDisabledLocked(b);
            hit->inpack_gated = InpackGatedLocked(b);
            return true;
        }
    }
    return false;
}

bool IsaExcaSocketPair::Memcs16Asserted(uint32_t off) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return Memcs16AssertedLocked(off);
}

uint32_t IsaExcaSocketPair::MaskIoRead(int bank, uint32_t off, uint32_t value, unsigned bytes) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return MaskIoReadLocked(bank, off, value, bytes);
}

uint8_t IsaExcaSocketPair::ReadCardMem8(uint32_t off) {
    CardMemHit hit;
    if (!DecodeMem(off, &hit) || hit.isolated)
        return floating_.Read8("ISA ExCA memory", off);
    const uint8_t card = static_cast<uint8_t>(
        ReadCardMemory(Slot(hit.bank), hit.addr, hit.attribute, hit.sixteen_bit, 1u));
    if (!hit.sixteen_bit && (off & 1u) != 0u && Memcs16Asserted(off))
        return floating_.Read8("ISA ExCA high lane of an 8-bit window cycle", off);
    return card;
}

uint16_t IsaExcaSocketPair::ReadCardMem16(uint32_t off) {
    CardMemHit hit;
    if (!DecodeMem(off, &hit) || hit.isolated)
        return floating_.Read16("ISA ExCA memory", off);
    if (hit.sixteen_bit)
        return static_cast<uint16_t>(ReadCardMemory(Slot(hit.bank), hit.addr, hit.attribute, true, 2u));
    if (!Memcs16Asserted(off))
        return static_cast<uint16_t>(ReadCardMemory(Slot(hit.bank), hit.addr, hit.attribute, false, 2u));
    const uint32_t low = ReadCardMemory(Slot(hit.bank), hit.addr, hit.attribute, false, 1u);
    const uint32_t high = floating_.Read8("ISA ExCA high byte of an 8-bit window cycle", off + 1u);
    return static_cast<uint16_t>(low | (high << 8));
}

void IsaExcaSocketPair::WriteCardMem8(uint32_t off, uint8_t value) {
    CardMemHit hit;
    if (!DecodeMem(off, &hit)) {
        floating_.Write("ISA ExCA memory", 8u, off);
        return;
    }
    if (!hit.writable || hit.isolated)
        return;
    WriteCardMemory(Slot(hit.bank), hit.addr, hit.attribute, hit.sixteen_bit, 1u, value);
}

void IsaExcaSocketPair::WriteCardMem16(uint32_t off, uint16_t value) {
    CardMemHit hit;
    if (!DecodeMem(off, &hit)) {
        floating_.Write("ISA ExCA memory", 16u, off);
        return;
    }
    if (!hit.writable || hit.isolated)
        return;
    if (hit.sixteen_bit) {
        WriteCardMemory(Slot(hit.bank), hit.addr, hit.attribute, true, 2u, value);
        return;
    }
    if (!Memcs16Asserted(off)) {
        WriteCardMemory(Slot(hit.bank), hit.addr, hit.attribute, false, 2u, value);
        return;
    }
    WriteCardMemory(Slot(hit.bank), hit.addr, hit.attribute, false, 1u, value & 0xFFu);
}

uint8_t IsaExcaSocketPair::ReadCardIo8(uint32_t off) {
    CardIoHit hit;
    if (!DecodeIo(off, &hit) || hit.isolated)
        return floating_.Read8("ISA ExCA I/O", off);
    const uint32_t v = ReadCardIo(Slot(hit.bank), hit.io, hit.width, 1u);
    if (hit.inpack_gated && !Slot(hit.bank).InputAcknowledge(hit.io))
        return floating_.Read8("ISA ExCA I/O read without INPACK#", off);
    return static_cast<uint8_t>(MaskIoRead(hit.bank, off, v, 1u));
}

uint16_t IsaExcaSocketPair::ReadCardIo16(uint32_t off) {
    CardIoHit hit;
    if (!DecodeIo(off, &hit) || hit.isolated)
        return floating_.Read16("ISA ExCA I/O", off);
    const uint32_t v = ReadCardIo(Slot(hit.bank), hit.io, hit.width, 2u);
    if (hit.inpack_gated && !Slot(hit.bank).InputAcknowledge(hit.io))
        return floating_.Read16("ISA ExCA I/O read without INPACK#", off);
    return static_cast<uint16_t>(MaskIoRead(hit.bank, off, v, 2u));
}

void IsaExcaSocketPair::WriteCardIo8(uint32_t off, uint8_t value) {
    CardIoHit hit;
    if (!DecodeIo(off, &hit)) {
        floating_.Write("ISA ExCA I/O", 8u, off);
        return;
    }
    if (hit.isolated)
        return;
    WriteCardIo(Slot(hit.bank), hit.io, hit.width, 1u, value);
}

void IsaExcaSocketPair::WriteCardIo16(uint32_t off, uint16_t value) {
    CardIoHit hit;
    if (!DecodeIo(off, &hit)) {
        floating_.Write("ISA ExCA I/O", 16u, off);
        return;
    }
    if (hit.isolated)
        return;
    WriteCardIo(Slot(hit.bank), hit.io, hit.width, 2u, value);
}

void IsaExcaSocketPair::OnCardDetectChanged(PcmciaSlot& slot) {
    sequencer_.Run([&] {
        ExcaSocketUpdates updates{};
        std::lock_guard<std::mutex> lk(state_mutex_);
        const int bank = BankOf(slot);
        Exca(bank).CardDetectChanged(slot.HasCard(), &updates[0].effect);
        CardDetectLocked(bank);
        updates[0].slot = &slot;
        UpdateIrqLineLocked();
        return updates;
    });
}

void IsaExcaSocketPair::OnReadyIreqLow(PcmciaSlot& slot) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    Exca(BankOf(slot)).SetReadyIreqLow(true);
    UpdateIrqLineLocked();
}

void IsaExcaSocketPair::OnReadyIreqHigh(PcmciaSlot& slot) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    Exca(BankOf(slot)).SetReadyIreqLow(false);
    UpdateIrqLineLocked();
}

void IsaExcaSocketPair::SaveState(StateWriter& w) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.Write("index", index_);
        SaveChipGlobal(w);
    }
    for (int bank = 0; bank < kSockets; ++bank) {
        w.BeginFrame(static_cast<uint32_t>(bank));
        {
            std::lock_guard<std::mutex> lk(state_mutex_);
            SaveChipBank(w, bank);
            Exca(bank).SaveState(w);
        }
        Slot(bank).SaveSlotState(w);
        w.EndFrame();
    }
}

void IsaExcaSocketPair::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read("index", index_);
        RestoreChipGlobal(r);
    }
    for (int bank = 0; bank < kSockets; ++bank) {
        if (const uint32_t frame = r.EnterFrame(); frame != static_cast<uint32_t>(bank))
            r.Reject("%s socket frame 0x%X where this build writes socket %d", ChipName(), frame, bank);
        {
            std::lock_guard<std::mutex> lk(state_mutex_);
            RestoreChipBank(r, bank);
            Exca(bank).RestoreState(r);
        }
        Slot(bank).RestoreSlotState(r);
        r.LeaveFrame();
    }
    std::lock_guard<std::mutex> lk(state_mutex_);
    AfterRestoreLocked();
}

void IsaExcaSocketPair::PostRestore() {
    sequencer_.Run([this] {
        std::lock_guard<std::mutex> lk(state_mutex_);
        return ExcaSocketUpdates{{
            {&slot0_, ExcaWriteEffect::Outputs(exca0_.OutputsEnabled())},
            {&slot1_, ExcaWriteEffect::Outputs(exca1_.OutputsEnabled())} }};
    });
    slot0_.PostRestoreSlot();
    slot1_.PostRestoreSlot();
    std::lock_guard<std::mutex> lk(state_mutex_);
    UpdateIrqLineLocked();
}

}
