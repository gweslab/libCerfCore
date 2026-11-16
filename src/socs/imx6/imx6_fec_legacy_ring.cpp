#include "imx6_fec_legacy_ring.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/crc32.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../net/network_backend.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kDescriptorStride = 8u;
/* Safety limit to prevent an infinite loop on a broken descriptor ring. */
constexpr uint32_t kMaximumDescriptors = 1024u;
constexpr uint32_t kDemandActive = 0x01000000u;

constexpr uint16_t kDescriptorOwned = 0x8000u;
constexpr uint16_t kDescriptorWrap = 0x2000u;
constexpr uint16_t kDescriptorLast = 0x0800u;
constexpr uint16_t kDescriptorLarge = 0x0020u;
constexpr uint16_t kDescriptorCrcError = 0x0004u;
constexpr uint16_t kDescriptorTruncated = 0x0001u;

constexpr uint32_t kEirRxb = 0x01000000u;
constexpr uint32_t kEirRxf = 0x02000000u;
constexpr uint32_t kEirTxb = 0x04000000u;
constexpr uint32_t kEirTxf = 0x08000000u;
constexpr uint32_t kEirBabt = 0x20000000u;

uint32_t NextDescriptor(uint32_t current, uint16_t status, uint32_t base) {
    return (status & kDescriptorWrap) ? base : current + kDescriptorStride;
}
}

uint8_t* Imx6FecLegacyRing::RequireDescriptor(uint32_t pa) {
    if (uint8_t* host = emu_.Get<EmulatedMemory>().TryTranslateRange(pa, kDescriptorStride, true)) return host;
    emu_.Get<Fatal>().Die("i.MX6 FEC: buffer descriptor at 0x%08X is not mapped for %u bytes",
                          pa, kDescriptorStride);
}

uint8_t* Imx6FecLegacyRing::RequireBuffer(uint32_t pa, std::size_t bytes, bool write) {
    if (uint8_t* host = emu_.Get<EmulatedMemory>().TryTranslateRange(pa, bytes, write)) return host;
    emu_.Get<Fatal>().Die("i.MX6 FEC: buffer 0x%08X+%zu is not mapped for %s", pa, bytes,
                          write ? "write" : "read");
}

void Imx6FecLegacyRing::Reset() {
    rdar_ = 0u;
    tdar_ = 0u;
    rx_descriptor_ = 0u;
    tx_descriptor_ = 0u;
}

void Imx6FecLegacyRing::SaveState(StateWriter& writer) const {
    writer.Write("rdar", rdar_);
    writer.Write("tdar", tdar_);
    writer.Write("rx_descriptor", rx_descriptor_);
    writer.Write("tx_descriptor", tx_descriptor_);
}

void Imx6FecLegacyRing::RestoreState(StateReader& reader) {
    reader.Read("rdar", rdar_);
    reader.Read("tdar", tdar_);
    reader.Read("rx_descriptor", rx_descriptor_);
    reader.Read("tx_descriptor", tx_descriptor_);
}

void Imx6FecLegacyRing::SetRxDescriptorBase(uint32_t base) {
    rx_descriptor_ = base;
}

void Imx6FecLegacyRing::SetTxDescriptorBase(uint32_t base) {
    tx_descriptor_ = base;
}

void Imx6FecLegacyRing::Disable(uint32_t rx_base, uint32_t tx_base) {
    rdar_ = 0u;
    tdar_ = 0u;
    rx_descriptor_ = rx_base;
    tx_descriptor_ = tx_base;
}

void Imx6FecLegacyRing::RequestReceive(bool controller_enabled) {
    if (!controller_enabled) {
        rdar_ = 0u;
        return;
    }
    RefreshReceiveDemand();
}

void Imx6FecLegacyRing::RefreshReceiveDemand() {
    uint8_t* descriptor = RequireDescriptor(rx_descriptor_);
    const uint16_t status = cerf::le::U16(descriptor, 2u);
    rdar_ = (status & kDescriptorOwned) ? kDemandActive : 0u;
}

