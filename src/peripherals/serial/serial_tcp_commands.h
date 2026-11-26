#pragma once

#include "serial_tcp_address.h"

#include <cerfcore/core/service.h>
#include <cerfcore/host/machine_socket.h>

#include <functional>
#include <vector>

namespace cerfcore {

class SerialTcpCommands : public Service {
public:
    using Service::Service;
    using Plug = std::function<void(const SerialTcpAddress&)>;

    std::vector<InsertCommand> Commands() const;
    void                       Run(MachineObject& socket, Plug plug);
};

}
