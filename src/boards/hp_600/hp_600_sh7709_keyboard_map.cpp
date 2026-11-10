#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../jornada/jornada_keyboard_layers.h"
#include "hp_600_family_id.h"
#include "hp_600_sh7709_key_matrix.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kCells = Hp600Sh7709KeyMatrix::kRows * Hp600Sh7709KeyMatrix::kColumns;

constexpr uint8_t kCellVk[kCells] = {
    0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u, 0x38u, 0x39u, 0x30u, 0xBDu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x51u, 0x57u, 0x45u, 0x52u, 0x54u, 0x59u, 0x55u, 0x49u, 0x4Fu, 0x50u, 0xBBu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x41u, 0x53u, 0x44u, 0x46u, 0x47u, 0x48u, 0x4Au, 0x4Bu, 0x4Cu, 0xDBu, 0xDCu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0xA0u, 0xC0u, 0x1Bu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xBAu, 0xDDu, 0xA1u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x5Bu, 0x09u, 0x00u, 0x00u, 0x56u, 0x00u, 0x00u, 0xBEu, 0xBFu, 0xDEu, 0x0Du, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x58u, 0x43u, 0xA4u, 0x42u, 0x4Eu, 0x4Du, 0xBCu, 0x26u, 0x27u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x5Au, 0x90u, 0x00u, 0x20u, 0x00u, 0x79u, 0x2Eu, 0x25u, 0x28u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0xCFu, 0x00u, 0xC2u, 0xC3u, 0xC4u, 0xC5u, 0xC6u, 0xC7u, 0xC8u, 0xC9u, 0x00u, 0xDFu, 0xA2u, 0x08u, 0x77u, 0x78u,
};

constexpr uint16_t kScannedColumnsLowRows = 0x07FFu;
constexpr uint16_t kScannedColumnsLastRow = 0xBFFFu;
constexpr uint32_t kLastRow               = Hp600Sh7709KeyMatrix::kRows - 1u;

constexpr uint8_t kVkShift       = 0x10u;
constexpr uint8_t kVkControl     = 0x11u;
constexpr uint8_t kVkMenu        = 0x12u;
constexpr uint8_t kVkLeftShift   = 0xA0u;
constexpr uint8_t kVkLeftControl = 0xA2u;
constexpr uint8_t kVkLeftMenu    = 0xA4u;
constexpr uint8_t kVkRightMenu   = 0xA5u;
constexpr uint8_t kCellAltGr     = 0x79u;

constexpr uint8_t kAltLayerId   = 2u;
constexpr uint8_t kAltGrLayerId = 3u;

struct GenericModifier {
    uint8_t vk;
    uint8_t sided_vk;
};

constexpr GenericModifier kGenericModifiers[] = {
    {kVkShift, kVkLeftShift},
    {kVkControl, kVkLeftControl},
    {kVkMenu, kVkLeftMenu},
};

struct HostAlias {
    uint8_t     host_vk;
    uint8_t     guest_vk;
    const char* label;
};

constexpr HostAlias kHostAliases[] = {
    {0x70u, 0xC2u, "Voice Application"}, {0x71u, 0xC3u, "Pocket PowerPoint"}, {0x72u, 0xC4u, "Pocket Word"},
    {0x73u, 0xC5u, "Pocket Excel"},      {0x74u, 0xC6u, "Calendar"},          {0x75u, 0xC7u, "Tasks"},
    {0x76u, 0xC8u, "Contacts"},          {0x77u, 0xC9u, "Fiscal Pro"},        {0x79u, 0xCFu, "Voice Recorder"},
};

constexpr jornada_kbd::LayerKey kContrastLayer[] = {
    {0xBCu, "Contrast -"},
    {0xBEu, "Contrast +"},
};

