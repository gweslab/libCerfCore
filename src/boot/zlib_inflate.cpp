#include "zlib_inflate.h"

#include <cerfcore/core/byte_order.h>

#include <algorithm>

namespace cerfcore {

namespace cerf::zlib_inflate {
namespace {

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

constexpr uint32_t kMaxBits = 15u;
constexpr uint32_t kSlots = 1u << kMaxBits;

struct Slot {
    uint16_t symbol = 0;
    uint8_t length = 0;
};

class PrefixTable {
public:
    PrefixTable() : slots_(kSlots) {}

    bool Assign(const uint8_t* lengths, uint32_t symbols) {
        std::fill(slots_.begin(), slots_.end(), Slot{});
        uint32_t bl_count[kMaxBits + 1] = {0};
        for (uint32_t n = 0; n < symbols; ++n) {
            if (lengths[n] > kMaxBits) return false;
            ++bl_count[lengths[n]];
        }
        bl_count[0] = 0;
        uint32_t next_code[kMaxBits + 1] = {0};
        uint32_t code = 0;
        for (uint32_t bits = 1; bits <= kMaxBits; ++bits) {
            code = (code + bl_count[bits - 1u]) << 1u;
            next_code[bits] = code;
        }
        for (uint32_t n = 0; n < symbols; ++n) {
            const uint32_t len = lengths[n];
            if (len == 0u) continue;
            const uint32_t assigned = next_code[len]++;
            if (assigned >= (1u << len)) return false;
            const uint32_t shift = kMaxBits - len;
            for (uint32_t slot = assigned << shift; slot < ((assigned + 1u) << shift); ++slot) {
                if (slots_[slot].length != 0u) return false;
                slots_[slot].symbol = static_cast<uint16_t>(n);
                slots_[slot].length = static_cast<uint8_t>(len);
            }
        }
        return true;
    }

    const Slot& At(uint32_t prefix, uint32_t len) const { return slots_[prefix << (kMaxBits - len)]; }

private:
    std::vector<Slot> slots_;
};

class BitStream {
public:
    BitStream(const uint8_t* src, size_t size) : src_(src), bit_limit_(size * 8u) {}

    bool Bit(uint32_t& out) {
        if (bit_ >= bit_limit_) return false;
        out = (src_[bit_ >> 3u] >> (bit_ & 7u)) & 1u;
        ++bit_;
        return true;
    }

    bool Field(uint32_t width, uint32_t& out) {
        out = 0;
        for (uint32_t i = 0; i < width; ++i) {
            uint32_t bit = 0;
            if (!Bit(bit)) return false;
            out |= bit << i;
        }
        return true;
    }

    bool Decode(const PrefixTable& table, uint32_t& out) {
        uint32_t prefix = 0;
        for (uint32_t len = 1; len <= kMaxBits; ++len) {
            uint32_t bit = 0;
            if (!Bit(bit)) return false;
            prefix = (prefix << 1u) | bit;
            const Slot& slot = table.At(prefix, len);
            if (slot.length == len) {
                out = slot.symbol;
                return true;
            }
        }
        return false;
    }

    void SkipToByte() { bit_ = (bit_ + 7u) & ~static_cast<size_t>(7u); }

    bool Bytes(size_t n, const uint8_t*& at) {
        if (n > (bit_limit_ - bit_) / 8u) return false;
        at = src_ + bit_ / 8u;
        bit_ += n * 8u;
        return true;
    }

    size_t BytesUsed() const { return (bit_ + 7u) / 8u; }

private:
    const uint8_t* src_;
    size_t bit_limit_;
    size_t bit_ = 0;
};

bool FixedTables(PrefixTable& lit, PrefixTable& dist) {
    uint8_t lengths[288];
    std::fill(lengths, lengths + 144, uint8_t{8});
    std::fill(lengths + 144, lengths + 256, uint8_t{9});
    std::fill(lengths + 256, lengths + 280, uint8_t{7});
    std::fill(lengths + 280, lengths + 288, uint8_t{8});
    uint8_t dist_lengths[30];
    std::fill(dist_lengths, dist_lengths + 30, uint8_t{5});
    return lit.Assign(lengths, 288u) && dist.Assign(dist_lengths, 30u);
}

bool DynamicTables(BitStream& in, PrefixTable& lit, PrefixTable& dist) {
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!in.Field(5u, hlit) || !in.Field(5u, hdist) || !in.Field(4u, hclen)) return false;
    const uint32_t lit_count = hlit + 257u;
    const uint32_t dist_count = hdist + 1u;
    if (lit_count > 286u || dist_count > 30u) return false;

