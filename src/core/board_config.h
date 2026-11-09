#pragma once

#include <cerfcore/core/service.h>

#include "name_table.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace cerfcore {

class BoardConfig : public Service {
public:
    using Service::Service;

    void OnReady() override;

    const std::string& EnumId(std::string_view param_id) const;

    template <typename T, size_t N>
    T Enum(std::string_view param_id, const std::pair<std::string_view, T> (&table)[N]) const {
        return ValueByName(table, EnumId(param_id));
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

}
