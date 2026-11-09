#include "vr41xx_giu_interrupts.h"

#include <cerfcore/state/state_stream.h>
#include "vr41xx_giu_regs.h"

namespace cerfcore {

using namespace cerf_vr41xx_giu_detail;

void Vr41xxGiuInterrupts::OnPinLevel(int pin, bool prev, bool level) {
    const bool     high  = pin >= 16;
    const uint16_t bit   = static_cast<uint16_t>(1u << (pin & 15));
    const uint16_t typ   = high ? inttyp_h_ : inttyp_l_;
    const uint16_t alsel = high ? intalsel_h_ : intalsel_l_;
    if ((typ & bit) == 0u) {
        ApplyCondition(pin, level == ((alsel & bit) != 0u));
        return;
    }
    if (prev != level) ApplyCondition(pin, true);
}

void Vr41xxGiuInterrupts::EvaluateLevels(uint32_t pins, uint32_t levels) {
    const uint32_t typ   = (static_cast<uint32_t>(inttyp_h_) << 16) | inttyp_l_;
    const uint32_t alsel = (static_cast<uint32_t>(intalsel_h_) << 16) | intalsel_l_;
    for (int pin = 0; pin < 32; ++pin) {
        const uint32_t bit = 1u << pin;
        if ((pins & bit) == 0u || (typ & bit) != 0u) continue;
        ApplyCondition(pin, (levels & bit) == (alsel & bit));
    }
}

void Vr41xxGiuInterrupts::ApplyCondition(int pin, bool met) {
    const bool     high  = pin >= 16;
    const uint16_t bit   = static_cast<uint16_t>(1u << (pin & 15));
    uint16_t&      stat  = high ? intstat_h_ : intstat_l_;
    const uint16_t typ   = high ? inttyp_h_ : inttyp_l_;
    const uint16_t htsel = high ? inthtsel_h_ : inthtsel_l_;
    const uint16_t en    = high ? inten_h_ : inten_l_;
    const bool     hold  = (htsel & bit) != 0u;

    if (met) {
        if (intstat_sets_while_disabled_) {
            stat |= bit;
        } else if (en & bit) {
            stat |= bit;
        } else if (hold) {
            (high ? retained_h_ : retained_l_) |= bit;
        }
    } else if ((typ & bit) == 0u && !hold) {
        stat = static_cast<uint16_t>(stat & ~bit);
    }
}

bool Vr41xxGiuInterrupts::ReadRegister(uint32_t off, uint16_t* value) const {
    switch (off) {
        case kOffIntStatL:  *value = intstat_l_;  return true;
        case kOffIntStatH:  *value = intstat_h_;  return true;
        case kOffIntEnL:    *value = inten_l_;    return true;
        case kOffIntEnH:    *value = inten_h_;    return true;
        case kOffIntTypL:   *value = inttyp_l_;   return true;
        case kOffIntTypH:   *value = inttyp_h_;   return true;
        case kOffIntAlSelL: *value = intalsel_l_; return true;
        case kOffIntAlSelH: *value = intalsel_h_; return true;
        case kOffIntHtSelL: *value = inthtsel_l_; return true;
        case kOffIntHtSelH: *value = inthtsel_h_; return true;
        default: return false;
    }
}

bool Vr41xxGiuInterrupts::WriteRegister(uint32_t off, uint16_t value) {
    switch (off) {
        case kOffIntStatL:
            intstat_l_  = static_cast<uint16_t>(intstat_l_ & ~value);
            retained_l_ = static_cast<uint16_t>(retained_l_ & ~value);
            return true;
        case kOffIntStatH:
            intstat_h_  = static_cast<uint16_t>(intstat_h_ & ~value);
            retained_h_ = static_cast<uint16_t>(retained_h_ & ~value);
            return true;

        case kOffIntEnL: {
            const uint16_t promote = static_cast<uint16_t>(retained_l_ & value);
            intstat_l_ |= promote;
            retained_l_ = static_cast<uint16_t>(retained_l_ & ~promote);
            inten_l_    = value;
            return true;
        }
        case kOffIntEnH: {
            const uint16_t promote = static_cast<uint16_t>(retained_h_ & value);
            intstat_h_ |= promote;
            retained_h_ = static_cast<uint16_t>(retained_h_ & ~promote);
            inten_h_    = value;
            return true;
        }

        case kOffIntTypL:   inttyp_l_   = value; return true;
        case kOffIntTypH:   inttyp_h_   = value; return true;
        case kOffIntAlSelL: intalsel_l_ = value; return true;
        case kOffIntAlSelH: intalsel_h_ = value; return true;
        case kOffIntHtSelL: inthtsel_l_ = value; return true;
        case kOffIntHtSelH: inthtsel_h_ = value; return true;
        default: return false;
    }
}

void Vr41xxGiuInterrupts::Reset() {
    intstat_l_  = 0; intstat_h_  = 0;
    inten_l_    = 0; inten_h_    = 0;
    inttyp_l_   = 0; inttyp_h_   = 0;
    intalsel_l_ = 0; intalsel_h_ = 0;
    inthtsel_l_ = 0; inthtsel_h_ = 0;
    retained_l_ = 0; retained_h_ = 0;
}

uint16_t Vr41xxGiuInterrupts::IcuLow(bool enable_gates) const {
    return enable_gates ? static_cast<uint16_t>(intstat_l_ & inten_l_) : intstat_l_;
}

uint16_t Vr41xxGiuInterrupts::IcuHigh(bool enable_gates) const {
    return enable_gates ? static_cast<uint16_t>(intstat_h_ & inten_h_) : intstat_h_;
}

void Vr41xxGiuInterrupts::Save(StateWriter& w) const {
    w.Write("intstat_l", intstat_l_);   w.Write("intstat_h", intstat_h_);
    w.Write("inten_l", inten_l_);       w.Write("inten_h", inten_h_);
    w.Write("inttyp_l", inttyp_l_);     w.Write("inttyp_h", inttyp_h_);
    w.Write("intalsel_l", intalsel_l_); w.Write("intalsel_h", intalsel_h_);
    w.Write("inthtsel_l", inthtsel_l_); w.Write("inthtsel_h", inthtsel_h_);
    if (intstat_sets_while_disabled_) return;
    w.Write("retained_l", retained_l_);
    w.Write("retained_h", retained_h_);
}

void Vr41xxGiuInterrupts::Restore(StateReader& r) {
    r.Read("intstat_l", intstat_l_);   r.Read("intstat_h", intstat_h_);
    r.Read("inten_l", inten_l_);       r.Read("inten_h", inten_h_);
    r.Read("inttyp_l", inttyp_l_);     r.Read("inttyp_h", inttyp_h_);
    r.Read("intalsel_l", intalsel_l_); r.Read("intalsel_h", intalsel_h_);
    r.Read("inthtsel_l", inthtsel_l_); r.Read("inthtsel_h", inthtsel_h_);
    if (intstat_sets_while_disabled_) return;
    r.Read("retained_l", retained_l_);
    r.Read("retained_h", retained_h_);
}

}
