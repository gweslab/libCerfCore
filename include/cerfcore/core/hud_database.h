#pragma once

#include "board_database.h"
#include "service.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

struct HudFace {
    std::string label;
    std::string icon;
};

struct HudLayerFace {
    uint8_t held;
    HudFace face;
};

struct HudButton {
    std::vector<uint8_t>      keys;
    std::string               action;
    HudFace                   face;
    std::vector<HudLayerFace> layer_faces;
};

enum class HudDpadShape : uint8_t { kRounded, kCircle };

struct HudDpad {
    std::vector<uint8_t> up;
    std::vector<uint8_t> down;
    std::vector<uint8_t> left;
    std::vector<uint8_t> right;
    std::vector<uint8_t> center;
    HudDpadShape         shape = HudDpadShape::kRounded;
};

struct HudNode {
    enum class Kind : uint8_t { kRow, kColumn, kButton, kDpad, kSpace };

    Kind                 kind = Kind::kSpace;
    double               grow = 1.0;
    std::vector<HudNode> children;
    HudButton            button;
    HudDpad              dpad;
};

enum class HudSide : uint8_t { kTop, kBottom, kLeft, kRight };
enum class HudAlign : uint8_t { kStart, kCenter, kEnd };

struct HudPanel {
    HudSide  side   = HudSide::kBottom;
    HudAlign align  = HudAlign::kCenter;
    double   width  = 1.0;
    double   height = 1.0;
    HudNode  root;
};

struct DbHud {
    std::string                 id;
    std::string                 name;
    std::vector<std::string>    board_ids;
    std::vector<std::string>    board_family_ids;
    std::vector<DbLcdPanelSize> screens;
    double                      gap = 0.0;
    std::vector<HudPanel>       panels;
    std::vector<HudPanel>       portrait_panels;

    const DbLcdPanelSize&        ScreenFor(double width, double height) const;
    const std::vector<HudPanel>& InsidePanels(double area_w, double area_h) const;
};

class HudDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    const std::vector<DbHud>& Huds() const { return huds_; }
    const DbHud*              Find(std::string_view id) const;
    const DbHud*              AutoFor(std::string_view board_id, std::string_view board_family_id) const;

private:
    std::vector<DbHud> huds_;
};

}
