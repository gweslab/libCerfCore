#pragma once

#include "imx6_gic.h"

#define NOMINMAX

#include "imx6_usdhc_adma.h"
#include "imx6_usdhc_regs.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/mmc/emmc_card_base.h"
#include "../../peripherals/mmc/mmc_card.h"

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cerfcore {

namespace cerf_imx6_usdhc_detail {

class Imx6UsdhcTransfer : public Peripheral {
public:
    using Peripheral::Peripheral;

protected:
    virtual int Spi() const = 0;
    virtual uint32_t SlotIndex() const = 0;

    void IssueAutoCmd12() {
        if ((mix_ctrl_ & (kMixAutoCmd12 | kMixMultiBlk)) != (kMixAutoCmd12 | kMixMultiBlk)) return;
        MmcCard* card = Card();
        if (!card) HaltUnsupportedAccess("imx6-usdhc auto CMD12 without a card", MmioBase() + kCMD_XFR_TYP, 12u);
        uint32_t resp[4] = {};
        const MmcCommandResult res =
            card->Command(cerf_mmc::kCmdStopTransmission, 0u, resp);
        autocmd12_err_status_ &= ~kAc12NotExecuted;
        if (res == MmcCommandResult::NoResponse) {
            autocmd12_err_status_ |= kAc12NotExecuted;
            SetIrqStatus(kAC12E);
            return;
        }
        rsp_[3] = resp[0];
    }

    void ExecuteCommand(uint32_t cmd_xfr_typ) {
        const uint8_t idx = static_cast<uint8_t>((cmd_xfr_typ >> 24) & 0x3Fu);
        const bool dpsel = ((cmd_xfr_typ >> 21) & 1u) != 0u;

        MmcCard* card = Card();
        uint32_t resp[4] = {};
        MmcCommandResult res = MmcCommandResult::NoResponse;
        if (card) res = card->Command(idx, cmdarg_, resp);

        const bool starts_read = card && !card->ReadData().empty();
        const bool starts_write = card && (idx == cerf_mmc::kCmdWriteBlock ||
                                           idx == cerf_mmc::kCmdWriteMultiBlock);

        if (res == MmcCommandResult::Long) {
            uint8_t b[16];
            for (uint32_t i = 0; i < 4u; ++i) cerf::be::Put32(b + i * 4u, resp[i]);
            rsp_[0] = cerf::be::U32(b, 11);
            rsp_[1] = cerf::be::U32(b, 7);
            rsp_[2] = cerf::be::U32(b, 3);
            rsp_[3] = cerf::be::U32(b, 0) >> 8;
        } else {
            rsp_[0] = resp[0];
            rsp_[1] = rsp_[2] = rsp_[3] = 0u;
        }
        const bool no_response = res == MmcCommandResult::NoResponse;
        const bool response_timeout = no_response && (((cmd_xfr_typ >> 16) & 3u) != 0u);
        const bool dma_mode = (mix_ctrl_ & kMixDmaEn) != 0u;

        if (idx == cerf_mmc::kCmdStopTransmission && !response_timeout) {
            buf_reading_ = false;
            buf_writing_ = false;
            open_ended_read_ = false;
            open_ended_write_ = false;
            irqstat_ &= ~(kBRR | kBWR);
            SetIrqStatus(kTC);
        }

        if (response_timeout) {
            SetIrqStatus(kCTOE);
        } else {
            SetIrqStatus(kCC);
        }
        if (dpsel && dma_mode) {
            if (!card)
                HaltUnsupportedAccess("imx6-usdhc DMA data command without a card", MmioBase() + kCMD_XFR_TYP,
                                      cmd_xfr_typ);
            if (!starts_read && !starts_write)
                HaltUnsupportedAccess("imx6-usdhc DMA data command whose card starts no data phase",
                                      MmioBase() + kCMD_XFR_TYP, cmd_xfr_typ);
            if (starts_read) {
                AdmaDmaRead();
                card->EndDataPhase();
            } else if (starts_write) {
                AdmaDmaWrite();
            }
            IssueAutoCmd12();
            SetIrqStatus(kTC | kDINT);
        } else if (dpsel && starts_read) {
            StartPioTransfer(true);
            StageReadBlock(*card);
            buf_pos_ = 0u;
            buf_reading_ = true;
            buf_writing_ = false;
            SetIrqStatus(kBRR);
        } else if (dpsel && starts_write) {
            StartPioTransfer(false);
            buf_pos_ = 0u;
            buf_reading_ = false;
            buf_writing_ = true;
            SetIrqStatus(kBWR);
        } else if (dpsel && !no_response) {
            HaltUnsupportedAccess("imx6-usdhc data command whose card starts no data phase",
                                  MmioBase() + kCMD_XFR_TYP, cmd_xfr_typ);
        }

        UpdateIrqLine();
    }

    uint32_t ReadData() {
        if (!buf_reading_ || buf_pos_ >= BlockSize())
            HaltUnsupportedAccess("imx6-usdhc data port read outside a transfer", MmioBase() + kDATA_BUFF, 0);
        uint32_t val = 0u;
        std::memcpy(&val, buf_ + buf_pos_, 4u);
        buf_pos_ += 4u;
        const uint32_t blksize = BlockSize();
        if (buf_pos_ >= blksize) {
            CountBlock();
            irqstat_ &= ~kBRR;
            if (open_ended_read_) {
                NextReadBlock();
                buf_pos_ = 0u;
                SetIrqStatus(kBRR);
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                NextReadBlock();
                buf_pos_ = 0u;
                SetIrqStatus(kBRR);
            } else {
                buf_reading_ = false;
                Card()->EndDataPhase();
                IssueAutoCmd12();
                SetIrqStatus(kTC);
            }
            UpdateIrqLine();
        }
        return val;
    }

    void WriteData(uint32_t v) {
        uint8_t tmp[4];
        std::memcpy(tmp, &v, sizeof(tmp));
        WriteDataBytes(tmp, 4u);
    }

    void WriteDataBytes(const uint8_t* src, uint32_t count) {
        if (!buf_writing_)
            HaltUnsupportedAccess("imx6-usdhc data port write outside a transfer", MmioBase() + kDATA_BUFF, 0);
        const uint32_t blksize = BlockSize();
        const uint32_t n = std::min<uint32_t>(count, blksize - buf_pos_);
        std::memcpy(buf_ + buf_pos_, src, n);
        buf_pos_ += n;
        if (buf_pos_ >= blksize) {
            Card()->ReceiveBlock(buf_, blksize);
            CountBlock();
            irqstat_ &= ~kBWR;
            if (open_ended_write_) {
                buf_pos_ = 0u;
                SetIrqStatus(kBWR);
            } else if (blocks_rem_ > 0u) {
                --blocks_rem_;
                buf_pos_ = 0u;
                SetIrqStatus(kBWR);
            } else {
                buf_writing_ = false;
                IssueAutoCmd12();
                SetIrqStatus(kTC);
            }
            UpdateIrqLine();
        }
    }

    uint32_t Adma2TableAddress() {
        if ((prot_ctrl_ & kProtDmaSelMask) != kProtDmaSelAdma2)
            HaltUnsupportedAccess("imx6-usdhc simple DMA transfer is not modelled", MmioBase() + kPROT_CTRL, prot_ctrl_);
        return adma_sys_addr_;
    }

    uint32_t AdmaBlockCount() const {
        return (mix_ctrl_ & kMixMultiBlk) != 0u ? (blk_att_ >> 16) & 0xFFFFu : 1u;
    }

    void AdmaDmaRead() {
        const uint32_t blksize = BlockSize();
        const Imx6UsdhcAdma::Transfer transfer{Adma2TableAddress(), blksize, AdmaBlockCount(),
                                               (mix_ctrl_ & kMixMultiBlk) == 0u || BlockCountRuns()};
        emu_.Get<Imx6UsdhcAdma>().Read(*Card(), transfer, buf_);
        if (BlockCountRuns()) blk_att_ &= 0xFFFFu;
    }

    void AdmaDmaWrite() {
        const uint32_t blksize = BlockSize();
        const Imx6UsdhcAdma::Transfer transfer{Adma2TableAddress(), blksize, AdmaBlockCount(),
                                               (mix_ctrl_ & kMixMultiBlk) == 0u || BlockCountRuns()};
        emu_.Get<Imx6UsdhcAdma>().Write(*Card(), transfer, buf_);
        if (BlockCountRuns()) blk_att_ &= 0xFFFFu;
    }

    uint32_t BlockSize() const { return blk_att_ & 0x1FFFu; }

    bool BlockCountRuns() const {
        return (mix_ctrl_ & (kMixMultiBlk | kMixBlkCntEn)) == (kMixMultiBlk | kMixBlkCntEn);
    }

    void CountBlock() {
        if (BlockCountRuns() && (blk_att_ >> 16) != 0u) blk_att_ -= 1u << 16;
    }

    void StartPioTransfer(bool read) {
        const uint32_t blkcnt = (blk_att_ >> 16) & 0xFFFFu;
        if (BlockCountRuns() && blkcnt == 0u)
            HaltUnsupportedAccess("imx6-usdhc no-data transfer with BLKCNT 0 is not modelled",
                                  MmioBase() + kBLK_ATT, blk_att_);
        const bool infinite = (mix_ctrl_ & kMixMultiBlk) != 0u && !BlockCountRuns();
        blocks_rem_ = BlockCountRuns() ? blkcnt - 1u : 0u;
        open_ended_read_ = read && infinite;
        open_ended_write_ = !read && infinite;
    }

    uint32_t PresentState() const {
        uint32_t p = kPS_CLK_STBL | kPS_DAT_IDLE | kPS_CMD_LVL;
        if (Card()) p |= kPS_CARD_PRES | kPS_CARD_DET | kPS_WP_LVL;
        const uint32_t blksize = BlockSize();
        const uint32_t words_left = (blksize - std::min(buf_pos_, blksize)) / 4u;
        if (buf_reading_ && (open_ended_read_ || buf_pos_ < blksize))
            p |= kPS_DAT_INH | kPS_DLA | kPS_DRD |
                 (words_left > (wtmk_lvl_ & 0xFFu) ? kPS_BUF_RDY : 0u);
        if (buf_writing_ && (open_ended_write_ || buf_pos_ < blksize))
            p |= kPS_DAT_INH | kPS_DLA | kPS_DWR |
                 (words_left > ((wtmk_lvl_ >> 16) & 0xFFu) ? kPS_BUF_SPC : 0u);
        return p;
    }

    void SetIrqStatus(uint32_t bits) {
        irqstat_ |= bits & irqstaten_;
    }

    void ResetAll() {
        cmdarg_ = 0u;
        cmd_xfr_typ_ = 0u;
        mix_ctrl_ = kMixCtrlReset;
        irqstat_ = 0u;
        irqstaten_ = kIntStatusEnReset;
        irqsigen_ = 0u;
        sys_ctrl_ = kSysCtrlReset;
        prot_ctrl_ = kProtCtrlReset;
        blk_att_ = 0u;
        wtmk_lvl_ = kWtmkLvlReset;
        vend_spec_ = kVendSpecReset;
        ds_addr_ = 0u;
        adma_sys_addr_ = 0u;
        autocmd12_err_status_ = 0u;
        std::fill(std::begin(rsp_), std::end(rsp_), 0u);
        ResetDataLine();
    }

    void ResetDataLine() {
        std::fill(std::begin(buf_), std::end(buf_), uint8_t{0});
        buf_pos_ = 0u;
        blocks_rem_ = 0u;
        buf_reading_ = false;
        buf_writing_ = false;
        open_ended_read_ = false;
        open_ended_write_ = false;
        prot_ctrl_ &= ~(kProtCreq | kProtSabgreq);
        irqstat_ &= ~(kBRR | kBWR | kDINT | kBGE | kTC);
    }

    void UpdateIrqLine() {
        if (irqstat_ & irqsigen_)
            emu_.Get<cerfcore::Imx6Gic>().AssertSpi(Spi());
        else
            emu_.Get<cerfcore::Imx6Gic>().DeAssertSpi(Spi());
    }

    MmcCard* Card() const {
        auto* card = emu_.TryGet<MmcCard>();
        return (card != nullptr && card->SlotIndex() == SlotIndex()) ? card : nullptr;
    }

    void StageReadBlock(MmcCard& card) {
        const std::vector<uint8_t>& staged = card.ReadData();
        const uint32_t blksize = BlockSize();
        if (staged.size() < blksize)
            HaltUnsupportedAccess("imx6-usdhc read block shorter than the block size",
                                  MmioBase() + kDATA_BUFF, static_cast<uint32_t>(staged.size()));
        std::memcpy(buf_, staged.data(), blksize);
    }

    void NextReadBlock() {
        MmcCard& card = *Card();
        card.NextBlock();
        StageReadBlock(card);
    }

    uint32_t cmdarg_ = 0u;
    uint32_t cmd_xfr_typ_ = 0u;
    uint32_t mix_ctrl_ = kMixCtrlReset;
    uint32_t irqstat_ = 0u;
    uint32_t irqstaten_ = kIntStatusEnReset;
    uint32_t irqsigen_ = 0u;
    uint32_t sys_ctrl_ = kSysCtrlReset;
    uint32_t prot_ctrl_ = kProtCtrlReset;
    uint32_t blk_att_ = 0u;
    uint32_t wtmk_lvl_ = kWtmkLvlReset;
    uint32_t vend_spec_ = kVendSpecReset;
    uint32_t ds_addr_ = 0u;
    uint32_t adma_sys_addr_ = 0u;
    uint32_t autocmd12_err_status_ = 0u;
    uint32_t rsp_[4] = {};

    uint8_t buf_[512] = {};
    uint32_t buf_pos_ = 0u;
    uint32_t blocks_rem_ = 0u;
    bool buf_reading_ = false;
    bool buf_writing_ = false;
    bool open_ended_read_ = false;
    bool open_ended_write_ = false;
};

}

}
