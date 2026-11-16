#include "siemens_simatic_mobile_panels_gen2_f_module_device.h"
#include <cerfcore/core/host_fs_path.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/platform/host_file.h>
#include "../../core/ascii_case.h"
#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "../../socs/imx6/imx6_gpio_pads.h"
#include "../../socs/imx6/imx6_iomuxc.h"
#include "../../socs/imx6/imx6_variant.h"
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_firmware.h"
#include "siemens_simatic_mobile_panels_gen2_fwf_fsf_container.h"

#include <cstring>
#include <algorithm>
#include <fstream>
#include <vector>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/crc32.h>

namespace cerfcore {

namespace {

constexpr int64_t kSpeculationWindowNs = 1000000;
constexpr int64_t kStoreIntervalNs = 1000000000;

struct SpeculativeFault {};

}

SiemensSimaticMobilePanelsGen2FModuleDevice::~SiemensSimaticMobilePanelsGen2FModuleDevice() = default;

bool SiemensSimaticMobilePanelsGen2FModuleDevice::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    if (!board) return false;
    const auto* profile = TrySiemensSimaticMobilePanelsGen2BoardProfileFor(board->GetBoardId());
    return profile && profile->has_f_module;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::OnReady() {
    const auto& config = emu_.Get<DeviceConfig>();
    const std::string fwf_path = ResolveDeviceFile(config.device_name, config.rom_primary);
    std::ifstream input(HostFsPath(fwf_path), std::ios::binary | std::ios::ate);
    if (!input.good())
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot open FWF '%s'", fwf_path.c_str());
    const std::streamoff size = input.tellg();
    if (size <= 0)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: empty FWF '%s'", fwf_path.c_str());
    std::vector<uint8_t> fwf(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(fwf.data()), size);
    if (!input)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot read FWF '%s'", fwf_path.c_str());

    std::vector<siemens_simatic_mobile_panels_gen2_fwf::FsfEntry> addon_files;
    std::string error;
    if (!siemens_simatic_mobile_panels_gen2_fwf::ParseFsfVolume(fwf, addon_files, error))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: the AddOn files of '%s' cannot be read: %s", fwf_path.c_str(),
                              error.c_str());
    std::vector<uint8_t> firmware;
    unsigned firmware_matches = 0;
    for (const auto& entry : addon_files) {
        if (cerf::AsciiEqualIgnoreCase(entry.dir, "AddOn") &&
            cerf::AsciiEqualIgnoreCase(entry.name, "komp2.upd")) {
            firmware = entry.data;
            ++firmware_matches;
        }
    }
    if (firmware_matches != 1u) {
        emu_.Get<Fatal>().Die(
            "Mobile Panels 2nd Gen F-module: expected one \\Flash\\AddOn\\komp2.upd in '%s', found %u",
            fwf_path.c_str(), firmware_matches);
    }
    std::vector<uint8_t> application;
    if (!siemens_simatic_mobile_panels_gen2::ExtractApplication(firmware.data(), firmware.size(), application))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: invalid komp2.upd in '%s'", fwf_path.c_str());
    selected_container_crc32_ = Crc32::Update(0u, firmware.data(), firmware.size());

    hardware_ = std::make_unique<siemens_simatic_mobile_panels_gen2::FModuleHardware>(
        emu_, static_cast<siemens_simatic_mobile_panels_gen2::FModuleHardwareHost&>(*this));
    flash_path_ = ResolveDeviceFile(config.device_name, kFlashImageName);
    if (HostFileNonEmpty(flash_path_)) {
        LoadFlashImage();
    } else {
        if (!hardware_->LoadApplication(application.data(), application.size()))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: the komp2.upd application of %u bytes in '%s' does not fit "
                                  "the MCU Flash",
                                  static_cast<unsigned>(application.size()), fwf_path.c_str());
        StoreFlashImage();
        LOG(Board, "Mobile Panels 2nd Gen F-module: komp2.upd application of %u bytes loaded into both MCUs\n",
            static_cast<unsigned>(application.size()));
    }
    hardware_->Reset();
    clock_ = &emu_.Get<GuestCycleClock>();
    tick_ = clock_->Add([this] { OnTick(); });
    edge_ = clock_->Add([this] { OnEdge(); });
    clock_->RegisterRateListener([this] {
        ArmAt(tick_, window_end_ns_);
        if (!edges_.empty()) ArmAt(edge_, edges_.front().ns);
    });
    base_ns_ = clock_->NowNs();
    delivered_ = hardware_->Lines();
    Resume();
}

