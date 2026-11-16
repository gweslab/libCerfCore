#include "imx6_ecspi_sdma.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_ecspi_endpoint.h"

#include <algorithm>
#include <string>

namespace cerfcore {

namespace {

uint32_t TxThreshold(uint32_t dmareg) { return dmareg & 0x3Fu; }
uint32_t RxThreshold(uint32_t dmareg) { return (dmareg >> 16) & 0x3Fu; }

}

void Imx6EcspiSdma::Bind(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event,
                         std::function<void()> on_claim) {
    if (host_ == host) return;
    host_     = host;
    tx_event_ = tx_event;
    rx_event_ = rx_event;
    on_claim_ = std::move(on_claim);
    host->RegisterChannelSink(this);
}

bool Imx6EcspiSdma::HandlesEvent(int event) const {
    return host_ != nullptr &&
           (event == static_cast<int>(tx_event_) || event == static_cast<int>(rx_event_));
}

bool Imx6EcspiSdma::ClaimChannel(const FreescaleSdmaChannelStart& start) {
    if (!HandlesEvent(start.event)) return false;
    Direction& d = start.event == static_cast<int>(tx_event_) ? tx_ : rx_;
    uint32_t watermark = 0u;
    if (!host_->ChannelWatermark(start.channel, watermark) || watermark == 0u || (watermark & 3u) != 0u)
        emu_.Get<Fatal>().Die("[ECSPI] SDMA channel %u watermark %u is not a whole number of 32-bit words",
                              start.channel, watermark);
    d.ring.Begin(start);
    d.active = true;
    d.open   = false;
    d.burst  = watermark / 4u;
    on_claim_();
    return true;
}

void Imx6EcspiSdma::ReleaseChannel(uint32_t channel) {
    if (tx_.active && channel == tx_.ring.Channel()) tx_.active = tx_.open = false;
    if (rx_.active && channel == rx_.ring.Channel()) rx_.active = rx_.open = false;
}

void Imx6EcspiSdma::OnSdmaClock(bool running) {
    const bool resumed = running && !sdma_clocked_;
    sdma_clocked_ = running;
    if (resumed) on_claim_();
}

void Imx6EcspiSdma::FlushFifos() {
    tx_level_      = 0u;
    rx_level_      = 0u;
    rx_data_ready_ = false;
}

bool Imx6EcspiSdma::Open(Direction& d) {
    if (d.open) return true;
    FreescaleSdmaBd bd;
    if (!d.ring.Open(bd)) {
        d.active = false;
        host_->StopChannel(d.ring.Channel());
        return false;
    }
    if ((bd.count & 3u) != 0u)
        emu_.Get<Fatal>().Die("[ECSPI] SDMA BD 0x%08X count %u is not whole 32-bit words", bd.pa, bd.count);
    d.open   = true;
    d.buffer = bd.buffer;
    d.count  = bd.count;
    d.moved  = 0u;
    d.cont   = bd.cont;
    return true;
}

void Imx6EcspiSdma::Retire(Direction& d) {
    d.open = false;
    if (!d.ring.Retire(*host_, d.cont)) d.active = false;
}

void Imx6EcspiSdma::ServiceTx(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words) {
    while (tx_.active && (dmareg & kDmaTeden) != 0u && tx_level_ + tx_data_words <= TxThreshold(dmareg) &&
           tx_level_ + tx_data_words < kFifoWords) {
        if (!Open(tx_)) return;
        const uint32_t words = tx_.count / 4u;
        const uint32_t n = std::min({tx_.burst, kFifoWords - tx_level_ - tx_data_words, words - tx_.moved});
        tx_level_ += n;
        tx_.moved += n;
        if (tx_.moved != words) continue;
        if (endpoint->HasStagedTransmit())
            emu_.Get<Fatal>().Die("[ECSPI] SDMA transmit BD ends while the previous burst is still unsent");
        endpoint->StageDmaTransmit(tx_.buffer, tx_.count);
        Retire(tx_);
    }
}

void Imx6EcspiSdma::ServiceRx(Imx6EcspiEndpoint* endpoint, uint32_t dmareg) {
    for (;;) {
        if (rx_.open && rx_.moved == rx_.count / 4u) {
            if (!rx_data_ready_) return;
            endpoint->StageDmaReceive(rx_.buffer, rx_.count);
            rx_data_ready_ = false;
            Retire(rx_);
        }
        if (!rx_.active || (dmareg & kDmaRxden) == 0u || rx_level_ <= RxThreshold(dmareg)) return;
        if (!Open(rx_)) return;
        const uint32_t n = std::min({rx_.burst, rx_level_, rx_.count / 4u - rx_.moved});
        rx_level_ -= n;
        rx_.moved += n;
    }
}

void Imx6EcspiSdma::Service(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words) {
    if (!sdma_clocked_) return;
    if (endpoint == nullptr) {
        if ((tx_.active && (dmareg & kDmaTeden) != 0u) || (rx_.active && (dmareg & kDmaRxden) != 0u))
            emu_.Get<Fatal>().Die("[ECSPI] SDMA request with no device wired to this instance");
        return;
    }
    ServiceTx(endpoint, dmareg, tx_data_words);
    ServiceRx(endpoint, dmareg);
}

void Imx6EcspiSdma::Exchange(Imx6EcspiEndpoint* endpoint, uint32_t dmareg, uint32_t tx_data_words,
                             uint32_t words, uint32_t conreg, uint32_t configreg) {
    for (uint32_t i = 0; i < words; ++i) {
        if (tx_level_ == 0u) Service(endpoint, dmareg, tx_data_words);
        if (tx_level_ == 0u)
            emu_.Get<Fatal>().Die("[ECSPI] TXFIFO runs empty at word %u of a %u-word burst", i, words);
        --tx_level_;
        if (rx_level_ == kFifoWords)
            emu_.Get<Fatal>().Die("[ECSPI] RXFIFO overflow at word %u of a %u-word burst", i, words);
        ++rx_level_;
        Service(endpoint, dmareg, tx_data_words);
    }
    endpoint->Exchange(conreg, configreg);
    rx_data_ready_ = true;
    Service(endpoint, dmareg, tx_data_words);
}

void Imx6EcspiSdma::SaveDirection(StateWriter& w, const Direction& d, const char* prefix) const {
    const std::string p(prefix);
    d.ring.Save(w, prefix);
    w.Write<uint8_t>((p + "active").c_str(), d.active ? 1u : 0u);
    w.Write<uint8_t>((p + "open").c_str(), d.open ? 1u : 0u);
    w.Write((p + "burst").c_str(), d.burst);
    w.Write((p + "buffer").c_str(), d.buffer);
    w.Write((p + "count").c_str(), d.count);
    w.Write((p + "moved").c_str(), d.moved);
    w.Write<uint8_t>((p + "cont").c_str(), d.cont ? 1u : 0u);
}

void Imx6EcspiSdma::RestoreDirection(StateReader& r, Direction& d, const char* prefix) {
    const std::string p(prefix);
    uint8_t active = 0, open = 0, cont = 0;
    d.ring.Restore(r, prefix);
    r.Read((p + "active").c_str(), active);
    r.Read((p + "open").c_str(), open);
    r.Read((p + "burst").c_str(), d.burst);
    r.Read((p + "buffer").c_str(), d.buffer);
    r.Read((p + "count").c_str(), d.count);
    r.Read((p + "moved").c_str(), d.moved);
    r.Read((p + "cont").c_str(), cont);
    d.active = active != 0u;
    d.open   = open != 0u;
    d.cont   = cont != 0u;
}

void Imx6EcspiSdma::Save(StateWriter& w) const {
    SaveDirection(w, tx_, "ecspi_tx_");
    SaveDirection(w, rx_, "ecspi_rx_");
    w.Write("ecspi_tx_level", tx_level_);
    w.Write("ecspi_rx_level", rx_level_);
    w.Write<uint8_t>("ecspi_rx_data_ready", rx_data_ready_ ? 1u : 0u);
}

void Imx6EcspiSdma::Restore(StateReader& r) {
    uint8_t rx_data_ready = 0;
    RestoreDirection(r, tx_, "ecspi_tx_");
    RestoreDirection(r, rx_, "ecspi_rx_");
    r.Read("ecspi_tx_level", tx_level_);
    r.Read("ecspi_rx_level", rx_level_);
    r.Read("ecspi_rx_data_ready", rx_data_ready);
    rx_data_ready_ = rx_data_ready != 0u;
}

}
