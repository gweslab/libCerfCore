#include "msm8255_modem_peer.h"

#include "msm8255_dal_remote_server.h"
#include "msm8255_dem_master.h"
#include "msm8255_qmux_peer.h"
#include "msm8255_rpc_router_peer.h"
#include "msm8255_smd_packet_channel.h"
#include "msm8255_smd_stage.h"
#include "msm8255_smem.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../irq_controller.h"
#include "msm8255_system_power.h"

#include <cstdint>
#include <cstring>

namespace cerfcore {

namespace {

/* Linux arch/arm/mach-msm proc_comm.c: APP_COMMAND 0x00, MDM_STATUS 0x14. */
constexpr uint32_t kAppCommandOff = 0x00u;
constexpr uint32_t kMdmStatusOff  = 0x14u;

/* Linux arch/arm/mach-msm proc_comm.h: PCOM_READY. */
constexpr uint32_t kPcomReady = 1u;

constexpr uint32_t kPcomPowerDown = 10u;

/* Linux arch/arm/mach-msm smd_private.h: SMEM_SMSM_SHARED_STATE evaluated over
   its enum with SMEM_NUM_SMD_CHANNELS 64, and SMSM_STATE_MODEM from the
   non-MSM7X00A enum smsm_state_item. */
constexpr uint32_t kIdSmsmSharedState = 0x55u;
constexpr uint32_t kSmsmStateModem    = 1u;

/* Linux arch/arm/mach-msm smd_private.h SMSM_V1_SIZE, the eight-entry shared
   state that smd.c smd_core_init accepts. */
constexpr uint32_t kSmsmStateBytes = 32u;

/* Linux arch/arm/mach-msm smd.c publishes SMSM_INIT | SMSM_SMDINIT |
   SMSM_RPCINIT | SMSM_RUN for a processor that is up and running. */
constexpr uint32_t kSmsmModemUp = 0x129u;

/* Linux arch/arm/mach-msm smd_private.h msm_a2m_int writes 1 << irq to
   MSM_GCC_BASE + 0x8; proc_comm.c uses irq 6 and smd.c uses irqs 0 and 5. */
constexpr uint32_t kA2mSmdModem = 1u << 0;
constexpr uint32_t kA2mSmsm     = 1u << 5;
constexpr uint32_t kA2mProcComm = 1u << 6;

/* Linux arch/arm/mach-msm irqs-7x30.h INT_A9_M2A_0 and INT_A9_M2A_5, which
   smd.c smd_core_init requests IRQF_TRIGGER_RISING for smd_modem_irq_handler
   and smsm_irq_handler. */
constexpr int kVicM2aSmd  = 22;
constexpr int kVicM2aSmsm = 27;

constexpr int kVicM2a6 = 28;

/* Linux arch/arm/mach-msm smd_private.h: SMEM_CHANNEL_ALLOC_TBL,
   SMEM_SMD_BASE_ID and SMEM_SMD_FIFO_BASE_ID evaluated over its enum with
   SMEM_NUM_SMD_CHANNELS 64, struct smd_alloc_elm, and SMD_CHANNELS. */
constexpr uint32_t kIdChannelAllocTbl = 13u;
constexpr uint32_t kIdSmdBase         = 14u;
constexpr uint32_t kIdSmdFifoBase     = 338u;

constexpr uint32_t kFifoWindowAlign = 0x1Fu;
constexpr uint32_t kFifoWindowMin   = 0x400u;
constexpr uint32_t kFifoWindowMax   = 0x10000u;

static_assert(Msm8255SmdStage::WriteCapacity() >= kFifoWindowMax,
              "the write stage must cover the largest accepted fifo direction");

constexpr uint32_t kSmdChannels       = 64u;
constexpr uint32_t kAllocElmBytes     = 32u;
constexpr uint32_t kAllocElmNameBytes = 20u;
constexpr uint32_t kAllocElmCidOff    = 20u;
constexpr uint32_t kAllocElmCtypeOff  = 24u;
constexpr uint32_t kAllocElmRefOff    = 28u;

/* Linux arch/arm/mach-msm smd_private.h: struct smd_half_channel and
   struct smd_shared_v2, whose ch0 and ch1 are one half-channel apart. */
constexpr uint32_t kHalfChannelBytes = 20u;
constexpr uint32_t kSmdSharedBytes   = kHalfChannelBytes * 2u;
constexpr uint32_t kHcFDsrOff        = 4u;
constexpr uint32_t kHcFCtsOff        = 5u;
constexpr uint32_t kHcFCdOff         = 6u;
constexpr uint32_t kHcFHeadOff       = 8u;
constexpr uint32_t kHcFTailOff       = 9u;
constexpr uint32_t kHcFStateOff      = 10u;
constexpr uint32_t kHcTailOff        = 12u;
constexpr uint32_t kHcHeadOff        = 16u;

/* Linux arch/arm/mach-msm smd.c smd_stream_write_avail answers
   fifo_mask - ((head - tail) & fifo_mask), leaving one byte unwritten so that
   head meeting tail always reads as empty. */
constexpr uint32_t SmdWriteAvail(uint32_t half, uint32_t head, uint32_t tail) {
    return half - 1u - ((head + half - tail) % half);
}

/* Linux arch/arm/mach-msm smd_private.h: SMD_SS_*, SMD_TYPE_MASK and
   SMD_TYPE_APPS_MODEM. */
constexpr uint32_t kSmdSsClosed       = 0u;
constexpr uint32_t kSmdSsOpening      = 1u;
constexpr uint32_t kSmdSsOpened       = 2u;
constexpr uint32_t kSmdTypeMask       = 0xFFu;
constexpr uint32_t kSmdTypeAppsModem  = 0x00u;

/* Linux arch/arm/mach-msm smd_private.h: SMD_KIND_MASK, SMD_KIND_UNKNOWN,
   SMD_KIND_STREAM and SMD_KIND_PACKET. */
constexpr uint32_t kSmdKindMask    = 0xF00u;
constexpr uint32_t kSmdKindUnknown = 0x000u;
constexpr uint32_t kSmdKindStream  = 0x100u;
constexpr uint32_t kSmdKindPacket  = 0x200u;

constexpr uint32_t kRpcRouterCid = 2u;

constexpr char kDalPortName[] = "DAL0";

}

bool Msm8255ModemPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ModemPeer::OnReady() {
    irq_ = &emu_.Get<IrqController>();
    SeedProcCommReady();
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { SeedProcCommReady(); });
    emu_.Get<GuestColdBoot>().RegisterReplay([this] { SeedProcCommReady(); });
}

