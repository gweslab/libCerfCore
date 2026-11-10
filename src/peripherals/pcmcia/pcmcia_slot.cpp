#include "pcmcia_slot.h"

#include "pcmcia_card_catalog.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/product_name.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/state/state_stream.h>

#include <chrono>
#include <thread>
#include <type_traits>
#include <utility>

namespace cerfcore {

uint16_t PcmciaSlot::UndrivenDataWord() const {
    const uint8_t lane = host_.UndrivenDataLane();
    return static_cast<uint16_t>((lane << 8) | lane);
}

PcmciaSlot::PcmciaSlot(CerfEmulator& emu, PcmciaSlotHost& host,
                       std::string label)
    : emu_(emu), host_(host), label_(std::move(label)) {}

void PcmciaSlot::PublishUiLocked() {
    UiView view;
    view.generation = generation_;
    if (card_) {
        view.has_card       = true;
        view.icon           = card_->IconResource();
        view.tooltip_detail = card_->TooltipDetail();
        view.save_id        = card_->SaveId();
        view.display_name   = card_->DisplayName();
        for (auto& c : card_->CardControls())
            view.card_controls.push_back(GuardControlLocked(std::move(c), generation_));
    }
    std::lock_guard<std::mutex> lk(ui_mtx_);
    ui_ = std::move(view);
}

void PcmciaSlot::PublishPinsLocked() {
    uint8_t v = (card_ ? kPinPresent : 0u) | (powered_ ? kPinPowered : 0u);
    if (card_) {
        const PcmciaVoltageSense vs = card_->VoltageSense();
        if (vs.vs1_grounded)    v |= kPinVs1Grounded;
        if (vs.vs2_grounded)    v |= kPinVs2Grounded;
        if (vs.low_voltage_key) v |= kPinLowVoltageKey;
    }
    pins_.store(v, std::memory_order_release);
}

void PcmciaSlot::SetPowered(bool on) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (powered_ == on) return;
        powered_ = on;
        LOG(Pcmcia, "[Slot %s] Vcc %s\n", label_.c_str(), on ? "on" : "off");
        PublishPinsLocked();
        if (card_) {
            if (on) card_->PowerOn();
            else    card_->PowerOff();
        }
        if (!on) DriveLine(kLineBusy, false);
    }
    host_.OnCardPowerChanged(*this);
}

void PcmciaSlot::PowerCycleCard() {
    if (!HasCard()) return;
    SetPowered(false);
    SetPowered(true);
}

void PcmciaSlot::SetResetLine(bool asserted) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (reset_asserted_ == asserted) return;
    reset_asserted_ = asserted;
    if (!card_ || !powered_) return;
    if (asserted) {
        if (card_->HoldsReadyBusyInReset()) DriveLine(kLineBusy, true);
        return;
    }
    card_->SocketReset();
    DriveLine(kLineBusy, false);
}

void PcmciaSlot::DriveLine(uint8_t source, bool low) {
    const uint8_t before = low
        ? line_.fetch_or(source, std::memory_order_acq_rel)
        : line_.fetch_and(static_cast<uint8_t>(~source), std::memory_order_acq_rel);
    const uint8_t after = low ? static_cast<uint8_t>(before | source)
                              : static_cast<uint8_t>(before & ~source);
    if ((before != 0u) == (after != 0u)) return;
    if (after != 0u) host_.OnReadyIreqLow(*this);
    else             host_.OnReadyIreqHigh(*this);
}

void PcmciaSlot::SetCardEnabled(bool on) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    enabled_ = on;
}

bool PcmciaSlot::CardSelectedLocked() {
    return card_ && powered_ && enabled_;
}

uint8_t PcmciaSlot::ReadAttribute8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return host_.UndrivenDataLane();
    return card_->ReadAttribute8(offset);
}

void PcmciaSlot::WriteAttribute8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return;
    card_->WriteAttribute8(offset, value);
}

uint16_t PcmciaSlot::ReadAttribute16(uint32_t offset) {
    return static_cast<uint16_t>((host_.UndrivenDataLane() << 8) | ReadAttribute8(offset & ~1u));
}

void PcmciaSlot::WriteAttribute16(uint32_t offset, uint16_t value) {
    WriteAttribute8(offset & ~1u, static_cast<uint8_t>(value));
}

uint8_t PcmciaSlot::ReadCommon8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return host_.UndrivenDataLane();
    return card_->ReadCommon8(offset);
}

uint16_t PcmciaSlot::ReadCommon16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return UndrivenDataWord();
    return card_->ReadCommon16(offset);
}

void PcmciaSlot::WriteCommon8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return;
    card_->WriteCommon8(offset, value);
}

void PcmciaSlot::WriteCommon16(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return;
    card_->WriteCommon16(offset, value);
}

uint8_t PcmciaSlot::ReadIo8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return host_.UndrivenDataLane();
    return card_->ReadIo8(offset);
}

uint16_t PcmciaSlot::ReadIo16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return UndrivenDataWord();
    return card_->ReadIo16(offset);
}

void PcmciaSlot::WriteIo8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return;
    card_->WriteIo8(offset, value);
}

