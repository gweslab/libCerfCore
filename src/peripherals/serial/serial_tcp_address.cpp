#include "serial_tcp_address.h"

#include <cstddef>

namespace cerfcore {

namespace {

constexpr size_t   kIpv4Octets     = 4;
constexpr size_t   kOctetDigitsMax = 3;
constexpr unsigned kOctetMax       = 255;
constexpr size_t   kPortDigitsMax  = 5;
constexpr unsigned kPortMax        = 65535;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

}

std::string SerialTcpAddress::Binding() const { return address + ":" + std::to_string(port); }

std::optional<SerialTcpAddress> SerialTcpAddress::Parse(const std::string& binding) {
    const size_t colon = binding.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    const std::string             address = binding.substr(0, colon);
    const std::optional<uint16_t> port    = ParsePort(binding.substr(colon + 1));
    if (!port || !IsIpv4Literal(address)) return std::nullopt;
    return SerialTcpAddress{address, *port};
}

bool SerialTcpAddress::IsIpv4Literal(const std::string& text) {
    size_t   octets = 0;
    size_t   digits = 0;
    unsigned value  = 0;
    for (const char c : text) {
        if (c == '.') {
            if (digits == 0) return false;
            ++octets;
            digits = 0;
            value  = 0;
            continue;
        }
        if (!IsDigit(c) || ++digits > kOctetDigitsMax) return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
        if (value > kOctetMax) return false;
    }
    return digits != 0 && octets + 1 == kIpv4Octets;
}

std::optional<uint16_t> SerialTcpAddress::ParsePort(const std::string& text) {
    if (text.empty() || text.size() > kPortDigitsMax) return std::nullopt;
    unsigned value = 0;
    for (const char c : text) {
        if (!IsDigit(c)) return std::nullopt;
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > kPortMax) return std::nullopt;
    return static_cast<uint16_t>(value);
}

}