void Msm8255ModemPeer::RingDoorbell(uint32_t mask) {
    switch (mask) {
    case kA2mSmdModem: NotifySmd();         return;
    case kA2mSmsm:     PublishModemState(); return;
    case kA2mProcComm: RunProcComm();       return;
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: apps-to-modem doorbell mask 0x%03X is not "
            "modeled", mask);
    }
}

void Msm8255ModemPeer::SeedProcCommReady() {
    auto& smem = emu_.Get<Msm8255Smem>();
    emu_.Get<EmulatedMemory>().WriteWord(smem.SmemPa() + kMdmStatusOff,
                                         kPcomReady);
}

void Msm8255ModemPeer::PublishModemState() {
    auto& smem = emu_.Get<Msm8255Smem>();
    const uint32_t state = smem.ItemPa(kIdSmsmSharedState, kSmsmStateBytes);
    if (state == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: the guest rang the SMSM doorbell before "
            "allocating smem item %u", kIdSmsmSharedState);
    }
    auto&      mem     = emu_.Get<EmulatedMemory>();
    const bool changed = mem.ReadWord(state + 4u * kSmsmStateModem) != kSmsmModemUp;
    if (changed) mem.WriteWord(state + 4u * kSmsmStateModem, kSmsmModemUp);
    const bool dem_changed = emu_.Get<Msm8255DemMaster>().Answer(state);
    if (changed || dem_changed) irq_->PulseIrq(kVicM2aSmsm);
}

void Msm8255ModemPeer::RaiseM2a6() {
    irq_->PulseIrq(kVicM2a6);
}

