#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/service.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include "compaq_ipaq_h3000_micro_frame.h"
#include "compaq_ipaq_h3000_battery.h"
#include "compaq_ipaq_h3000_micro_display.h"
#include "compaq_ipaq_h3000_micro_keys.h"
#include "compaq_ipaq_h3000_micro_led.h"
#include "compaq_ipaq_h3000_sleeve_nvm.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <algorithm>
#include "../../peripherals/touch/guest_clock_touch_input.h"
#include "../../socs/sa11xx/sa11xx_sp1_uart.h"
#include "../../socs/uart_endpoint.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

namespace {

/* Message IDs from Linux include/linux/mfd/ipaq-micro.h. */
constexpr uint8_t kMsgVersion     = 0x0;
constexpr uint8_t kMsgTouch       = 0x3;
constexpr uint8_t kMsgEepromRd    = 0x4;
constexpr uint8_t kMsgLed         = 0x8;
constexpr uint8_t kMsgBattery     = 0x9;
constexpr uint8_t kMsgSpiRead     = 0xB;
constexpr uint8_t kMsgSpiWrite    = 0xC;
constexpr uint8_t kMsgBacklight   = 0xD;
constexpr uint8_t kMsgDisplayCtrl = 0xF;

constexpr uint8_t kEepromMaxWords = kCompaqIpaqH3000MicroMaxPayload / 2u;

constexpr uint8_t kVersionReplyLength = 9;
constexpr uint8_t kBootTypeOffset     = 8;
constexpr uint8_t kBootPowerOn        = 1;
constexpr uint8_t kBootReset          = 2;

constexpr uint16_t kAdcXMin = 60;
constexpr uint16_t kAdcXMax = 960;
constexpr uint16_t kAdcYMin = 35;
constexpr uint16_t kAdcYMax = 980;
/* ipaq_h3600_ppc2002 touch.dll sub_1F21340 (TouchPanelGetDeviceCaps): TPDC_SAMPLE_RATE
   low 100, high 100. */
constexpr GuestCycleClock::Rate kSampleRate{100u, 1u};

constexpr GuestClockTouchInput::PanelRange kPanel{kAdcXMin, kAdcXMax, kAdcYMin, kAdcYMax};

class CompaqIpaqH3000MicroP : public GuestClockTouchInput, public UartEndpoint {
public:
    using GuestClockTouchInput::GuestClockTouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

    void OnGuestTx(uint8_t b) override {
        std::lock_guard<std::mutex> lk(state_mtx_);
        switch (state_) {
            case kWaitSof:
                if (b == kCompaqIpaqH3000MicroSof) state_ = kHaveSof;
                break;
            case kHaveSof:
                rx_id_    = (b >> 4) & 0x0Fu;
                rx_len_   = b & 0x0Fu;
                rx_count_ = 0;
                state_    = (rx_len_ == 0) ? kWaitCksum : kData;
                break;
            case kData:
                if (rx_count_ < sizeof(rx_buf_)) rx_buf_[rx_count_] = b;
                ++rx_count_;
                if (rx_count_ >= rx_len_) state_ = kWaitCksum;
                break;
            case kWaitCksum:
                RespondTo(rx_id_, rx_buf_, rx_count_);
                state_ = kWaitSof;
                break;
        }
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mtx_);
        w.Write("microp_state", state_);
        w.Write("microp_rx_id", rx_id_);
        w.Write("microp_rx_len", rx_len_);
        w.Write("microp_rx_count", rx_count_);
        w.WriteBytes("microp_rx_buf", rx_buf_, sizeof(rx_buf_));
        w.Write<uint8_t>("microp_pen_down", pen_down_ ? 1u : 0u);
        w.Write("microp_boot_type", boot_type_);
        emu_.Get<CompaqIpaqH3000MicroKeys>().SaveState(w);
        emu_.Get<CompaqIpaqH3000MicroDisplay>().SaveState(w);
        emu_.Get<CompaqIpaqH3000MicroLed>().SaveState(w);
        emu_.Get<CompaqIpaqH3000SleeveNvm>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        uint8_t down = 0;
        {
            std::lock_guard<std::mutex> lk(state_mtx_);
            r.Read("microp_state", state_);
            r.Read("microp_rx_id", rx_id_);
            r.Read("microp_rx_len", rx_len_);
            r.Read("microp_rx_count", rx_count_);
            r.ReadBytes("microp_rx_buf", rx_buf_, sizeof(rx_buf_));
            r.Read("microp_pen_down", down);
            r.Read("microp_boot_type", boot_type_);
        }
        ClearOnRestore();
        pen_down_ = false;
        if (down != 0u) SendTouchRelease();
        emu_.Get<CompaqIpaqH3000MicroKeys>().RestoreState(r);
        emu_.Get<CompaqIpaqH3000MicroDisplay>().RestoreState(r);
        emu_.Get<CompaqIpaqH3000MicroLed>().RestoreState(r);
        emu_.Get<CompaqIpaqH3000SleeveNvm>().RestoreState(r);
    }

protected:
    void OnTouchReady() override {
        emu_.Get<Sa11xxSp1Uart>().AttachEndpoint(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            std::lock_guard<std::mutex> lk(state_mtx_);
            boot_type_ = kind == ResetLineKind::Rtc ? kBootPowerOn : kBootReset;
        });
    }

    void ApplyHostPen(const HostPen& pen) override {
        const bool down = pen.kind == kPenDown;
        if (down == pen_down_) return;
        pen_down_ = down;
        if (!pen_down_) {
            StopSamples();
            SendTouchRelease();
            return;
        }
        SendSample(pen.x, pen.y);
        StartSamples(kSampleRate);
    }

    void OnSample() override {
        int x = 0;
        int y = 0;
        LatestPosition(&x, &y);
        SendSample(x, y);
    }

