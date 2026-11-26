#include "serial_tcp_startup.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/execution/jit_runner.h>

namespace cerfcore {

REGISTER_SERVICE(SerialTcpStartup);

void SerialTcpStartup::OnReady() {
    if (emu_.Get<DeviceConfig>().serial_tcp_binding.empty()) return;
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { RequireClaimed(); });
}

std::optional<SerialTcpAddress> SerialTcpStartup::Claim(const std::string& socket_id) {
    const DeviceConfig& config = emu_.Get<DeviceConfig>();
    if (config.serial_tcp_binding.empty() || config.serial_tcp_socket != socket_id) return std::nullopt;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        claimed_ = true;
    }
    return *SerialTcpAddress::Parse(config.serial_tcp_binding);
}

void SerialTcpStartup::RequireClaimed() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (claimed_) return;
    }
    emu_.Get<Fatal>().UserError("--serial-tcp names the serial port '%s', which this device does not have",
                                emu_.Get<DeviceConfig>().serial_tcp_socket.c_str());
}

}
