#pragma once

#include "service.h"

#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

enum class DeviceFileSection { kRom, kStorage };

struct DeviceFileType {
    DeviceFileSection        section;
    std::string              id;
    std::string              name;
    std::vector<std::string> formats;
    std::string              note;
    bool                     optional;
    std::vector<std::string> optional_for_storage_ids;
    std::vector<std::string> board_ids;
    std::vector<std::string> board_family_ids;
};

class DeviceFileTypes : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::vector<DeviceFileType> ForBoard(std::string_view board_id) const;

private:
    std::vector<DeviceFileType> rom_types_;
    std::vector<DeviceFileType> storage_types_;
};

}
