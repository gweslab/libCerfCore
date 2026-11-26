#pragma once

#include "host_io_endpoint.h"
#include "serial_line.h"
#include "serial_tcp_address.h"
#include "../../platform/host_tcp_stream.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

class HostTcpSerial : public HostIoEndpoint {
public:
    HostTcpSerial(SerialTcpAddress address, CerfEmulator& emu);
    ~HostTcpSerial() override;

    void OnGuestTx(const uint8_t* data, size_t n) override;
    void OnBreak(bool on) override;
    void OnOpen() override;
    void OnClose() override;
    void StopHostIo() override;
    void ResendModemInputs() override;
    void PostRestore() override;

private:
    using Connection = host_platform::HostTcpConnection;

    void ReaderLoop();
    void WriterLoop();
    bool AcceptClient();
    void RefuseExtraClients();
    bool ServeClient(Connection& client);
    bool WaitRxDrained(Connection& client);
    void DropClient(const Connection* expected, const char* why);
    std::shared_ptr<Connection> Client();
    void ClearGuestTx();

    bool WaitStopped(uint32_t timeout_ms) override;

    SerialTcpAddress               address_;
    host_platform::HostTcpListener listener_;

    std::thread reader_;
    std::thread writer_;

    std::mutex                  client_mu_;
    std::shared_ptr<Connection> client_;
    std::atomic<bool>           connected_{false};
    std::atomic<bool>           lines_dirty_{false};

    std::mutex              tx_mu_;
    std::condition_variable tx_cv_;
    std::vector<uint8_t>    tx_buf_;

    std::mutex              rx_mu_;
    std::condition_variable rx_cv_;
    bool                    rx_drained_ = false;
};

}
