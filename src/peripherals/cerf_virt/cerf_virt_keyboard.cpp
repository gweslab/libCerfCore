#include "cerf_virt_keyboard.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include "cerf_guest_liveness.h"

#include "../peripheral_dispatcher.h"
#include "../../host/pc_special_key_order.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(CerfVirtKeyboard);

bool CerfVirtKeyboard::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtKeyboard::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume())
            no_injector_.store(false, std::memory_order_release);
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t CerfVirtKeyboard::MmioBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kKeyboardOffset;
}
uint32_t CerfVirtKeyboard::MmioSize() const { return CerfVirt::kKeyboardSize; }

uint32_t CerfVirtKeyboard::ReadWord(uint32_t addr) {
    uint32_t off = addr - MmioBase();
    if (off == CerfVirt::kKbWriteSeq) return write_seq_.load();
    if (off >= CerfVirt::kKbRingBase &&
        off < CerfVirt::kKbRingBase + CerfVirt::kKbRingCount * 4u) {
        return ring_[(off - CerfVirt::kKbRingBase) / 4u].load();
    }
    emu_.Get<Fatal>().Die("[CerfVirtKeyboard] read +0x%X", off);
}

void CerfVirtKeyboard::WriteWord(uint32_t addr, uint32_t value) {
    if (addr - MmioBase() != CerfVirt::kKbNoInjector)
        emu_.Get<Fatal>().Die("[CerfVirtKeyboard] write +0x%X = 0x%08X", addr - MmioBase(), value);
    no_injector_.store(value != 0u, std::memory_order_release);
    LOG(GuestAdditions, "guest additions: the guest %s keyboard input\n",
        value != 0u ? "cannot inject" : "injects");
    emu_.Get<KeyboardRouter>().ReevaluateAuto();
}

void CerfVirtKeyboard::PushKey(uint8_t vk, bool key_up) {
    uint32_t seq = write_seq_.load();
    uint32_t entry = (uint32_t)vk | (key_up ? CerfVirt::kKbEntryKeyUpBit : 0u);
    ring_[seq % CerfVirt::kKbRingCount].store(entry);

    write_seq_.store(seq + 1u);
}

void CerfVirtKeyboard::SaveState(StateWriter& w) {
    w.Write<uint32_t>("write_seq", write_seq_.load());
    for (uint32_t i = 0; i < CerfVirt::kKbRingCount; ++i)
        w.Write<uint32_t>("ring", ring_[i].load());
    w.Write<uint8_t>("no_injector", no_injector_.load(std::memory_order_acquire) ? 1u : 0u);
}

void CerfVirtKeyboard::RestoreState(StateReader& r) {
    uint32_t v;
    r.Read("write_seq", v); write_seq_.store(v);
    for (uint32_t i = 0; i < CerfVirt::kKbRingCount; ++i) {
        r.Read("ring", v); ring_[i].store(v);
    }
    uint8_t no_injector = 0;
    r.Read("no_injector", no_injector);
    no_injector_.store(no_injector != 0u, std::memory_order_release);
}

namespace {

class CerfVirtKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<KeyboardRouter>().Register(this);
    }

    static constexpr char kSourceName[] = "Guest Additions keyboard";
    std::string    SourceId() const override { return "guest_additions_keyboard"; }
    std::string    SourceName() const override { return kSourceName; }
    bool           Stock() const override { return false; }
    int            SourcePriority() const override { return 100; }

    bool SourceReady() const override {
        return emu_.Get<CerfGuestLiveness>().IsAlive() && emu_.Get<CerfVirtKeyboard>().GuestInjects();
    }

    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<CerfVirtKeyboard>().PushKey(vk, key_up);
    }

    bool CanDeliverVk(uint8_t /*vk*/) const override { return true; }

    std::vector<SpecialKey> SpecialKeys() const override {
        std::vector<SpecialKey> keys;
        for (const uint8_t vk : PcSpecialKeyOrder()) keys.push_back({vk, nullptr});
        return keys;
    }
};

REGISTER_SERVICE(CerfVirtKeyboardInput);

}

}
