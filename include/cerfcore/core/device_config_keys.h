#pragma once

namespace cerfcore::config_key {

inline constexpr char kMeta[]                  = "meta";
inline constexpr char kName[]                  = "name";
inline constexpr char kDeviceName[]            = "device_name";
inline constexpr char kOs[]                    = "os";
inline constexpr char kVerMajor[]              = "ver_major";
inline constexpr char kVerMinor[]              = "ver_minor";
inline constexpr char kBoard[]                 = "board";
inline constexpr char kId[]                    = "id";
inline constexpr char kScreenWidth[]           = "configurable_screen_width";
inline constexpr char kScreenHeight[]          = "configurable_screen_height";
inline constexpr char kScreenDpi[]             = "configurable_screen_dpi";
inline constexpr char kScreenBpp[]             = "configurable_screen_bpp";
inline constexpr char kRom[]                   = "rom";
inline constexpr char kPrimary[]               = "primary";
inline constexpr char kEeprom[]                = "eeprom";
inline constexpr char kCeDictionary[]          = "ce_dictionary";
inline constexpr char kLumia800UserAreaErase[] = "lumia800_user_area_erase";
inline constexpr char kRecovery[]              = "recovery";
inline constexpr char kExtensions[]            = "extensions";
inline constexpr char kStorage[]               = "storage";
inline constexpr char kStorageNand[]           = "nand";
inline constexpr char kStorageHdd[]            = "hdd";
inline constexpr char kStorageEmmc[]           = "emmc";
inline constexpr char kNetwork[]               = "network";
inline constexpr char kEnabled[]               = "enabled";
inline constexpr char kMac[]                   = "mac";
inline constexpr char kMtu[]                   = "mtu";
inline constexpr char kForwardTcp[]            = "forward_tcp";
inline constexpr char kForwardUdp[]            = "forward_udp";
inline constexpr char kGuestAdditions[]        = "guest_additions";
inline constexpr char kColorScheme[]           = "override_color_scheme";
inline constexpr char kFontSize[]              = "override_font_size";
inline constexpr char kClearType[]             = "override_cleartype";
inline constexpr char kAllowUntrustedApps[]    = "allow_untrusted_apps";
inline constexpr char kShareFolder[]           = "share_folder";
inline constexpr char kShareFolderMountPoint[] = "shared_folder_mount_point";
inline constexpr char kAutorun[]               = "autorun";
inline constexpr char kFullScreen[]            = "full_screen";
inline constexpr char kDoubleTapHelper[]       = "double_tap_helper";
inline constexpr char kAdoptGaResolution[]     = "adopt_guest_additions_resolution_for_host_screen";
inline constexpr char kAdditionalPackages[]    = "additional_packages";
inline constexpr char kCompactFlashCards[]     = "compact_flash_cards";
inline constexpr char kSdCards[]               = "sd_cards";
inline constexpr char kUsbDisks[]              = "usb_disks";
inline constexpr char kFile[]                  = "file";
inline constexpr char kInsertOnLaunch[]        = "insert_on_launch";
inline constexpr char kCid[]                   = "cid";
inline constexpr char kCpuConfig[]             = "cpu_config";
inline constexpr char kThrottleMultiplier[]    = "throttle_multiplier";
inline constexpr char kCpuRateMultiplier[]     = "cpu_rate_multiplier";

}
