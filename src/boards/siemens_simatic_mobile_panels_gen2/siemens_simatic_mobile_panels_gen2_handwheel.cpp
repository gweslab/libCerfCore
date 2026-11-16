#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/imx6/imx6_i2c_bus.h"
#include "../../socs/i2c_slave.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include "hmi_ktp700f_hw_mobile_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kI2c1Base = 0x021A0000u;
constexpr uint8_t kSlaveAddress = 0x2Fu;
constexpr uint8_t kVersionRequest = 0x55u;
constexpr uint8_t kDataRequest = 0x01u;

constexpr std::size_t kResponseBytes = 4u;
constexpr uint8_t kVersionCommandStub = 0x00u;
constexpr uint8_t kControllerReadyState = 0x02u;
constexpr uint8_t kVersionMajorStub = 0x00u;
constexpr uint8_t kVersionMinorStub = 0x00u;
constexpr uint8_t kDataReplyCommand = 0x02u;
constexpr uint8_t kDataReplyVersion = 0x10u;
constexpr uint8_t kOverrideStub = 0x00u;
constexpr uint8_t kCounterDeltaStubValue = 0x00u;
constexpr std::size_t kCommandFrameBytes = 3u;

class SiemensSimaticMobilePanelsGen2Handwheel final : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override {
        const auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardId() == BoardId::HmiKtp700fHwMobile;
    }

    void OnReady() override { emu_.Get<Imx6I2cBus>().Register(this, kI2c1Base); }

    bool MatchesAddress(uint8_t slave_addr) const override { return slave_addr == kSlaveAddress; }

    void TxnStart(uint8_t) override {
        byte_index_ = 0;
        start_pending_ = true;
    }

    void TxnWriteByte(uint8_t, uint8_t value) override {
        start_pending_ = false;
        if (byte_index_ >= kCommandFrameBytes)
            emu_.Get<Fatal>().Die("KTP hand wheel command frame longer than %u bytes",
                                  static_cast<unsigned>(kCommandFrameBytes));
        if (byte_index_ == 0)
            command_ = value;
        else if (value != 0u)
            emu_.Get<Fatal>().Die("KTP hand wheel command 0x%02X argument byte 0x%02X is not modelled",
                                  command_, value);
        ++byte_index_;
    }

    uint8_t TxnReadByte(uint8_t) override {
        if (start_pending_) {
            start_pending_ = false;
            BuildResponse();
        }
        if (byte_index_ >= response_.size())
            emu_.Get<Fatal>().Die("KTP hand wheel read past the %u-byte response",
                                  static_cast<unsigned>(response_.size()));
        return response_[byte_index_++];
    }

    void SaveState(StateWriter& writer) override {
        writer.Write("command", command_);
        writer.Write<uint32_t>("byte_index", static_cast<uint32_t>(byte_index_));
        writer.WriteBytes("response", response_.data(), response_.size());
        writer.Write("start_pending", static_cast<uint8_t>(start_pending_));
    }

    void RestoreState(StateReader& reader) override {
        reader.Read("command", command_);
        uint32_t index = 0;
        reader.Read("byte_index", index);
        byte_index_ = index;
        reader.ReadBytes("response", response_.data(), response_.size());
        uint8_t start_pending = 0;
        reader.Read("start_pending", start_pending);
        start_pending_ = start_pending != 0u;
    }

private:
    void BuildResponse() {
        if (command_ == kVersionRequest) {
            response_ = {kVersionCommandStub, kControllerReadyState, kVersionMajorStub, kVersionMinorStub};
        } else if (command_ == kDataRequest) {
            response_ = {kDataReplyCommand, static_cast<uint8_t>(kDataReplyVersion | kControllerReadyState),
                         kOverrideStub, kCounterDeltaStubValue};
        } else {
            emu_.Get<Fatal>().Die("KTP hand wheel command 0x%02X is not modelled", command_);
        }
    }

    uint8_t command_ = 0u;
    std::size_t byte_index_ = 0;
    bool start_pending_ = false;
    std::array<uint8_t, kResponseBytes> response_{};
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2Handwheel);

}
