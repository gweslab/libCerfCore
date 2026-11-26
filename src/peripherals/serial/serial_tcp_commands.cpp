#include "serial_tcp_commands.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/machine_requests.h>

#include <limits>
#include <utility>

namespace cerfcore {

namespace {

constexpr char kListen[]      = "listen";
constexpr char kPortRequest[] = "serial_tcp_port";

}

REGISTER_SERVICE(SerialTcpCommands);

std::vector<InsertCommand> SerialTcpCommands::Commands() const { return {InsertCommand{kListen, {}}}; }

void SerialTcpCommands::Run(MachineObject& socket, Plug plug) {
    MachineRequest request;
    request.kind        = MachineRequestKind::kSize;
    request.id          = kPortRequest;
    request.object      = &socket;
    request.minimum     = 1;
    request.maximum     = std::numeric_limits<uint16_t>::max();
    request.recommended = SerialTcpAddress::kDefaultPort;
    emu_.Get<MachineRequests>().Ask(std::move(request), [plug = std::move(plug)](const MachineReply& reply) {
        if (reply.cancelled) return;
        SerialTcpAddress address;
        address.port = static_cast<uint16_t>(reply.size);
        plug(address);
    });
}

}