void PcmciaSlot::WriteIo16(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked()) return;
    card_->WriteIo16(offset, value);
}

bool PcmciaSlot::Iois16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked())
        return false;
    return card_->Iois16(offset);
}

bool PcmciaSlot::InputAcknowledge(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!CardSelectedLocked())
        return false;
    return card_->InputAcknowledge(offset);
}

void PcmciaSlot::RaiseIrq() { DriveLine(kLineIreq, true); }
void PcmciaSlot::ClearIrq() { DriveLine(kLineIreq, false); }

ExternalDeviceClock::Timer& PcmciaSlot::CardTimer() {
    if (!card_timer_) card_timer_ = &emu_.Get<ExternalDeviceClock>().AddTimer([this] { OnCardTimer(); });
    return *card_timer_;
}

void PcmciaSlot::ArmCardTimer(int64_t delay_ns) {
    emu_.Get<ExternalDeviceClock>().Arm(CardTimer(), delay_ns);
}

void PcmciaSlot::DisarmCardTimer() { emu_.Get<ExternalDeviceClock>().Disarm(CardTimer()); }

void PcmciaSlot::OnCardTimer() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (card_) card_->OnSlotTimer();
}

void PcmciaSlot::InsertLocked(std::unique_ptr<PcmciaCard> card) {
    card_ = std::move(card);
    ++generation_;
    PublishPinsLocked();
    card_->AttachSlot(this, generation_);
    card_->OnInserted();
    if (powered_) card_->PowerOn();
    PublishUiLocked();
    LOG(Pcmcia, "[Slot %s] inserted: %s\n", label_.c_str(),
        card_->DisplayName().c_str());
}

void PcmciaSlot::EjectLocked() {
    LOG(Pcmcia, "[Slot %s] ejected: %s\n", label_.c_str(),
        card_->DisplayName().c_str());
    if (powered_) card_->PowerOff();
    card_.reset();
    ++generation_;
    PublishPinsLocked();
    PublishUiLocked();
    DriveLine(kLineIreq | kLineBusy, false);
}

void PcmciaSlot::InsertCard(std::unique_ptr<PcmciaCard> card) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (card_) {
            LOG(Caution, "[Slot %s] InsertCard into occupied slot "
                    "(%s) - rejected\n",
                label_.c_str(), card_->DisplayName().c_str());
            return;
        }
        InsertLocked(std::move(card));
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::OnShutdown() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (card_) card_->OnShutdown();
}

void PcmciaSlot::SaveSlotState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    w.Write<uint8_t>("powered", powered_ ? 1u : 0u);
    w.Write<uint8_t>("reset_line", reset_asserted_ ? 1u : 0u);
    w.Write<uint8_t>("card_busy", (line_.load(std::memory_order_acquire) & kLineBusy) != 0u ? 1u : 0u);
    emu_.Get<ExternalDeviceClock>().SaveTimer(CardTimer(), w, "card_timer_");
    const bool has = (card_ != nullptr);
    w.Write<uint8_t>("has", has ? 1u : 0u);
    if (!has) return;
    const std::string id = card_->SaveId();
    w.Write<uint32_t>("id_count", static_cast<uint32_t>(id.size()));
    if (!id.empty()) w.WriteBytes("id", id.data(), id.size());
    const std::u16string binding = Utf8ToUtf16(card_->SaveBinding());
    w.Write<uint32_t>("binding_count", static_cast<uint32_t>(binding.size()));
    if (!binding.empty())
        w.WriteBytes("binding", binding.data(), binding.size() * sizeof(char16_t));
    w.BeginFrame(0);
    card_->SaveState(w);
    w.EndFrame();
}

void PcmciaSlot::PostRestoreSlot() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    emu_.Get<ExternalDeviceClock>().ResumeTimer(CardTimer());
    if (card_) card_->PostRestore();
}

void PcmciaSlot::RestoreSlotState(StateReader& r) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    uint8_t powered = 0, reset_line = 0, card_busy = 0, has = 0;
    r.Read("powered", powered);
    r.Read("reset_line", reset_line);
    r.Read("card_busy", card_busy);
    emu_.Get<ExternalDeviceClock>().RestoreTimer(CardTimer(), r, "card_timer_");
    r.Read("has", has);
    reset_asserted_ = reset_line != 0u;
    if (!has) {
        if (card_) EjectLocked();
        powered_ = (powered != 0);
        PublishPinsLocked();
        line_.store(card_busy != 0u ? kLineBusy : 0u, std::memory_order_release);
        return;
    }
    uint32_t idlen = 0;
    r.Read("id_count", idlen);
    std::string id(static_cast<size_t>(idlen), '\0');
    if (idlen) r.ReadBytes("id", &id[0], idlen);
    uint32_t blen = 0;
    r.Read("binding_count", blen);
    std::u16string units(static_cast<size_t>(blen), u'\0');
    if (blen) r.ReadBytes("binding", &units[0], blen * sizeof(char16_t));
    const std::string binding = Utf16ToUtf8(units);
    if (const uint32_t frame = r.EnterFrame(); frame != 0u)
        r.Reject("PC Card body in frame 0x%X, this build writes frame 0", frame);
    /* Resume the exact desktop: rebuild the saved card from its id + host
       binding when the socket is empty or holds a different card, then apply
       its register state. */
    if (!card_ || id != card_->SaveId()) {
        auto card = emu_.Get<PcmciaCardCatalog>().TryCreate(id, binding);
        if (!card) r.Reject("PC Card '%s' is unknown to this build", id.c_str());
        if (card_) EjectLocked();
        powered_ = (powered != 0);
        PublishPinsLocked();
        InsertLocked(std::move(card));
    } else if (powered_ != (powered != 0)) {
        powered_ = (powered != 0);
        PublishPinsLocked();
        if (powered_) card_->PowerOn();
        else          card_->PowerOff();
    }
    card_->RestoreState(r);
    r.LeaveFrame();
    line_.store(card_busy != 0u ? kLineBusy : 0u, std::memory_order_release);
    PublishUiLocked();
}

