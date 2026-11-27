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

constexpr uint8_t  kQmiServiceWds                  = 0x01u;
constexpr uint16_t kMessageGetProfileList          = 0x002Au;
constexpr uint16_t kMessageGetActiveMipProfile     = 0x003Cu;
constexpr uint16_t kMessageSetActiveMipProfile     = 0x003Du;
constexpr uint16_t kMessageGetMipProfile           = 0x003Eu;
constexpr uint16_t kMessageGetDefaultProfileNumber = 0x0049u;
constexpr uint16_t kErrorInvalidQmiCommand         = 71u;
constexpr uint8_t  kTlvProfileList                 = 0x01u;
constexpr uint8_t  kTlvProfileType                 = 0x01u;
constexpr uint8_t  kTlvDefaultIndex                = 0x01u;
constexpr uint16_t kProfileTypeTlvBytes            = 2u;
constexpr uint8_t  kProfileType3gpp                = 0u;
constexpr uint8_t  kProfileFamilyEmbedded          = 0u;
constexpr uint8_t  kProfileFamilyTethered          = 1u;
constexpr uint8_t  kProfileFamilyGuestDefault      = 3u;
constexpr uint8_t  kStoredProfileCount             = 0u;
constexpr uint8_t  kDefaultProfileIndex            = 1u;

class Msm8255QmiWdsService : public Msm8255QmiService {
public:
    using Msm8255QmiService::Msm8255QmiService;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255QmuxPeer>().RegisterService(this); }

    uint8_t QmiService() const override { return kQmiServiceWds; }

    Msm8255QmiAnswer Answer(uint32_t port, uint8_t client, uint16_t message,
                            const std::vector<uint8_t>& tlvs) override {
        Msm8255QmiAnswer answer;
        if (message == kMessageGetProfileList && tlvs.empty()) {
            AppendMsm8255QmiTlv(answer.tlvs, kTlvProfileList, {kStoredProfileCount});
            LOG(SocModem, "msm8255 qmi wds service: port %u client %u get profile list, %u profiles\n",
                port, client, kStoredProfileCount);
            return answer;
        }
        if (message == kMessageGetActiveMipProfile || message == kMessageSetActiveMipProfile ||
            message == kMessageGetMipProfile) {
            answer.error = kErrorInvalidQmiCommand;
            LOG(SocModem, "msm8255 qmi wds service: port %u client %u mobile IP message 0x%04X answered with error %u\n",
                port, client, message, answer.error);
            return answer;
        }
        if (message == kMessageGetDefaultProfileNumber) {
            Msm8255QmiTlv request;
            const uint32_t size = static_cast<uint32_t>(tlvs.size());
            if (ReadMsm8255QmiTlv(tlvs.data(), size, 0u, request) && request.type == kTlvProfileType &&
                request.length == kProfileTypeTlvBytes && request.value + request.length == size &&
                tlvs[request.value] == kProfileType3gpp && KnownFamily(tlvs[request.value + 1u])) {
                AppendMsm8255QmiTlv(answer.tlvs, kTlvDefaultIndex, {kDefaultProfileIndex});
                LOG(SocModem,
                    "msm8255 qmi wds service: port %u client %u default profile of family %u is %u\n",
                    port, client, tlvs[request.value + 1u], kDefaultProfileIndex);
                return answer;
            }
        }
        emu_.Get<Fatal>().Die(
            "msm8255 qmi wds service: message 0x%04X with %zu tlv bytes is not modeled",
            message, tlvs.size());
    }

    void SaveState(StateWriter&) override {}
    void RestoreState(StateReader&) override {}

private:
    static bool KnownFamily(uint8_t family) {
        return family == kProfileFamilyEmbedded || family == kProfileFamilyTethered ||
               family == kProfileFamilyGuestDefault;
    }
};

}

REGISTER_SERVICE(Msm8255QmiWdsService);

}
