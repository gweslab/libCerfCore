#define _CRT_SECURE_NO_WARNINGS
#include "config_loader.h"
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/config_reader.h>
#include "config_mutable_fields.h"
#include <cerfcore/core/device_config_keys.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/cerf_config.h>
#include <cerfcore/core/cli_option.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/host/device_selection.h>
#include "../net/mac_address.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace cerfcore {

using nlohmann::json;

REGISTER_SERVICE(ConfigLoader);

namespace {

void SplitCommaList(const std::string& v, std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= v.size()) {
        size_t comma = v.find(',', start);
        size_t end   = (comma == std::string::npos) ? v.size() : comma;
        std::string item = v.substr(start, end - start);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
            item.erase(item.begin());
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
            item.pop_back();
        if (!item.empty()) out.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

}

ConfigReader& ConfigLoader::Reader() { return emu_.Get<ConfigReader>(); }

void ConfigLoader::SetNonEmptyString(std::string& field, const json& obj, const char* key,
                                     const std::string& path, const std::string& ctx) {
    std::string v = Reader().ReadOptString(obj, key, path, ctx);
    if (!v.empty()) field = std::move(v);
}

void ConfigLoader::SetMetaInt(int& field, const json& obj, const char* key,
                              const std::string& path, const std::string& ctx) {
    int v = Reader().ReadOptInt(obj, key, path, ctx);
    if (v) field = v;
}

void ConfigLoader::LoadMeta(const json& root, DeviceMeta& meta, const std::string& path) {
    if (!root.contains(config_key::kMeta)) return;
    const auto& m = root[config_key::kMeta];
    if (!m.is_object())
        Reader().Fail(path, std::string("'") + config_key::kMeta + "' must be an object");

    SetNonEmptyString(meta.name,        m, config_key::kName,       path, config_key::kMeta);
    SetNonEmptyString(meta.device_name, m, config_key::kDeviceName, path, config_key::kMeta);

    if (m.contains(config_key::kOs)) {
        const auto&       o      = m[config_key::kOs];
        const std::string os_ctx = CfgKeyPath({config_key::kMeta, config_key::kOs});
        if (!o.is_object())
            Reader().Fail(path, "'" + os_ctx + "' must be an object");
        SetNonEmptyString(meta.os_name,  o, config_key::kName,     path, os_ctx);
        SetMetaInt   (meta.os_ver_major, o, config_key::kVerMajor, path, os_ctx);
        SetMetaInt   (meta.os_ver_minor, o, config_key::kVerMinor, path, os_ctx);
    }
}

void ConfigLoader::LoadBoard(const json& root, DeviceConfig& config, const std::string& path) {
    if (!root.contains(config_key::kBoard)) return;
    const auto& b = root[config_key::kBoard];
    if (!b.is_object())
        Reader().Fail(path, std::string("'") + config_key::kBoard + "' must be an object");

    if (b.contains(config_key::kId))
        config.board_id = Reader().ReadOptString(b, config_key::kId, path, config_key::kBoard);

    emu_.Get<ConfigMutableFields>().LoadScreenFields(b, config, path);
}

void ConfigLoader::LoadGaAutorun(const json& ga, DeviceConfig& config, const std::string& path) {
    if (!ga.contains(config_key::kAutorun)) return;
    const auto& list = ga[config_key::kAutorun];
    if (list.is_null()) {
        config.guest_additions_autorun.clear();
        return;
    }
    config.guest_additions_autorun =
        Reader().ReadStringArray(list, path, CfgKeyPath({config_key::kGuestAdditions, config_key::kAutorun}));
}

void ConfigLoader::LoadFeatures(const json& root, DeviceConfig& config, const std::string& path) {
    if (root.contains(config_key::kGuestAdditions)) {
        const auto& ga = root[config_key::kGuestAdditions];
        if (!ga.is_object())
            Reader().Fail(path, std::string("'") + config_key::kGuestAdditions + "' must be an object: { \"" +
                                    config_key::kEnabled + "\": true }");
        Reader().LoadOptBool(ga, config_key::kEnabled, config.guest_additions, path, config_key::kGuestAdditions);
        auto& fields = emu_.Get<ConfigMutableFields>();
        fields.LoadColorScheme(ga, config, path);
        fields.LoadGaFontSize(ga, config, path);
        fields.LoadGaClearType(ga, config, path);
        fields.LoadGaAllowUntrustedApps(ga, config, path);
        fields.LoadShareFolder(ga, config, path);
        fields.LoadShareFolderMountPoint(ga, config, path);
        LoadGaAutorun(ga, config, path);
    }
    Reader().LoadOptBool(root, config_key::kFullScreen, config.start_fullscreen, path, {});
    Reader().LoadOptBool(root, config_key::kDoubleTapHelper, config.double_tap_helper, path, {});
    Reader().LoadOptBool(root, config_key::kAdoptGaResolution, config.adopt_guest_additions_resolution_for_host_screen,
                         path, {});
}

void ConfigLoader::LoadNetwork(const json& root, DeviceConfig& config, const std::string& path) {
    if (!root.contains(config_key::kNetwork)) return;
    const auto& n = root[config_key::kNetwork];
    if (!n.is_object())
        Reader().Fail(path, std::string("'") + config_key::kNetwork + "' must be an object");

    Reader().LoadOptBool(n, config_key::kEnabled, config.network_enabled, path, config_key::kNetwork);
    if (n.contains(config_key::kMac)) {
        std::string v = Reader().ReadOptString(n, config_key::kMac, path, config_key::kNetwork);
        std::array<uint8_t, cerf::inet::kEthMacSize> mac{};
        if (!cerf::inet::ParseMac(v, mac))
            Reader().Fail(path, "'" + CfgKeyPath({config_key::kNetwork, config_key::kMac}) +
                                    "' must be XX:XX:XX:XX:XX:XX hex, not '" + v + "'");
        config.network_mac = v;
    }
    if (n.contains(config_key::kMtu)) {
        int mtu = Reader().ReadOptInt(n, config_key::kMtu, path, config_key::kNetwork);
        /* linux-2.6.25 include/linux/if_ether.h ETH_DATA_LEN: 1500 is the
           maximum octets in an Ethernet payload. */
        if (mtu < 64 || mtu > 1500)
            Reader().Fail(path, "'" + CfgKeyPath({config_key::kNetwork, config_key::kMtu}) +
                                    "' is out of range (64..1500)");
        config.network_mtu = (uint32_t)mtu;
    }
    if (n.contains(config_key::kForwardTcp))
        config.network_forward_tcp = Reader().ReadOptString(n, config_key::kForwardTcp, path, config_key::kNetwork);
    if (n.contains(config_key::kForwardUdp))
        config.network_forward_udp = Reader().ReadOptString(n, config_key::kForwardUdp, path, config_key::kNetwork);
}

void ConfigLoader::LoadRom(const json& root, DeviceConfig& config, const std::string& path) {
    if (!root.contains(config_key::kRom)) return;
    const auto& r = root[config_key::kRom];
    if (!r.is_object())
        Reader().Fail(path, std::string("'") + config_key::kRom + "' must be an object");

    if (r.contains(config_key::kPrimary))
        config.rom_primary = Reader().ReadOptString(r, config_key::kPrimary, path, config_key::kRom);
    if (r.contains(config_key::kEeprom))
        config.rom_eeprom = Reader().ReadOptString(r, config_key::kEeprom, path, config_key::kRom);
    if (r.contains(config_key::kCeDictionary))
        config.rom_ce_dictionary = Reader().ReadOptString(r, config_key::kCeDictionary, path, config_key::kRom);
    if (r.contains(config_key::kLumia800UserAreaErase))
        config.rom_lumia800_user_area_erase =
            Reader().ReadOptString(r, config_key::kLumia800UserAreaErase, path, config_key::kRom);
    if (r.contains(config_key::kRecovery))
        config.rom_recovery = Reader().ReadOptString(r, config_key::kRecovery, path, config_key::kRom);
    if (r.contains(config_key::kExtensions)) {
        const auto&       e              = r[config_key::kExtensions];
        const std::string extensions_key = CfgKeyPath({config_key::kRom, config_key::kExtensions});
        config.rom_extensions.clear();
        if (e.is_string()) {
            SplitCommaList(e.get<std::string>(), config.rom_extensions);
        } else if (e.is_array()) {
            for (const auto& item : e) {
                if (!item.is_string())
                    Reader().Fail(path, "'" + CfgElementPath(extensions_key) + "' entries must be strings");
                config.rom_extensions.push_back(item.get<std::string>());
            }
        } else if (!e.is_null()) {
            Reader().Fail(path, "'" + extensions_key + "' must be a string or an array of strings");
        }
    }
}

void ConfigLoader::LoadStorage(const json& root, DeviceConfig& config, const std::string& path) {
    if (!root.contains(config_key::kStorage)) return;
    const auto& s = root[config_key::kStorage];
    if (!s.is_object())
        Reader().Fail(path, std::string("'") + config_key::kStorage + "' must be an object");

    SetNonEmptyString(config.storage_nand, s, config_key::kStorageNand, path, config_key::kStorage);
    SetNonEmptyString(config.storage_hdd,  s, config_key::kStorageHdd,  path, config_key::kStorage);
    SetNonEmptyString(config.storage_emmc, s, config_key::kStorageEmmc, path, config_key::kStorage);
}

template <class Card>
Card ConfigLoader::ReadBundledImage(const json& e, const std::string& entry, const std::string& path) {
    Card card;
    card.file             = Reader().ReadOptString(e, config_key::kFile, path, entry);
    card.name             = Reader().ReadOptString(e, config_key::kName, path, entry);
    card.insert_on_launch = Reader().ReadOptBool(e, config_key::kInsertOnLaunch, path, entry);
    if (card.file.empty()) Reader().Fail(path, "'" + CfgKeyPath({entry, config_key::kFile}) + "' is required");
    if (card.name.empty()) card.name = card.file;
    return card;
}

void ConfigLoader::LoadUsbMedia(const json& packages, const char* key, bool sd,
                                std::vector<BundledUsbMedia>& cards, const std::string& path) {
    const json* entries = Reader().ReadOptObjectArray(packages, key, path, config_key::kAdditionalPackages);
    if (!entries) return;
    const std::string entry = CfgElementPath(CfgKeyPath({config_key::kAdditionalPackages, key}));
    cards.clear();
    bool launch = false;
    for (const auto& e : *entries) {
        BundledUsbMedia card = ReadBundledImage<BundledUsbMedia>(e, entry, path);
        if (e.contains(config_key::kCid)) {
            const std::string cid_key = CfgKeyPath({entry, config_key::kCid});
            if (!sd) Reader().Fail(path, "'" + cid_key + "' applies only to SD cards");
            card.cid = ParseSdCardCid(Reader().ReadOptString(e, config_key::kCid, path, entry));
            if (!card.cid) Reader().Fail(path, "'" + cid_key + "' must contain exactly 32 hexadecimal digits");
        }
        if (card.insert_on_launch && launch)
            Reader().Fail(path, "'" + CfgKeyPath({entry, config_key::kInsertOnLaunch}) +
                                    "' can be true for one image only");
        launch = launch || card.insert_on_launch;
        cards.push_back(std::move(card));
    }
}

void ConfigLoader::LoadAdditionalPackages(const json& root, DeviceConfig& config,
                                          const std::string& path) {
    if (!root.contains(config_key::kAdditionalPackages)) return;
    const auto& p = root[config_key::kAdditionalPackages];
    if (p.is_null()) return;
    if (!p.is_object())
        Reader().Fail(path, std::string("'") + config_key::kAdditionalPackages + "' must be an object");

    LoadUsbMedia(p, config_key::kSdCards, true, config.bundled_sd_cards, path);
    LoadUsbMedia(p, config_key::kUsbDisks, false, config.bundled_usb_disks, path);

    const json* cards =
        Reader().ReadOptObjectArray(p, config_key::kCompactFlashCards, path, config_key::kAdditionalPackages);
    if (!cards) return;
    const std::string entry =
        CfgElementPath(CfgKeyPath({config_key::kAdditionalPackages, config_key::kCompactFlashCards}));
    config.bundled_compact_flash_cards.clear();
    for (const auto& e : *cards)
        config.bundled_compact_flash_cards.push_back(ReadBundledImage<BundledCompactFlashCard>(e, entry, path));
}

void ConfigLoader::LoadInto(DeviceConfig& config) {
    const int    argc = emu_.Argc();
    char** const argv = emu_.Argv();

    const std::string device_name = emu_.Get<DeviceSelection>().SelectedDevice();
    if (device_name.empty())
        emu_.Get<Fatal>().Die("DeviceSelection named no device");
    config.device_name = device_name;

    const std::string dev_path = GetDeviceDir(device_name) + kDeviceConfigFileName;
    json dev = Reader().ReadLoggedJsonFile(dev_path);
    if (dev.is_null()) {
        LOG(Cfg, "No device config: %s (using DeviceConfig defaults)\n", dev_path.c_str());
    } else {
        LOG(Cfg, "Loading device config: %s\n", dev_path.c_str());
        LoadMeta    (dev, config.meta, dev_path);
        LoadBoard   (dev, config,      dev_path);
        LoadNetwork (dev, config,      dev_path);
        LoadRom     (dev, config,      dev_path);
        LoadStorage (dev, config,      dev_path);
        LoadFeatures(dev, config,      dev_path);
        LoadAdditionalPackages(dev, config, dev_path);
    }

    const std::string user_path = UserConfigPath(device_name);
    json user = Reader().ReadLoggedJsonFile(user_path, CfgBrokenFile::kSkip);
    if (!user.is_null()) {
        LOG(Cfg, "Loading user device config: %s\n", user_path.c_str());
        LoadMeta    (user, config.meta, user_path);
        LoadBoard   (user, config,      user_path);
        LoadNetwork (user, config,      user_path);
        LoadRom     (user, config,      user_path);
        LoadStorage (user, config,      user_path);
        LoadFeatures(user, config,      user_path);
        LoadAdditionalPackages(user, config, user_path);
    }

    bool cli_autorun_seen = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = nullptr;
        if (strcmp(a, kArgDisableNetwork) == 0) {
            config.network_enabled = false;
        } else if (ReadCliOption(a, kArgBoardId, v)) {
            config.board_id = v;
        } else if (ReadCliOption(a, kArgRomPrimary, v)) {
            config.rom_primary = v;
        } else if (strcmp(a, kArgGuestAdditions) == 0) {
            config.guest_additions = true;
        } else if (ReadCliOption(a, kArgGaColorScheme, v)) {
            config.guest_additions_color_scheme = v;
        } else if (ReadCliOption(a, kArgGaFontSize, v)) {
            config.guest_additions_font_size     = (int32_t)atoi(v);
            config.guest_additions_font_size_set = true;
        } else if (ReadCliOption(a, kArgGaAutorun, v)) {
            if (!v[0]) Reader().Fail("(command line)", "--ga-autorun needs a guest path");
            if (!cli_autorun_seen) config.guest_additions_autorun.clear();
            cli_autorun_seen = true;
            config.guest_additions_autorun.push_back(v);
        } else if (strcmp(a, kArgRecovery) == 0) {
            config.boot_in_recovery = true;
        } else if (ReadCliOption(a, kArgScreenWidth, v)) {
            int n = atoi(v);
            if (n < 1) Reader().Fail("(command line)", "--screen-width must be >= 1");
            config.board_configurable_screen_width = (uint32_t)n;
            config.board_configurable_screen_explicit = true;
            config.adopt_guest_additions_resolution_for_host_screen = false;
        } else if (ReadCliOption(a, kArgScreenHeight, v)) {
            int n = atoi(v);
            if (n < 1) Reader().Fail("(command line)", "--screen-height must be >= 1");
            config.board_configurable_screen_height = (uint32_t)n;
            config.board_configurable_screen_explicit = true;
            config.adopt_guest_additions_resolution_for_host_screen = false;
        } else if (ReadCliOption(a, kArgScreenDpi, v)) {
            int n = atoi(v);
            if (n < 1) Reader().Fail("(command line)", "--screen-dpi must be >= 1");
            config.screen_dpi = (uint32_t)n;
        } else if (ReadCliOption(a, kArgScreenBpp, v)) {
            int n = atoi(v);
            if (n < 1) Reader().Fail("(command line)", "--screen-bpp must be >= 1");
            config.board_configurable_screen_bpp = (uint32_t)n;
        } else if (ReadCliOption(a, kArgScreenRefreshRate, v)) {
            int n = atoi(v);
            if (n < 1) Reader().Fail("(command line)", "--screen-refresh-rate must be >= 1");
            config.screen_refresh_rate = (uint32_t)n;
        } else if (ReadCliOption(a, kArgGaShareFolder, v)) {
            config.share_folder = v;
        } else if (strcmp(a, kArgFullScreen) == 0) {
            config.start_fullscreen = true;
        } else if (strcmp(a, kArgNoFocus) == 0) {
            config.no_focus = true;
        } else if (ReadCliOption(a, kArgBoot, v)) {
            if      (strcmp(v, kBootResume) == 0) config.boot_mode = StateBootMode::Resume;
            else if (strcmp(v, kBootWarm)   == 0) config.boot_mode = StateBootMode::Warm;
            else if (strcmp(v, kBootCold)   == 0) config.boot_mode = StateBootMode::Cold;
            else Reader().Fail("(command line)", std::string(CliOptionName(kArgBoot)) + " must be " + kBootResume +
                                                     ", " + kBootWarm + ", or " + kBootCold);
        } else if (ReadCliOption(a, kArgTab, v)) {
            if      (strcmp(v, kTabBoot)        == 0) config.start_tab = CanvasTab::Boot;
            else if (strcmp(v, kTabHw)          == 0) config.start_tab = CanvasTab::Hw;
            else if (strcmp(v, kTabFramebuffer) == 0) config.start_tab = CanvasTab::Framebuffer;
            else Reader().Fail("(command line)", std::string(CliOptionName(kArgTab)) + " must be " + kTabBoot + ", " +
                                                     kTabHw + ", or " + kTabFramebuffer);
        }
    }

    ApplyBoardPanelDefault(config);
}

void ConfigLoader::ApplyBoardPanelDefault(DeviceConfig& config) {
    if (config.board_configurable_screen_explicit) return;
    const DbBoard* board = emu_.Get<BoardDatabase>().FindBoard(config.board_id);
    if (!board || !board->lcd_panel_size) return;
    config.board_configurable_screen_width  = board->lcd_panel_size->width;
    config.board_configurable_screen_height = board->lcd_panel_size->height;
}

}
