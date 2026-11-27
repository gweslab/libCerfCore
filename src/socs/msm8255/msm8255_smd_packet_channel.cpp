#include "msm8255_smd_packet_channel.h"

#include "msm8255_qmux_peer.h"
#include "msm8255_smd_stage.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kSmdPacketHeaderBytes = 20u;
constexpr uint32_t kAppsWriterSlackBytes = 4u;

}

bool Msm8255SmdPacketChannel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

uint32_t Msm8255SmdPacketChannel::Answer(uint32_t port, uint32_t in_pa, uint32_t in_avail,
                                         uint32_t ring_bytes, uint32_t out_pa,
                                         uint32_t out_cap, uint32_t& consumed) {
    consumed = 0u;
    if (in_avail < kSmdPacketHeaderBytes) return 0u;

    auto& mem = emu_.Get<EmulatedMemory>();
    uint8_t hdr[kSmdPacketHeaderBytes] = {};
    mem.CopyOut(in_pa, hdr, kSmdPacketHeaderBytes);
    const uint32_t len = cerf::le::U32(hdr, 0u);
    for (uint32_t off = 4u; off < kSmdPacketHeaderBytes; off += 4u) {
        if (cerf::le::U32(hdr, off) != 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 smd packet channel: smd packet header word +%u carries "
                "0x%08X, and only zero is modeled", off,
                cerf::le::U32(hdr, off));
        }
    }
    const uint32_t queued_max = ring_bytes - kAppsWriterSlackBytes;
    if (len > queued_max - kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 smd packet channel: a %u-byte smd packet and its %u-byte header "
            "exceed the %u bytes the apps writer keeps queued in a %u-byte fifo",
            len, kSmdPacketHeaderBytes, queued_max, ring_bytes);
    }
    if (len > Msm8255SmdStage::ReadCapacity() - kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 smd packet channel: a %u-byte smd packet exceeds the %u-byte read "
            "stage", len, Msm8255SmdStage::ReadCapacity());
    }
    if (in_avail < kSmdPacketHeaderBytes + len) return 0u;

    if (out_cap < kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 smd packet channel: an smd packet reply needs its %u-byte header "
            "and the window at 0x%08X has %u", kSmdPacketHeaderBytes, out_pa,
            out_cap);
    }
    const uint32_t reply = emu_.Get<Msm8255QmuxPeer>().Answer(
        port, in_pa + kSmdPacketHeaderBytes, len, out_pa + kSmdPacketHeaderBytes,
        out_cap - kSmdPacketHeaderBytes);

    uint8_t out_hdr[kSmdPacketHeaderBytes] = {};
    cerf::le::Put32(out_hdr, reply);
    mem.CopyIn(out_pa, out_hdr, kSmdPacketHeaderBytes);

    consumed = kSmdPacketHeaderBytes + len;
    return kSmdPacketHeaderBytes + reply;
}

REGISTER_SERVICE(Msm8255SmdPacketChannel);

}
