#include <cerfcore/core/hud_database.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/host/virtual_key_names.h>

#include "name_table.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cerfcore {

REGISTER_SERVICE(HudDatabase);

namespace {

using nlohmann::json;

constexpr char kElements[]       = "elements";
constexpr char kHuds[]           = "huds";
constexpr char kId[]             = "id";
constexpr char kName[]           = "name";
constexpr char kBoardIds[]       = "board_ids";
constexpr char kBoardFamilyIds[] = "board_family_ids";
constexpr char kGap[]            = "gap";
constexpr char kPanels[]         = "panels";
constexpr char kSide[]           = "side";
constexpr char kAlign[]          = "align";
constexpr char kWidth[]          = "width";
constexpr char kHeight[]         = "height";
constexpr char kContent[]        = "content";
constexpr char kRow[]            = "row";
constexpr char kColumn[]         = "column";
constexpr char kButton[]         = "button";
constexpr char kDpad[]           = "dpad";
constexpr char kGrow[]           = "grow";
constexpr char kKeys[]           = "keys";
constexpr char kLabel[]          = "label";
constexpr char kIcon[]           = "icon";
constexpr char kAction[]         = "action";
constexpr char kHeld[]           = "held";
constexpr char kUp[]             = "up";
constexpr char kDown[]           = "down";
constexpr char kLeft[]           = "left";
constexpr char kRight[]          = "right";
constexpr char kCenter[]         = "center";
constexpr char kShape[]          = "shape";
constexpr char kPortrait[]       = "portrait";

constexpr std::pair<std::string_view, HudSide> kSides[] = {
    {"top", HudSide::kTop}, {"bottom", HudSide::kBottom}, {"left", HudSide::kLeft}, {"right", HudSide::kRight}};

constexpr std::pair<std::string_view, HudAlign> kAligns[] = {
    {"start", HudAlign::kStart}, {"center", HudAlign::kCenter}, {"end", HudAlign::kEnd}};

constexpr std::pair<std::string_view, HudDpadShape> kShapes[] = {
    {"rounded", HudDpadShape::kRounded}, {"circle", HudDpadShape::kCircle}};

std::vector<uint8_t> Keys(const json& list) {
    std::vector<uint8_t> out;
    for (const json& name : list) out.push_back(VirtualKeyByName(name.get<std::string>()).value());
    return out;
}

std::vector<uint8_t> OptKeys(const json& object, const char* key) {
    const auto it = object.find(key);
    return it == object.end() ? std::vector<uint8_t>() : Keys(*it);
}

HudFace Face(const json& object) {
    return HudFace{object.value(kLabel, std::string()), object.value(kIcon, std::string())};
}

std::vector<HudLayerFace> LayerFaces(const json& object) {
    std::vector<HudLayerFace> out;
    const auto                held = object.find(kHeld);
    if (held == object.end()) return out;
    for (const auto& [name, face] : held->items()) out.push_back({VirtualKeyByName(name).value(), Face(face)});
    return out;
}

HudButton Button(const json& object) {
    HudButton button;
    button.keys        = OptKeys(object, kKeys);
    button.action      = object.value(kAction, std::string());
    button.face        = Face(object);
    button.layer_faces = LayerFaces(object);
    return button;
}

HudDpad Dpad(const json& object) {
    HudDpad dpad;
    dpad.up     = Keys(object.at(kUp));
    dpad.down   = Keys(object.at(kDown));
    dpad.left   = Keys(object.at(kLeft));
    dpad.right  = Keys(object.at(kRight));
    dpad.center = OptKeys(object, kCenter);
    dpad.shape  = ValueByName(kShapes, object.value(kShape, std::string("rounded")));
    return dpad;
}

class HudReader {
public:
    explicit HudReader(const json& elements) : elements_(elements) {}

