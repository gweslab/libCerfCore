#include "../../execution/guest_cycle_clock.h"
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_pads.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../../socs/imx6/imx6_iomuxc.h"
#include "../../socs/imx6/imx6_uart2.h"
#include "../../socs/imx6/imx6_variant.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include "siemens_simatic_mobile_panels_gen2_conn_box_identity.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2ConnBoxGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }
    void OnReady() override {
        const Imx6GpioPadTable& pads = Imx6GpioPadsFor(emu_.Get<Imx6Variant>().Family());
        tx_pad_ = FindImx6GpioPad(pads, 1u, 7u);
        rx_pad_ = FindImx6GpioPad(pads, 1u, 8u);
        clock_ = &emu_.Get<GuestCycleClock>();
        ack_ = clock_->Add([this] { Acknowledge(); });
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
    }

    uint32_t GpioBase() const override { return 0x0209C000u; }

    uint32_t DrivenPins() const override { return kRxMask; }
    uint32_t ApplyPadInputs(uint32_t inputs) override { return rx_high_ ? (inputs | kRxMask) : (inputs & ~kRxMask); }

    void OnEffectiveOutputs(uint32_t dr, uint32_t gdir) override {
        if ((gdir & kTxMask) == 0u || !PadInMode(tx_pad_, tx_pad_->gpio_mode)) return;
        const bool tx_high = (dr & kTxMask) != 0u;
        if (tx_high == tx_high_) return;
        tx_high_ = tx_high;
        if (!tx_high) {
            clock_->Disarm(ack_);
            ack_at_ = 0u;
            rx_high_ = false;
            Reevaluate();
        } else {
            ack_at_ = clock_->Cycles() + kAckCycles;
            clock_->Arm(ack_, ack_at_);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write("tx_high", static_cast<uint8_t>(tx_high_ ? 1u : 0u));
        w.Write("rx_high", static_cast<uint8_t>(rx_high_ ? 1u : 0u));
        w.Write("ack_at", ack_at_);
    }
    void RestoreState(StateReader& r) override {
        uint8_t tx = 0;
        uint8_t rx = 0;
        r.Read("tx_high", tx);
        r.Read("rx_high", rx);
        r.Read("ack_at", ack_at_);
        tx_high_ = tx != 0;
        rx_high_ = rx != 0;
        clock_->Disarm(ack_);
        if (ack_at_ != 0u) clock_->Arm(ack_, ack_at_);
    }
    void OnControllerReset() override {
        clock_->Disarm(ack_);
        ack_at_ = 0u;
        tx_high_ = true;
        rx_high_ = true;
    }

private:
    static constexpr uint32_t kTxMask          = 1u << 7u;
    static constexpr uint32_t kRxMask          = 1u << 8u;
    static constexpr uint32_t kUartMode        = 4u;
    static constexpr uint64_t kAckCycles       = 64u; /* Estimated response delay of the box. */
    static constexpr uint8_t kBoxType3Response = 0xA0u;

    bool PadInMode(const Imx6GpioPad* pad, uint32_t mode) const {
        return (emu_.Get<Imx6Iomuxc>().PadRegister(pad->mux_offset) & kImx6PadMuxMode) == mode;
    }

    void Acknowledge() {
        ack_at_ = 0u;
        rx_high_ = true;
        Reevaluate();
        if (!PadInMode(rx_pad_, kUartMode)) return;
        const uint8_t id[] = {siemens_simatic_mobile_panels_gen2::kConnBoxId, kBoxType3Response};
        emu_.Get<Imx6Uart2>().InjectRx(id, sizeof(id));
    }

    const Imx6GpioPad* tx_pad_ = nullptr;
    const Imx6GpioPad* rx_pad_ = nullptr;
    GuestCycleClock* clock_ = nullptr;
    GuestCycleClock::Event* ack_ = nullptr;
    uint64_t ack_at_ = 0u;
    bool tx_high_ = true;
    bool rx_high_ = true;
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2ConnBoxGpio);

}
