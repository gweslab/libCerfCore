#include "../../socs/sh77xx/sh77xx_pcmcia_card_bus.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "../pcmcia/pcmcia_host_cycles.h"
#include "hd64461_pcc.h"

namespace cerfcore {

namespace {

class Hd64461PccCardBus : public Sh77xxPcmciaCardBus {
public:
    using Sh77xxPcmciaCardBus::Sh77xxPcmciaCardBus;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    void OnReady() override { pcc_ = &emu_.Get<Hd64461Pcc>(); }

    uint32_t ReadCycles(uint32_t area, uint32_t offset, bool io, PcmciaIoWidth width, unsigned bytes) override {
        const Hd64461Pcc::CardCycle c = pcc_->DecodeCycle(area, offset, io, false);
        if (io) return ReadCardIo(*c.slot, c.card_address, width, bytes);
        return ReadCardMemory(*c.slot, c.card_address, c.attribute, width != PcmciaIoWidth::kEightBit, bytes);
    }

    void WriteCycles(uint32_t area, uint32_t offset, bool io, PcmciaIoWidth width, unsigned bytes,
                     uint32_t value) override {
        const Hd64461Pcc::CardCycle c = pcc_->DecodeCycle(area, offset, io, true);
        if (c.dropped) return;
        if (io) {
            WriteCardIo(*c.slot, c.card_address, width, bytes, value);
            return;
        }
        WriteCardMemory(*c.slot, c.card_address, c.attribute, width != PcmciaIoWidth::kEightBit, bytes, value);
    }

private:
    Hd64461Pcc* pcc_ = nullptr;
};

}

REGISTER_SERVICE_AS(Hd64461PccCardBus, Sh77xxPcmciaCardBus);

}
