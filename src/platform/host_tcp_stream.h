#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cerfcore {

namespace host_platform {

enum class TcpWaitResult { kReady, kTimeout, kFailed };
enum class TcpIoResult { kDone, kClosed, kFailed };

class HostTcpConnection {
public:
    HostTcpConnection();
    ~HostTcpConnection();
    HostTcpConnection(const HostTcpConnection&)            = delete;
    HostTcpConnection& operator=(const HostTcpConnection&) = delete;

    TcpWaitResult WaitReadable(uint32_t timeout_ms, uint32_t& error);
    TcpIoResult   Read(uint8_t* buf, uint32_t size, uint32_t& got, uint32_t& error);
    TcpIoResult   Peek(uint32_t& error);
    TcpIoResult   Write(const uint8_t* data, uint32_t size, uint32_t timeout_ms, uint32_t& wrote, uint32_t& error);
    void          Shutdown();
    std::string   PeerName() const;

private:
    friend class HostTcpListener;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class HostTcpListener {
public:
    HostTcpListener();
    ~HostTcpListener();
    HostTcpListener(const HostTcpListener&)            = delete;
    HostTcpListener& operator=(const HostTcpListener&) = delete;

    bool Listen(const std::string& address, uint16_t port, uint32_t& error);
    void Close();
    bool IsListening() const;

    TcpWaitResult                      WaitAcceptable(uint32_t timeout_ms, uint32_t& error);
    std::unique_ptr<HostTcpConnection> Accept(uint32_t& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

}
