#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cerfcore {

struct SerialTcpAddress {
    static constexpr const char* kLoopback    = "127.0.0.1";
    static constexpr uint16_t    kDefaultPort = 9991;

    std::string address = kLoopback;
    uint16_t    port    = kDefaultPort;

    std::string Binding() const;

    static std::optional<SerialTcpAddress> Parse(const std::string& binding);
    static bool                            IsIpv4Literal(const std::string& text);
    static std::optional<uint16_t>         ParsePort(const std::string& text);
};

}
