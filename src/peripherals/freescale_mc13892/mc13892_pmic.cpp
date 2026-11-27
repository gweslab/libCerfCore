#include "mc13892_pmic.h"

namespace cerfcore {

REGISTER_SERVICE(Mc13892Pmic);

void Mc13892Pmic::TxnStart(uint8_t /*slave_addr*/) {
    sub_addr_.Start();
    wr_idx_ = 0;
    rd_idx_ = 0;
}

bool Mc13892Pmic::TxnWriteByte(uint8_t , uint8_t byte) {
    if (sub_addr_.Select(static_cast<uint8_t>(byte & (kRegCount - 1)))) return true;
    if (wr_idx_ > 2) return true;
    const uint32_t shift = 16u - 8u * wr_idx_;
    uint32_t& r = regs_[sub_addr_.Register()];
    r = (r & ~(0xFFu << shift)) | (static_cast<uint32_t>(byte) << shift);
    ++wr_idx_;
    return true;
}

uint8_t Mc13892Pmic::TxnReadByte(uint8_t /*slave_addr*/) {
    const uint8_t idx   = rd_idx_ > 2 ? 2 : rd_idx_;
    const uint32_t shift = 16u - 8u * idx;
    if (rd_idx_ <= 2) ++rd_idx_;
    return static_cast<uint8_t>(regs_[sub_addr_.Register()] >> shift);
}

}
