#include "hd64461_system_control.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_lcdc.h"
#include "hd64461_socket_wiring.h"
#include "hd64461_timer.h"

namespace cerfcore {

REGISTER_SERVICE(Hd64461SystemControl);

namespace {

constexpr uint32_t kStbcr = 0u;
constexpr uint32_t kSyscr = 1u;

constexpr uint16_t kSlckeIst   = 0x0800u;
constexpr uint16_t kSlckeOst   = 0x0200u;
constexpr uint16_t kSlcdst     = 0x0080u;
constexpr uint16_t kSpc0st     = 0x0040u;
constexpr uint16_t kSpc1st     = 0x0020u;
constexpr uint16_t kStm0st     = 0x0008u;
constexpr uint16_t kStm1st     = 0x0004u;
constexpr uint16_t kLcdcClock  = kSlckeIst | kSlckeOst | kSlcdst;
constexpr uint16_t kTimerClock = kStm0st | kStm1st;

constexpr uint16_t kSptaIr = 0x0080u;
constexpr uint16_t kSptbUr = 0x0020u;

}

bool Hd64461SystemControl::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461SystemControl::OnReady() {
    wiring_ = &emu_.Get<Hd64461SocketWiring>();
    RegWindow::OnReady();
    std::lock_guard<std::mutex> lk(lock_);
    PublishStbcrLocked();
}

void Hd64461SystemControl::RestoreState(StateReader& r) {
    RegWindow::RestoreState(r);
    std::lock_guard<std::mutex> lk(lock_);
    PublishStbcrLocked();
}

bool Hd64461SystemControl::LcdcClockRunning() const {
    return (stbcr_.load(std::memory_order_acquire) & kLcdcClock) == 0u;
}

bool Hd64461SystemControl::PccChannelClockRunning(uint32_t channel) const {
    return (stbcr_.load(std::memory_order_acquire) & (channel == 0u ? kSpc0st : kSpc1st)) == 0u;
}

bool Hd64461SystemControl::TimerClockRunning(uint32_t channel) const {
    return (stbcr_.load(std::memory_order_acquire) & (channel == 0u ? kStm0st : kStm1st)) == 0u;
}

uint16_t Hd64461SystemControl::SampleRegister(uint32_t i, uint32_t addr) {
    (void)addr;
    return static_cast<uint16_t>(StoredReg(i) | wiring_->ModePins());
}

void Hd64461SystemControl::BeforeStore(uint32_t i, uint16_t value) {
    if (i == kSyscr && (value & (kSptaIr | kSptbUr)) == (kSptaIr | kSptbUr)) {
        emu_.Get<Fatal>().Die("Hd64461SystemControl: SYSCR write 0x%04X selects UART and IrDA together", value);
    }
}

void Hd64461SystemControl::AfterStore(uint32_t i) {
    if (i != kStbcr) return;
    const uint16_t before = stbcr_.load(std::memory_order_relaxed);
    PublishStbcrLocked();
    if (((before ^ StoredReg(kStbcr)) & kLcdcClock) != 0u) emu_.Get<Hd64461Lcdc>().OnModuleClockChanged();
    if (((before ^ StoredReg(kStbcr)) & kTimerClock) != 0u) emu_.Get<Hd64461Timer>().OnModuleClockChanged();
}

void Hd64461SystemControl::AfterReset() { PublishStbcrLocked(); }

void Hd64461SystemControl::PublishStbcrLocked() { stbcr_.store(StoredReg(kStbcr), std::memory_order_release); }

}
