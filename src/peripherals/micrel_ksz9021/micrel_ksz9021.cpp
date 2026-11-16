#include "../mii_phy.h"
#include "micrel_ksz9021_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint16_t kLinkPartnerAbility = 0x45E1u; /* Same speeds as our PHY + pause + acknowledgment. */

class MicrelKsz9021 final : public MiiPhy {
public:
    using MiiPhy::MiiPhy;

    bool ShouldRegister() override { return emu_.TryGet<MicrelKsz9021Wiring>() != nullptr; }
    void OnReady() override { wiring_ = &emu_.Get<MicrelKsz9021Wiring>(); }

    uint16_t ReadRegister(uint32_t phy, uint32_t reg, bool link_up) const override;
    void WriteRegister(uint32_t phy, uint32_t reg, uint16_t value) override;
    void ResetPhyState() override { bmcr_ = kBmcrReset; gbcr_ = kGbcrReset; ext_address_ = 0u; }
    void SavePhyState(StateWriter& writer) const override;
    void RestorePhyState(StateReader& reader) override;

private:
    bool MasterSlaveResolution() const;
    void WriteExtendedRegister(uint32_t reg, uint16_t value);

    static constexpr uint16_t kBmcrAutoNegotiationEnable = 0x1000u;
    static constexpr uint16_t kBmcrSpeedDuplexStrap = 0x2100u;
    static constexpr uint16_t kBmcrReset = kBmcrAutoNegotiationEnable | kBmcrSpeedDuplexStrap;
    static constexpr uint16_t kGbcrReset = 0x0200u;

    MicrelKsz9021Wiring* wiring_ = nullptr;
    uint16_t bmcr_ = kBmcrReset;
    uint16_t gbcr_ = kGbcrReset;
    uint32_t ext_address_ = 0u;
};

void MicrelKsz9021::SavePhyState(StateWriter& writer) const {
    writer.Write("phy_bmcr", bmcr_);
    writer.Write("phy_gbcr", gbcr_);
    writer.Write("phy_ext_address", ext_address_);
}

void MicrelKsz9021::RestorePhyState(StateReader& reader) {
    reader.Read("phy_bmcr", bmcr_);
    reader.Read("phy_gbcr", gbcr_);
    reader.Read("phy_ext_address", ext_address_);
}

bool MicrelKsz9021::MasterSlaveResolution() const {
    return (gbcr_ & 0x1000u) ? ((gbcr_ & 0x0800u) != 0u) : ((gbcr_ & 0x0400u) != 0u);
}

uint16_t MicrelKsz9021::ReadRegister(uint32_t phy, uint32_t reg, bool link_up) const {
    if (phy != wiring_->PhyAddress()) return 0xFFFFu;
    switch (reg) {
    case 0x00: return bmcr_;
    case 0x01:
        return link_up ? 0x796Du : 0x7949u;
    case 0x02: return 0x0022u;
    case 0x03: return 0x1611u;
    case 0x04: return 0x01E1u;
    case 0x05:
        return link_up ? kLinkPartnerAbility : 0x0000u;
    case 0x1F: return link_up ? 0x0228u : 0x0200u;
    case 0x09: return static_cast<uint16_t>(gbcr_ & 0xFF00u);
    case 0x0A: return MasterSlaveResolution() ? 0x4000u : 0x0000u;
    case 0x0F: return 0x3000u;
    case 0x0D:
        emu_.Get<Fatal>().Die("[KSZ9021] MDIO read of extended register 0x%03X is not modelled", ext_address_);
    default: emu_.Get<Fatal>().Die("[KSZ9021] MDIO read of unmodelled PHY register %u", reg);
    }
}

void MicrelKsz9021::WriteRegister(uint32_t phy, uint32_t reg, uint16_t value) {
    if (phy != wiring_->PhyAddress()) return;
    switch (reg) {
    case 0x00:
        if ((value & 0x8000u) != 0u) {
            bmcr_ = kBmcrReset;
            return;
        }
        if ((value & 0x1000u) == 0u || (value & 0x4C80u) != 0u)
            emu_.Get<Fatal>().Die("[KSZ9021] PHY BMCR 0x%04X selects loop-back, power down, isolate, reserved bit 0.7 "
                                  "or a forced mode, which are not modelled", value);
        bmcr_ = value & ~0x0200u;
        return;
    case 0x09:
        if ((value & 0xE000u) != 0u)
            emu_.Get<Fatal>().Die("[KSZ9021] PHY 1000Base-T test mode 0x%04X is not modelled", value);
        gbcr_ = value;
        return;
    case 0x0B: ext_address_ = value & 0x0FFFu; return;
    case 0x0C: WriteExtendedRegister(ext_address_, value); return;
    case 0x0D:
    case 0x0E:
    case 0x1D:
    case 0x1E: return;
    default:
        emu_.Get<Fatal>().Die("[KSZ9021] MDIO write of unmodelled PHY register %u value 0x%04X", reg, value);
    }
}

void MicrelKsz9021::WriteExtendedRegister(uint32_t reg, uint16_t value) {
    switch (reg) {
    case 0x104:
    case 0x105:
    case 0x106: return;
    default:
        emu_.Get<Fatal>().Die("[KSZ9021] MDIO write of unmodelled extended register 0x%03X value 0x%04X", reg, value);
    }
}

}

REGISTER_SERVICE_AS(MicrelKsz9021, MiiPhy);

}
