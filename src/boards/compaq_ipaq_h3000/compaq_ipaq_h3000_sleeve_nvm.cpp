#include "compaq_ipaq_h3000_sleeve_nvm.h"

#include "compaq_ipaq_h3000_family_id.h"
#include "compaq_ipaq_h3000_micro_frame.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <iterator>

namespace cerfcore {

namespace {

constexpr size_t kAddressSpace = 0x10000;

constexpr uint8_t kSleeveNvm[] = {
    0xAA,
    0x00, 0x00, 0x00, 0x00,
    0x01,
    0x00, 0x00,
    0x00, 0x00,
    'C', 'E', 'R', 'F', 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,
    0xBB,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x0F, 0x0F, 0x0F, 0x0F,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,
    0x00, 0x00, 0x00, 0x00,
    0x0F, 0x0F, 0x0F, 0x0F,
};

uint32_t AddressOf(const uint8_t* req) { return cerf::le::U16(req); }

}

REGISTER_SERVICE(CompaqIpaqH3000SleeveNvm);

bool CompaqIpaqH3000SleeveNvm::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000SleeveNvm::OnReady() {
    nvm_.assign(kAddressSpace, 0u);
    std::copy(std::begin(kSleeveNvm), std::end(kSleeveNvm), nvm_.begin());
}

uint8_t CompaqIpaqH3000SleeveNvm::Read(const uint8_t* req, uint8_t len, uint8_t* reply) {
    if (len != 3 || req[2] > kCompaqIpaqH3000MicroMaxPayload ||
        AddressOf(req) + req[2] > kAddressSpace) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000SleeveNvm: read request of %u bytes, count %u",
                              len, len > 2 ? req[2] : 0u);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    std::copy_n(nvm_.begin() + AddressOf(req), req[2], reply);
    return req[2];
}

void CompaqIpaqH3000SleeveNvm::Write(const uint8_t* req, uint8_t len) {
    if (len < 2 || AddressOf(req) + (len - 2u) > kAddressSpace) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000SleeveNvm: write request of %u bytes", len);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    std::copy_n(req + 2, len - 2u, nvm_.begin() + AddressOf(req));
}

void CompaqIpaqH3000SleeveNvm::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.WriteBytes("sleeve_nvm", nvm_.data(), nvm_.size());
}

void CompaqIpaqH3000SleeveNvm::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.ReadBytes("sleeve_nvm", nvm_.data(), nvm_.size());
}

}
