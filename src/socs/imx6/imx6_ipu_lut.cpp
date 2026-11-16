#include "imx6_ipu_lut.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx6_id.h"

namespace cerfcore {

bool Imx6IpuLut::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

bool Imx6IpuLut::ContainsOffset(uint32_t offset) const {
    return offset >= kIpuLutCpuOffsetStub && offset < kIpuLutCpuOffsetStub + pixels_.size() * sizeof(uint32_t);
}

uint8_t Imx6IpuLut::ReadByte(uint32_t offset) const {
    const uint32_t relative = offset - kIpuLutCpuOffsetStub;
    return static_cast<uint8_t>(pixels_[relative >> 2] >> ((relative & 3u) * 8u));
}

uint16_t Imx6IpuLut::ReadHalf(uint32_t offset) const {
    const uint32_t relative = offset - kIpuLutCpuOffsetStub;
    return static_cast<uint16_t>(pixels_[relative >> 2] >> ((relative & 2u) * 8u));
}

uint32_t Imx6IpuLut::ReadWord(uint32_t offset) const {
    return pixels_[(offset - kIpuLutCpuOffsetStub) >> 2];
}

void Imx6IpuLut::WriteByte(uint32_t offset, uint8_t value) {
    const uint32_t relative = offset - kIpuLutCpuOffsetStub;
    const uint32_t shift = (relative & 3u) * 8u;
    uint32_t& pixel = pixels_[relative >> 2];
    pixel = (pixel & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
}

void Imx6IpuLut::WriteHalf(uint32_t offset, uint16_t value) {
    const uint32_t relative = offset - kIpuLutCpuOffsetStub;
    const uint32_t shift = (relative & 2u) * 8u;
    uint32_t& pixel = pixels_[relative >> 2];
    pixel = (pixel & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift);
}

void Imx6IpuLut::WriteWord(uint32_t offset, uint32_t value) {
    pixels_[(offset - kIpuLutCpuOffsetStub) >> 2] = value;
}

void Imx6IpuLut::Reset() {
    pixels_.fill(0u);
}

uint32_t Imx6IpuLut::Decode(uint32_t pixel, uint32_t dec_sel, bool four_bit) const {
    const uint32_t index = four_bit ? (dec_sel << 6) | pixel : pixel;
    return pixels_[index];
}

void Imx6IpuLut::SaveState(StateWriter& writer) {
    writer.WriteBytes("pixels", pixels_.data(), sizeof(pixels_));
}

void Imx6IpuLut::RestoreState(StateReader& reader) {
    reader.ReadBytes("pixels", pixels_.data(), sizeof(pixels_));
}

REGISTER_SERVICE(Imx6IpuLut);

}
