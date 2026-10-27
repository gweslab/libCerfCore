#pragma once

#include <memory>

struct SlirpConfig;

namespace cerfcore {

namespace host_platform {

class HostDnsProxy {
public:
    virtual ~HostDnsProxy() = default;

    virtual void Configure(SlirpConfig& config) const = 0;
};

int OpenHostDnsProxy(std::unique_ptr<HostDnsProxy>& out);

}

}