private:
    /* Linux drivers/input/touchscreen/ipaq-micro-ts.c:
       press = MSG_TOUCHSCREEN with len=4, payload [Y_BE16, X_BE16].
       release = MSG_TOUCHSCREEN with len=0. */
    void SendTouchPress(uint16_t adc_x, uint16_t adc_y) {
        uint8_t payload[4];
        cerf::be::Put16(payload, adc_y);
        cerf::be::Put16(payload + 2, adc_x);
        SendFrame(kMsgTouch, payload, 4);
    }
    void SendTouchRelease() { SendFrame(kMsgTouch, nullptr, 0); }

    enum State : uint8_t { kWaitSof, kHaveSof, kData, kWaitCksum };

    std::mutex state_mtx_;
    State    state_         = kWaitSof;
    uint8_t  rx_id_         = 0;
    uint8_t  rx_len_        = 0;
    uint8_t  rx_count_      = 0;
    uint8_t  rx_buf_[16]    = {};
    bool     pen_down_      = false;
    uint8_t  boot_type_     = kBootPowerOn;

    void SendSample(int x, int y) {
        uint16_t adc_x = 0;
        uint16_t adc_y = 0;
        MapToPanel(x, y, kPanel, &adc_x, &adc_y);
        SendTouchPress(adc_x, adc_y);
    }

    void SendFrame(uint8_t msg_id, const uint8_t* data, uint8_t len) {
        uint8_t frame[kCompaqIpaqH3000MicroMaxFrame];
        const size_t n = BuildCompaqIpaqH3000MicroFrame(msg_id, data, len, frame);
        emu_.Get<Sa11xxSp1Uart>().PushRxBurstNow(frame, n);
    }

    void RespondTo(uint8_t id, const uint8_t* req, uint8_t req_len) {
        switch (id) {
            case kMsgVersion: {
                if (req_len != 0) {
                    emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroP: version request of %u bytes",
                                          req_len);
                }
                uint8_t v[kVersionReplyLength] = { '1', '.', '7', '7' };
                v[kBootTypeOffset] = boot_type_;
                SendFrame(kMsgVersion, v, kVersionReplyLength);
                break;
            }
            case kMsgEepromRd: {
                if (req_len != 2 || req[1] == 0u || req[1] > kEepromMaxWords) {
                    emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroP: EEPROM read of %u bytes, "
                                          "count %u", req_len, req_len > 1 ? req[1] : 0u);
                }
                uint8_t words[kCompaqIpaqH3000MicroMaxPayload];
                std::fill_n(words, 2u * req[1], uint8_t{0xFFu});
                SendFrame(kMsgEepromRd, words, static_cast<uint8_t>(2u * req[1]));
                break;
            }
            case kMsgLed:
                emu_.Get<CompaqIpaqH3000MicroLed>().SetLed(req, req_len);
                SendFrame(kMsgLed, nullptr, 0);
                break;
            case kMsgBattery: {
                if (req_len != 0) {
                    emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroP: battery request of %u bytes",
                                          req_len);
                }
                uint8_t v[CompaqIpaqH3000Battery::kReplyLength];
                emu_.Get<CompaqIpaqH3000Battery>().Reply(v);
                SendFrame(kMsgBattery, v, CompaqIpaqH3000Battery::kReplyLength);
                break;
            }
            case kMsgSpiRead: {
                uint8_t data[kCompaqIpaqH3000MicroMaxPayload];
                const uint8_t n = emu_.Get<CompaqIpaqH3000SleeveNvm>().Read(req, req_len, data);
                SendFrame(kMsgSpiRead, data, n);
                break;
            }
            case kMsgSpiWrite:
                emu_.Get<CompaqIpaqH3000SleeveNvm>().Write(req, req_len);
                SendFrame(kMsgSpiWrite, nullptr, 0);
                break;
            case kMsgBacklight: {
                uint8_t data[1];
                const uint8_t n =
                    emu_.Get<CompaqIpaqH3000MicroDisplay>().Backlight(req, req_len, data);
                SendFrame(kMsgBacklight, data, n);
                break;
            }
            case kMsgDisplayCtrl:
                emu_.Get<CompaqIpaqH3000MicroDisplay>().DisplayControl(req, req_len);
                SendFrame(kMsgDisplayCtrl, nullptr, 0);
                break;
            default:
                emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroP: command 0x%X of %u bytes", id,
                                      req_len);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CompaqIpaqH3000MicroP, TouchInput);

}
