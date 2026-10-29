#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_autorun_regs.h>
#include "cerf_virt_utf16_window.h"

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/utf16.h>

#include <string>
#include <vector>

namespace cerfcore {

namespace {

class CerfVirtAutorun : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        const auto& list = emu_.Get<DeviceConfig>().guest_additions_autorun;
        if (list.size() > CerfVirt::kArMaxEntries) {
            emu_.Get<Fatal>().UserError("FATAL: guest_additions.autorun has %u entries, max %u",
                                        (unsigned)list.size(), CerfVirt::kArMaxEntries);
        }
        for (const std::string& s : list) {
            std::u16string w = Utf8ToUtf16(s);
            if (w.size() >= CerfVirt::kArEntryWchars) {
                emu_.Get<Fatal>().UserError("FATAL: guest_additions.autorun entry '%s' exceeds %u characters",
                                            s.c_str(), CerfVirt::kArEntryWchars - 1);
            }
            LOG(GuestAdditions, "autorun[%u] = '%s'", (unsigned)entries_.size(), s.c_str());
            entries_.push_back(std::move(w));
        }
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase() + CerfVirt::kAutorunOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kAutorunSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == CerfVirt::kArPresent) return entries_.empty() ? 0u : CerfVirt::kArMagic;
        if (off == CerfVirt::kArCount)   return (uint32_t)entries_.size();
        if (off >= CerfVirt::kArEntries && off + 4u <= CerfVirt::kAutorunSize) {
            const uint32_t rel = off - CerfVirt::kArEntries;
            const uint32_t idx = rel / CerfVirt::kArEntryStride;
            const uint32_t ch  = (rel % CerfVirt::kArEntryStride) / 2u;
            if (idx >= entries_.size()) return 0u;
            return CerfVirt::Utf16WindowWord(entries_[idx], ch);
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    std::vector<std::u16string> entries_;
};

}

REGISTER_SERVICE(CerfVirtAutorun);

}
