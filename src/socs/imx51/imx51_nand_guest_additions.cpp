#include "imx51_nand_placing_mode_id.h"
#include "imx51_nand_store.h"

#include "../../boot/imgfs_stub_injector.h"
#include "../../boot/imgfs_volume_access.h"
#include "../../boot/rom_image_parse.h"
#include "../../boot/rom_record_layout.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace cerfcore {

namespace {

using cerf::rom_image_parse::ImgfsSuperblock;
using cerf::rom_image_parse::ReadImgfsSuperblock;

constexpr char kCantRun[] = "Guest Additions can't run on this ROM";

class Imx51NandGuestAdditions : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || bd->GetRomPlacingMode() != RomPlacingModeId::Imx51Nand) return false;
        if (!emu_.Get<DeviceConfig>().guest_additions) return false;
        return emu_.TryGet<Imx51NandStore>() != nullptr;
    }

    void OnReady() override {
        auto&    store = emu_.Get<Imx51NandStore>();
        uint64_t base  = 0;
        if (!LocateVolume(store, base)) {
            LOG(GuestAdditions, "[NandGA] IMGFS volume not found in storage.nand %s; the guest "
                                "boots its stock display driver\n",
                store.ImagePath().c_str());
            return;
        }
        Volume     volume(*this, store, base);
        const int  replaced = emu_.Get<ImgfsStubInjector>().ReplaceVictims(volume);
        LOG(GuestAdditions, "[NandGA] IMGFS at NAND page %llu: %d victim(s) replaced\n",
            static_cast<unsigned long long>(base), replaced);
        if (replaced == 0) {
            LOG(GuestAdditions, "[NandGA] no display-driver victim matched\n");
            emu_.Get<Fatal>().UserError(kCantRun);
        }
    }

private:
    class Volume final : public ImgfsVolumeAccess {
    public:
        Volume(Imx51NandGuestAdditions& owner, Imx51NandStore& store, uint64_t base_page)
            : owner_(owner), store_(store), base_page_(base_page),
              bytes_((store.DevicePages() - base_page) * Imx51NandStore::kMainBytes) {}

        uint32_t SectorBytes() const override { return Imx51NandStore::kMainBytes; }

        std::vector<uint8_t> Read(uint32_t offset, uint32_t length) override {
            const uint64_t end = uint64_t(offset) + length;
            if (length == 0u || end > bytes_) return {};
            std::vector<uint8_t> out(length);
            for (uint32_t done = 0; done < length;) {
                const uint64_t at = uint64_t(offset) + done;
                const uint32_t in = uint32_t(at % Imx51NandStore::kMainBytes);
                const uint32_t n  = (std::min)(Imx51NandStore::kMainBytes - in, length - done);
                const uint64_t page = base_page_ + at / Imx51NandStore::kMainBytes;
                store_.ReadMain(page * Imx51NandStore::kMainBytes + in, out.data() + done, n);
                done += n;
            }
            return out;
        }

        void Patch(uint32_t offset, std::span<const uint8_t> bytes) override {
            if (uint64_t(offset) + bytes.size() > bytes_) {
                owner_.emu_.Get<Fatal>().Die(
                    "[NandGA] a %zu-byte IMGFS patch at 0x%X ends past the 0x%llX bytes of "
                    "the volume", bytes.size(), offset, static_cast<unsigned long long>(bytes_));
            }
            for (size_t done = 0; done < bytes.size();) {
                const uint64_t at = uint64_t(offset) + done;
                const uint32_t in = uint32_t(at % Imx51NandStore::kMainBytes);
                const size_t   n  = (std::min)(size_t(Imx51NandStore::kMainBytes - in),
                                               bytes.size() - done);
                const uint64_t page = base_page_ + at / Imx51NandStore::kMainBytes;
                std::array<uint8_t, Imx51NandStore::kMainBytes>  main{};
                std::array<uint8_t, Imx51NandStore::kSpareBytes> spare{};
                store_.ReadPage(page * Imx51NandStore::kMainBytes, main.data(), spare.data());
                std::memcpy(main.data() + in, bytes.data() + done, n);
                store_.SetReadOverlayPage(page, main.data(), spare.data());
                done += n;
            }
        }

    private:
        Imx51NandGuestAdditions& owner_;
        Imx51NandStore&          store_;
        uint64_t                 base_page_;
        uint64_t                 bytes_;
    };

    bool LocateVolume(Imx51NandStore& store, uint64_t& base) {
        std::array<uint8_t, kImgfsSbSize> hdr{};
        for (uint64_t p = 0; p < store.DevicePages(); ++p) {
            store.ReadMain(p * Imx51NandStore::kMainBytes, hdr.data(), uint32_t(hdr.size()));
            ImgfsSuperblock sb;
            if (!ReadImgfsSuperblock(hdr, 0, sb)) continue;
            base = p;
            LOG(GuestAdditions, "[NandGA] IMGFS volume header at NAND page %llu\n",
                static_cast<unsigned long long>(p));
            return true;
        }
        return false;
    }
};

}

REGISTER_SERVICE(Imx51NandGuestAdditions);

}
