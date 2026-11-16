#include "siemens_simatic_mobile_panels_gen2_emmc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kSlotIndex   = 3u;
constexpr uint32_t kSectorCount = siemens_simatic_mobile_panels_gen2_emmc::kCardSectors;

constexpr SdCardCid kCid = {
    0x03u, 'C', 'E', 'C', 'E', 'R', 'F', '0',
    0x10u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x40u, 0x00u,
};

constexpr uint8_t kTaac = 0x0Eu;
constexpr uint8_t kR2wFactor = 0u;
constexpr uint8_t kVddCurrent = 0u;
constexpr uint8_t kEraseGroupSize = 0u;
constexpr uint8_t kEraseGroupMult = 0u;
constexpr uint8_t kWpGroupSize = 0u;
constexpr uint8_t kCardType = 0x03u;
constexpr uint32_t kCccBlockReadAndWrite = (1u << 2) | (1u << 4);
constexpr uint32_t kCccOtherClasses = 0x5A1u;
constexpr uint8_t kNsac = 0x00u;
constexpr uint8_t kTranSpeed = 0x32u;
constexpr uint16_t kCSize = 0xFFFu;
constexpr uint8_t kCSizeMult = 4u;

constexpr EmmcCsdFields kCsd = {
    .csd_structure  = 3u,
    .spec_vers      = 4u,
    .taac           = kTaac,
    .nsac           = kNsac,
    .tran_speed     = kTranSpeed,
    .ccc            = kCccBlockReadAndWrite | kCccOtherClasses,
    .read_bl_len    = 9u,
    .c_size         = kCSize,
    .vdd_r_curr_min = kVddCurrent,
    .vdd_r_curr_max = kVddCurrent,
    .vdd_w_curr_min = kVddCurrent,
    .vdd_w_curr_max = kVddCurrent,
    .c_size_mult    = kCSizeMult,
    .erase_grp_size = kEraseGroupSize,
    .erase_grp_mult = kEraseGroupMult,
    .wp_grp_size    = kWpGroupSize,
    .wp_grp_enable  = 0u,
    .r2w_factor     = kR2wFactor,
    .write_bl_len   = 9u,
};

constexpr uint8_t kS_CmdSet = 0x07u;
constexpr uint8_t kExtCsdRev = 0x08u;
constexpr uint8_t kExtCsdStructure = 0x02u;
constexpr uint8_t kTimingAndCurrent = 0x01u;
constexpr uint8_t kBootSizeMult = 0x20u;
constexpr uint8_t kBootInfo = 0x07u;
constexpr uint8_t kPartitionConfigBoot = 0x48u;

constexpr EmmcExtCsdByte kExtCsdProperties[] = {
    {179u, kPartitionConfigBoot}, {196u, kCardType},
    {160u, kS_CmdSet}, {192u, kExtCsdRev}, {194u, kExtCsdStructure},
    {197u, kTimingAndCurrent}, {199u, kTimingAndCurrent},
    {221u, kTimingAndCurrent}, {222u, kTimingAndCurrent},
    {223u, kTimingAndCurrent}, {224u, kTimingAndCurrent},
    {225u, kTimingAndCurrent}, {226u, kBootSizeMult}, {228u, kBootInfo},
};

}

bool SiemensSimaticMobilePanelsGen2Emmc::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
}

void SiemensSimaticMobilePanelsGen2Emmc::OnReady() {
    card_ = &emu_.Get<SiemensSimaticMobilePanelsGen2CardStore>();
    EmmcCardBase::OnReady();
}

void SiemensSimaticMobilePanelsGen2Emmc::Reset() {
    if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
    EmmcCardBase::Reset();
}

uint32_t SiemensSimaticMobilePanelsGen2Emmc::SlotIndex() const { return kSlotIndex; }

SdCardCid SiemensSimaticMobilePanelsGen2Emmc::Cid() const { return kCid; }

EmmcCsdFields SiemensSimaticMobilePanelsGen2Emmc::Csd() const { return kCsd; }

std::span<const EmmcExtCsdByte> SiemensSimaticMobilePanelsGen2Emmc::ExtCsdProperties() const {
    return kExtCsdProperties;
}

uint32_t SiemensSimaticMobilePanelsGen2Emmc::SectorCount() const { return kSectorCount; }

uint8_t SiemensSimaticMobilePanelsGen2Emmc::ErasedMemCont() const {
    return cerf_mmc::kErasedMemContZeros;
}

void SiemensSimaticMobilePanelsGen2Emmc::ReadBlock(uint32_t sector, uint8_t* out) {
    card_->ReadSector(sector, out);
}

void SiemensSimaticMobilePanelsGen2Emmc::WriteBlock(uint32_t sector, const uint8_t* data) {
    card_->WriteSector(sector, data);
}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2Emmc, MmcCard);

}
