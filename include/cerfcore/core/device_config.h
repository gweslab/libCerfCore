#pragma once
#include "device_config_keys.h"
#include "service.h"
#include "sd_card_cid.h"
#include "shader_preset_entry.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cerfcore {

struct BoardConfigEntry {
    std::string value;
    std::string path;
};

struct BundledUsbMedia {
    std::string file;
    std::string name;
    bool insert_on_launch = false;
    std::optional<SdCardCid> cid;
};

struct DeviceMeta {
    std::string name;
    std::string device_name;
    std::string os_name;
    int         os_ver_major = 0;
    int         os_ver_minor = 0;
};

struct BundledCompactFlashCard {
    std::string file;
    std::string name;
    bool insert_on_launch = false;
};

/* Boot action when a saved state image exists in the device directory
   (--boot=resume|cold|warm). */
enum class StateBootMode {
    Resume,
    Warm,
    Cold,     /* ignore state.img, cold boot */
};

enum class CanvasTab {
    Boot,
    Hw,
    Framebuffer,
};

constexpr uint32_t kDefaultConfigurableScreenWidth  = 800;
constexpr uint32_t kDefaultConfigurableScreenHeight = 600;

constexpr bool kDefaultNetworkEnabled     = true;
constexpr bool kDefaultGuestAdditions     = false;
constexpr bool kDefaultStartFullscreen    = false;
constexpr bool kDefaultDoubleTapHelper    = true;
constexpr bool kDefaultAutomaticCalibration = true;
constexpr bool kDefaultShaderScreenshots  = true;
constexpr bool kDefaultAllowUntrustedApps = false;

std::string DefaultStorageFileName(std::string_view storage_id);

struct LiveDeviceSettings {
    uint32_t board_configurable_screen_width    = kDefaultConfigurableScreenWidth;
    uint32_t board_configurable_screen_height   = kDefaultConfigurableScreenHeight;
    uint32_t screen_dpi                         = 0;
    uint32_t board_configurable_screen_bpp      = 0;
    bool     board_configurable_screen_explicit = false;

    std::string           guest_additions_color_scheme;
    int32_t               guest_additions_font_size            = 0;
    bool                  guest_additions_font_size_set        = false;
    bool                  guest_additions_cleartype            = false;
    bool                  guest_additions_cleartype_set        = false;
    bool                  guest_additions_allow_untrusted_apps = kDefaultAllowUntrustedApps;
    std::vector<uint32_t> guest_additions_palette;

    std::string share_folder;
    std::string share_folder_mount_point;

    std::string hud;

    std::vector<ShaderPresetEntry>             shader_presets;
    std::vector<std::pair<std::string, float>> shader_parameters;
    bool                                       shader_screenshots = kDefaultShaderScreenshots;
};

struct DeviceConfig : public Service {
    using Service::Service;

    /* Populates every field below from the global + per-device cerf.json and
       the CLI, via ConfigLoader. Lazy: runs on first emu_.Get<DeviceConfig>(). */
    void OnReady() override;

    std::string device_name;

    std::string board_id;

    std::map<std::string, BoardConfigEntry, std::less<>> board_config;

    DeviceMeta meta;

    uint32_t screen_refresh_rate = 0;

    bool        network_enabled = kDefaultNetworkEnabled;
    std::string network_mac     = "02:CE:5F:00:00:01";
    uint32_t    network_mtu     = 1500;
    std::string network_forward_tcp;
    std::string network_forward_udp;

    std::string              rom_primary;
    std::vector<std::string> rom_extensions;
    std::string              rom_recovery;

    std::vector<BundledUsbMedia> bundled_sd_cards;
    std::vector<BundledUsbMedia> bundled_usb_disks;

    /* Optional serial-config EEPROM image (cerf.json rom.eeprom). A board's
       SSP/SPI EEPROM peripheral loads it from the device directory; empty
       when the device has none. */
    std::string              rom_eeprom;

    std::string              rom_ce_dictionary;

    std::string              rom_lumia800_user_area_erase;

    std::string              rom_qcn;

    std::string              rom_velo1_rom_upgrade_card;

    std::string storage_nand   = DefaultStorageFileName(config_key::kStorageNand);
    std::string storage_hdd    = DefaultStorageFileName(config_key::kStorageHdd);
    std::string storage_emmc   = DefaultStorageFileName(config_key::kStorageEmmc);
    std::string storage_nor    = DefaultStorageFileName(config_key::kStorageNor);
    std::string storage_eeprom = DefaultStorageFileName(config_key::kStorageEeprom);
    std::string storage_modem_nv = DefaultStorageFileName(config_key::kStorageModemNv);
    std::string storage_fuel_gauge = DefaultStorageFileName(config_key::kStorageFuelGauge);
    std::string storage_touch_controller = DefaultStorageFileName(config_key::kStorageTouchController);

    /* Optional CF images bundled with the ROM (cerf.json
       "additional_packages.compact_flash_cards"); the CF insert menu offers
       each entry whose file is present in the device directory. */
    std::vector<BundledCompactFlashCard> bundled_compact_flash_cards;

    bool boot_in_recovery = false;
    bool guest_additions = kDefaultGuestAdditions;
    std::vector<std::string> guest_additions_autorun;

    /* Startup tab. Dev builds default to the hardware console so debug output
       shows instantly; production defaults to the boot screen. --tab overrides. */
    CanvasTab start_tab =
#if CERF_DEV_MODE
        CanvasTab::Hw;
#else
        CanvasTab::Boot;
#endif

    /* Dev builds default to cold so iteration never auto-resumes a stale
       image; production resumes a saved state. --boot overrides either. */
    StateBootMode boot_mode =
#if CERF_DEV_MODE
        StateBootMode::Cold;
#else
        StateBootMode::Resume;
#endif

    std::string state_path;

    bool start_fullscreen = kDefaultStartFullscreen;

    bool double_tap_helper = kDefaultDoubleTapHelper;

    bool automatic_calibration = kDefaultAutomaticCalibration;

    bool no_focus = false;

    bool calibration_auto_answer = false;

    bool adopt_guest_additions_resolution_for_host_screen = false;

    std::shared_ptr<const LiveDeviceSettings> Live() const;
    uint64_t LiveGeneration() const;
    void PublishLive(LiveDeviceSettings settings);
    bool PublishLiveAt(uint64_t generation, LiveDeviceSettings settings);
    void EditLive(const std::function<void(LiveDeviceSettings&)>& edit);

private:
    mutable std::mutex                        live_mtx_;
    std::shared_ptr<const LiveDeviceSettings> live_ = std::make_shared<const LiveDeviceSettings>();
    uint64_t                                  live_generation_ = 0;
};

}
