#include "msm8255_qmux_peer.h"

#include "msm8255_qmi_service.h"
#include "msm8255_qmi_tlv.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint8_t kQmuxMarker = 0x01u;

constexpr uint32_t kOffQmuxLength  = 1u;
constexpr uint32_t kOffQmuxFlags   = 3u;
constexpr uint32_t kOffQmuxService = 4u;
constexpr uint32_t kOffQmuxClient  = 5u;

constexpr uint8_t kQmuxFlagsFromClient  = 0x00u;
constexpr uint8_t kQmuxFlagsFromService = 0x80u;

constexpr uint8_t kServiceCtl = 0x00u;

constexpr uint32_t kOffCtlFlags     = 6u;
constexpr uint32_t kOffCtlTxn       = 7u;
constexpr uint32_t kOffCtlMessage   = 8u;
constexpr uint32_t kOffCtlTlvLength = 10u;
constexpr uint32_t kCtlFrameBytes   = 12u;

constexpr uint32_t kQmuxHeaderBytes = 6u;

constexpr uint32_t kOffServiceFlags     = 6u;
constexpr uint32_t kOffServiceTxn       = 7u;
constexpr uint32_t kOffServiceMessage   = 9u;
constexpr uint32_t kOffServiceTlvLength = 11u;
constexpr uint32_t kServiceFrameBytes   = 13u;

constexpr uint8_t kServiceFlagsRequest  = 0x00u;
constexpr uint8_t kServiceFlagsResponse = 0x02u;

constexpr uint8_t kCtlFlagsRequest  = 0x00u;
constexpr uint8_t kCtlFlagsResponse = 0x01u;

constexpr uint16_t kCtlMessageAllocateCid        = 0x0022u;
constexpr uint16_t kCtlMessageSetDataFormat      = 0x0026u;
constexpr uint16_t kCtlMessageSync               = 0x0027u;
constexpr uint16_t kCtlMessageSetPowerSaveConfig = 0x0029u;

constexpr uint8_t  kTlvDataFormat        = 0x01u;
constexpr uint8_t  kTlvLinkProtocol      = 0x10u;
constexpr uint8_t  kDataFormatQosAbsent  = 0u;
constexpr uint8_t  kDataFormatQosPresent = 1u;
constexpr uint16_t kLinkProtocol8023     = 1u;
constexpr uint16_t kLinkProtocolRawIp    = 2u;

constexpr uint8_t  kTlvPowerSaveState      = 0x01u;
constexpr uint16_t kPowerSaveStateTlvBytes = 5u;
constexpr uint32_t kPowerSaveSuspend       = 1u;
constexpr uint32_t kPowerSavePowerdown     = 2u;

constexpr uint8_t  kTlvResult         = 0x02u;
constexpr uint8_t  kTlvService        = 0x01u;
constexpr uint8_t  kTlvAllocationInfo = 0x01u;
constexpr uint16_t kResultSuccess     = 0x0000u;
constexpr uint16_t kResultFailure     = 0x0001u;
constexpr uint16_t kErrorNone         = 0x0000u;

constexpr uint16_t kServiceTlvBytes       = 1u;
constexpr uint16_t kDataFormatTlvBytes    = 1u;
constexpr uint16_t kLinkProtocolTlvBytes  = 2u;
constexpr uint32_t kAllocateCidRequestTlv = kMsm8255QmiTlvHeaderBytes + kServiceTlvBytes;
constexpr uint32_t kDataFormatRequestTlv  =
    2u * kMsm8255QmiTlvHeaderBytes + kDataFormatTlvBytes + kLinkProtocolTlvBytes;
constexpr uint32_t kPowerSaveRequestTlv   = kMsm8255QmiTlvHeaderBytes + kPowerSaveStateTlvBytes;

void AppendMessage(std::vector<uint8_t>& sdu, uint16_t message,
                   const std::vector<uint8_t>& tlvs) {
    cerf::le::Append16(sdu, message);
    cerf::le::Append16(sdu, static_cast<uint16_t>(tlvs.size()));
    sdu.insert(sdu.end(), tlvs.begin(), tlvs.end());
}

