#include "imx6_gic.h"
#include "imx6_fec.h"
#include <cerfcore/core/crc32.h>
#include <cerfcore/core/byte_order.h>
#include "imx6_fec_legacy_ring.h"
#include "imx6_mmio_lane.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../net/network_backend.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/mii_phy.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>
#include <mutex>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6Fec final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }

    void OnReady() override {
        phy_ = emu_.TryGet<MiiPhy>();
        guest_mac_ = emu_.Get<NetworkBackend>().AttachReceiver(
            kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet,
            [this](const uint8_t* frame, std::size_t len) { OnHostFrame(frame, len); });
        rx_installed_ = true;
        const uint32_t kPalrSeed = cerf::be::U32(guest_mac_.data());
        palr_ = kPalrSeed;
        const uint32_t kPaurSeed = uint32_t(cerf::be::U16(guest_mac_.data(), 4)) << 16;
        paur_ = kPaurSeed | kPaurType;
        seed_palr_ = palr_;
        seed_paur_ = paur_;
        Reset();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            PostRestore();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    void OnShutdown() override {
        if (rx_installed_) {
            emu_.Get<NetworkBackend>().DetachReceiver(kImx6FecReceiverId);
            rx_installed_ = false;
        }
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        return ReadReg(addr - kBase);
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
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        WriteReg(addr - kBase, value);
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        w.Write("eir", eir_);
        w.Write("eimr", eimr_);
        w.Write("ecr", ecr_);
        w.Write("rcr", rcr_);
        w.Write("tcr", tcr_);
        w.Write("ftrl", ftrl_);
        w.Write("mmfr", mmfr_);
        w.Write("mscr", mscr_);
        w.Write("mii_pending", static_cast<uint32_t>(mii_pending_ ? 1u : 0u));
        w.Write("iaur", iaur_);
        w.Write("ialr", ialr_);
        w.Write("gaur", gaur_);
        w.Write("galr", galr_);
        w.Write("palr", palr_);
        w.Write("paur", paur_);
        w.Write("tfwr", tfwr_);
        w.Write("emrbr", emrbr_);
        w.Write("erdsr", erdsr_);
        w.Write("etdsr", etdsr_);
        if (phy_) phy_->SavePhyState(w);
        rings_.SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        r.Read("eir", eir_);
        r.Read("eimr", eimr_);
        r.Read("ecr", ecr_);
        r.Read("rcr", rcr_);
        r.Read("tcr", tcr_);
        r.Read("ftrl", ftrl_);
        r.Read("mmfr", mmfr_);
        r.Read("mscr", mscr_);
        uint32_t mii_pending = 0u;
        r.Read("mii_pending", mii_pending);
        mii_pending_ = mii_pending != 0u;
        r.Read("iaur", iaur_);
        r.Read("ialr", ialr_);
        r.Read("gaur", gaur_);
        r.Read("galr", galr_);
        r.Read("palr", palr_);
        r.Read("paur", paur_);
        r.Read("tfwr", tfwr_);
        r.Read("emrbr", emrbr_);
        r.Read("erdsr", erdsr_);
        r.Read("etdsr", etdsr_);
        if (phy_) phy_->RestorePhyState(r);
        rings_.RestoreState(r);
    }

    void PostRestore() override {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        UpdateIrq();
    }

private:
    void Reset() {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        if (phy_) phy_->ResetPhyState();
        ResetController();
    }

    void WriteLane(const Imx6MmioLane& lane) {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        const uint32_t off = lane.address - kBase;
        if (off == kEir) {
            eir_ &= ~lane.value;
            UpdateIrq();
            return;
        }
        WriteReg(off, lane.Merge(ReadReg(off)));
    }

    static constexpr uint32_t kBase = 0x02188000u;
    static constexpr uint32_t kSize = 0x4000u;
    static constexpr uint32_t kEir = 0x004u;
    static constexpr uint32_t kEimr = 0x008u;
    static constexpr uint32_t kRdar = 0x010u;
    static constexpr uint32_t kTdar = 0x014u;
    static constexpr uint32_t kEcr = 0x024u;
    static constexpr uint32_t kMmfr = 0x040u;
    static constexpr uint32_t kPaurType = 0x00008808u;
    static constexpr uint32_t kMscrSpeedMask = 0x0000007Eu;
    static constexpr uint32_t kTfwrStoreAndForward = 0x00000100u;
    static constexpr uint32_t kMscr = 0x044u;
    static constexpr uint32_t kRcr = 0x084u;
    static constexpr uint32_t kFtrl = 0x1B0u;
    static constexpr uint32_t kRcrLoop = 1u << 0;
    static constexpr uint32_t kRcrProm = 1u << 3;
    static constexpr uint32_t kRcrBcRej = 1u << 4;
    static constexpr uint32_t kRcrPaden = 1u << 12;
    static constexpr uint32_t kRcrCrcFwd = 1u << 14;
    static constexpr uint32_t kRcrNlc = 1u << 30;
    static constexpr uint32_t kFtrlTruncFlMask = 0x3FFFu;
    static constexpr uint32_t kTcr = 0x0C4u;
    static constexpr uint32_t kTcrFden = 1u << 2;
    static constexpr uint32_t kPalr = 0x0E4u;
    static constexpr uint32_t kPaur = 0x0E8u;
    static constexpr uint32_t kIaur = 0x118u;
    static constexpr uint32_t kIalr = 0x11Cu;
    static constexpr uint32_t kGaur = 0x120u;
    static constexpr uint32_t kGalr = 0x124u;
    static constexpr uint32_t kTfwr = 0x144u;
    static constexpr uint32_t kErdSr = 0x180u;
    static constexpr uint32_t kEtdSr = 0x184u;
    static constexpr uint32_t kEmrbr = 0x188u;
    static constexpr uint32_t kEcrReset = 0x00000001u;
    static constexpr uint32_t kEcrEtherEn = 0x00000002u;
    static constexpr uint32_t kEcrUnmodelled = 0x0000003Cu;
    static constexpr uint32_t kEcrDbswp = 0x00000100u;
    static constexpr uint32_t kEirMii = 0x00800000u;
    static constexpr uint32_t kEirBabr = 0x40000000u;

    uint32_t ReadReg(uint32_t off) {
        switch (off) {
        case kEir: return eir_;
        case kEimr: return eimr_;
        case kRdar: return rings_.Rdar();
        case kTdar: return rings_.Tdar();
        case kEcr: return ecr_ & ~kEcrReset;
        case kMmfr: return mmfr_;
        case kMscr: return mscr_;
        case kRcr: return rcr_;
        case kFtrl: return ftrl_;
        case kTcr: return tcr_;
        case kPalr: return palr_;
        case kPaur: return paur_;
        case kIaur: return iaur_;
        case kIalr: return ialr_;
        case kGaur: return gaur_;
        case kGalr: return galr_;
        case kTfwr: return tfwr_;
        case kErdSr: return erdsr_;
        case kEtdSr: return etdsr_;
        case kEmrbr: return emrbr_;
        default: HaltUnsupportedAccess("imx6-fec read32 unmodelled register", kBase + off, 0);
        }
    }

    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
        case kEir:
            eir_ &= ~value;
            UpdateIrq();
            return;
        case kEimr:
            eimr_ = value;
            UpdateIrq();
            return;
        case kRdar: rings_.RequestReceive((ecr_ & kEcrEtherEn) != 0u); return;
        case kTdar:
            if ((ecr_ & kEcrEtherEn) != 0u && (rcr_ & kRcrLoop) != 0u)
                emu_.Get<Fatal>().Die("[FEC] transmit with RCR[LOOP] internal loopback is not modelled");
            eir_ |= rings_.RequestTransmit((ecr_ & kEcrEtherEn) != 0u, LinkIsUp(), etdsr_,
                                           (rcr_ >> 16) & 0x3FFFu);
            UpdateIrq();
            return;
        case kEcr:
            if (value & kEcrReset) {
                ResetController();
                return;
            }
            if ((value & kEcrUnmodelled) != 0u ||
                ((value & kEcrEtherEn) != 0u && (value & kEcrDbswp) == 0u))
                emu_.Get<Fatal>().Die("[FEC] ECR 0x%08X: sleep, magic packet, 1588, 1000 Mbps or "
                                      "big-endian descriptors are not modelled", value);
            ecr_ = value & ~kEcrReset;
            if ((ecr_ & kEcrEtherEn) == 0u) rings_.Disable(erdsr_, etdsr_);
            return;
        case kMmfr:
            mmfr_ = value;
            mii_pending_ = (mscr_ & kMscrSpeedMask) == 0u;
            if (!mii_pending_) CompleteMiiTransaction();
            return;
        case kMscr: {
            const bool was_zero = (mscr_ & kMscrSpeedMask) == 0u;
            mscr_ = value;
            if (was_zero && (mscr_ & kMscrSpeedMask) != 0u && mii_pending_) {
                mii_pending_ = false;
                CompleteMiiTransaction();
            }
            return;
        }
        case kRcr:
            if ((value & kRcrNlc) != 0u)
                emu_.Get<Fatal>().Die("[FEC] RCR 0x%08X payload length check is not modelled", value);
            rcr_ = value;
            return;
        case kFtrl: ftrl_ = value & kFtrlTruncFlMask; return;
        case kTcr:
            if ((value & ~kTcrFden) != 0u)
                emu_.Get<Fatal>().Die("[FEC] TCR 0x%08X beyond FDEN is not modelled", value);
            tcr_ = value;
            return;
        case kPalr: palr_ = value; return;
        case kPaur: paur_ = (value | 0x0000FFFFu) & 0xFFFF8808u; return;
        case kIaur: iaur_ = value; return;
        case kIalr: ialr_ = value; return;
        case kGaur: gaur_ = value; return;
        case kGalr: galr_ = value; return;
        case kTfwr:
            if ((value & kTfwrStoreAndForward) == 0u)
                emu_.Get<Fatal>().Die("[FEC] TFWR cut-through is not modelled (0x%08X)", value);
            tfwr_ = value;
            return;
        case kErdSr:
            erdsr_ = value & ~7u;
            rings_.SetRxDescriptorBase(erdsr_);
            return;
        case kEtdSr:
            etdsr_ = value & ~7u;
            rings_.SetTxDescriptorBase(etdsr_);
            return;
        case kEmrbr:
            emrbr_ = value & 0x00003FF0u;
            return;
        default: HaltUnsupportedAccess("imx6-fec write32 unmodelled register", kBase + off, value);
        }
    }

    void ResetController() {
        eir_ = 0u;
        eimr_ = 0u;
        ecr_ = 0xF0000000u;
        rcr_ = 0x05EE0001u;
        ftrl_ = 0x000007FFu;
        tcr_ = 0u;
        mmfr_ = 0u;
        mscr_ = 0u;
        mii_pending_ = false;
        palr_ = seed_palr_;
        paur_ = seed_paur_;
        iaur_ = 0u;
        ialr_ = 0u;
        gaur_ = 0u;
        galr_ = 0u;
        tfwr_ = 0u;
        erdsr_ = 0u;
        etdsr_ = 0u;
        emrbr_ = 0u;
        rings_.Reset();
        UpdateIrq();
    }

    void OnHostFrame(const uint8_t* frame, std::size_t len) {
        if (!frame || len < 14u) return;
        if (!LinkIsUp()) return;

        std::lock_guard<std::recursive_mutex> lk(mtx_);
        if ((ecr_ & kEcrEtherEn) == 0u || erdsr_ == 0u || emrbr_ == 0u) return;
        if ((rcr_ & kRcrLoop) != 0u)
            emu_.Get<Fatal>().Die("[FEC] receive with RCR[LOOP] internal loopback is not modelled");
        const bool over_max_frame = len + 4u > ((rcr_ >> 16) & 0x3FFFu);
        if (over_max_frame) eir_ |= kEirBabr;
        if (!AcceptFrame(frame)) {
            if (over_max_frame) UpdateIrq();
            return;
        }
        if ((rcr_ & kRcrPaden) != 0u && cerf::be::U16(frame, 12) < 0x0600)
            emu_.Get<Fatal>().Die("[FEC] RCR[PADEN] pad removal from a length-field frame is not modelled");
        const bool strip_crc = (rcr_ & (kRcrCrcFwd | kRcrPaden)) != 0u;
        const uint32_t events = rings_.Receive(frame, len, erdsr_, emrbr_, strip_crc,
                                               static_cast<uint32_t>(len + 4u),
                                               (rcr_ >> 16) & 0x3FFFu, ftrl_ & kFtrlTruncFlMask);
        if (events != 0u || over_max_frame) {
            eir_ |= events;
            UpdateIrq();
        }
    }

    bool AcceptFrame(const uint8_t* frame) const {
        const bool promiscuous = (rcr_ & kRcrProm) != 0u;
        const bool broadcast = frame[0] == 0xFFu && frame[1] == 0xFFu && frame[2] == 0xFFu && frame[3] == 0xFFu &&
                               frame[4] == 0xFFu && frame[5] == 0xFFu;
        if (broadcast) return promiscuous || (rcr_ & kRcrBcRej) == 0u;
        if (promiscuous) return true;
        const bool multicast = (frame[0] & 1u) != 0u;
        const uint32_t da_lo = cerf::be::U32(frame);
        const uint32_t da_hi = uint32_t(cerf::be::U16(frame, 4)) << 16;
        if (!multicast && da_lo == palr_ && da_hi == (paur_ & 0xFFFF0000u)) return true;
        const uint32_t index = ~Crc32::Update(0u, frame, 6u) >> 26u;
        const uint32_t table = (index & 0x20u) != 0u ? (multicast ? gaur_ : iaur_) : (multicast ? galr_ : ialr_);
        return ((table >> (index & 0x1Fu)) & 1u) != 0u;
    }

    void CompleteMiiTransaction() {
        if ((mmfr_ >> 30) != 1u || ((mmfr_ >> 16) & 3u) != 2u)
            emu_.Get<Fatal>().Die("[FEC] MMFR 0x%08X: ST must be 01 and TA 10 for a clause-22 frame", mmfr_);
        const uint32_t op = (mmfr_ >> 28) & 3u;
        const uint32_t phy = (mmfr_ >> 23) & 0x1Fu;
        const uint32_t reg = (mmfr_ >> 18) & 0x1Fu;
        if (!phy_) emu_.Get<Fatal>().Die("[FEC] MII frame 0x%08X on a board with no PHY wired to the MDIO bus", mmfr_);

        if (op == 2u) {
            mmfr_ = (mmfr_ & 0xFFFF0000u) | phy_->ReadRegister(phy, reg, LinkIsUp());
        } else if (op == 1u) {
            phy_->WriteRegister(phy, reg, static_cast<uint16_t>(mmfr_));
        } else {
            emu_.Get<Fatal>().Die("[FEC] MII operation %u is not modelled (MMFR 0x%08X)", op, mmfr_);
        }
        eir_ |= kEirMii;
        UpdateIrq();
    }

    bool LinkIsUp() const { return rx_installed_; }

    void UpdateIrq() {
        if ((eir_ & eimr_) != 0u)
            emu_.Get<Imx6Gic>().AssertSpi(118);
        else
            emu_.Get<Imx6Gic>().DeAssertSpi(118);
    }

    uint32_t eir_ = 0u;
    uint32_t eimr_ = 0u;
    uint32_t ecr_ = 0xF0000000u;
    uint32_t rcr_ = 0x05EE0001u;
    uint32_t ftrl_ = 0x000007FFu;
    uint32_t tcr_ = 0u;
    uint32_t mmfr_ = 0u;
    bool mii_pending_ = false;
    uint32_t seed_palr_ = 0u;
    uint32_t seed_paur_ = kPaurType;
    uint32_t mscr_ = 0u;
    uint32_t iaur_ = 0u;
    uint32_t ialr_ = 0u;
    uint32_t gaur_ = 0u;
    uint32_t galr_ = 0u;
    uint32_t palr_ = 0u;
    uint32_t paur_ = 0x00008808u;
    uint32_t tfwr_ = 0u;
    uint32_t erdsr_ = 0u;
    uint32_t etdsr_ = 0u;
    uint32_t emrbr_ = 0u;
    Imx6FecLegacyRing rings_{emu_};
    std::array<uint8_t, 6> guest_mac_{};
    bool rx_installed_ = false;
    mutable std::recursive_mutex mtx_;
    MiiPhy* phy_ = nullptr;
};

}

REGISTER_SERVICE(Imx6Fec);

}
