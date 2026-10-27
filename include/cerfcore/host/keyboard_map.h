#pragma once

#include "../core/service.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

struct KeyBinding {
    uint8_t        vk;
    uint32_t       device_code;  /* Opaque, board-interpreted output. */
    const char*    guest_label;
    uint8_t        layer;        /* 0 = base layer; N = a binding visible in the
                                    dialog only while the modifier holding layer
                                    N is held. */
    uint8_t        holds_layer;  /* 0 = not a modifier; N = holding this host key
                                    activates layer N in the dialog (e.g. Fn). */
};

/* A lock-style layer (e.g. Num Lock) with no key that holds it: the dialog shows
   a checkbox that toggles its preview. */
struct KeyboardToggleLayer {
    uint8_t     layer;
    const char* name;
};

struct SpecialKey {
    uint8_t     vk;
    const char* guest_label;
};

class KeyboardMap : public Service {
public:
    using Service::Service;
    ~KeyboardMap() override = default;

    /* Every binding: the base layer plus all modifier/lock layers. */
    virtual const std::vector<KeyBinding>& Bindings() const = 0;

    /* Lock layers shown as dialog checkboxes; empty if the board has none. */
    virtual std::vector<KeyboardToggleLayer> ToggleLayers() const { return {}; }

    virtual std::vector<uint8_t> SpecialKeyOrder() const = 0;

    std::vector<SpecialKey> SpecialKeys() const;

    /* Base-layer (input-path) device_code for a host VK; false if unmapped. */
    bool BaseDeviceCode(uint8_t vk, uint32_t& out) const;

private:
    const KeyBinding* BaseBinding(uint8_t vk) const;
};

}