std::vector<uint8_t> ResultTlv(uint16_t error) {
    std::vector<uint8_t> value;
    cerf::le::Append16(value, error == kErrorNone ? kResultSuccess : kResultFailure);
    cerf::le::Append16(value, error);
    std::vector<uint8_t> tlvs;
    AppendMsm8255QmiTlv(tlvs, kTlvResult, value);
    return tlvs;
}

static_assert(StateVisitCoversAllBytes<Msm8255QmuxPort>(
                  [](Msm8255QmuxPort& p, StateFieldBytes& f) { Msm8255QmuxPort::Visit(p, f); }),
              "Msm8255QmuxPort::Visit must name or skip every field of the port");

}  // namespace

REGISTER_SERVICE(Msm8255QmuxPeer);

bool Msm8255QmuxPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255QmuxPeer::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        clients_.Clear();
        for (Msm8255QmuxPort& p : ports_) p = Msm8255QmuxPort{};
    });
}

uint32_t Msm8255QmuxPeer::Answer(uint32_t port, uint32_t in_pa, uint32_t in_bytes,
                                 uint32_t out_pa, uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();

    if (in_bytes < kCtlFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: a %u-byte packet is short of the %u-byte "
            "control frame", in_bytes, kCtlFrameBytes);
    }

    uint8_t frame[kCtlFrameBytes] = {};
    mem.CopyOut(in_pa, frame, kCtlFrameBytes);

    if (frame[0] != kQmuxMarker) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: interface marker 0x%02X is not modeled",
            frame[0]);
    }
    const uint32_t qmux_len = cerf::le::U16(frame, kOffQmuxLength);
    if (qmux_len != in_bytes - 1u) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: the qmux header declares %u bytes in a %u-byte "
            "packet", qmux_len, in_bytes);
    }
    if (frame[kOffQmuxFlags] != kQmuxFlagsFromClient) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmux flags 0x%02X are not modeled",
            frame[kOffQmuxFlags]);
    }
    const uint8_t service = frame[kOffQmuxService];
    const uint8_t client  = frame[kOffQmuxClient];
    if (service != kServiceCtl) {
        return AnswerService(port, service, client, in_pa, in_bytes, out_pa, out_cap);
    }

    const uint8_t  ctl_flags = frame[kOffCtlFlags];
    const uint8_t  txn       = frame[kOffCtlTxn];
    const uint16_t message   = cerf::le::U16(frame, kOffCtlMessage);
    const uint32_t tlv_len   = cerf::le::U16(frame, kOffCtlTlvLength);
    if (ctl_flags != kCtlFlagsRequest) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl flags 0x%02X on message 0x%04X are not "
            "modeled", ctl_flags, message);
    }
    if (tlv_len != in_bytes - kCtlFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl message 0x%04X declares %u tlv bytes in a "
            "%u-byte packet", message, tlv_len, in_bytes);
    }
    std::vector<uint8_t> tlvs = ResultTlv(kErrorNone);
    if (message == kCtlMessageAllocateCid) {
        const std::vector<uint8_t> info = AllocateClient(port, in_pa + kCtlFrameBytes, tlv_len);
        tlvs.insert(tlvs.end(), info.begin(), info.end());
    } else if (message == kCtlMessageSetDataFormat) {
        const uint16_t protocol = SetDataFormat(port, in_pa + kCtlFrameBytes, tlv_len);
        std::vector<uint8_t> value;
        cerf::le::Append16(value, protocol);
        AppendMsm8255QmiTlv(tlvs, kTlvLinkProtocol, value);
    } else if (message == kCtlMessageSetPowerSaveConfig) {
        SetPowerSaveConfig(port, in_pa + kCtlFrameBytes, tlv_len);
    } else if (message != kCtlMessageSync || tlv_len != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl message 0x%04X with %u tlv bytes is not "
            "modeled", message, tlv_len);
    }
    std::vector<uint8_t> sdu;
    sdu.push_back(kCtlFlagsResponse);
    sdu.push_back(txn);
    AppendMessage(sdu, message, tlvs);
    return WriteFrame(out_pa, out_cap, kServiceCtl, client, sdu);
}

void Msm8255QmuxPeer::RegisterService(Msm8255QmiService* service) {
    const auto at = std::find_if(services_.begin(), services_.end(), [service](auto* s) {
        return s->QmiService() >= service->QmiService();
    });
    services_.insert(at, service);
}

