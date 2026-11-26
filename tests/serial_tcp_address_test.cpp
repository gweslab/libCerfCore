#include "../src/peripherals/serial/serial_tcp_address.h"

#include <cstdio>
#include <optional>
#include <string>

namespace {

void Check(bool ok, const char* name, int& failures) {
    if (ok) return;
    std::printf("FAIL %s\n", name);
    ++failures;
}

bool Parses(const std::string& binding, const std::string& address, uint16_t port) {
    const std::optional<cerfcore::SerialTcpAddress> parsed = cerfcore::SerialTcpAddress::Parse(binding);
    return parsed && parsed->address == address && parsed->port == port;
}

bool Refuses(const std::string& binding) { return !cerfcore::SerialTcpAddress::Parse(binding).has_value(); }

}

int main() {
    int failures = 0;

    Check(cerfcore::SerialTcpAddress{}.Binding() == "127.0.0.1:9991", "default binding", failures);
    Check(Parses("127.0.0.1:9991", "127.0.0.1", 9991), "loopback", failures);
    Check(Parses("0.0.0.0:1", "0.0.0.0", 1), "any address, lowest port", failures);
    Check(Parses("192.168.1.20:65535", "192.168.1.20", 65535), "highest port", failures);

    Check(Refuses("127.0.0.1:0"), "port 0", failures);
    Check(Refuses("127.0.0.1:65536"), "port above 65535", failures);
    Check(Refuses("127.0.0.1:"), "empty port", failures);
    Check(Refuses("127.0.0.1:99a"), "port with a letter", failures);
    Check(Refuses("9991"), "no address", failures);
    Check(Refuses("localhost:9991"), "host name", failures);
    Check(Refuses("256.0.0.1:9991"), "octet above 255", failures);
    Check(Refuses("1.2.3:9991"), "three octets", failures);
    Check(Refuses("1.2.3.4.5:9991"), "five octets", failures);
    Check(Refuses("1..3.4:9991"), "empty octet", failures);
    Check(Refuses("1.2.3.4.:9991"), "trailing dot", failures);
    Check(Refuses("1.2.3.0004:9991"), "four-digit octet", failures);

    if (failures == 0) std::printf("serial_tcp_address_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
