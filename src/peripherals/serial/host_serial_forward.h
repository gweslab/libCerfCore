#pragma once

#include "host_io_endpoint.h"
#include "serial_line.h"
#include "../../platform/host_serial_port.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cerfcore {

class CerfEmulator;

class HostSerialForward : public HostIoEndpoint {
public:
    HostSerialForward(std::string host_port, CerfEmulator& emu);
    ~HostSerialForward() override;

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnControlLines(bool dtr, bool rts) override;
    void OnBreak(bool on) override;
    void OnOpen()  override;
    void OnClose() override;
    void StopHostIo() override;
    void ResendModemInputs() override;
    void PostRestore() override;

private:
    struct TxItem {
        enum class Kind : uint8_t { kByte, kBreakOn, kBreakOff, kLines, kConfig };
        Kind                   kind;
        uint8_t                byte;
        bool                   dtr = false;
        bool                   rts = false;
        int64_t                at  = 0;
        SerialLine::LineConfig cfg{};
    };

    void ReaderLoop();
    void WriterLoop();
    void ApplyLineConfig(const SerialLine::LineConfig& c);
    void ApplyBreak(bool on);
    void ApplyControlLines(bool dtr, bool rts);
    void Queue(const TxItem& item);
    bool WaitStopped(uint32_t timeout_ms) override;

    std::string                 port_name_;
    host_platform::HostSerialPort port_;

    std::thread reader_;
    std::thread writer_;

    std::mutex              tx_mu_;
    std::condition_variable tx_cv_;
    std::vector<TxItem>     tx_buf_;

    std::mutex ctl_mu_;
    bool       dtr_   = false;
    bool       rts_   = false;
    bool       break_ = false;

    static constexpr uint8_t kModemLinesUnpushed = 0xFFu;
    std::atomic<uint8_t> last_ms_{kModemLinesUnpushed};
};

}