uint32_t Msm8255QmuxPeer::AnswerService(uint32_t port, uint8_t service, uint8_t client,
                                        uint32_t in_pa, uint32_t in_bytes, uint32_t out_pa,
                                        uint32_t out_cap) {
    uint8_t frame[kServiceFrameBytes] = {};
    if (in_bytes < kServiceFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: a %u-byte qmi service %u packet is short of the %u-byte service "
            "frame", in_bytes, service, kServiceFrameBytes);
    }
    emu_.Get<EmulatedMemory>().CopyOut(in_pa, frame, kServiceFrameBytes);
    const uint16_t txn     = cerf::le::U16(frame, kOffServiceTxn);
    const uint16_t message = cerf::le::U16(frame, kOffServiceMessage);
    const uint32_t tlv_len = cerf::le::U16(frame, kOffServiceTlvLength);
    if (frame[kOffServiceFlags] != kServiceFlagsRequest ||
        tlv_len != in_bytes - kServiceFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmi service %u message 0x%04X with flags 0x%02X and %u tlv bytes "
            "in a %u-byte packet is not modeled", service, message, frame[kOffServiceFlags],
            tlv_len, in_bytes);
    }
    const Msm8255QmiClient* c = clients_.Find(client);
    if (!c || c->port != port || c->service != service) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmi service %u message 0x%04X names client %u on port %u, which "
            "this port never issued for that service", service, message, client, port);
    }
    const auto it = std::find_if(services_.begin(), services_.end(),
                                 [service](auto* s) { return s->QmiService() == service; });
    if (it == services_.end()) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmi service %u (client %u) message 0x%04X with %u tlv bytes is "
            "not modeled", service, client, message, tlv_len);
    }
    std::vector<uint8_t> request(tlv_len);
    emu_.Get<EmulatedMemory>().CopyOut(in_pa + kServiceFrameBytes, request.data(), tlv_len);
    const Msm8255QmiAnswer answer = (*it)->Answer(port, client, message, request);
    std::vector<uint8_t> tlvs = ResultTlv(answer.error);
    tlvs.insert(tlvs.end(), answer.tlvs.begin(), answer.tlvs.end());

    std::vector<uint8_t> sdu;
    sdu.push_back(kServiceFlagsResponse);
    cerf::le::Append16(sdu, txn);
    AppendMessage(sdu, message, tlvs);
    return WriteFrame(out_pa, out_cap, service, client, sdu);
}

std::vector<uint8_t> Msm8255QmuxPeer::AllocateClient(uint32_t port, uint32_t in_pa,
                                                     uint32_t tlv_len) {
    uint8_t tlv[kAllocateCidRequestTlv] = {};
    if (tlv_len != kAllocateCidRequestTlv) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: allocate cid with %u tlv bytes is not modeled", tlv_len);
    }
    emu_.Get<EmulatedMemory>().CopyOut(in_pa, tlv, kAllocateCidRequestTlv);
    Msm8255QmiTlv request;
    if (!ReadMsm8255QmiTlv(tlv, kAllocateCidRequestTlv, 0u, request) || request.type != kTlvService ||
        request.length != kServiceTlvBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: allocate cid with tlv 0x%02X of %u bytes is not modeled",
            request.type, request.length);
    }
    const uint8_t service = tlv[request.value];
    Msm8255QmiClient* c = clients_.Issue();
    if (!c) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: allocate cid for qmi service %u after %u issued clients",
            service, clients_.LastId());
    }
    c->port    = port;
    c->service = service;
    LOG(SocModem, "msm8255 qmux peer: port %u client %u for qmi service %u\n", port, c->id,
        service);
    std::vector<uint8_t> info;
    AppendMsm8255QmiTlv(info, kTlvAllocationInfo, {service, static_cast<uint8_t>(c->id)});
    return info;
}

