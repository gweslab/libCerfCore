#include <cerfcore/core/service.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "../../socs/uart_endpoint.h"
#include "../../socs/imx6/imx6_uart2.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "siemens_simatic_mobile_panels_gen2_conn_box_identity.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2ConnBox : public Service, public UartEndpoint {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }
    void OnReady() override {
        emu_.Get<Imx6Uart2>().AttachEndpoint(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            ResetParser();
        });
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>("prev_dle", prev_dle_ ? 1u : 0u);
        w.Write<uint8_t>("in_frame", in_frame_ ? 1u : 0u);
        w.Write<uint32_t>("rx_payload_count", static_cast<uint32_t>(rx_payload_.size()));
        w.WriteBytes("rx_payload", rx_payload_.data(), rx_payload_.size());
        w.WriteBytes("word_a", word_a_.data(), word_a_.size());
        w.WriteBytes("word_b", word_b_.data(), word_b_.size());
    }

    void RestoreState(StateReader& r) override {
        uint8_t prev_dle = 0;
        uint8_t in_frame = 0;
        uint32_t payload_size = 0;
        r.Read("prev_dle", prev_dle);
        r.Read("in_frame", in_frame);
        r.Read("rx_payload_count", payload_size);
        prev_dle_ = prev_dle != 0u;
        in_frame_ = in_frame != 0u;
        rx_payload_.resize(payload_size);
        r.ReadBytes("rx_payload", rx_payload_.data(), rx_payload_.size());
        r.ReadBytes("word_a", word_a_.data(), word_a_.size());
        r.ReadBytes("word_b", word_b_.data(), word_b_.size());
    }

    void OnGuestTx(uint8_t byte) override {
        if (!in_frame_) {
            if (!prev_dle_) {
                if (byte != 0x10u) {
                    emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox expected DLE, got 0x%02X",
                                          static_cast<unsigned>(byte));
                }
                prev_dle_ = true;
                return;
            }
            prev_dle_ = false;
            if (byte != 0x02u) {
                emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox expected STX, got 0x%02X",
                                      static_cast<unsigned>(byte));
            }
            in_frame_ = true;
            rx_payload_.clear();
            return;
        }

        if (prev_dle_) {
            prev_dle_ = false;
            if (byte == 0x10u) {
                AppendPayloadByte(0x10u);
                return;
            }
            if (byte == 0x03u) {
                in_frame_ = false;
                ReplyToMicroOmsFrame();
                return;
            }
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox malformed DLE escape 0x%02X",
                                  static_cast<unsigned>(byte));
        }

        if (byte == 0x10u) {
            prev_dle_ = true;
            return;
        }
        AppendPayloadByte(byte);
    }

private:
    void ResetParser() {
        prev_dle_ = false;
        in_frame_ = false;
        rx_payload_.clear();
    }

    void AppendPayloadByte(uint8_t byte) {
        if (rx_payload_.size() >= 7u) {
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox payload exceeds 7 bytes");
        }
        rx_payload_.push_back(byte);
    }

    void ReplyToMicroOmsFrame() {
        static constexpr uint8_t kVersionQuery[7] = {0x4Au, 0x55u, 0x0Fu, 0x00u, 0x00u, 0x00u, 0x00u};
        if (rx_payload_.size() == 7u && std::equal(rx_payload_.begin(), rx_payload_.end(), kVersionQuery)) {
            std::vector<uint8_t> reply = {0x4Au, 0xAAu, 0xF0u};
            cerf::be::Append32(reply, kConnBoxVersionStub);
            SendFrame(reply);
            return;
        }
        if (rx_payload_.size() == 7u) {
            ReplyToExchange();
            return;
        }
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox unsupported frame size=%u",
                              static_cast<unsigned>(rx_payload_.size()));
    }

    void ReplyToExchange() {
        const uint8_t command = rx_payload_[0];
        std::vector<uint8_t> reply(7u, 0u);
        reply[0] = command;
        reply[1] = ReverseBits(rx_payload_[1]);
        reply[2] = ReverseBits(rx_payload_[2]);
        switch (command) {
        case 1u: {
            static constexpr uint8_t kNoId = 0u;
            const uint8_t step = rx_payload_[3];
            if (step > 3u) emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox exchange command 1 test %u", step);
            const uint8_t answers[4] = {kNoId, siemens_simatic_mobile_panels_gen2::kConnBoxId,
                                        static_cast<uint8_t>(~siemens_simatic_mobile_panels_gen2::kConnBoxId), 0xFFu};
            reply[3] = answers[step];
            break;
        }
        case 211u: std::copy(rx_payload_.begin() + 3, rx_payload_.end(), word_a_.begin()); break;
        case 84u: std::copy(rx_payload_.begin() + 3, rx_payload_.end(), word_b_.begin()); break;
        case 134u: std::copy(word_a_.begin(), word_a_.end(), reply.begin() + 3); break;
        case 152u: std::copy(word_b_.begin(), word_b_.end(), reply.begin() + 3); break;
        default:
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen ConnBox exchange command %u is not one komp2 V1.0.6 sends",
                                  static_cast<unsigned>(command));
        }
        SendFrame(reply);
    }

    static uint8_t ReverseBits(uint8_t value) {
        uint8_t out = 0u;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            if ((value >> bit) & 1u) out = static_cast<uint8_t>(out | (0x80u >> bit));
        }
        return out;
    }

    void SendFrame(const std::vector<uint8_t>& reply) {
        std::vector<uint8_t> wire;
        wire.reserve(reply.size() + 4u);
        wire.push_back(0x10u);
        wire.push_back(0x02u);
        for (uint8_t b : reply) {
            wire.push_back(b);
            if (b == 0x10u) wire.push_back(0x10u);
        }
        wire.push_back(0x10u);
        wire.push_back(0x03u);
        emu_.Get<Imx6Uart2>().InjectRx(wire.data(), wire.size());
    }

    static constexpr uint32_t kConnBoxVersionStub = 0u;

    std::array<uint8_t, 4> word_a_{{0xFFu, 0xFFu, 0xFFu, 0xFFu}};
    std::array<uint8_t, 4> word_b_{{0xFFu, 0xFFu, 0xFFu, 0xFFu}};

    bool prev_dle_ = false;
    bool in_frame_ = false;
    std::vector<uint8_t> rx_payload_;
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2ConnBox);

}
