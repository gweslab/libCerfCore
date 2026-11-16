#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>
#include "imx6_ccm.h"
#include "imx6_mmio_lane.h"

#include <mutex>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6Snvs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        sleep_ = &emu_.Get<GuestDeepSleep>();
        slept_seen_ns_ = sleep_->SleptNs();
        sleep_->RegisterParkClock([this] { AdvanceParkedRtc(); });
        {
            std::lock_guard<std::mutex> lock(mtx_);
            rtc_baseline_ns_ = NowNs();
        }
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x020CC000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u: return lpcr_;
        case 0x4Cu: return lpsr_;
        case 0x50u: return static_cast<uint32_t>(RtcCounterLocked() >> 32);
        case 0x54u: return static_cast<uint32_t>(RtcCounterLocked());
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x38u:
            if ((value & ~kSrtcEnable) != 0u)
                HaltUnsupportedAccess("imx6-snvs LPCR alarm, wake and power-off control", addr, value);
            RebaseCounterLocked();
            lpcr_ = value;
            return;
        case 0x4Cu:  lpsr_ &= ~value; return;
        case 0x50u:
            if ((lpcr_ & kSrtcEnable) == 0)
                rtc_base_ = (rtc_base_ & 0xFFFFFFFFu) | (static_cast<uint64_t>(value & 0x7FFFu) << 32);
            return;
        case 0x54u:
            if ((lpcr_ & kSrtcEnable) == 0) rtc_base_ = (rtc_base_ & 0x7FFF00000000ull) | value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lock(mtx_);
        w.Write("rtc_counter", RtcCounterLocked());
        w.Write("lpcr", lpcr_);
        w.Write("lpsr", lpsr_);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lock(mtx_);
        r.Read("rtc_counter", rtc_base_);
        rtc_fraction_ = 0u;
        rtc_baseline_ns_ = NowNs();
        slept_seen_ns_ = sleep_->SleptNs();
        r.Read("lpcr", lpcr_);
        r.Read("lpsr", lpsr_);
    }

private:
    static constexpr uint32_t kSrtcEnable = 1u;
    static constexpr uint64_t kCounterMask = 0x7FFFFFFFFFFFull;
    static constexpr uint64_t kNanosecondsPerSecond = 1000000000u;

    uint64_t RtcCounterLocked() {
        if ((lpcr_ & kSrtcEnable) == 0) return rtc_base_;

        const int64_t elapsed = NowNs() - rtc_baseline_ns_;
        if (elapsed <= 0) return rtc_base_ & kCounterMask;
        const uint64_t ns = static_cast<uint64_t>(elapsed);
        const uint64_t cycles = (ns / kNanosecondsPerSecond) * Imx6Ccm::kCkilHz +
                                (((ns % kNanosecondsPerSecond) * Imx6Ccm::kCkilHz) + rtc_fraction_) /
                                    kNanosecondsPerSecond;
        return (rtc_base_ + cycles) & kCounterMask;
    }

    void RebaseCounterLocked() {
        AdvanceCounterLocked(NowNs() - rtc_baseline_ns_);
        rtc_baseline_ns_ = NowNs();
    }

    void AdvanceCounterLocked(int64_t elapsed) {
        if ((lpcr_ & kSrtcEnable) == 0u || elapsed <= 0) return;
        const uint64_t ns = static_cast<uint64_t>(elapsed);
        const uint64_t fraction = (ns % kNanosecondsPerSecond) * Imx6Ccm::kCkilHz +
                                  rtc_fraction_;
        rtc_base_ = (rtc_base_ + (ns / kNanosecondsPerSecond) * Imx6Ccm::kCkilHz +
                     fraction / kNanosecondsPerSecond) & kCounterMask;
        rtc_fraction_ = fraction % kNanosecondsPerSecond;
    }

    void AdvanceParkedRtc() {
        const int64_t slept_ns = sleep_->SleptNs();
        std::lock_guard<std::mutex> lock(mtx_);
        if (slept_ns > slept_seen_ns_) AdvanceCounterLocked(slept_ns - slept_seen_ns_);
        slept_seen_ns_ = slept_ns;
        rtc_baseline_ns_ = NowNs();
    }

    int64_t NowNs() { return emu_.Get<GuestCycleClock>().NowNs(); }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == 0x4Cu) {
            std::lock_guard<std::mutex> lock(mtx_);
            lpsr_ &= ~lane.value;
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    std::mutex mtx_;
    uint32_t lpcr_ = 0;
    uint32_t lpsr_ = 0x00000008u;
    uint64_t rtc_base_ = 0;
    uint64_t rtc_fraction_ = 0;
    int64_t rtc_baseline_ns_ = 0;
    int64_t slept_seen_ns_ = 0;
    GuestDeepSleep* sleep_ = nullptr;
};

}

REGISTER_SERVICE(Imx6Snvs);

}
