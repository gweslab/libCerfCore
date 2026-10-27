#pragma once

#include <cerfcore/core/cpu_config_database.h>
#include <cerfcore/core/service.h>

#include "name_table.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace cerfcore {

class CpuConfig : public Service {
public:
    using Service::Service;

    void OnReady() override;

    bool               Boolean(std::string_view id) const;
    uint32_t           Number(std::string_view id) const;
    const std::string& EnumId(std::string_view id) const;
    const std::string& Origin(std::string_view id) const;

    template <typename T, size_t N>
    T Enum(std::string_view id, const std::pair<std::string_view, T> (&table)[N]) const {
        return ValueByName(table, EnumId(id));
    }

private:
    void ApplyUserOverrides();

    CpuConfigValues                                 values_;
    std::map<std::string, std::string, std::less<>> origins_;
};

}
