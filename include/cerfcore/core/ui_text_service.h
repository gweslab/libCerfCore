#pragma once

#include <cerfcore/core/cerf_emulator.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace cerfcore {

using UiTextArguments = std::initializer_list<std::pair<std::string_view, std::string>>;

class UiTextService : public Service {
public:
    using Service::Service;

    virtual std::string Translate(std::string_view key,
                                  std::string_view fallback) const = 0;
    virtual std::string FormatText(std::string_view key,
                                  std::string_view fallback,
                                  UiTextArguments arguments) const = 0;
};

inline std::string GetUiText(CerfEmulator& emu, std::string_view key,
                             std::string_view fallback) {
    if (const UiTextService* service = emu.TryGet<UiTextService>())
        return service->Translate(key, fallback);
    return std::string(fallback);
}

inline std::string FormatUiText(CerfEmulator& emu, std::string_view key,
                                std::string_view fallback,
                                UiTextArguments arguments) {
    if (const UiTextService* service = emu.TryGet<UiTextService>())
        return service->FormatText(key, fallback, arguments);

    std::string result;
    for (std::size_t i = 0; i < fallback.size();) {
        if (fallback[i] == '{' && i + 1 < fallback.size() && fallback[i + 1] == '{') {
            result.push_back('{');
            i += 2;
            continue;
        }
        if (fallback[i] == '}' && i + 1 < fallback.size() && fallback[i + 1] == '}') {
            result.push_back('}');
            i += 2;
            continue;
        }
        if (fallback[i] == '{') {
            const std::size_t end = fallback.find('}', i + 1);
            if (end != std::string_view::npos) {
                const std::string_view name = fallback.substr(i + 1, end - i - 1);
                bool replaced = false;
                for (const auto& [argument, value] : arguments) {
                    if (argument == name) {
                        result += value;
                        replaced = true;
                        break;
                    }
                }
                if (replaced) {
                    i = end + 1;
                    continue;
                }
            }
        }
        result.push_back(fallback[i++]);
    }
    return result;
}

}