    HudNode Node(const json& value, bool transposed) const {
        if (value.is_string()) return Node(elements_.at(value.get<std::string>()), transposed);
        HudNode node;
        node.grow = value.value(kGrow, 1.0);
        if (const auto row = value.find(kRow); row != value.end()) {
            node.kind     = transposed ? HudNode::Kind::kColumn : HudNode::Kind::kRow;
            node.children = Children(*row, transposed);
        } else if (const auto column = value.find(kColumn); column != value.end()) {
            node.kind     = transposed ? HudNode::Kind::kRow : HudNode::Kind::kColumn;
            node.children = Children(*column, transposed);
        } else if (const auto button = value.find(kButton); button != value.end()) {
            node.kind   = HudNode::Kind::kButton;
            node.button = Button(*button);
        } else if (const auto dpad = value.find(kDpad); dpad != value.end()) {
            node.kind = HudNode::Kind::kDpad;
            node.dpad = Dpad(*dpad);
        } else {
            node.kind = HudNode::Kind::kSpace;
        }
        return node;
    }

    HudPanel Panel(const json& object) const {
        HudPanel panel;
        Shape(object, panel);
        panel.root = Node(object.at(kContent), false);
        return panel;
    }

    HudPanel PortraitPanel(const json& object) const {
        const auto portrait = object.find(kPortrait);
        if (portrait == object.end()) return Panel(object);
        HudPanel panel;
        Shape(object, panel);
        const bool across = AcrossX(panel.side);
        Shape(*portrait, panel);
        panel.root = Node(object.at(kContent), across != AcrossX(panel.side));
        return panel;
    }

private:
    static bool AcrossX(HudSide side) { return side == HudSide::kTop || side == HudSide::kBottom; }

    static void Shape(const json& object, HudPanel& panel) {
        panel.side   = ValueByName(kSides, object.at(kSide).get<std::string>());
        panel.align  = ValueByName(kAligns, object.value(kAlign, std::string("center")));
        panel.width  = object.at(kWidth).get<double>();
        panel.height = object.at(kHeight).get<double>();
    }

    std::vector<HudNode> Children(const json& list, bool transposed) const {
        std::vector<HudNode> out;
        for (const json& child : list) out.push_back(Node(child, transposed));
        return out;
    }

    const json& elements_;
};

bool Contains(const std::vector<std::string>& list, std::string_view id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

}

void HudDatabase::OnReady() {
    const json      root = CfgReadInstalledJsonFile(HudsJsonPath());
    const HudReader reader(root.at(kElements));
    for (const json& h : root.at(kHuds)) {
        DbHud hud;
        hud.id               = h.at(kId).get<std::string>();
        hud.name             = h.at(kName).get<std::string>();
        hud.board_ids        = h.value(kBoardIds, std::vector<std::string>());
        hud.board_family_ids = h.value(kBoardFamilyIds, std::vector<std::string>());
        for (const DbBoard& board : emu_.Get<BoardDatabase>().Boards()) {
            if (!board.lcd_panel_size) continue;
            if (!Contains(hud.board_ids, board.id) && !Contains(hud.board_family_ids, board.family_id)) continue;
            hud.screens.push_back(*board.lcd_panel_size);
        }
        hud.gap = h.value(kGap, 0.0);
        for (const json& panel : h.at(kPanels)) {
            hud.panels.push_back(reader.Panel(panel));
            hud.portrait_panels.push_back(reader.PortraitPanel(panel));
        }
        huds_.push_back(std::move(hud));
    }
}

const std::vector<HudPanel>& DbHud::InsidePanels(double area_w, double area_h) const {
    return area_h > area_w ? portrait_panels : panels;
}

const DbLcdPanelSize& DbHud::ScreenFor(double width, double height) const {
    const DbLcdPanelSize* best = &screens.at(0);
    double                best_distance = 0.0;
    for (const DbLcdPanelSize& screen : screens) {
        const double distance = std::abs(screen.width - width) + std::abs(screen.height - height);
        if (&screen != best && distance >= best_distance) continue;
        best          = &screen;
        best_distance = distance;
    }
    return *best;
}

const DbHud* HudDatabase::Find(std::string_view id) const {
    for (const DbHud& hud : huds_)
        if (hud.id == id) return &hud;
    return nullptr;
}

const DbHud* HudDatabase::AutoFor(std::string_view board_id, std::string_view board_family_id) const {
    for (const DbHud& hud : huds_)
        if (Contains(hud.board_ids, board_id)) return &hud;
    for (const DbHud& hud : huds_)
        if (Contains(hud.board_family_ids, board_family_id)) return &hud;
    return nullptr;
}

}