void Msm8255ModemPeer::NotifySmd() {
    auto& smem = emu_.Get<Msm8255Smem>();
    auto& mem  = emu_.Get<EmulatedMemory>();
    const uint32_t tbl =
        smem.ItemPa(kIdChannelAllocTbl, kAllocElmBytes * kSmdChannels);
    if (tbl == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: the guest rang the SMD doorbell before "
            "allocating smem item %u", kIdChannelAllocTbl);
    }

    for (uint32_t n = 0; n < kSmdChannels; ++n) {
        const uint32_t rec = tbl + kAllocElmBytes * n;
        if (mem.ReadWord(rec + kAllocElmRefOff) == 0u) continue;
        if (mem.ReadByte(rec) == 0u) continue;

        const uint32_t ctype = mem.ReadWord(rec + kAllocElmCtypeOff);
        if ((ctype & kSmdTypeMask) != kSmdTypeAppsModem) continue;

        const uint32_t cid = mem.ReadWord(rec + kAllocElmCidOff);
        if (cid >= kSmdChannels) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem peer: smd channel %u names cid %u, which is "
                "outside the %u-channel table", n, cid, kSmdChannels);
        }

        const uint32_t item = smem.ItemPa(kIdSmdBase + cid, kSmdSharedBytes);
        if (item == 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem peer: smd channel %u names cid %u, whose smem "
                "item %u is not allocated", n, cid, kIdSmdBase + cid);
        }

        ServiceSmdChannel(cid, rec, item);
    }
}

void Msm8255ModemPeer::ServiceSmdChannel(uint32_t cid, uint32_t rec,
                                         uint32_t item) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t apps_half  = item;
    const uint32_t modem_half = item + kHalfChannelBytes;

    const uint32_t apps_state = mem.ReadWord(apps_half);
    if (apps_state == kSmdSsClosed) return;
    if (apps_state != kSmdSsOpening && apps_state != kSmdSsOpened) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: apps smd half-channel state %u is not modeled",
            apps_state);
    }

    if (apps_state == kSmdSsOpened &&
        mem.ReadWord(modem_half) == kSmdSsOpened) {
        ConsumeAppsSmdFlags(cid, rec, apps_half);
    }

    if (apps_state == kSmdSsOpening) {
        mem.WriteWord(apps_half + kHcTailOff, 0u);
    }

    if (mem.ReadWord(modem_half) != kSmdSsOpened) {
        OpenModemSmdHalf(modem_half);
    }
}

void Msm8255ModemPeer::ConsumeAppsSmdFlags(uint32_t cid, uint32_t rec,
                                           uint32_t apps_half_pa) {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (mem.ReadByte(apps_half_pa + kHcFHeadOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFHeadOff, 0u);
    }
    if (mem.ReadByte(apps_half_pa + kHcFTailOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFTailOff, 0u);
    }
    if (mem.ReadByte(apps_half_pa + kHcFStateOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFStateOff, 0u);
    }
    if (mem.ReadWord(apps_half_pa + kHcHeadOff) !=
        mem.ReadWord(apps_half_pa + kHcTailOff)) {
        ServiceSmdData(cid, rec, apps_half_pa);
    }
}

bool Msm8255ModemPeer::ChannelNameIs(uint32_t rec, const char* name,
                                     uint32_t bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t i = 0; i < bytes; ++i) {
        if (mem.ReadByte(rec + i) != (uint8_t)name[i]) return false;
    }
    return true;
}

uint32_t Msm8255ModemPeer::QmuxControlPort(uint32_t rec) {
    for (uint32_t port = 0; port < kMsm8255QmuxPortCount; ++port) {
        const char* name = kMsm8255QmuxControlPorts[port];
        if (ChannelNameIs(rec, name, static_cast<uint32_t>(std::strlen(name)) + 1u)) {
            return port;
        }
    }
    return kMsm8255QmuxPortCount;
}

void Msm8255ModemPeer::HaltUnroutedSmdChannel(uint32_t cid, uint32_t rec) {
    auto& mem = emu_.Get<EmulatedMemory>();

    char name[kAllocElmNameBytes + 1] = {};
    for (uint32_t i = 0; i < kAllocElmNameBytes; ++i) {
        name[i] = static_cast<char>(mem.ReadByte(rec + i));
    }

    emu_.Get<Fatal>().Die(
        "msm8255 modem peer: smd channel %u \"%s\" carries data, and only the "
        "rpc router channel %u, the \"%s\" ports and the qmux control ports "
        "are modeled", cid, name, kRpcRouterCid, kDalPortName);
}

