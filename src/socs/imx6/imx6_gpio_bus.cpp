#include "imx6_gpio_bus.h"

#include "imx6_gpio_source.h"
#include "imx6_id.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

REGISTER_SERVICE(Imx6GpioBus);

bool Imx6GpioBus::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6GpioBus::RegisterBank(uint32_t base, std::function<void()> reevaluate,
                               std::function<void(uint32_t pin)> before_pad_change,
                               std::function<void(uint32_t pin)> after_pad_change) {
    banks_.push_back({base, std::move(reevaluate), std::move(before_pad_change), std::move(after_pad_change)});
    Wire(base);
}

uint32_t Imx6GpioBus::BankBase(uint32_t bank) { return 0x0209C000u + (bank - 1u) * 0x4000u; }

void Imx6GpioBus::BeforePadChange(uint32_t bank, uint32_t pin) const {
    for (const Bank& b : banks_)
        if (b.base == BankBase(bank)) b.before_pad_change(pin);
}

void Imx6GpioBus::AfterPadChange(uint32_t bank, uint32_t pin) const {
    for (const Bank& b : banks_)
        if (b.base == BankBase(bank)) b.after_pad_change(pin);
}

void Imx6GpioBus::RegisterSource(Imx6GpioInputSource* source) {
    sources_.push_back(source);
    Wire(source->GpioBase());
}

Imx6GpioInputSource* Imx6GpioBus::Find(uint32_t base) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) return s;
    return nullptr;
}

void Imx6GpioBus::Wire(uint32_t base) {
    Imx6GpioInputSource* s = Find(base);
    if (!s) return;
    for (const Bank& b : banks_)
        if (b.base == base && b.reevaluate) s->SetReevaluate(b.reevaluate);
}

void Imx6GpioBus::SaveSources(uint32_t base, StateWriter& w) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) s->SaveState(w);
}

void Imx6GpioBus::RestoreSources(uint32_t base, StateReader& r) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) s->RestoreState(r);
}

void Imx6GpioBus::PostRestoreSources(uint32_t base) const {
    for (Imx6GpioInputSource* s : sources_)
        if (s->GpioBase() == base) s->PostRestore();
}

}