    uint8_t cl_lengths[19] = {0};
    for (uint32_t i = 0; i < hclen + 4u; ++i) {
        uint32_t length = 0;
        if (!in.Field(3u, length)) return false;
        cl_lengths[kClOrder[i]] = static_cast<uint8_t>(length);
    }
    PrefixTable cl;
    if (!cl.Assign(cl_lengths, 19u)) return false;

    const uint32_t total = lit_count + dist_count;
    uint8_t lengths[286 + 30] = {0};
    uint32_t filled = 0;
    bool has_previous = false;
    uint8_t previous = 0;
    while (filled < total) {
        uint32_t sym = 0;
        if (!in.Decode(cl, sym)) return false;
        uint32_t run = 1;
        uint8_t value = static_cast<uint8_t>(sym);
        if (sym == 16u) {
            if (!has_previous || !in.Field(2u, run)) return false;
            run += 3u;
            value = previous;
        } else if (sym == 17u) {
            if (!in.Field(3u, run)) return false;
            run += 3u;
            value = 0;
        } else if (sym == 18u) {
            if (!in.Field(7u, run)) return false;
            run += 11u;
            value = 0;
        }
        if (run > total - filled) return false;
        std::fill(lengths + filled, lengths + filled + run, value);
        filled += run;
        previous = value;
        has_previous = true;
    }
    return lit.Assign(lengths, lit_count) && dist.Assign(lengths + lit_count, dist_count);
}

bool CopyMatch(BitStream& in, const PrefixTable& dist, uint32_t code, std::vector<uint8_t>& out, size_t out_size) {
    const uint32_t len_index = code - 257u;
    if (len_index >= 29u) return false;
    uint32_t extra = 0;
    if (!in.Field(kLenExtra[len_index], extra)) return false;
    const uint32_t length = kLenBase[len_index] + extra;
    uint32_t dist_index = 0;
    if (!in.Decode(dist, dist_index) || dist_index >= 30u) return false;
    if (!in.Field(kDistExtra[dist_index], extra)) return false;
    const size_t distance = kDistBase[dist_index] + extra;
    if (distance > out.size() || length > out_size - out.size()) return false;
    for (uint32_t i = 0; i < length; ++i)
        out.push_back(out[out.size() - distance]);
    return true;
}

bool InflateBlocks(BitStream& in, std::vector<uint8_t>& out, size_t out_size) {
    PrefixTable lit, dist;
    uint32_t last = 0;
    do {
        uint32_t btype = 0;
        if (!in.Field(1u, last) || !in.Field(2u, btype)) return false;
        if (btype == 0u) {
            in.SkipToByte();
            const uint8_t* header = nullptr;
            if (!in.Bytes(4u, header)) return false;
            const uint32_t len = cerf::le::U16(header);
            const uint32_t nlen = cerf::le::U16(header, 2);
            const uint8_t* data = nullptr;
            if (nlen != (~len & 0xFFFFu) || len > out_size - out.size() || !in.Bytes(len, data)) return false;
            out.insert(out.end(), data, data + len);
            continue;
        }
        if (btype == 3u) return false;
        if (!(btype == 1u ? FixedTables(lit, dist) : DynamicTables(in, lit, dist))) return false;
        for (;;) {
            uint32_t code = 0;
            if (!in.Decode(lit, code)) return false;
            if (code == 256u) break;
            if (code > 256u) {
                if (!CopyMatch(in, dist, code, out, out_size)) return false;
                continue;
            }
            if (out.size() == out_size) return false;
            out.push_back(static_cast<uint8_t>(code));
        }
    } while (last == 0u);
    return true;
}

bool HasZlibHeader(const uint8_t* src, size_t src_size) {
    return src && src_size >= 2u && (src[0] & 0x0Fu) == 8u &&
           cerf::be::U16(src) % 31u == 0u && (src[1] & 0x20u) == 0u;
}

uint32_t Adler32(const std::vector<uint8_t>& data) {
    uint32_t s1 = 1u, s2 = 0u;
    for (uint8_t byte : data) {
        s1 = (s1 + byte) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16u) | s1;
}

}

std::vector<uint8_t> Decompress(const uint8_t* src, size_t src_size, size_t out_size, size_t* consumed) {
    if (!HasZlibHeader(src, src_size)) return {};

    std::vector<uint8_t> out;
    out.reserve(out_size);
    BitStream in(src + 2u, src_size - 2u);
    if (!InflateBlocks(in, out, out_size) || out.size() != out_size) return {};
    const size_t trailer = 2u + in.BytesUsed();
    if (src_size - trailer < 4u) return {};
    const uint32_t stored = cerf::be::U32(src, trailer);
    if (stored != Adler32(out)) return {};
    if (consumed) *consumed = trailer + 4u;
    return out;
}

}

}
