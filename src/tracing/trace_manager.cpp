#include "trace_manager.h"

#include <algorithm>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/crc32.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boot/rom_parser_service.h>
#include "../execution/engine_bridge.h"
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/sh/sh_cpu_state.h>

namespace cerfcore {

REGISTER_SERVICE(TraceManager);

std::optional<uint8_t> TraceContext::ReadVa8(uint32_t va) const {
    auto host = emu.Get<EngineBridge>().Guest().PeekGuestVa(va);
    if (!host) return std::nullopt;
    return **host;
}

std::optional<uint16_t> TraceContext::ReadVa16(uint32_t va) const {
    auto lo = ReadVa8(va);
    auto hi = ReadVa8(va + 1);
    if (!lo || !hi) return std::nullopt;
    return static_cast<uint16_t>(*lo | (*hi << 8));
}

std::optional<uint32_t> TraceContext::ReadVa32(uint32_t va) const {
    auto lo = ReadVa16(va);
    auto hi = ReadVa16(va + 2);
    if (!lo || !hi) return std::nullopt;
    return static_cast<uint32_t>(*lo) | (static_cast<uint32_t>(*hi) << 16);
}

void TraceManager::OnReady() {
    bundle_crc32_ = ComputeBundleCrc32();
    LOG(Trace, "bundle CRC32 = 0x%08X (awaiting hook registrations)\n",
        bundle_crc32_);
    if (auto* rom = emu_.TryGet<RomParserService>())
        rom->RegisterReplaceListener([this] { RebindBundles(); });
}

void TraceManager::RebindBundles() {
    for (uint32_t va : bundle_pcs_) {
        pc_traces_.erase(va);
        traced_pcs_.erase(std::lower_bound(traced_pcs_.begin(), traced_pcs_.end(), va));
    }
    bundle_pcs_.clear();
#if CERF_DEV_MODE
    bundle_iter_handlers_ = {};
#endif
    bundles_matched_ = 0;
    bundles_skipped_ = 0;
    bundle_crc32_ = ComputeBundleCrc32();
    LOG(Trace, "bundle CRC32 = 0x%08X after the ROM was replaced\n", bundle_crc32_);
    for (const auto& r : registrations_) Bind(r.expected_crc32, r.register_fn);
}

uint32_t TraceManager::ComputeBundleCrc32() const {
    uint32_t crc = 0;
    if (auto* rom = emu_.TryGet<RomParserService>()) {
        for (const auto& r : rom->Loaded()) {
            const std::span<const uint8_t> image = r.is_fwf ? r.flat : std::span<const uint8_t>(r.raw);
            crc = Crc32::Update(crc, image.data(), image.size());
        }
    }
    return crc;
}

void TraceManager::RegisterForBundle(
        uint32_t expected_crc32,
        const std::function<void()>& register_fn) {
    registrations_.push_back({expected_crc32, register_fn});
    Bind(expected_crc32, register_fn);
}

void TraceManager::Bind(uint32_t expected_crc32, const std::function<void()>& register_fn) {
    if (expected_crc32 != bundle_crc32_) {
        ++bundles_skipped_;
        LOG(Trace, "skipping hook file (expected CRC 0x%08X, bundle is 0x%08X)\n",
            expected_crc32, bundle_crc32_);
        return;
    }
    ++bundles_matched_;
    binding_bundle_ = true;
    register_fn();
    binding_bundle_ = false;
}

void TraceManager::GuardUnique(uint32_t runtime_va) {
    if (pc_traces_.count(runtime_va) == 0) return;
    emu_.Get<Fatal>().Die("TraceManager duplicate registration at runtime_va=0x%08X - a "
                          "trace handler is already bound at this guest PC. A VA may be "
                          "hooked exactly once, by OnPc or OnPcFiltered (not both, not "
                          "twice).", runtime_va);
}

void TraceManager::NoteTracedPc(uint32_t runtime_va) {
    if (binding_bundle_) bundle_pcs_.push_back(runtime_va);
    traced_pcs_.insert(std::lower_bound(traced_pcs_.begin(), traced_pcs_.end(), runtime_va),
                       runtime_va);
}

void TraceManager::OnPc(uint32_t runtime_va, TraceHandler handler) {
    GuardUnique(runtime_va);
    pc_traces_[runtime_va] = {std::nullopt, std::move(handler)};
    NoteTracedPc(runtime_va);
}

void TraceManager::OnPcFiltered(uint32_t       runtime_va,
                                TracePredicate predicate,
                                TraceHandler   handler) {
    GuardUnique(runtime_va);
    pc_traces_[runtime_va] =
        {std::optional<TracePredicate>{std::move(predicate)},
         std::move(handler)};
    NoteTracedPc(runtime_va);
}

bool TraceManager::HasPcTrace(uint32_t pc) const {
    if (pc_traces_.empty()) return false;
    return pc_traces_.count(pc) > 0;
}

bool TraceManager::HasPcTraceIn(uint32_t first_pc, uint32_t last_pc) const {
    if (traced_pcs_.empty()) return false;
    const auto it = std::lower_bound(traced_pcs_.begin(), traced_pcs_.end(), first_pc);
    return it != traced_pcs_.end() && *it <= last_pc;
}

void TraceManager::DispatchContext(uint32_t pc, const TraceContext& ctx) {
    auto it = pc_traces_.find(pc);
    if (it == pc_traces_.end()) return;
    const PcEntry& e = it->second;
    if (e.predicate.has_value() && !(*e.predicate)(ctx)) return;
    e.handler(ctx);
}

void TraceManager::DispatchPc(uint32_t pc,
                              const uint32_t* regs, uint32_t cpsr) {
    TraceContext ctx{regs, cpsr, pc, emu_};
    DispatchContext(pc, ctx);
}

void TraceManager::DispatchPcMips(uint32_t pc, const cerfengine::MipsCpuState* st) {
    TraceContext ctx{nullptr, 0u, pc, emu_, st};
    DispatchContext(pc, ctx);
}

void TraceManager::DispatchPcSh(uint32_t pc, const cerfengine::ShCpuState* st) {
    TraceContext ctx{nullptr, 0u, pc, emu_, nullptr, st};
    DispatchContext(pc, ctx);
}

#if CERF_DEV_MODE

void TraceManager::OnRunLoopIter(TraceHandler handler) {
    (binding_bundle_ ? bundle_iter_handlers_ : iter_handlers_).Add(std::move(handler));
}

void TraceManager::DispatchIterContext(const TraceContext& ctx) {
    iter_handlers_.Call(ctx);
    bundle_iter_handlers_.Call(ctx);
}

void TraceManager::DispatchRunLoopIter(const uint32_t* regs, uint32_t cpsr) {
    if (iter_handlers_.Empty() && bundle_iter_handlers_.Empty()) return;
    TraceContext ctx{regs, cpsr, regs[15], emu_};
    DispatchIterContext(ctx);
}

void TraceManager::DispatchRunLoopIterMips(const cerfengine::MipsCpuState* st) {
    if (iter_handlers_.Empty() && bundle_iter_handlers_.Empty()) return;
    TraceContext ctx{nullptr, 0u, st->pc, emu_, st};
    DispatchIterContext(ctx);
}

void TraceManager::DispatchRunLoopIterSh(const cerfengine::ShCpuState* st) {
    if (iter_handlers_.Empty() && bundle_iter_handlers_.Empty()) return;
    TraceContext ctx{nullptr, 0u, st->pc, emu_, nullptr, st};
    DispatchIterContext(ctx);
}

#endif  /* CERF_DEV_MODE */

}
