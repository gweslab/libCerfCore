#pragma once

#include "config_enum_value.h"
#include "service.h"
#include <cerfengine/isa.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

using CpuArch = cerfengine::Isa;

enum class SupportLevel { BootableFull, BootablePartial, UnknownPartial, NoSupport };

inline constexpr uint32_t kDefaultGaColorDepth = 24u;
inline constexpr uint32_t kDefaultGaWindowSize = 0x10000000u;

struct DbLcdPanelSize {
    uint32_t width;
    uint32_t height;

    bool operator==(const DbLcdPanelSize&) const = default;
};

struct DbSocFamily {
    std::string id;
    std::string name;
    CpuArch     arch;
};

struct DbSoc {
    std::string id;
    std::string name;
    std::string family_id;
};

struct DbBoardFamily {
    std::string id;
    std::string name;
};

struct DbGenericPart {
    std::string id;
    std::string name;
    std::string icon;
};

struct DbFeature {
    std::string generic_part_id;
    bool        supported;
};

struct DbPart {
    std::string             id;
    std::string             name;
    std::string             generic_part_id;
    bool                    unknown_name;
    bool                    supported;
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
    std::optional<uint32_t> size_mb;
    std::optional<uint32_t> chip_count;
    std::optional<uint32_t> row_bits;
    std::optional<uint32_t> column_bits;
    std::optional<uint32_t> bank_bits;
    std::string             technology;
    bool                    clamp_viewport;
    std::string             chemistry;
};

struct DbBoardConfigParam {
    std::string                  id;
    std::string                  name;
    std::string                  generic_part_id;
    bool                         part_picker;
    std::vector<ConfigEnumValue> enum_values;
};

struct DbBoardConfigSetting {
    std::string              param_id;
    std::string              default_value;
    std::vector<std::string> values;

    bool Accepts(std::string_view value) const;
};

const DbBoardConfigSetting* FindBoardConfigSetting(std::span<const DbBoardConfigSetting> settings,
                                                   std::string_view param_id);

struct DbBoard {
    std::string                   id;
    std::string                   name;
    std::string                   short_name;
    std::string                   family_id;
    SupportLevel                  support_level;
    bool                          runs;
    std::string                   soc_id;
    std::optional<DbLcdPanelSize> lcd_panel_size;
    uint32_t                      ga_color_depth;
    uint32_t                      ga_window_size;
    int                           dynamic_scale_factor;
    std::string                   rom_placing_mode;
    bool                          configurable_screen;
    uint32_t                      device_year;
    std::optional<std::array<uint8_t, 3>> led_color;
    std::optional<uint32_t>               hw_revision;
    std::vector<std::string>              notes;
    std::vector<DbFeature>            features;
    std::vector<const DbPart*>        connected_parts;
    std::vector<const DbPart*>        parts;
    std::vector<DbBoardConfigSetting> config;
};

class BoardDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::span<const DbBoard>         Boards() const { return boards_; }
    std::span<const DbBoardFamily>   Families() const { return board_families_; }
    std::span<const DbGenericPart>   GenericParts() const { return generic_parts_; }

    const DbGenericPart& GenericPart(std::string_view id) const;
    const DbPart*        FindPart(std::string_view id) const;

    const std::string& GaSharedFolderMountPoint() const {
        return ga_shared_folder_mount_point_;
    }

    std::span<const std::string> GaVideoDriverNames() const {
        return ga_video_driver_names_;
    }

    const DbBoardConfigParam* FindBoardConfigParam(std::string_view id) const;

    template <class Pick>
    std::vector<const DbPart*> ComposeParts(const DbBoard& board, Pick pick) const {
        std::vector<const DbPart*> parts = board.connected_parts;
        for (const DbBoardConfigSetting& s : board.config)
            if (FindBoardConfigParam(s.param_id)->part_picker)
                if (const DbPart* picked = pick(s)) parts.push_back(picked);
        return parts;
    }

    const DbBoard*       FindBoard(std::string_view id) const;
    const DbSoc&         Soc(std::string_view id) const;
    const DbSocFamily&   SocFamily(std::string_view id) const;
    const DbBoardFamily& BoardFamily(std::string_view id) const;
    CpuArch              SocArch(std::string_view soc_id) const;

private:
    std::vector<DbSocFamily>     soc_families_;
    std::vector<DbSoc>           socs_;
    std::vector<DbBoardFamily>   board_families_;
    std::vector<DbGenericPart>   generic_parts_;
    std::vector<DbPart>          parts_;
    std::vector<DbBoard>         boards_;
    std::string              ga_shared_folder_mount_point_;
    std::vector<std::string> ga_video_driver_names_;
    std::vector<DbBoardConfigParam> board_config_params_;
};

}
