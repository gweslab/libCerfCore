#include "adreno_a2xx_texture.h"
#include "adreno_a2xx_float.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_texture_layout.h"
#include "adreno_a2xx_tiling.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "adreno_a2xx_socs.h"
#include <algorithm>
#include <cmath>
#include <bit>

namespace cerfcore {

namespace {

template <uint32_t kMax>
const std::array<float, kMax + 1u>& UnormTable() {
    static const std::array<float, kMax + 1u> table = [] {
        std::array<float, kMax + 1u> values{};
        for (uint32_t i = 0; i <= kMax; ++i) values[i] = float(i) / float(kMax);
        return values;
    }();
    return table;
}

}

REGISTER_SERVICE(AdrenoA2xxTexture);
bool AdrenoA2xxTexture::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

/* Mesa e97ad748, a2xx.xml: A2XX_SQ_TEX; instr-a2xx.h: instr_fetch_tex_t;
   fd2_gmem.c: emit_mem2gmem_surf. */
AdrenoA2xxVec4 AdrenoA2xxTexture::Sample(const std::unordered_map<uint32_t,uint32_t>& registers,
    const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels, const std::array<uint32_t,6>* descriptor,
    uint32_t slot, const AdrenoA2xxVec4& coordinates, std::array<uint32_t,3> instruction,
    const AdrenoA2xxVec4* dx, const AdrenoA2xxVec4* dy, float register_lod) {
    const AdrenoA2xxSampleRequest request{coordinates, dx, dy, register_lod};
    AdrenoA2xxVec4 result{};
    SampleLanes(registers, mmu_config, texels, descriptor, slot, instruction, {&request, 1u}, {&result, 1u});
    return result;
}

void AdrenoA2xxTexture::SampleLanes(const std::unordered_map<uint32_t,uint32_t>& registers,
    const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels, const std::array<uint32_t,6>* descriptor,
    uint32_t slot, std::array<uint32_t,3> instruction, std::span<const AdrenoA2xxSampleRequest> requests,
    std::span<AdrenoA2xxVec4> results) {
    auto fail = [&](const char* reason, uint32_t value) {
        emu_.Get<Fatal>().Die("GPU texture %s slot=%u value=%08X", reason, slot, value);
    };
    if (slot >= 32u) fail("slot", slot);
    const bool query_weights = (instruction[0] & 31u) == 19u;
    const bool query_border = (instruction[0] & 31u) == 16u;
    std::array<uint32_t,6> state{};
    if (descriptor) state = *descriptor;
    else for (uint32_t i = 0; i < state.size(); ++i) {
        const auto found = registers.find(0x4800u + slot * 6u + i);
        if (found == registers.end()) fail("missing descriptor", i);
        state[i] = found->second;
    }
    constexpr uint32_t kFetchConstantTexture = 2u;
    constexpr uint32_t kTexturePageMask      = 0xFFFFF000u;
    if ((state[0] & 3u) != kFetchConstantTexture ||
        ((state[1] | state[5]) & kTexturePageMask) == 0u) {
        for (auto& result : results) result = AdrenoA2xxVec4{};
        return;
    }
    const uint32_t format = state[1] & 63u, pitch = ((state[0] >> 22) & 511u) * 32u;
    const uint32_t width = (state[2] & 8191u) + 1u, height = ((state[2] >> 13) & 8191u) + 1u;
    const uint32_t clamp_x = (state[0] >> 10) & 7u, clamp_y = (state[0] >> 13) & 7u;
    const bool tiled = (state[0] & 0x80000000u) != 0;
    const uint32_t dimension = (state[5] >> 9) & 3u;
    const bool cube = dimension == 3u;
    const bool sample_fetch = (instruction[0] & 31u) == 1u;
    const uint32_t fetch_dimension = (instruction[2] >> 14) & 3u;
    if (fetch_dimension != 0u && (!sample_fetch || fetch_dimension != dimension))
        fail("unsupported fetch dimension", instruction[2]);
    if ((state[0] & 0x000003FDu) != 0 || ((state[1] >> 6) & 15u) != 0 || (state[3] & 1u) != 0)
        fail("unsupported type/sign/endian", state[0]);
    if ((dimension != 1u && !cube) || (state[2] >> 26) != 0 || pitch < width)
        fail("unsupported dimension/pitch", state[5]);
    auto compute_lod = [&](const AdrenoA2xxSampleRequest& request) {
        const AdrenoA2xxVec4* dx = request.dx;
        const AdrenoA2xxVec4* dy = request.dy;
        if (!dx || !dy) fail("unavailable texture gradients",instruction[0]);
        for (unsigned i = 0; i < 2u; ++i)
            if (!AdrenoA2xxFinite((*dx)[i]) || !AdrenoA2xxFinite((*dy)[i])) fail("nonfinite texture gradients",slot);
        const double sx = (instruction[0] & (1u << 25)) ? 1.0 : double(width);
        const double sy = (instruction[0] & (1u << 25)) ? 1.0 : double(height);
        /* Khronos GLES 2.0.25 section 3.7.7, equation 3.12. */
        const double rho = (std::max)(std::hypot((*dx)[0]*sx,(*dx)[1]*sy),
                                     std::hypot((*dy)[0]*sx,(*dy)[1]*sy));
        return rho > 0.0 ? std::log2(rho) : -INFINITY;
    };
    if ((instruction[0] & 31u) == 17u) {
        const uint32_t aniso = (instruction[1] >> 18) & 7u;
        if (cube || (instruction[2] & 0x7FFF3FFDu) || (instruction[1] & 0x60000000u) ||
            (aniso != 0u && aniso != 7u)) fail("unsupported LOD query controls",instruction[1]);
        for (size_t lane = 0; lane < requests.size(); ++lane)
            results[lane] = {static_cast<float>(compute_lod(requests[lane])),0,0,0};
        return;
    }
    if ((clamp_x != 0u && clamp_x != 1u && clamp_x != 2u) ||
        (clamp_y != 0u && clamp_y != 1u && clamp_y != 2u)) fail("unsupported clamp", state[0]);
    if ((state[4] & 0x003FFC3Cu) != 0) fail("unsupported descriptor LOD/aniso-walk state", state[4]);
    if ((state[3] & 0xFE07E000u) != 0) fail("unsupported descriptor exponent/aniso/border", state[3]);
    if ((instruction[2] & 0x7FFF3FFCu) != 0) fail("unsupported fetch LOD bias/offset", instruction[2]);
    const uint32_t aniso = (instruction[1] >> 18) & 7u, arbitrary = (instruction[1] >> 21) & 7u;
    const uint32_t reg_lod = (instruction[1] >> 29) & 3u;
    const bool computed_lod = (instruction[1] & (1u << 28)) != 0 || (instruction[2] & 1u) != 0;
    if ((aniso != 0u && aniso != 7u) || (arbitrary != 0u && arbitrary != 7u) ||
        reg_lod > 1u) fail("unsupported anisotropy/register LOD",instruction[1]);
    if (reg_lod)
        for (const auto& request : requests)
            if (std::isnan(request.register_lod)) fail("NaN register LOD",instruction[1]);
    auto filter = [&](uint32_t shift, uint32_t constant_shift) {
        const uint32_t selected = (instruction[1] >> shift) & 3u;
        return selected == 3u ? (state[3] >> constant_shift) & 3u : selected;
    };
    const uint32_t mag = filter(12u,19u), min = filter(14u,21u), mip = filter(16u,23u);
    const uint32_t mip_max_level = (state[4] >> 6) & 15u;
    const bool mipmapped = mip <= 1u && mip_max_level != 0u;
    if (mag > 1u || mag != min || mip > 2u) fail("unsupported filter",instruction[1]);
    const AdrenoA2xxTextureLayout layout{state};
    const uint32_t bytes = layout.Bytes();
    if (bytes == 0u) fail("unsupported format", format);
    if (cube) {
        if (tiled || mipmapped || width != height || (state[5] & 0xFFFu) != 0x600u ||
            clamp_x != 2u || clamp_y != 2u || (instruction[0] & (1u << 25)))
            fail("unsupported cube layout/filter",state[5]);
    }
    const uint32_t last_level = layout.LastLevel();
    if (mipmapped) {
        /* i.MX51 libGLESv2.so.2 rb_init_tile_info 0xE7AA8: uncompressed bytes-per-texel layout;
           NXP yamato_enum.h: FMT_8_8_8_8=6, FMT_8_8=10. */
        const char* invalid = !tiled ? "mip linear layout" : (format != 10u && format != 6u) ? "mip format" :

            (width < 32u || width != height || !std::has_single_bit(width)) ? "mip dimensions" : pitch != width ? "mip pitch" :
            state[4] != (last_level << 6) ? "mip levels/LOD state" : (state[5] & 0xFFFu) != 0xA00u ? "mip packing controls" :
            (instruction[0] & (1u << 25)) ? "mip denormalized coordinates" : nullptr;
        if (invalid) fail(invalid,state[5]);
    }
    const auto& unorm8 = UnormTable<255u>();
    const auto& unorm6 = UnormTable<63u>();
    const auto& unorm5 = UnormTable<31u>();
    const auto& unorm4 = UnormTable<15u>();
    std::array<uint32_t, 4> swizzles{};
    for (unsigned c = 0; c < 4; ++c) swizzles[c] = (state[3] >> (1u + c * 3u)) & 7u;
    bool swizzles_checked = false;
    auto& memory = emu_.Get<AdrenoA2xxMemory>();
    const size_t count = requests.size();
    using LaneValues   = std::array<float, 4>;
    using LaneChannels = std::array<LaneValues, 4>;
    std::array<uint32_t, 4> face{};
    std::array<double, 4> lod{};
    for (size_t lane = 0; lane < count; ++lane) {
        const AdrenoA2xxSampleRequest& request = requests[lane];
        if (cube) {
            const float selected = request.coordinates[2];
            if (!std::isfinite(selected) || selected < 0 || selected > 5 || selected != std::floor(selected))
                fail("cube face",slot);
            face[lane] = static_cast<uint32_t>(selected);
        }
        double value = 0;
        if (mipmapped) {
            if (computed_lod) value = compute_lod(request);
            if (reg_lod) value += request.register_lod;
            if (std::isnan(value)) fail("indeterminate combined LOD",instruction[1]);
            value = std::clamp(value, 0.0, double(last_level));
        }
        // Mesa instr-a2xx.h TEX_FILTER_POINT; GLES 2.0 equation 3.17:
        // nearest-mipmap filters select the closest level, with ties going lower.
        if (mip == 0u && value != 0.0) value = (std::max)(0.0, std::ceil(value + 0.5) - 1.0);
        lod[lane] = value;
    }
    auto reduce = [](double x, uint32_t size, uint32_t clamp) {
        if (clamp == 2u) return std::clamp(x, 0.0, double(size));
        const double period = double(size) * (clamp == 1u ? 2.0 : 1.0);
        return x - std::floor(x / period) * period;
    };
    auto index = [](int value, uint32_t size, uint32_t clamp) {
        if (static_cast<uint32_t>(value) < size) return value;
        const int n = static_cast<int>(size);
        if (clamp == 2u) return std::clamp(value, 0, n - 1);
        const int period = clamp == 1u ? n * 2 : n;
        int wrapped = value % period; if (wrapped < 0) wrapped += period;
        return wrapped >= n ? period - wrapped - 1 : wrapped;
    };
    struct LevelLane : AdrenoA2xxTextureLevel {
        double         u      = 0, v = 0;
        const uint8_t* data   = nullptr;
    };
    auto sample_levels = [&](const std::array<uint32_t, 4>& levels) {
        std::array<LevelLane, 4> lanes{};
        for (size_t lane = 0; lane < count; ++lane) {
            const uint32_t level = levels[lane];
            LevelLane& s = lanes[lane];
            static_cast<AdrenoA2xxTextureLevel&>(s) = layout.Level(level, face[lane]);
            if (s.base > UINT32_MAX) fail("mip address overflow",level);
            double u = requests[lane].coordinates[0], v = requests[lane].coordinates[1];
            if (!AdrenoA2xxFinite(u) || !AdrenoA2xxFinite(v)) fail("nonfinite coordinate", instruction[0]);
            // Mesa ir2_nir emits CUBE, reciprocal major axis, +1.5, then YXW fetch.
            if (cube) { u -= 1.0; v -= 1.0; }
            if ((instruction[0] & (1u << 25)) == 0) { u *= s.width; v *= s.height; }
            s.u = reduce(u,s.width,clamp_x);
            s.v = reduce(v,s.height,clamp_y);
        }
        LaneChannels out{};
        if (query_border) return out;
        if (query_weights) {
            for (size_t lane = 0; lane < count; ++lane) {
                const LevelLane& s = lanes[lane];
                out[0][lane] = mag ? static_cast<float>(s.u - 0.5 - std::floor(s.u - 0.5)) : 0.0f;
                out[1][lane] = mag ? static_cast<float>(s.v - 0.5 - std::floor(s.v - 0.5)) : 0.0f;
                out[3][lane] = static_cast<float>(lod[lane] - std::floor(lod[lane]));
            }
            return out;
        }
        if (!tiled)
            for (size_t lane = 0; lane < count; ++lane)
                lanes[lane].data = memory.UntranslatedRead(lanes[lane].base,
                    uint64_t(height - 1u) * pitch * bytes + uint64_t(width) * bytes, mmu_config);
        const unsigned taps = mag == 0u ? 1u : 4u;
        std::array<std::array<const uint8_t*, 4>, 4> texel{};
        std::array<std::array<std::array<uint8_t, 4>, 4>, 4> copies{};
        LaneValues fx{}, fy{};
        for (size_t lane = 0; lane < count; ++lane) {
            const LevelLane& s = lanes[lane];
            int x = 0, y = 0;
            if (mag == 0u) {
                x = static_cast<int>(std::floor(s.u));
                y = static_cast<int>(std::floor(s.v));
            } else {
                const double u = s.u - 0.5, v = s.v - 0.5;
                x = static_cast<int>(std::floor(u));
                y = static_cast<int>(std::floor(v));
                fx[lane] = static_cast<float>(u - x);
                fy[lane] = static_cast<float>(v - y);
            }
            for (unsigned tap = 0; tap < taps; ++tap) {
                const int tx = index(x + int(tap & 1u),s.width,clamp_x);
                const int ty = index(y + int(tap >> 1),s.height,clamp_y);
                const uint64_t offset = (uint64_t(ty) * pitch + static_cast<uint32_t>(tx)) * bytes;
                const uint8_t* p = s.data ? s.data + offset : nullptr;
                if (!p) {
                    const uint64_t address = tiled ? AdrenoA2xxTiledAddress(static_cast<uint32_t>(s.base),s.pitch,bytes,
                        static_cast<uint32_t>(tx)+s.mip_x,static_cast<uint32_t>(ty)+s.mip_y) : s.base + offset;
                    if (texels) p = memory.CachedRead(*texels, address, bytes, mmu_config);
                    if (!p) {
                        memory.ReadBytes(address, bytes, mmu_config, copies[tap][lane].data());
                        p = copies[tap][lane].data();
                    }
                }
                texel[tap][lane] = p;
            }
        }
        if (!swizzles_checked) {
            for (unsigned c = 0; c < 4; ++c) if (swizzles[c] > 5u) fail("unsupported swizzle", swizzles[c]);
            swizzles_checked = true;
        }
        std::array<LaneChannels, 4> raw{};
        for (unsigned tap = 0; tap < taps; ++tap)
            for (size_t lane = 0; lane < count; ++lane) {
                const uint8_t* p = texel[tap][lane];
                AdrenoA2xxVec4 channels{};
                if (bytes == 4u) for (unsigned c = 0; c < 4; ++c) channels[c] = unorm8[p[c]];
                else if (bytes == 2u) {
                    const uint32_t packed = cerf::le::U16(p);
                    /* Mesa e97ad748, fd2_util.c: pipe2surface, CASE(8,8,0,0), FMT_8_8. */
                    if (format == 10u) channels = {unorm8[p[0]],unorm8[p[1]],0.0f,1.0f};
                    else if (format == 15u) for (unsigned i=0;i<4;++i) channels[i]=unorm4[(packed>>(i*4u))&15u];
                    else channels = {unorm5[packed & 31u],unorm6[(packed >> 5) & 63u],unorm5[(packed >> 11) & 31u],1.0f};
                } else channels = {unorm8[p[0]],0.0f,0.0f,1.0f};
                for (unsigned c = 0; c < 4; ++c) raw[tap][c][lane] = channels[c];
            }
        auto component = [&](unsigned tap, unsigned c) {
            if (swizzles[c] < 4u) return raw[tap][swizzles[c]];
            const float constant = swizzles[c] == 5u ? 1.0f : 0.0f;
            return LaneValues{constant, constant, constant, constant};
        };
        for (unsigned c = 0; c < 4; ++c)
            out[c] = mag == 0u ? component(0, c) :
                AdrenoA2xxLerpLanes(AdrenoA2xxLerpLanes(component(0, c), component(1, c), fx),
                                    AdrenoA2xxLerpLanes(component(2, c), component(3, c), fx), fy);
        return out;
    };
    std::array<uint32_t, 4> lower{};
    LaneValues weight{};
    std::array<bool, 4> upper{};
    bool any_upper = false;
    for (size_t lane = 0; lane < count; ++lane) {
        lower[lane]  = static_cast<uint32_t>(std::floor(lod[lane]));
        upper[lane]  = !query_weights && !query_border && lod[lane] > lower[lane];
        weight[lane] = static_cast<float>(lod[lane] - lower[lane]);
        any_upper   |= upper[lane];
    }
    LaneChannels result = sample_levels(lower);
    if (any_upper) {
        std::array<uint32_t, 4> next = lower;
        for (size_t lane = 0; lane < count; ++lane) if (upper[lane]) next[lane] = lower[lane] + 1u;
        const LaneChannels high = sample_levels(next);
        for (unsigned c = 0; c < 4; ++c) {
            const LaneValues blended = AdrenoA2xxLerpLanes(result[c], high[c], weight);
            for (size_t lane = 0; lane < count; ++lane) if (upper[lane]) result[c][lane] = blended[lane];
        }
    }
    for (size_t lane = 0; lane < count; ++lane)
        results[lane] = {result[0][lane], result[1][lane], result[2][lane], result[3][lane]};
}

}
