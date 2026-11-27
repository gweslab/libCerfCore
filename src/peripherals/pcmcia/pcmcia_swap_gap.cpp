#include "pcmcia_swap_gap.h"

#include "pcmcia_card.h"
#include "pcmcia_card_catalog.h"
#include "pcmcia_card_identity.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../core/deferred_jobs.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <utility>

namespace cerfcore {

namespace {

constexpr int64_t kSwapGapNs = 1'500'000'000;

}

PcmciaSwapGap::PcmciaSwapGap(CerfEmulator& emu, Seat seat) : emu_(emu), seat_(std::move(seat)) {}

PcmciaSwapGap::~PcmciaSwapGap() = default;

void PcmciaSwapGap::Open() {
    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        open_    = true;
        started_ = false;
        held_.reset();
        seq = ++seq_;
    }
    PostStart(seq);
}

bool PcmciaSwapGap::Hold(std::unique_ptr<PcmciaCard>& card, bool& replaced) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_) return false;
    replaced = held_ != nullptr;
    held_    = std::move(card);
    return true;
}

bool PcmciaSwapGap::Drop() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!open_ || !held_) return false;
    held_.reset();
    return true;
}

std::optional<InsertedItem> PcmciaSwapGap::HeldItem() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!held_) return std::nullopt;
    return InsertedItem{held_->SaveId(), held_->DisplayName(), held_->Facts()};
}

void PcmciaSwapGap::PostStart(uint64_t seq) {
    emu_.Get<DeferredJobs>().PostToGuestThread([this, seq] { Start(seq); });
}

void PcmciaSwapGap::Start(uint64_t seq) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!open_ || seq != seq_) return;
        started_     = true;
        armed_seq_   = seq;
        deadline_ns_ = emu_.Get<GuestCycleClock>().NowNs() + kSwapGapNs;
    }
    ArmAtDeadline();
}

void PcmciaSwapGap::ArmAtDeadline() {
    int64_t deadline = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!open_ || !started_) return;
        deadline = deadline_ns_;
    }
    auto& clock = emu_.Get<GuestCycleClock>();
    clock.Arm(Event(), clock.CycleAtNs(deadline));
}

void PcmciaSwapGap::Close() {
    std::unique_ptr<PcmciaCard> card;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!open_ || !started_ || armed_seq_ != seq_) return;
        open_    = false;
        started_ = false;
        card     = std::move(held_);
    }
    if (card) seat_(std::move(card));
}

GuestCycleClock::Event* PcmciaSwapGap::Event() {
    if (!event_) {
        auto& clock = emu_.Get<GuestCycleClock>();
        event_      = clock.Add([this] { Close(); });
        clock.RegisterRateListener([this] { ArmAtDeadline(); });
    }
    return event_;
}

void PcmciaSwapGap::Save(StateWriter& w) {
    const int64_t now = emu_.Get<GuestCycleClock>().NowNs();
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write<uint8_t>("gap_open", open_ ? 1u : 0u);
    w.Write<uint8_t>("gap_started", started_ ? 1u : 0u);
    w.Write<int64_t>("gap_left_ns", started_ ? std::max<int64_t>(deadline_ns_ - now, 0) : 0);
    w.Write<uint8_t>("held", held_ ? 1u : 0u);
    if (held_) PcmciaCardIdentity{held_->SaveId(), held_->SaveBinding()}.Save(w, "held_");
}

void PcmciaSwapGap::Restore(StateReader& r) {
    uint8_t open = 0, started = 0, held = 0;
    int64_t left = 0;
    r.Read("gap_open", open);
    r.Read("gap_started", started);
    r.Read("gap_left_ns", left);
    r.Read("held", held);
    std::unique_ptr<PcmciaCard> card;
    if (held != 0u) {
        const PcmciaCardIdentity identity = PcmciaCardIdentity::Restore(r, "held_");
        card = emu_.Get<PcmciaCardCatalog>().TryCreate(identity.id, identity.binding);
        if (!card) r.Reject("PC Card '%s' is unknown to this build", identity.id.c_str());
    }
    std::lock_guard<std::mutex> lk(mtx_);
    open_      = open != 0u;
    started_   = started != 0u;
    left_ns_   = left;
    held_      = std::move(card);
    armed_seq_ = ++seq_;
}

void PcmciaSwapGap::PostRestore() {
    uint64_t seq     = 0;
    bool     started = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!open_) {
            emu_.Get<GuestCycleClock>().Disarm(Event());
            return;
        }
        started = started_;
        seq     = seq_;
        if (started) deadline_ns_ = emu_.Get<GuestCycleClock>().NowNs() + left_ns_;
    }
    if (started) ArmAtDeadline();
    else         PostStart(seq);
}

}
