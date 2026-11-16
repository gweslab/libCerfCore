#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

class BoardBootImage : public Service {
public:
    using Service::Service;
    virtual bool ReadBootImage(std::vector<uint8_t>& flat, std::string& error) = 0;
};

}