void PcmciaSlot::EjectCardIfResident(uint64_t card_id) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (!card_ || card_->CardId() != card_id) return;
        EjectLocked();
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::MenuEject(uint64_t gen) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen || !card_) return;
        EjectLocked();
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::CombinedSwap(uint64_t gen, std::unique_ptr<PcmciaCard> card) {
    bool ejected = false;
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen) return;
        if (card_) {
            EjectLocked();
            ejected = true;
        }
    }
    if (ejected) {
        host_.OnCardDetectChanged(*this);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (card_) return;
        InsertLocked(std::move(card));
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::MenuInsert(uint64_t gen, const std::string& card_id) {
    CombinedSwap(gen, emu_.Get<PcmciaCardCatalog>().Create(card_id));
}

void PcmciaSlot::MenuInsertCard(uint64_t gen, std::unique_ptr<PcmciaCard> card) {
    if (!card) return;
    CombinedSwap(gen, std::move(card));
}

std::vector<SlotEntry> PcmciaSlot::Available(uint64_t gen) {
    std::vector<SlotEntry> entries;
    for (const auto& e : emu_.Get<PcmciaCardCatalog>().Entries()) {
        SlotEntry entry;
        if (e.build_entry) {
            entry = e.build_entry([this, gen](std::unique_ptr<PcmciaCard> card) {
                MenuInsertCard(gen, std::move(card));
            });
        } else {
            entry.insert = [this, gen, id = e.id] { MenuInsert(gen, id); };
        }
        entry.id    = e.id;
        entry.label = e.display_name;
        entries.push_back(std::move(entry));
    }
    return entries;
}

template <class Fn>
Fn PcmciaSlot::GuardLocked(Fn fn, uint64_t gen) {
    if (!fn) return fn;
    return [this, gen, inner = std::move(fn)](auto&&... args) {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen) return;
        inner(std::forward<decltype(args)>(args)...);
    };
}

WidgetControl PcmciaSlot::GuardControlLocked(WidgetControl control, uint64_t gen) {
    std::visit([&](auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ActionControl>) {
            v.press = GuardLocked(std::move(v.press), gen);
        } else if constexpr (std::is_same_v<T, ToggleControl>) {
            v.set = GuardLocked(std::move(v.set), gen);
        } else if constexpr (std::is_same_v<T, ChoiceControl>) {
            v.select = GuardLocked(std::move(v.select), gen);
        } else if constexpr (std::is_same_v<T, GroupControl>) {
            for (auto& child : v.children) child = GuardControlLocked(std::move(child), gen);
        } else if constexpr (std::is_same_v<T, SlotControl>) {
            emu_.Get<Fatal>().Die("PcmciaSlot: a card exposes a nested slot control");
        }
    }, control.control);
    return control;
}

std::vector<WidgetControl> PcmciaSlot::Controls() {
    UiView view;
    {
        std::lock_guard<std::mutex> lk(ui_mtx_);
        view = ui_;
    }
    const uint64_t gen = view.generation;

    SlotControl slot;
    slot.id        = "slot";
    slot.kind      = SlotKind::kPcCard;
    slot.available = Available(gen);
    if (view.has_card) {
        slot.inserted          = ChoiceOption{view.save_id, view.display_name};
        slot.eject             = [this, gen] { MenuEject(gen); };
        slot.inserted_controls = std::move(view.card_controls);
    }
    return {WidgetControl{std::move(slot)}};
}

std::string PcmciaSlot::Tooltip() const {
    std::lock_guard<std::mutex> lk(ui_mtx_);
    if (!ui_.has_card) return label_ + " - empty";
    return label_ + " - " + ui_.tooltip_detail;
}

WidgetIcon PcmciaSlot::Icon() const {
    std::lock_guard<std::mutex> lk(ui_mtx_);
    return ResourceIcon{ui_.icon};
}

bool PcmciaSlot::PollDirty() {
    std::string res;
    {
        std::lock_guard<std::mutex> lk(ui_mtx_);
        res = ui_.icon;
    }
    if (res == ui_last_res_) return false;
    ui_last_res_ = std::move(res);
    return true;
}

}