void Msm8255ModemPeer::ServiceSmdData(uint32_t cid, uint32_t rec,
                                      uint32_t apps_half_pa) {
    const bool to_router = cid == kRpcRouterCid;
    const bool to_dal    = !to_router &&
                        ChannelNameIs(rec, kDalPortName,
                                      sizeof(kDalPortName) - 1u);
    const uint32_t qmux_port =
        to_router || to_dal ? kMsm8255QmuxPortCount : QmuxControlPort(rec);
    const bool to_qmux   = qmux_port != kMsm8255QmuxPortCount;
    if (!to_router && !to_dal && !to_qmux) HaltUnroutedSmdChannel(cid, rec);

    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t kind =
        mem.ReadWord(rec + kAllocElmCtypeOff) & kSmdKindMask;
    const bool kind_matches =
        to_qmux ? kind == kSmdKindPacket
                : kind == kSmdKindStream || kind == kSmdKindUnknown;
    if (!kind_matches) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd channel %u carries kind 0x%03X, and its "
            "route reads %s", cid, kind, to_qmux ? "packets" : "a stream");
    }
    uint32_t fifo_pa = 0u;
    uint32_t half    = 0u;
    ResolveFifo(cid, fifo_pa, half);
    uint32_t head = 0u;
    uint32_t tail = 0u;
    ReadHalfIndices(apps_half_pa, half, head, tail);

    const uint32_t modem_half = apps_half_pa + kHalfChannelBytes;
    uint32_t out_head = 0u;
    uint32_t out_tail = 0u;
    ReadHalfIndices(modem_half, half, out_head, out_tail);
    uint32_t cursor   = tail;
    uint32_t avail    = (head + half - tail) % half;
    uint32_t produced = 0u;
    while (avail != 0u) {
        const uint32_t run = half - cursor;
        uint32_t in_pa     = fifo_pa + cursor;
        uint32_t in_avail  = avail;
        if (avail > run) {
            auto& stage = emu_.Get<Msm8255SmdStage>();
            in_avail = stage.Linearize(fifo_pa, half, cursor, avail);
            in_pa    = stage.BasePa();
        }
        auto& out_stage = emu_.Get<Msm8255SmdStage>();
        const uint32_t out_pos = out_head % half;
        const uint32_t out_cap = SmdWriteAvail(half, out_pos, out_tail);
        uint32_t consumed = 0u;
        uint32_t sent     = 0u;
        if (to_router) {
            sent = emu_.Get<Msm8255RpcRouterPeer>().Answer(
                in_pa, in_avail, out_stage.WriteBasePa(), out_cap, consumed);
        } else if (to_dal) {
            sent = emu_.Get<Msm8255DalRemoteServer>().Answer(
                in_pa, in_avail, out_stage.WriteBasePa(), out_cap, consumed);
        } else {
            sent = emu_.Get<Msm8255SmdPacketChannel>().Answer(
                qmux_port, in_pa, in_avail, half, out_stage.WriteBasePa(), out_cap,
                consumed);
        }
        if (consumed == 0u) break;
        if (sent != 0u) {
            out_stage.Scatter(fifo_pa + half, half, out_pos, sent);
        }
        cursor    = (cursor + consumed) % half;
        avail    -= consumed;
        out_head += sent;
        produced += sent;
    }

    if (cursor != tail) {
        mem.WriteWord(apps_half_pa + kHcTailOff, cursor);
        mem.WriteByte(modem_half + kHcFTailOff, 1u);
    }
    if (produced != 0u) {
        PublishModemHead(modem_half, out_head % half);
    } else if (cursor != tail) {
        irq_->PulseIrq(kVicM2aSmd);
    }
}

void Msm8255ModemPeer::ResolveFifo(uint32_t cid, uint32_t& fifo_pa,
                                   uint32_t& half) {
    uint32_t fifo_bytes = 0u;
    if (!emu_.Get<Msm8255Smem>().ItemPaAndSize(kIdSmdFifoBase + cid, fifo_pa,
                                               fifo_bytes)) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd channel %u carries data and its fifo smem "
            "item %u is not allocated", cid, kIdSmdFifoBase + cid);
    }
    half = fifo_bytes / 2u;
    if ((fifo_bytes & 1u) != 0u || (half & kFifoWindowAlign) != 0u ||
        half < kFifoWindowMin || half > kFifoWindowMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd fifo smem item %u is %u bytes, so each "
            "direction gets %u, which the channel binding rejects",
            kIdSmdFifoBase + cid, fifo_bytes, half);
    }
}

