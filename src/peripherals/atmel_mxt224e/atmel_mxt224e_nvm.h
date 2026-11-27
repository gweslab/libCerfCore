#pragma once

#include "atmel_mxt224e_map.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace cerfcore {

class AtmelMxt224eNvm : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    const std::array<uint8_t, kMxtNvmBytes>& Bytes() const { return bytes_; }
    void     Backup(std::span<const uint8_t, kMxtNvmBytes> config);
    uint32_t Checksum() const { return checksum_; }

    static uint32_t Crc24(std::span<const uint8_t> data);

private:
    void Persist() const;

    std::string                        path_;
    std::array<uint8_t, kMxtNvmBytes>  bytes_{};
    uint32_t                           checksum_ = 0u;
};

}
