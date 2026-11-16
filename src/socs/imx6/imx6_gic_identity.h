#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx6_gic_detail {

inline constexpr uint32_t kIccIdr = 0x3901243Bu;

inline constexpr uint32_t kIcdIidr = 0x0102043Bu;

inline constexpr uint32_t kCortexA9CoreCount = 1u;
inline constexpr uint32_t kIcdIctrOtherFields = 0x0000FC00u;
inline constexpr uint32_t kIcdIctrCpuNumberShift = 5u;
inline constexpr uint32_t kIcdIctrInterruptNumber = 4u;
inline constexpr uint32_t kIcdIctrCpuNumber = (kCortexA9CoreCount - 1u) << kIcdIctrCpuNumberShift;
inline constexpr uint32_t kIcdIctr = kIcdIctrOtherFields | kIcdIctrCpuNumber | kIcdIctrInterruptNumber;

inline constexpr bool kIcdiptrReadAsZeroWriteIgnored = kCortexA9CoreCount == 1u;

}

}
