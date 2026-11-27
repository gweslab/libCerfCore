#include "msm8255_qmi_service.h"
#include "msm8255_qmi_tlv.h"
#include "msm8255_qmux_peer.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint8_t  kQmiServiceNas                = 0x03u;
constexpr uint16_t kMessageInitiateAttach        = 0x0023u;
constexpr uint16_t kMessageGetServingSystem      = 0x0024u;
constexpr uint8_t  kTlvServingSystem             = 0x01u;
constexpr uint8_t  kTlvPsAttachAction            = 0x10u;
constexpr uint16_t kPsAttachActionBytes          = 1u;
constexpr uint8_t  kPsAttachActionAttach         = 0x01u;
constexpr uint8_t  kPsAttachActionDetach         = 0x02u;
constexpr uint8_t  kRegistrationNotRegistered    = 0x00u;
constexpr uint8_t  kAttachDetached               = 0x02u;
constexpr uint8_t  kNetworkTypeUnknown           = 0x00u;
constexpr uint8_t  kRadioInterfaceNone           = 0x00u;
constexpr uint16_t kErrorNoNetworkFound          = 13u;
constexpr uint16_t kErrorNoEffect                = 26u;

class Msm8255QmiNasService : public Msm8255QmiService {
public:
    using Msm8255QmiService::Msm8255QmiService;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255QmuxPeer>().RegisterService(this); }

    uint8_t QmiService() const override { return kQmiServiceNas; }

    Msm8255QmiAnswer Answer(uint32_t port, uint8_t client, uint16_t message,
                            const std::vector<uint8_t>& tlvs) override {
        if (message == kMessageGetServingSystem && tlvs.empty()) return ServingSystem(port, client);
        if (message == kMessageInitiateAttach) return InitiateAttach(port, client, tlvs);
        emu_.Get<Fatal>().Die(
            "msm8255 qmi nas service: message 0x%04X with %zu tlv bytes is not modeled",
            message, tlvs.size());
    }

    void SaveState(StateWriter&) override {}
    void RestoreState(StateReader&) override {}

private:
    static Msm8255QmiAnswer ServingSystem(uint32_t port, uint8_t client) {
        const std::vector<uint8_t> serving_system = {
            kRegistrationNotRegistered,
            kAttachDetached,
            kAttachDetached,
            kNetworkTypeUnknown,
            1u,
            kRadioInterfaceNone,
        };
        Msm8255QmiAnswer answer;
        AppendMsm8255QmiTlv(answer.tlvs, kTlvServingSystem, serving_system);
        LOG(SocModem, "msm8255 qmi nas service: port %u client %u get serving system, not registered\n",
            port, client);
        return answer;
    }

    Msm8255QmiAnswer InitiateAttach(uint32_t port, uint8_t client, const std::vector<uint8_t>& tlvs) {
        Msm8255QmiTlv action;
        const uint32_t size = static_cast<uint32_t>(tlvs.size());
        const bool framed = ReadMsm8255QmiTlv(tlvs.data(), size, 0u, action) &&
                            action.type == kTlvPsAttachAction && action.length == kPsAttachActionBytes &&
                            action.value + action.length == size;
        const uint8_t value = framed ? tlvs[action.value] : 0u;
        if (!framed || (value != kPsAttachActionAttach && value != kPsAttachActionDetach)) {
            emu_.Get<Fatal>().Die(
                "msm8255 qmi nas service: an initiate attach with %u tlv bytes is not modeled", size);
        }
        Msm8255QmiAnswer answer;
        answer.error = value == kPsAttachActionDetach ? kErrorNoEffect : kErrorNoNetworkFound;
        LOG(SocModem, "msm8255 qmi nas service: port %u client %u ps %s answered with error %u\n", port,
            client, value == kPsAttachActionDetach ? "detach" : "attach", answer.error);
        return answer;
    }
};

}

REGISTER_SERVICE(Msm8255QmiNasService);

}
