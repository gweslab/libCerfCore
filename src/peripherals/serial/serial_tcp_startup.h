#pragma once

#include "serial_tcp_address.h"

#include <cerfcore/core/service.h>

#include <mutex>
#include <optional>
#include <string>

namespace cerfcore {

class SerialTcpStartup : public Service {
public:
    using Service::Service;

    void OnReady() override;

    std::optional<SerialTcpAddress> Claim(const std::string& socket_id);

private:
    void RequireClaimed();

    std::mutex mtx_;
    bool       claimed_ = false;
};

}