void Msm8255ModemPeer::ReadHalfIndices(uint32_t half_pa, uint32_t half,
                                       uint32_t& head, uint32_t& tail) {
    auto& mem = emu_.Get<EmulatedMemory>();
    head = mem.ReadWord(half_pa + kHcHeadOff);
    tail = mem.ReadWord(half_pa + kHcTailOff);
    if (head >= half || tail >= half) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd fifo indices (head=%u tail=%u) of the half "
            "channel at 0x%08X leave the %u-byte half the channel binding gives "
            "each direction", head, tail, half_pa, half);
    }
}

void Msm8255ModemPeer::PublishModemHead(uint32_t modem_half_pa, uint32_t head) {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteWord(modem_half_pa + kHcHeadOff, head);
    mem.WriteByte(modem_half_pa + kHcFHeadOff, 1u);
    irq_->PulseIrq(kVicM2aSmd);
}

uint32_t Msm8255ModemPeer::ChannelRecordPa(uint32_t cid) {
    auto&          mem = emu_.Get<EmulatedMemory>();
    const uint32_t tbl =
        emu_.Get<Msm8255Smem>().ItemPa(kIdChannelAllocTbl, kAllocElmBytes * kSmdChannels);
    for (uint32_t n = 0; tbl != 0u && n < kSmdChannels; ++n) {
        const uint32_t rec = tbl + kAllocElmBytes * n;
        if (mem.ReadWord(rec + kAllocElmRefOff) != 0u && mem.ReadWord(rec + kAllocElmCidOff) == cid) {
            return rec;
        }
    }
    emu_.Get<Fatal>().Die(
        "msm8255 modem peer: smd channel cid %u carries a modem message and has no allocation "
        "record", cid);
}

void Msm8255ModemPeer::PushRouterMessage(
    uint32_t prog, uint32_t proc,
    const std::function<uint32_t(uint32_t out_pa, uint32_t out_cap)>& produce) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t item =
        emu_.Get<Msm8255Smem>().ItemPa(kIdSmdBase + kRpcRouterCid, kSmdSharedBytes);
    if (item == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: a router message is due before the guest "
            "allocated smem item %u", kIdSmdBase + kRpcRouterCid);
    }
    const uint32_t modem_half = item + kHalfChannelBytes;
    if (mem.ReadWord(item) != kSmdSsOpened ||
        mem.ReadWord(modem_half) != kSmdSsOpened) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: a router message is due while smd channel %u "
            "is not open on both halves", kRpcRouterCid);
    }

    uint32_t fifo_pa = 0u;
    uint32_t half    = 0u;
    ResolveFifo(kRpcRouterCid, fifo_pa, half);
    uint32_t out_head = 0u;
    uint32_t out_tail = 0u;
    ReadHalfIndices(modem_half, half, out_head, out_tail);

    auto& stage = emu_.Get<Msm8255SmdStage>();
    const uint32_t sent =
        produce(stage.WriteBasePa(), SmdWriteAvail(half, out_head, out_tail));
    stage.Scatter(fifo_pa + half, half, out_head, sent);
    Msm8255DemMaster::PortName port{};
    const uint32_t rec = ChannelRecordPa(kRpcRouterCid);
    for (uint32_t i = 0; i < port.size(); ++i) port[i] = mem.ReadByte(rec + i);
    emu_.Get<Msm8255DemMaster>().WakeForRpc(prog, proc, port);
    PublishModemHead(modem_half, (out_head + sent) % half);
}

void Msm8255ModemPeer::OpenModemSmdHalf(uint32_t modem_half_pa) {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteByte(modem_half_pa + kHcFDsrOff, 1u);
    mem.WriteByte(modem_half_pa + kHcFCtsOff, 1u);
    mem.WriteByte(modem_half_pa + kHcFCdOff, 1u);
    mem.WriteWord(modem_half_pa, kSmdSsOpened);
    mem.WriteByte(modem_half_pa + kHcFStateOff, 1u);
    irq_->PulseIrq(kVicM2aSmd);
}

void Msm8255ModemPeer::RunProcComm() {
    auto& smem = emu_.Get<Msm8255Smem>();
    auto& mem  = emu_.Get<EmulatedMemory>();
    const uint32_t cmd = mem.ReadWord(smem.SmemPa() + kAppCommandOff);
    if (cmd == kPcomPowerDown) {
        emu_.Get<Msm8255SystemPower>().PowerDown();
        return;
    }
    emu_.Get<Fatal>().Die(
        "msm8255 modem peer: proc_comm command %u is not modeled", cmd);
}

REGISTER_SERVICE(Msm8255ModemPeer);

}