uint32_t Imx6FecLegacyRing::RequestTransmit(bool controller_enabled,
                                            bool cable_connected, uint32_t tx_base,
                                            uint32_t max_frame_length) {
    if (!controller_enabled || tx_base == 0u) {
        tdar_ = 0u;
        return 0u;
    }

    tdar_ = kDemandActive;
    uint32_t events = 0u;
    std::vector<uint8_t> frame;
    frame.reserve(max_frame_length);

    bool stopped_on_unowned = false;
    for (uint32_t count = 0u; count < kMaximumDescriptors; ++count) {
        uint8_t* descriptor = RequireDescriptor(tx_descriptor_);

        const uint16_t length = cerf::le::U16(descriptor);
        uint16_t status = cerf::le::U16(descriptor, 2u);
        const uint32_t data_address = cerf::le::U32(descriptor, 4u);
        if ((status & kDescriptorOwned) == 0u) {
            stopped_on_unowned = true;
            break;
        }
        if (length != 0u) {
            const uint8_t* data = RequireBuffer(data_address, length, false);
            frame.insert(frame.end(), data, data + length);
        }

        const bool last = (status & kDescriptorLast) != 0u;
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        cerf::le::Put16(descriptor + 2, status);
        events |= kEirTxb;
        tx_descriptor_ = NextDescriptor(tx_descriptor_, status, tx_base);

        if (last) {
            if (frame.size() + sizeof(uint32_t) > max_frame_length) events |= kEirBabt;
            if (cable_connected && !frame.empty()) emu_.Get<NetworkBackend>().SendFrame(frame.data(), frame.size());
            frame.clear();
            events |= kEirTxf;
        }
    }
    if (!stopped_on_unowned)
        emu_.Get<Fatal>().Die("i.MX6 FEC: transmit descriptor walk reached the ungrounded limit of %u",
                              kMaximumDescriptors);

    tdar_ = 0u;
    return events;
}

uint32_t Imx6FecLegacyRing::Receive(const uint8_t* frame, std::size_t length, uint32_t rx_base,
                                    uint32_t max_receive_buffer, bool strip_crc,
                                    uint32_t wire_frame_length, uint32_t max_frame_length,
                                    uint32_t truncation_length) {
    if (rdar_ == 0u || max_receive_buffer == 0u) return 0u;

    std::vector<uint8_t> packet(frame, frame + length);
    if (!strip_crc) cerf::le::Append32(packet, Crc32::Update(0u, frame, length));
    const bool over_max_frame = wire_frame_length > max_frame_length;
    const bool truncated = wire_frame_length > truncation_length;
    if (truncated) {
        const std::size_t crc_bytes = strip_crc ? 4u : 0u;
        packet.resize(truncation_length > crc_bytes ? truncation_length - crc_bytes : 0u);
    }

    uint32_t events = 0u;
    std::size_t offset = 0u;
    bool ring_exhausted = false;
    for (uint32_t count = 0u; count < kMaximumDescriptors && offset < packet.size(); ++count) {
        uint8_t* descriptor = RequireDescriptor(rx_descriptor_);

        uint16_t status = cerf::le::U16(descriptor, 2u);
        const uint32_t data_address = cerf::le::U32(descriptor, 4u);
        if ((status & kDescriptorOwned) == 0u) {
            ring_exhausted = true;
            break;
        }

        const std::size_t copy_length = std::min<std::size_t>(packet.size() - offset, max_receive_buffer);
        uint8_t* destination = RequireBuffer(data_address, copy_length, true);
        std::memcpy(destination, packet.data() + offset, copy_length);
        offset += copy_length;

        const bool last = offset == packet.size();
        const uint16_t descriptor_length = static_cast<uint16_t>(copy_length);
        status &= static_cast<uint16_t>(~kDescriptorOwned);
        if (last) {
            status |= kDescriptorLast;
            if (over_max_frame) status |= kDescriptorLarge;
            if (truncated) status |= kDescriptorTruncated | kDescriptorCrcError;
        }
        cerf::le::Put16(descriptor, descriptor_length);
        cerf::le::Put16(descriptor + 2, status);
        events |= last ? kEirRxf : kEirRxb;
        rx_descriptor_ = NextDescriptor(rx_descriptor_, status, rx_base);
    }
    if (ring_exhausted)
        emu_.Get<Fatal>().Die("i.MX6 FEC: receive ring has no empty descriptor for the rest of a frame "
                              "(%zu of %zu bytes placed), not modelled", offset, packet.size());
    if (offset < packet.size())
        emu_.Get<Fatal>().Die("i.MX6 FEC: receive descriptor walk reached the ungrounded limit of %u",
                              kMaximumDescriptors);

    RefreshReceiveDemand();
    return events;
}

}