uint16_t Msm8255QmuxPeer::SetDataFormat(uint32_t port, uint32_t in_pa, uint32_t tlv_len) {
    uint8_t tlv[kDataFormatRequestTlv] = {};
    if (tlv_len != kDataFormatRequestTlv) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: set data format with %u tlv bytes is not modeled", tlv_len);
    }
    emu_.Get<EmulatedMemory>().CopyOut(in_pa, tlv, kDataFormatRequestTlv);
    Msm8255QmiTlv format, link;
    const bool framed =
        ReadMsm8255QmiTlv(tlv, kDataFormatRequestTlv, 0u, format) && format.type == kTlvDataFormat &&
        format.length == kDataFormatTlvBytes &&
        ReadMsm8255QmiTlv(tlv, kDataFormatRequestTlv, format.value + format.length, link) &&
        link.type == kTlvLinkProtocol && link.length == kLinkProtocolTlvBytes;
    const uint8_t  qos      = framed ? tlv[format.value] : 0u;
    const uint16_t protocol = framed ? cerf::le::U16(tlv, link.value) : 0u;
    if (!framed || (qos != kDataFormatQosAbsent && qos != kDataFormatQosPresent) ||
        (protocol != kLinkProtocol8023 && protocol != kLinkProtocolRawIp)) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: set data format with tlv 0x%02X qos %u, tlv 0x%02X protocol %u "
            "is not modeled", format.type, qos, link.type, protocol);
    }
    Msm8255QmuxPort& p = ports_[port];
    p.data_format_set  = 1u;
    p.qos              = qos;
    p.link_protocol    = protocol;
    LOG(SocModem, "msm8255 qmux peer: port %u data format qos %u, link protocol %u\n", port, qos,
        protocol);
    return protocol;
}

void Msm8255QmuxPeer::SetPowerSaveConfig(uint32_t port, uint32_t in_pa, uint32_t tlv_len) {
    uint8_t tlv[kPowerSaveRequestTlv] = {};
    if (tlv_len != kPowerSaveRequestTlv) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: set power save config with %u tlv bytes is not modeled", tlv_len);
    }
    emu_.Get<EmulatedMemory>().CopyOut(in_pa, tlv, kPowerSaveRequestTlv);
    Msm8255QmiTlv request;
    const bool framed = ReadMsm8255QmiTlv(tlv, kPowerSaveRequestTlv, 0u, request) &&
                        request.type == kTlvPowerSaveState &&
                        request.length == kPowerSaveStateTlvBytes;
    const uint32_t state   = framed ? cerf::le::U32(tlv, request.value) : 0u;
    const uint8_t  service = framed ? tlv[request.value + 4u] : 0u;
    if (!framed || (state != kPowerSaveSuspend && state != kPowerSavePowerdown)) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: set power save config with tlv 0x%02X state %u for qmi service "
            "%u is not modeled", request.type, state, service);
    }
    ports_[port].power_save_services[(state - kPowerSaveSuspend) * kQmiServiceWords +
                                     service / 32u] |= 1u << (service % 32u);
}

uint32_t Msm8255QmuxPeer::WriteFrame(uint32_t out_pa, uint32_t out_cap, uint8_t service,
                                     uint8_t client, const std::vector<uint8_t>& sdu) {
    const uint32_t bytes = kQmuxHeaderBytes + static_cast<uint32_t>(sdu.size());
    if (out_cap < bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: the reply needs %u bytes and the window at "
            "0x%08X has %u", bytes, out_pa, out_cap);
    }
    std::vector<uint8_t> reply;
    reply.reserve(bytes);
    reply.push_back(kQmuxMarker);
    cerf::le::Append16(reply, static_cast<uint16_t>(bytes - 1u));
    reply.push_back(kQmuxFlagsFromService);
    reply.push_back(service);
    reply.push_back(client);
    reply.insert(reply.end(), sdu.begin(), sdu.end());
    emu_.Get<EmulatedMemory>().CopyIn(out_pa, reply.data(), reply.size());
    return bytes;
}

void Msm8255QmuxPeer::SaveState(StateWriter& w) {
    clients_.Save(w, "qmi");
    StateWriteField field(w);
    for (Msm8255QmuxPort& p : ports_) Msm8255QmuxPort::Visit(p, field);
    for (Msm8255QmiService* s : services_) s->SaveState(w);
}

void Msm8255QmuxPeer::RestoreState(StateReader& r) {
    clients_.Restore(r, "qmi");
    StateReadField field(r);
    for (Msm8255QmuxPort& p : ports_) Msm8255QmuxPort::Visit(p, field);
    for (Msm8255QmiService* s : services_) s->RestoreState(r);
}

}