constexpr jornada_kbd::LayerKey kAltGrLayer[] = {
    {0x30u, "\xE2\x80\x99"}, {0x31u, "\xC2\xA1"}, {0x32u, "\xC2\xB2"}, {0x33u, "\xC2\xB3"},
    {0x34u, "\xC2\xA4"}, {0x35u, "\xC3\xB7"}, {0x36u, "\xC2\xBC"}, {0x37u, "\xC2\xBD"},
    {0x38u, "\xC2\xBE"}, {0x39u, "\xE2\x80\x98"}, {0x41u, "\xC3\xA1"}, {0x42u, "\xC2\xB1"},
    {0x43u, "\xC2\xA9"}, {0x44u, "\xC3\xB0"}, {0x45u, "\xC3\xA9"}, {0x46u, "\xC2\xAB"},
    {0x47u, "\xC2\xBB"}, {0x48u, "\x5E"}, {0x49u, "\xC3\xAD"}, {0x4Au, "\xC2\xB0"},
    {0x4Bu, "\xC2\xB6"}, {0x4Cu, "\xC3\xB8"}, {0x4Du, "\xC2\xB5"}, {0x4Eu, "\xC3\xB1"},
    {0x4Fu, "\xC3\xB3"}, {0x50u, "\xC2\xB4"}, {0x51u, "\x60"}, {0x52u, "\xC2\xAE"},
    {0x53u, "\xC3\x9F"}, {0x54u, "\xC3\xBE"}, {0x55u, "\xC3\xBA"}, {0x56u, "\xC2\xA7"},
    {0x57u, "\x7E"}, {0x58u, "\xC2\xA2"}, {0x59u, "\xC3\xBC"}, {0x5Au, "\xC2\xA3"},
    {0xBDu, "\xC2\xA5"}, {0xBBu, "\xC3\x97"}, {0xC0u, "\xC2\xB9"}, {0xDBu, "\xC3\xA5"},
    {0xDDu, "\xC2\xA8"}, {0xDCu, "\xC2\xAC"}, {0xBAu, "\xC3\xB6"}, {0xDEu, "\xC3\xA4"},
    {0xBFu, "\xC2\xBF"}, {0xBCu, "\xC3\xA7"}, {0xBEu, "\xC3\xA6"},
};

class Hp600Sh7709KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override {
        for (uint32_t cell = 0; cell < kCells; ++cell) {
            const uint8_t vk = kCellVk[cell];
            if (vk == 0u || !Scanned(cell)) continue;
            if (vk == kCellAltGr) {
                bindings_.push_back({kVkRightMenu, cell, nullptr, 0u, kAltGrLayerId});
                continue;
            }
            bindings_.push_back({vk, cell, nullptr, 0u, vk == kVkLeftMenu ? kAltLayerId : uint8_t{0u}});
            for (const GenericModifier& m : kGenericModifiers) {
                if (m.sided_vk == vk) {
                    bindings_.push_back({m.vk, cell, nullptr, 0u, m.vk == kVkMenu ? kAltLayerId : uint8_t{0u}});
                }
            }
            for (const HostAlias& a : kHostAliases) {
                if (a.guest_vk == vk) bindings_.push_back({a.host_vk, cell, a.label, 0u, 0u});
            }
        }
        for (const jornada_kbd::LayerKey& k : jornada_kbd::kAltLayer) AddLayerLabel(k, kAltLayerId);
        for (const jornada_kbd::LayerKey& k : kContrastLayer) AddLayerLabel(k, kAltLayerId);
        for (const jornada_kbd::LayerKey& k : kAltGrLayer) AddLayerLabel(k, kAltGrLayerId);
    }

    const std::vector<KeyBinding>& Bindings() const override { return bindings_; }

private:
    static bool Scanned(uint32_t cell) {
        const uint32_t row     = cell / Hp600Sh7709KeyMatrix::kColumns;
        const uint16_t scanned = row == kLastRow ? kScannedColumnsLastRow : kScannedColumnsLowRows;
        return ((scanned >> (cell % Hp600Sh7709KeyMatrix::kColumns)) & 1u) != 0u;
    }

    void AddLayerLabel(const jornada_kbd::LayerKey& k, uint8_t layer) {
        bindings_.push_back({k.vk, 0u, k.label, layer, 0u});
    }

    std::vector<KeyBinding> bindings_;
};

}

REGISTER_SERVICE_AS(Hp600Sh7709KeyboardMap, KeyboardMap);

}
