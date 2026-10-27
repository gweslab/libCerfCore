#pragma once

#include "service.h"
#include <cerfengine/isa.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

using CpuArch = cerfengine::Isa;

enum class RomPlacingMode { FlatContainer, Imx51Nand, Unknown };

enum class SupportLevel { BootableFull, BootablePartial, UnknownPartial, NoSupport };

inline constexpr uint32_t kDefaultGaColorDepth = 24u;

struct DbLcdPanelSize { uint32_t width; uint32_t height; };

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

struct DbDeviceFeature {
    std::string id;
    std::string name;
    std::string icon;
};

struct DbFeatureSupport {
    std::string feature_id;
    bool        supported;
};

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
    bool                          has_high_dpi;
    RomPlacingMode                rom_placing_mode;
    bool                          configurable_screen;
    uint32_t                      device_year;
    std::vector<std::string>      notes;
    std::vector<DbFeatureSupport> features;
};

class BoardDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::span<const DbBoard>         Boards() const { return boards_; }
    std::span<const DbBoardFamily>   Families() const { return board_families_; }
    std::span<const DbDeviceFeature> DeviceFeatures() const { return device_features_; }

    const std::string& GaSharedFolderMountPoint() const {
        return ga_shared_folder_mount_point_;
    }

    std::span<const std::string> GaVideoDriverNames() const {
        return ga_video_driver_names_;
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
    std::vector<DbDeviceFeature> device_features_;
    std::vector<DbBoard>         boards_;
    std::string              ga_shared_folder_mount_point_;
    std::vector<std::string> ga_video_driver_names_;
};

}
