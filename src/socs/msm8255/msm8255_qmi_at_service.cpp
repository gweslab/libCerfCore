#include "msm8255_qmi_service.h"
#include "msm8255_qmi_tlv.h"
#include "msm8255_qmux_peer.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint8_t  kQmiServiceAt                     = 0x08u;
constexpr uint16_t kMessageRegisterAtCommandForward  = 0x0020u;
constexpr uint8_t  kTlvCommandList                   = 0x01u;
constexpr uint8_t  kTlvCommand                       = 0x01u;
constexpr uint32_t kCommandHeadBytes                 = 2u;
constexpr uint32_t kCommandNameMax                   = 255u;

struct QmiAtCommand {
    uint32_t port       = 0u;
    uint32_t client     = 0u;
    uint32_t entry_byte = 0u;
    uint32_t name_bytes = 0u;
    uint8_t  name[kCommandNameMax + 1u] = {};

    template <typename F>
    static constexpr void Visit(QmiAtCommand& c, F& field) {
        field("qmi_at_command_port", c.port);
        field("qmi_at_command_client", c.client);
        field("qmi_at_command_entry_byte", c.entry_byte);
        field("qmi_at_command_name_bytes", c.name_bytes);
        field("qmi_at_command_name", c.name);
    }
};

static_assert(StateVisitCoversAllBytes<QmiAtCommand>(
                  [](QmiAtCommand& c, StateFieldBytes& f) { QmiAtCommand::Visit(c, f); }),
              "QmiAtCommand::Visit must name or skip every field of the command");

class Msm8255QmiAtService : public Msm8255QmiService {
public:
    using Msm8255QmiService::Msm8255QmiService;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255QmuxPeer>().RegisterService(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            commands_.clear();
        });
    }

    uint8_t QmiService() const override { return kQmiServiceAt; }

    Msm8255QmiAnswer Answer(uint32_t port, uint8_t client, uint16_t message,
                            const std::vector<uint8_t>& tlvs) override {
        if (message != kMessageRegisterAtCommandForward) {
            emu_.Get<Fatal>().Die(
                "msm8255 qmi at service: message 0x%04X with %zu tlv bytes is not modeled",
                message, tlvs.size());
        }
        const uint32_t size = static_cast<uint32_t>(tlvs.size());
        Msm8255QmiTlv list;
        if (!ReadMsm8255QmiTlv(tlvs.data(), size, 0u, list) || list.type != kTlvCommandList ||
            list.value + list.length != size) {
            emu_.Get<Fatal>().Die(
                "msm8255 qmi at service: a command registration with %zu tlv bytes is not "
                "modeled", tlvs.size());
        }
        uint32_t off = list.value;
        while (off < size) RegisterCommand(port, client, tlvs, off);
        return {};
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("qmi_at_command_count", static_cast<uint32_t>(commands_.size()));
        StateWriteField field(w);
        for (QmiAtCommand& c : commands_) QmiAtCommand::Visit(c, field);
    }

    void RestoreState(StateReader& r) override {
        uint32_t count = 0u;
        r.Read("qmi_at_command_count", count);
        commands_.assign(count, QmiAtCommand{});
        StateReadField field(r);
        for (QmiAtCommand& c : commands_) QmiAtCommand::Visit(c, field);
    }

private:
    void RegisterCommand(uint32_t port, uint8_t client, const std::vector<uint8_t>& tlvs,
                         uint32_t& off) {
        Msm8255QmiTlv entry;
        const bool framed = ReadMsm8255QmiTlv(tlvs.data(), static_cast<uint32_t>(tlvs.size()), off, entry) &&
                            entry.type == kTlvCommand && entry.length >= kCommandHeadBytes;
        const uint32_t name_bytes = framed ? tlvs[entry.value + 1u] : 0u;
        if (!framed || name_bytes == 0u || entry.length != kCommandHeadBytes + name_bytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 qmi at service: a command entry of %u bytes at %u in the registration "
                "is not modeled", entry.length, off);
        }
        QmiAtCommand c;
        c.port       = port;
        c.client     = client;
        c.entry_byte = tlvs[entry.value];
        c.name_bytes = name_bytes;
        std::memcpy(c.name, tlvs.data() + entry.value + kCommandHeadBytes, name_bytes);
        const bool known =
            std::any_of(commands_.begin(), commands_.end(), [&c](const QmiAtCommand& e) {
                return e.port == c.port && e.client == c.client &&
                       e.name_bytes == c.name_bytes &&
                       std::memcmp(e.name, c.name, c.name_bytes) == 0;
            });
        if (known) {
            emu_.Get<Fatal>().Die(
                "msm8255 qmi at service: port %u client %u registers %.*s a second time", port,
                client, static_cast<int>(name_bytes), reinterpret_cast<const char*>(c.name));
        }
        LOG(SocModem, "msm8255 qmi at service: port %u client %u registers %.*s, entry byte %u\n",
            port, client, static_cast<int>(name_bytes), reinterpret_cast<const char*>(c.name),
            c.entry_byte);
        commands_.push_back(c);
        off = entry.value + entry.length;
    }

    std::vector<QmiAtCommand> commands_;
};

}

REGISTER_SERVICE(Msm8255QmiAtService);

}
