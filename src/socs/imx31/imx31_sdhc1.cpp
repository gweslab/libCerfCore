#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx31_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx31_avic.h"
#include "../freescale_module_clocks.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx31_ccm.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x50004000u;
constexpr uint32_t kSize = 0x00000040u;

constexpr uint32_t kStrStpClk  = 0x00u;
constexpr uint32_t kStatus     = 0x04u;
constexpr uint32_t kClkRate    = 0x08u;
constexpr uint32_t kCmdDatCont = 0x0Cu;
constexpr uint32_t kResTo      = 0x10u;
constexpr uint32_t kReadTo     = 0x14u;
constexpr uint32_t kBlkLen     = 0x18u;
constexpr uint32_t kNob        = 0x1Cu;
constexpr uint32_t kRevNo      = 0x20u;
constexpr uint32_t kIntCntr    = 0x24u;
constexpr uint32_t kCmd        = 0x28u;
constexpr uint32_t kArg        = 0x2Cu;
constexpr uint32_t kResFifo    = 0x34u;
constexpr uint32_t kBufAccess  = 0x38u;

constexpr uint32_t kClkSdhcReset   = 1u << 3;
constexpr uint32_t kClkStartClk    = 1u << 1;
constexpr uint32_t kClkStopClk     = 1u << 0;
constexpr uint32_t kStsTimeOutResp = 1u << 1;
constexpr uint32_t kStsCardClkRun  = 1u << 8;
constexpr uint32_t kStsEndCmdResp  = 1u << 13;
constexpr uint32_t kIntEndCmdResp  = 1u << 2;
constexpr uint32_t kStatusReset    = 0x30000000u;
constexpr uint32_t kClkRateReset   = 0x00000008u;
constexpr uint32_t kResToReset     = 0x00000040u;
constexpr uint32_t kReadToReset    = 0x0000FFFFu;
constexpr uint32_t kRevNoVal       = 0x00000400u;
constexpr uint32_t kAvicSourceSdhc1 = 9u;

constexpr uint8_t kClkReqNone  = 0u;
constexpr uint8_t kClkReqStart = 1u;
constexpr uint8_t kClkReqStop  = 2u;

class Imx31Sdhc1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        ccm_           = &emu_.Get<Imx31Ccm>();
        module_clocks_->RegisterGateListener([this] { RunHeld(); });
        ccm_->RegisterRateListener([this] { RunHeld(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kBase) {
            case kStrStpClk:  return str_stp_clk_;
            case kStatus:     return status_;
            case kClkRate:    return clk_rate_;
            case kCmdDatCont: return cmd_dat_cont_;
            case kResTo:      return res_to_;
            case kReadTo:     return read_to_;
            case kBlkLen:     return blk_len_;
            case kNob:        return nob_;
            case kRevNo:      return kRevNoVal;
            case kIntCntr:    return int_cntr_;
            case kCmd:        return cmd_;
            case kArg:        return arg_;
            case kResFifo:    return 0;
            case kBufAccess:  return 0;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kBase) {
            case kStrStpClk:
                str_stp_clk_ = value;
                if (value & kClkSdhcReset)     DropHeld();
                if (value & kClkStopClk)       clk_req_ = kClkReqStop;
                else if (value & kClkStartClk) clk_req_ = kClkReqStart;
                RunHeld();
                return;
            case kStatus:     status_ &= ~value; UpdateIrq(); return;
            case kClkRate:    clk_rate_ = value; return;
            case kCmdDatCont:
                cmd_dat_cont_ = value;
                cmd_held_ = true;
                RunHeld();
                return;
            case kResTo:      res_to_ = value; return;
            case kReadTo:     read_to_ = value; return;
            case kBlkLen:     blk_len_ = value; return;
            case kNob:        nob_ = value; return;
            case kIntCntr:    int_cntr_ = value; UpdateIrq(); return;
            case kCmd:        cmd_ = value; return;
            case kArg:        arg_ = value; return;
            case kBufAccess:  return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("str_stp_clk", str_stp_clk_);  w.Write("status", status_);    w.Write("clk_rate", clk_rate_);
        w.Write("cmd_dat_cont", cmd_dat_cont_); w.Write("res_to", res_to_);    w.Write("read_to", read_to_);
        w.Write("blk_len", blk_len_);      w.Write("nob", nob_);       w.Write("int_cntr", int_cntr_);
        w.Write("cmd", cmd_);          w.Write("arg", arg_);
        w.Write("clk_req", clk_req_);  w.Write<uint8_t>("cmd_held", cmd_held_ ? 1 : 0);
    }
    void RestoreState(StateReader& r) override {
        r.Read("str_stp_clk", str_stp_clk_);  r.Read("status", status_);    r.Read("clk_rate", clk_rate_);
        r.Read("cmd_dat_cont", cmd_dat_cont_); r.Read("res_to", res_to_);    r.Read("read_to", read_to_);
        r.Read("blk_len", blk_len_);      r.Read("nob", nob_);       r.Read("int_cntr", int_cntr_);
        r.Read("cmd", cmd_);          r.Read("arg", arg_);
        r.Read("clk_req", clk_req_);
        uint8_t held = 0;
        r.Read("cmd_held", held);
        cmd_held_ = held != 0;
    }

    void PostRestore() override { UpdateIrq(); }

private:
    void DropHeld() {
        clk_req_  = kClkReqNone;
        cmd_held_ = false;
    }

    void ResetRegisters() {
        str_stp_clk_  = 0;
        status_       = kStatusReset;
        clk_rate_     = kClkRateReset;
        cmd_dat_cont_ = 0;
        res_to_       = kResToReset;
        read_to_      = kReadToReset;
        blk_len_      = 0;
        nob_          = 0;
        int_cntr_     = 0;
        cmd_          = 0;
        arg_          = 0;
        DropHeld();
        UpdateIrq();
    }

    void RunHeld() {
        if (!module_clocks_->Running(FreescaleModule::kSdhc1) || !ccm_->PerClkRuns()) return;
        if (clk_req_ == kClkReqStart) status_ |= kStsCardClkRun;
        if (clk_req_ == kClkReqStop)  status_ &= ~kStsCardClkRun;
        clk_req_ = kClkReqNone;
        if (cmd_held_) {
            cmd_held_ = false;
            status_ |= kStsEndCmdResp | kStsTimeOutResp;
            UpdateIrq();
        }
    }

    void UpdateIrq() {
        const bool pending = (status_ & kStsEndCmdResp) && (int_cntr_ & kIntEndCmdResp);
        auto& avic = emu_.Get<Imx31Avic>();
        if (pending) avic.AssertSource(kAvicSourceSdhc1);
        else         avic.DeassertSource(kAvicSourceSdhc1);
    }

    uint32_t str_stp_clk_  = 0;
    uint32_t status_       = kStatusReset;
    uint32_t clk_rate_     = kClkRateReset;
    uint32_t cmd_dat_cont_ = 0;
    uint32_t res_to_       = kResToReset;
    uint32_t read_to_      = kReadToReset;
    uint32_t blk_len_      = 0;
    uint32_t nob_          = 0;
    uint32_t int_cntr_     = 0;
    uint32_t cmd_          = 0;
    uint32_t arg_          = 0;
    uint8_t  clk_req_      = kClkReqNone;
    bool     cmd_held_     = false;
    FreescaleModuleClocks* module_clocks_ = nullptr;
    Imx31Ccm*              ccm_           = nullptr;
};

}

REGISTER_SERVICE(Imx31Sdhc1);

}
