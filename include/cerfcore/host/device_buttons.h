#pragma once

#include "../core/service.h"
#include "machine_indicators.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cerfcore {

class CerfEmulator;

class DeviceButtons : public Service {
public:
    using Service::Service;
    ~DeviceButtons() override = default;

    void OnReady() override;

protected:
    struct ButtonSpec {
        std::string           id;
        std::string           name;
        std::function<void()> press;
    };

    struct RawTapButton {
        const char* id;
        const char* name;
        uint16_t    x;
        uint16_t    y;
    };

    virtual std::vector<ButtonSpec> BoardButtons() = 0;

    std::vector<ButtonSpec> RawTapButtons(std::span<const RawTapButton> buttons,
                                          std::function<void(uint16_t, uint16_t)> tap) const;

private:
    class WiredButton final : public MachineButton {
    public:
        WiredButton(CerfEmulator& emu, ButtonSpec spec) : MachineButton(emu), spec_(std::move(spec)) {}
        std::string ObjectId() const override { return spec_.id; }
        std::string ObjectName() const override { return spec_.name; }
        bool        Momentary() const override { return true; }

    private:
        bool Pressed() override;
        void Released() override {}

        ButtonSpec spec_;
    };

    std::vector<std::unique_ptr<WiredButton>> buttons_;
};

}
