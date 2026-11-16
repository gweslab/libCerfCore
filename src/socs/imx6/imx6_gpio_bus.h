#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

class Imx6GpioInputSource;

class Imx6GpioBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void RegisterBank(uint32_t base, std::function<void()> reevaluate,
                      std::function<void(uint32_t pin)> before_pad_change,
                      std::function<void(uint32_t pin)> after_pad_change);
    void BeforePadChange(uint32_t bank, uint32_t pin) const;
    void AfterPadChange(uint32_t bank, uint32_t pin) const;
    void RegisterSource(Imx6GpioInputSource* source);
    Imx6GpioInputSource* Find(uint32_t base) const;
    std::recursive_mutex& StateMutex() const { return state_mtx_; }

    void SaveSources(uint32_t base, StateWriter& w) const;
    void RestoreSources(uint32_t base, StateReader& r) const;
    void PostRestoreSources(uint32_t base) const;

private:
    struct Bank {
        uint32_t base;
        std::function<void()> reevaluate;
        std::function<void(uint32_t pin)> before_pad_change;
        std::function<void(uint32_t pin)> after_pad_change;
    };
    static uint32_t BankBase(uint32_t bank);
    void Wire(uint32_t base);

    std::vector<Bank> banks_;
    std::vector<Imx6GpioInputSource*> sources_;
    mutable std::recursive_mutex state_mtx_;
};

}
