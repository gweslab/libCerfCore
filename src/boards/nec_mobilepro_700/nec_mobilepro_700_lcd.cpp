#include "nec_mobilepro_700_lcd.h"

#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

bool NecMobilepro700Lcd::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
}

void NecMobilepro700Lcd::OnReady() {
    fb_.assign(kVramSize, 0u);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        fb_written_ = false;
        size_latch_.Reset();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void NecMobilepro700Lcd::PublishScreenSizeOnWriteEdge() {
    fb_written_ = true;
    if (size_latch_.PublishOnce(emu_, true)) {
        LOG(Lcd, "NecMobilepro700Lcd: splash surface %ux%u %ubpp stride=%u\n",
            GuestW(), GuestH(), Bpp(), StrideBytes());
    }
}

uint8_t NecMobilepro700Lcd::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off < kVramSize) return fb_[off];
    HaltUnsupportedAccess("NecMobilepro700Lcd ReadByte", addr, 0);
}

uint16_t NecMobilepro700Lcd::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 1u < kVramSize) return cerf::le::U16(fb_.data(), off);
    HaltUnsupportedAccess("NecMobilepro700Lcd ReadHalf", addr, 0);
}

uint32_t NecMobilepro700Lcd::ReadWordAsHalves(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 3u < kVramSize) return cerf::le::U32(fb_.data(), off);
    return Peripheral::ReadWordAsHalves(addr);
}

void NecMobilepro700Lcd::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (off < kVramSize) { fb_[off] = value; PublishScreenSizeOnWriteEdge(); return; }
    HaltUnsupportedAccess("NecMobilepro700Lcd WriteByte", addr, value);
}

void NecMobilepro700Lcd::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 1u < kVramSize) { cerf::le::Put16(fb_.data() + off, value); PublishScreenSizeOnWriteEdge(); return; }
    HaltUnsupportedAccess("NecMobilepro700Lcd WriteHalf", addr, value);
}

void NecMobilepro700Lcd::WriteWordAsHalves(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 3u < kVramSize) { cerf::le::Put32(fb_.data() + off, value); PublishScreenSizeOnWriteEdge(); return; }
    Peripheral::WriteWordAsHalves(addr, value);
}

void NecMobilepro700Lcd::SaveState(StateWriter& w) {
    w.WriteBytes("fb_pin", fb_.data(), fb_.size());
    w.Write<uint8_t>("fb_written", fb_written_ ? 1u : 0u);
    size_latch_.SaveState(w);
}

void NecMobilepro700Lcd::RestoreState(StateReader& r) {
    r.ReadBytes("fb_pin", fb_.data(), fb_.size());
    uint8_t b = 0;
    r.Read("fb_written", b); fb_written_     = (b != 0);
    size_latch_.RestoreState(r);
}

REGISTER_SERVICE(NecMobilepro700Lcd);

}