std::vector<std::vector<uint8_t>*> SiemensSimaticMobilePanelsGen2FModuleDevice::FlashRegions() {
    std::vector<std::vector<uint8_t>*> regions;
    for (uint32_t c = 0u; c < siemens_simatic_mobile_panels_gen2::FModuleHardware::kChannels; ++c) {
        regions.push_back(&hardware_->Flash(c));
        regions.push_back(&hardware_->Otp(c));
    }
    return regions;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::LoadFlashImage() {
    host_platform::HostFile file;
    uint64_t size = 0u;
    if (!file.Open(flash_path_, host_platform::FileMode::kReadExisting) || !file.Size(size))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot open %s", flash_path_.c_str());
    const auto regions = FlashRegions();
    uint64_t expected = 0u;
    for (const auto* region : regions) expected += region->size();
    if (size != expected)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: %s holds %llu bytes, not the Flash and OTP of both MCUs (%llu)",
                              flash_path_.c_str(), static_cast<unsigned long long>(size),
                              static_cast<unsigned long long>(expected));
    for (auto* region : regions) {
        uint32_t done = 0u;
        if (!file.Read(region->data(), static_cast<uint32_t>(region->size()), done) || done != region->size())
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot read %s", flash_path_.c_str());
    }
    LOG(Board, "Mobile Panels 2nd Gen F-module: MCU Flash loaded from %s\n", flash_path_.c_str());
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::StoreFlashImage() {
    const std::string staging = flash_path_ + ".writing";
    {
        host_platform::HostFile file;
        if (!file.Open(staging, host_platform::FileMode::kWriteNew))
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot create %s", staging.c_str());
        for (const auto* region : FlashRegions()) {
            uint32_t done = 0u;
            if (!file.Write(region->data(), static_cast<uint32_t>(region->size()), done) || done != region->size())
                emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot write %s", staging.c_str());
        }
    }
    if (!host_platform::ReplaceHostFile(staging, flash_path_, true))
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: cannot replace %s", flash_path_.c_str());
    stored_ns_ = clock_ != nullptr ? clock_->NowNs() : 0;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::PersistFlash(bool now) {
    flash_pending_ = hardware_->TakeFlashModified() || flash_pending_;
    if (!flash_pending_) return;
    if (!now && clock_->NowNs() - stored_ns_ < kStoreIntervalNs) return;
    StoreFlashImage();
    flash_pending_ = false;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::OnShutdown() {
    Settle();
    PersistFlash(true);
}

[[noreturn]] void SiemensSimaticMobilePanelsGen2FModuleDevice::FModuleFault(uint32_t channel, const char* what, uint32_t detail,
                                                       uint32_t pc) {
    if (speculating_) throw SpeculativeFault{};
    const cortex_m3::Core& core = hardware_->Core(channel);
    emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module MCU %u: %s (0x%08X) at pc 0x%08X lr 0x%08X sp 0x%08X "
                          "r0 0x%08X r1 0x%08X r2 0x%08X r3 0x%08X ipsr %u",
                          channel, what, detail, pc, core.Register(14u), core.Register(13u), core.Register(0u),
                          core.Register(1u), core.Register(2u), core.Register(3u), core.Ipsr());
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::LinesChanged(const siemens_simatic_mobile_panels_gen2::FModuleLines& lines,
                                                                    int64_t ns) {
    if (replaying_) return;
    if (speculating_) {
        edges_.push_back({base_ns_ + ns, lines});
        return;
    }
    delivered_ = lines;
    NotifyLines();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::NotifyLines() {
    for (const auto& listener : listeners_) listener.first(listener.second);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::ArmAt(GuestCycleClock::Event* event, int64_t ns) {
    const int64_t ahead = ns - clock_->NowNs();
    const uint64_t cycles = ahead > 0 ? clock_->NsToCycles(ahead) : 0u;
    clock_->Arm(event, clock_->Cycles() + std::max<uint64_t>(cycles, 2u) - 1u);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::Settle() {
    if (reset_held_) return;
    const int64_t now = clock_->NowNs();
    DeliverDueEdges(now);
    const int64_t module_ns = base_ns_ + hardware_->NowNs();
    if (!faulted_ && (module_ns <= now || clock_->NsToCycles(module_ns - now) == 0u)) {
        if (module_ns < now) hardware_->RunUntilNs(now - base_ns_);
        return;
    }
    hardware_->RestoreSnapshot(snapshot_);
    edges_.clear();
    clock_->Disarm(edge_);
    faulted_ = false;
    replaying_ = true;
    hardware_->RunUntilNs(now - base_ns_);
    replaying_ = false;
    const siemens_simatic_mobile_panels_gen2::FModuleLines& lines = hardware_->Lines();
    if (lines != delivered_)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: the re-run up to guest time %lld ns drove READY %u/%u, "
                              "the delivered READY is %u/%u",
                              static_cast<long long>(now), lines.ready_driven ? 1u : 0u, lines.ready ? 1u : 0u,
                              delivered_.ready_driven ? 1u : 0u, delivered_.ready ? 1u : 0u);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::Resume() {
    if (reset_held_) return;
    hardware_->SaveSnapshot(snapshot_);
    const int64_t now = clock_->NowNs();
    if (window_end_ns_ <= now) window_end_ns_ = now + kSpeculationWindowNs;
    speculating_ = true;
    try {
        hardware_->RunUntilNs(window_end_ns_ - base_ns_);
    } catch (const SpeculativeFault&) {
        faulted_ = true;
        window_end_ns_ = base_ns_ + hardware_->NowNs();
    }
    speculating_ = false;
    ArmAt(tick_, window_end_ns_);
    if (!edges_.empty()) ArmAt(edge_, edges_.front().ns);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::OnTick() {
    const int64_t now = clock_->NowNs();
    if (!faulted_ && window_end_ns_ > now && clock_->NsToCycles(window_end_ns_ - now) != 0u) {
        ArmAt(tick_, window_end_ns_);
        return;
    }
    Settle();
    PersistFlash(false);
    window_end_ns_ = 0;
    Resume();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::OnEdge() {
    DeliverDueEdges(clock_->NowNs());
    if (!edges_.empty()) ArmAt(edge_, edges_.front().ns);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::DeliverDueEdges(int64_t now) {
    while (!edges_.empty() && (edges_.front().ns <= now || clock_->NsToCycles(edges_.front().ns - now) == 0u)) {
        delivered_ = edges_.front().lines;
        edges_.pop_front();
        NotifyLines();
    }
}

siemens_simatic_mobile_panels_gen2::FModuleLines SiemensSimaticMobilePanelsGen2FModuleDevice::Lines() const {
    return reset_held_ ? siemens_simatic_mobile_panels_gen2::FModuleLines{} : delivered_;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::AddLinesListener(LinesChangedFn fn, void* context) {
    listeners_.emplace_back(fn, context);
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::StageDmaTransmit(uint32_t buffer_pa, uint32_t bytes) {
    dma_tx_ready_ = false;
    if (bytes != kTransferBytes)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: SDMA transmit of %u bytes, not %u", bytes, kTransferBytes);

    auto& memory = emu_.Get<EmulatedMemory>();
    for (uint32_t off = 0; off < bytes; off += 4u) {
        uint8_t* src = memory.TryTranslateRange(buffer_pa + off, sizeof(uint32_t));
        if (!src)
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: SDMA transmit word 0x%08X is not mapped",
                                  buffer_pa + off);
        uint32_t word = 0;
        std::memcpy(&word, src, sizeof(word));
        cerf::be::Put32(dma_tx_.data() + off, word);
    }
    dma_tx_ready_ = true;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::StageDmaReceive(uint32_t buffer_pa, uint32_t bytes) {
    dma_rx_buffer_pa_ = buffer_pa;
    dma_rx_bytes_ = bytes;
    dma_rx_pending_ = true;
    if (bytes != kTransferBytes)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: SDMA receive of %u bytes, not %u", bytes, kTransferBytes);
    FlushDmaReceive();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::FlushDmaReceive() {
    if (!dma_rx_pending_ || !dma_response_ready_) return;
    auto& memory = emu_.Get<EmulatedMemory>();
    for (uint32_t off = 0; off < dma_rx_bytes_; off += 4u) {
        uint8_t* dst = memory.TryTranslateRange(dma_rx_buffer_pa_ + off, sizeof(uint32_t), true);
        if (!dst)
            emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: SDMA receive word 0x%08X is not mapped",
                                  dma_rx_buffer_pa_ + off);
        const uint32_t word = cerf::be::U32(dma_rx_.data(), off);
        std::memcpy(dst, &word, sizeof(word));
    }
    dma_rx_pending_ = false;
    dma_response_ready_ = false;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::Exchange(uint32_t conreg, uint32_t configreg) {
    if (reset_held_ || !dma_tx_ready_)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: ECSPI3 exchange with reset %u, staged transmit %u is not modelled",
                              reset_held_ ? 1u : 0u, dma_tx_ready_ ? 1u : 0u);

    const uint32_t channel = (conreg >> 18u) & 3u;
    const uint32_t channel_mask = 1u << channel;
    const bool master = (conreg & (channel_mask << 4u)) != 0u;
    const bool idle_high = (configreg & (channel_mask << 4u)) != 0u;
    const bool phase_one = (configreg & channel_mask) != 0u;
    const bool clock_stays_high = (configreg & (channel_mask << 20u)) != 0u;
    const bool chip_select_active_high = (configreg & (channel_mask << 12u)) != 0u;
    const uint32_t burst_bits = ((conreg >> 20u) & 0xFFFu) + 1u;
    if (!master || idle_high || phase_one || clock_stays_high || chip_select_active_high ||
        burst_bits != kTransferBytes * 8u)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: ECSPI3 CONREG 0x%08X CONFIGREG 0x%08X is not the "
                              "mode-0 master %u-bit burst the module carries",
                              conreg, configreg, kTransferBytes * 8u);

    Settle();
    if (hardware_->SpiEnabled(0u)) {
        for (uint32_t i = 0u; i < kTransferBytes; ++i) dma_rx_[i] = hardware_->SpiExchange(0u, dma_tx_[i]);
    } else {
        dma_rx_.fill(UndrivenMisoByte());
    }
    miso_keeper_known_ = true;

    dma_tx_ready_ = false;
    dma_response_ready_ = true;
    FlushDmaReceive();
    if (!first_exchange_logged_) {
        first_exchange_logged_ = true;
        LOG(Board, "Mobile Panels 2nd Gen F-module: first 272-byte ECSPI3 exchange completed\n");
    }
    Resume();
}

uint8_t SiemensSimaticMobilePanelsGen2FModuleDevice::UndrivenMisoByte() {
    hardware_->RequireMisoReleased(0u);
    const uint32_t ctl_offset = emu_.Get<Imx6Variant>().Family() == Imx6Family::SoloDualLite ? 0x3F4u : 0x48Cu;
    const uint32_t ctl = emu_.Get<Imx6Iomuxc>().PadRegister(ctl_offset);
    const Imx6PadInputLevel input = ResolveImx6PadInputLevel(
        ctl, (dma_rx_.back() & 1u) != 0u, miso_keeper_known_);
    if (!input.known)
        emu_.Get<Fatal>().Die("F-module: ECSPI3 exchange with SPI2 disabled and MISO floating "
                              "(PAD_CTL 0x%08X)",
                              ctl);
    return input.high ? 0xFFu : 0x00u;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::ObservePanelAcknowledge(bool driven, bool high) {
    const Imx6GpioPad* pad = FindImx6GpioPad(Imx6GpioPadsFor(emu_.Get<Imx6Variant>().Family()), 5u, 6u);
    auto& iomuxc = emu_.Get<Imx6Iomuxc>();
    const uint32_t mux = iomuxc.PadRegister(pad->mux_offset) & kImx6PadMuxMode;
    if (mux != pad->gpio_mode)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen F-module: GPIO5_IO06 pad mux 0x%X", mux);
    const uint32_t ctl = iomuxc.PadRegister(pad->ctl_offset);
    if (driven) {
        panel_ack_last_driven_high_ = high;
        panel_ack_last_driven_known_ = true;
    } else {
        const Imx6PadInputLevel input = ResolveImx6PadInputLevel(
            ctl, panel_ack_last_driven_high_, panel_ack_last_driven_known_);
        high = input.high;
        driven = input.known;
    }
    if (driven == panel_ack_known_ && high == panel_ack_high_) return;
    Settle();
    panel_ack_known_ = driven;
    panel_ack_high_ = high;
    hardware_->SetAck(driven, high);
    Resume();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::ObserveModuleReset(bool high) {
    Settle();
    const bool was_held = reset_held_;
    if (!high) {
        reset_held_ = true;
    } else if (reset_held_) {
        reset_held_ = false;
        hardware_->Reset();
        hardware_->SetAck(panel_ack_known_, panel_ack_high_);
        base_ns_ = clock_->NowNs();
        dma_tx_ready_ = false;
        dma_rx_pending_ = false;
        dma_response_ready_ = false;
        miso_keeper_known_ = false;
        delivered_ = hardware_->Lines();
        window_end_ns_ = 0;
        Resume();
    }
    if (reset_held_ != was_held) NotifyLines();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::SaveState(StateWriter& w) {
    Settle();
    PersistFlash(true);
    hardware_->SaveState(w);
    w.WriteBytes("dma_tx", dma_tx_.data(), dma_tx_.size());
    w.WriteBytes("dma_rx", dma_rx_.data(), dma_rx_.size());
    w.Write("dma_rx_buffer_pa", dma_rx_buffer_pa_);
    w.Write("dma_rx_bytes", dma_rx_bytes_);
    w.Write<uint8_t>("dma_tx_ready", dma_tx_ready_ ? 1u : 0u);
    w.Write<uint8_t>("dma_rx_pending", dma_rx_pending_ ? 1u : 0u);
    w.Write<uint8_t>("dma_response_ready", dma_response_ready_ ? 1u : 0u);
    w.Write<uint8_t>("miso_keeper_known", miso_keeper_known_ ? 1u : 0u);
    w.Write<uint8_t>("panel_ack_known", panel_ack_known_ ? 1u : 0u);
    w.Write<uint8_t>("panel_ack_high", panel_ack_high_ ? 1u : 0u);
    w.Write<uint8_t>("panel_ack_last_driven_high", panel_ack_last_driven_high_ ? 1u : 0u);
    w.Write<uint8_t>("panel_ack_last_driven_known", panel_ack_last_driven_known_ ? 1u : 0u);
    w.Write<uint8_t>("reset_held", reset_held_ ? 1u : 0u);
    w.Write("fwf_container_crc32", selected_container_crc32_);
    Resume();
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::RestoreState(StateReader& r) {
    uint32_t fwf_container_crc32 = 0;
    hardware_->RestoreState(r);
    r.ReadBytes("dma_tx", dma_tx_.data(), dma_tx_.size());
    r.ReadBytes("dma_rx", dma_rx_.data(), dma_rx_.size());
    r.Read("dma_rx_buffer_pa", dma_rx_buffer_pa_);
    r.Read("dma_rx_bytes", dma_rx_bytes_);
    uint8_t flags[9]{};
    r.Read("dma_tx_ready", flags[0]);
    r.Read("dma_rx_pending", flags[1]);
    r.Read("dma_response_ready", flags[2]);
    r.Read("miso_keeper_known", flags[8]);
    r.Read("panel_ack_known", flags[5]);
    r.Read("panel_ack_high", flags[3]);
    r.Read("panel_ack_last_driven_high", flags[6]);
    r.Read("panel_ack_last_driven_known", flags[7]);
    r.Read("reset_held", flags[4]);
    r.Read("fwf_container_crc32", fwf_container_crc32);
    if (fwf_container_crc32 != selected_container_crc32_)
        r.Reject("F-module: saved state does not match the selected FWF firmware");
    dma_tx_ready_ = flags[0] != 0u;
    dma_rx_pending_ = flags[1] != 0u;
    dma_response_ready_ = flags[2] != 0u;
    miso_keeper_known_ = flags[8] != 0u;
    panel_ack_known_ = flags[5] != 0u;
    panel_ack_high_ = flags[3] != 0u;
    panel_ack_last_driven_high_ = flags[6] != 0u;
    panel_ack_last_driven_known_ = flags[7] != 0u;
    reset_held_ = flags[4] != 0u;
    clock_->Disarm(tick_);
    clock_->Disarm(edge_);
    edges_.clear();
    faulted_ = false;
}

void SiemensSimaticMobilePanelsGen2FModuleDevice::PostRestore() {
    base_ns_ = clock_->NowNs() - hardware_->NowNs();
    delivered_ = hardware_->Lines();
    window_end_ns_ = 0;
    Resume();
    FlushDmaReceive();
    NotifyLines();
}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2FModuleDevice, Imx6EcspiEndpoint);

}
