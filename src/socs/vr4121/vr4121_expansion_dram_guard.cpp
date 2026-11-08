#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "vr4121_dram_decode.h"
#include "vr4121_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class Vr4121ExpansionDramGuard : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr4121 &&
               emu_.Get<Vr4121DramDecode>().ExpansionChipFitted();
    }

    void OnReady() override {
        auto& decode = emu_.Get<Vr4121DramDecode>();
        base_ = decode.ExpansionBase();
        size_ = decode.ExpansionEnd() - base_;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return size_; }

    uint8_t  ReadByte (uint32_t addr) override { Halt("ReadByte", addr, 0u); }
    uint16_t ReadHalf (uint32_t addr) override { Halt("ReadHalf", addr, 0u); }
    uint32_t ReadWord (uint32_t addr) override { Halt("ReadWord", addr, 0u); }
    uint64_t ReadDword(uint32_t addr) override { Halt("ReadDword", addr, 0u); }
    void WriteByte (uint32_t addr, uint8_t v)  override { Halt("WriteByte", addr, v); }
    void WriteHalf (uint32_t addr, uint16_t v) override { Halt("WriteHalf", addr, v); }
    void WriteWord (uint32_t addr, uint32_t v) override { Halt("WriteWord", addr, v); }
    void WriteDword(uint32_t addr, uint64_t v) override { Halt("WriteDword", addr, static_cast<uint32_t>(v)); }

private:
    [[noreturn]] void Halt(const char* op, uint32_t addr, uint32_t value) {
        HaltUnsupportedAccess(op, addr, value);
    }

    uint32_t base_ = 0;
    uint32_t size_ = 0;
};

}

REGISTER_SERVICE(Vr4121ExpansionDramGuard);

}
