#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx6_ecspi_endpoint.h"
#include "imx6_ecspi_sdma.h"
#include "imx6_gic.h"

#include <cstdint>
#include <deque>
#include "imx6_id.h"

namespace cerfcore {

namespace cerf_imx6_ecspi_detail {

inline constexpr uint32_t kTestLoopBack = 0x80000000u;

template <uint32_t kBase> class Imx6Ecspi : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        Reset();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            PostRestore();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
        UpdateIrq();
    }

    uint32_t MmioBase() const override { return kBase; }

    void BindSdma(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event) {
        sdma_.Bind(host, tx_event, rx_event, [this] { ServiceSdma(); });
    }

private:
    uint32_t MmioSize() const override { return 0x4000u; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kBase) {
        case 0x00u:
            if (!Enabled()) return 0u;
            HaltUnsupportedAccess("imx6-ecspi CPU read of RXDATA is not modelled; RXFIFO word count",
                                  addr, sdma_.RxLevel());
        case 0x08u: return conreg_;
        case 0x0Cu: return configreg_;
        case 0x10u: return Enabled() ? intreg_ : 0u;
        case 0x14u: return Enabled() ? dmareg_ : 0u;
        case 0x18u: RecomputeStatus(); return statreg_;
        case 0x1Cu: return periodreg_;
        case 0x20u:
            return (sdma_.RxLevel() << 8) | TxLevel();
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kBase) {
        case 0x04u:
            if (!Enabled()) return;
            if (TxLevel() >= Imx6EcspiSdma::kFifoWords)
                HaltUnsupportedAccess("imx6-ecspi TXDATA write with a full transmit FIFO", addr, value);
            tx_fifo_.push_back(value);
            RecomputeStatus();
            return;
        case 0x08u:
            if ((value & (3u << 16)) != 0u)
                HaltUnsupportedAccess("imx6-ecspi CONREG DRCTL SPI_RDY trigger is not modelled", addr, value);
            if ((value & kConSmc) != 0u)
                HaltUnsupportedAccess("imx6-ecspi Start Mode Control is not modelled", addr, value);
            conreg_ = value;
            if (!(value & kConEn)) {
                ClearOnDisable();
            } else if ((value & kConXch) != 0u) {
                StartExchange();
            }
            ServiceSdma();
            return;
        case 0x0Cu:
            if (!Enabled()) return;
            configreg_ = value;
            return;
        case 0x10u:
            if (!Enabled()) return;
            intreg_ = value;
            UpdateIrq();
            return;
        case 0x14u:
            if (!Enabled()) return;
            if ((value & kDmaRxtden) != 0u)
                HaltUnsupportedAccess("imx6-ecspi RXFIFO tail DMA request is not modelled", addr, value);
            dmareg_ = value;
            ServiceSdma();
            return;
        case 0x18u:
            statreg_ &= ~(value & (kStRo | kStTc));
            UpdateIrq();
            return;
        case 0x1Cu:
            if (!Enabled()) return;
            if ((value & ~0x003F0000u) != 0u)
                HaltUnsupportedAccess("imx6-ecspi PERIODREG sample period is not modelled", addr, value);
            periodreg_ = value;
            return;
        case 0x20u:
            if ((value & kTestLoopBack) != 0u)
                HaltUnsupportedAccess("imx6-ecspi TESTREG loop back is not modelled", addr, value);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("conreg", conreg_);
        w.Write("configreg", configreg_);
        w.Write("intreg", intreg_);
        w.Write("dmareg", dmareg_);
        w.Write("statreg", statreg_);
        w.Write("periodreg", periodreg_);
        w.Write("tx_fifo_count", static_cast<uint32_t>(tx_fifo_.size()));
        for (uint32_t v : tx_fifo_)
            w.Write("v", v);
        sdma_.Save(w);
    }
    void RestoreState(StateReader& r) override {
        r.Read("conreg", conreg_);
        r.Read("configreg", configreg_);
        r.Read("intreg", intreg_);
        r.Read("dmareg", dmareg_);
        r.Read("statreg", statreg_);
        r.Read("periodreg", periodreg_);
        tx_fifo_.clear();
        uint32_t n = 0, v = 0;
        r.Read("tx_fifo_count", n);
        for (uint32_t i = 0; i < n; ++i) {
            r.Read("v", v);
            tx_fifo_.push_back(v);
        }
        sdma_.Restore(r);
    }
    void PostRestore() override {
        irq_level_ = false;
        RecomputeStatus();
    }

private:
    void Reset() {
        conreg_ = 0u;
        ClearOnDisable();
        irq_level_ = false;
    }

