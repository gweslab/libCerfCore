#include "../../socs/sh77xx/sh77xx_serial_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_data.h"
#include "../../socs/sh77xx/sh77xx_port_wiring.h"
#include "../../socs/sh77xx/sh77xx_scif.h"
#include "hp_600_family_id.h"

#include <atomic>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kPortScDriverPowerOff = 0x08u;
constexpr uint8_t kPortScRtsOff         = 0x40u;
constexpr uint8_t kPortHDtrOff          = 0x80u;

class Hp600Sh7709SerialWiring : public Sh77xxSerialWiring {
public:
    using Sh77xxSerialWiring::Sh77xxSerialWiring;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override {
        ports_ = &emu_.Get<Sh77xxPortWiring>();
        emu_.Get<Sh77xxPortData>().AddDriveListener([this] { TrackNets(); });
        TrackNets();
    }

    std::string CradleLabel(uint32_t channel_base) const override {
        return channel_base == kSh77xxScif2Base ? "Serial port" : std::string();
    }

    bool DriverPowered(uint32_t channel_base) const override {
        return channel_base == kSh77xxScif2Base && powered_.load(std::memory_order_acquire);
    }

    SerialControlLines Control(uint32_t) const override {
        SerialControlLines lines;
        lines.dtr = dtr_.load(std::memory_order_acquire);
        lines.rts = rts_.load(std::memory_order_acquire);
        return lines;
    }

    void SetModemInputs(uint32_t, bool cts, bool, bool, bool dcd) override {
        Sh77xxIntc& intc = emu_.Get<Sh77xxIntc>();
        intc.SetPinLevel(Sh77xxIntcPin::kIrq5, !cts);
        intc.SetPinLevel(Sh77xxIntcPin::kIrq2, !dcd);
    }

    void SetControlListener(uint32_t, std::function<void()> listener) override { listener_ = std::move(listener); }

private:
    void TrackNets() {
        const uint8_t port_sc = ports_->NetLevels(kSh77xxPortSc);
        const uint8_t port_h  = ports_->NetLevels(kSh77xxPortH);
        const bool    powered = (port_sc & kPortScDriverPowerOff) == 0u;
        const bool    dtr     = powered && (port_h & kPortHDtrOff) == 0u;
        const bool    rts     = powered && (port_sc & kPortScRtsOff) == 0u;
        bool changed = powered_.exchange(powered, std::memory_order_acq_rel) != powered;
        if (dtr_.exchange(dtr, std::memory_order_acq_rel) != dtr) changed = true;
        if (rts_.exchange(rts, std::memory_order_acq_rel) != rts) changed = true;
        if (changed && listener_) listener_();
    }

    Sh77xxPortWiring*     ports_ = nullptr;
    std::atomic<bool>     powered_{false};
    std::atomic<bool>     dtr_{false};
    std::atomic<bool>     rts_{false};
    std::function<void()> listener_;
};

}

REGISTER_SERVICE_AS(Hp600Sh7709SerialWiring, Sh77xxSerialWiring);

}
