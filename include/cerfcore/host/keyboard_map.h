#pragma once

#include "../core/service.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

struct KeyBinding {
    uint8_t     vk;
    uint32_t    device_code;
    const char* guest_label;
    uint8_t     layer;
    uint8_t     holds_layer;
};

struct SpecialKey {
    uint8_t     vk;
    const char* guest_label;
};

class KeyboardMap : public Service {
public:
    using Service::Service;
    ~KeyboardMap() override = default;

    virtual const std::vector<KeyBinding>& Bindings() const = 0;

    std::vector<SpecialKey> SpecialKeys() const;

    bool BaseDeviceCode(uint8_t vk, uint32_t& out) const;
    bool LayerDeviceCode(uint8_t vk, uint8_t layer, uint32_t& out) const;
    uint8_t HeldLayer(uint8_t vk) const;
    const char* GuestLabel(uint8_t vk, uint8_t layer) const;

private:
    const KeyBinding* LayerBinding(uint8_t vk, uint8_t layer) const;
};

}