    void ClearOnDisable() {
        tx_fifo_.clear();
        configreg_ = 0u;
        intreg_ = 0u;
        dmareg_ = 0u;
        periodreg_ = 0u;
        statreg_ = kStTe | kStTdr;
        conreg_ &= ~kConXch;
        sdma_.FlushFifos();
    }

    Imx6EcspiEndpoint* Endpoint() const {
        auto* endpoint = emu_.TryGet<Imx6EcspiEndpoint>();
        return endpoint && endpoint->EcspiBase() == kBase ? endpoint : nullptr;
    }

    void ServiceSdma() {
        sdma_.Service(Endpoint(), Enabled() ? dmareg_ : 0u, static_cast<uint32_t>(tx_fifo_.size()));
        RecomputeStatus();
    }

    void StartExchange() {
        auto* endpoint = Endpoint();
        if (!endpoint)
            HaltUnsupportedAccess("imx6-ecspi XCH with no device wired to this instance", kBase + 0x08u, conreg_);
        if (!tx_fifo_.empty())
            HaltUnsupportedAccess("imx6-ecspi XCH with TXDATA words for a DMA-fed device", kBase + 0x08u, conreg_);
        conreg_ &= ~kConXch;
        const uint32_t burst_bits = ((conreg_ >> 20) & 0xFFFu) + 1u;
        sdma_.Exchange(endpoint, dmareg_, static_cast<uint32_t>(tx_fifo_.size()), (burst_bits + 31u) / 32u,
                       conreg_, configreg_);
        statreg_ |= kStTc;
    }
    void RecomputeStatus() {
        uint32_t s = statreg_ & (kStRo | kStTc);
        if (!Enabled()) {
            statreg_ = kStTe | kStTdr;
            UpdateIrq();
            return;
        }
        const uint32_t tx = TxLevel();
        const uint32_t rx = sdma_.RxLevel();
        if (tx == 0u) s |= kStTe;
        if (tx <= (dmareg_ & 0x3Fu)) s |= kStTdr;
        if (tx >= Imx6EcspiSdma::kFifoWords) s |= kStTf;
        if (rx != 0u) s |= kStRr;
        if (rx > ((dmareg_ >> 16) & 0x3Fu)) s |= kStRdr;
        if (rx >= Imx6EcspiSdma::kFifoWords) s |= kStRf;
        statreg_ = s;
        UpdateIrq();
    }

    void UpdateIrq() {
        const bool level = Enabled() && (statreg_ & intreg_ & 0xFFu) != 0u;
        if (level == irq_level_) return;
        irq_level_ = level;
        if (level)
            emu_.Get<Imx6Gic>().AssertSpi(IrqSpi());
        else
            emu_.Get<Imx6Gic>().DeAssertSpi(IrqSpi());
    }

    static constexpr uint32_t IrqSpi() {
        if constexpr (kBase == 0x02008000u)
            return 31u;
        else if constexpr (kBase == 0x0200C000u)
            return 32u;
        else if constexpr (kBase == 0x02010000u)
            return 33u;
        else if constexpr (kBase == 0x02014000u)
            return 34u;
        else
            return 35u;
    }

    bool Enabled() const { return (conreg_ & kConEn) != 0; }
    uint32_t TxLevel() const { return static_cast<uint32_t>(tx_fifo_.size()) + sdma_.TxLevel(); }

    static constexpr uint32_t kConEn = 1u << 0;
    static constexpr uint32_t kConXch = 1u << 2;
    static constexpr uint32_t kConSmc = 1u << 3;
    static constexpr uint32_t kStTe = 1u << 0;
    static constexpr uint32_t kStTdr = 1u << 1;
    static constexpr uint32_t kStTf = 1u << 2;
    static constexpr uint32_t kStRr = 1u << 3;
    static constexpr uint32_t kStRdr = 1u << 4;
    static constexpr uint32_t kStRf = 1u << 5;
    static constexpr uint32_t kStRo = 1u << 6;
    static constexpr uint32_t kStTc = 1u << 7;
    static constexpr uint32_t kDmaRxtden = 1u << 31;

    uint32_t conreg_ = 0, configreg_ = 0, intreg_ = 0, dmareg_ = 0, periodreg_ = 0;
    uint32_t statreg_ = kStTe | kStTdr;
    bool irq_level_ = false;
    std::deque<uint32_t> tx_fifo_;
    Imx6EcspiSdma sdma_{emu_};
};

}

using cerf_imx6_ecspi_detail::Imx6Ecspi;

}
