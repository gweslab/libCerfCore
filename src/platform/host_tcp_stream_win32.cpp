#include "host_tcp_stream.h"

#include "host_network.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

namespace cerfcore {

namespace host_platform {

namespace {

TcpWaitResult WaitFor(SOCKET socket, int events, uint32_t timeout_ms, uint32_t& error) {
    HostPollSet poll_set;
    poll_set.Add(static_cast<uintptr_t>(socket), events);
    const int ready = poll_set.Wait(static_cast<int>(timeout_ms));
    if (ready > 0) return TcpWaitResult::kReady;
    if (ready == 0) return TcpWaitResult::kTimeout;
    error = static_cast<uint32_t>(WSAGetLastError());
    return TcpWaitResult::kFailed;
}

bool PeerGone(int code) {
    return code == WSAECONNRESET || code == WSAECONNABORTED || code == WSAENOTCONN || code == WSAESHUTDOWN ||
           code == WSAETIMEDOUT || code == WSAENETRESET;
}

SOCKET NonInheritableStreamSocket() {
    SOCKET socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket != INVALID_SOCKET || WSAGetLastError() != WSAEINVAL) return socket;
    socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    if (socket != INVALID_SOCKET) SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
    return socket;
}

void CloseSocket(SOCKET& socket) {
    if (socket == INVALID_SOCKET) return;
    closesocket(socket);
    socket = INVALID_SOCKET;
}

}

struct HostTcpConnection::Impl {
    SOCKET      socket = INVALID_SOCKET;
    std::string peer;
};

HostTcpConnection::HostTcpConnection() : impl_(std::make_unique<Impl>()) {}

HostTcpConnection::~HostTcpConnection() { CloseSocket(impl_->socket); }

TcpWaitResult HostTcpConnection::WaitReadable(uint32_t timeout_ms, uint32_t& error) {
    return WaitFor(impl_->socket, kPollReadable, timeout_ms, error);
}

TcpIoResult HostTcpConnection::Read(uint8_t* buf, uint32_t size, uint32_t& got, uint32_t& error) {
    got = 0;
    const int n = recv(impl_->socket, reinterpret_cast<char*>(buf), static_cast<int>(size), 0);
    if (n > 0) {
        got = static_cast<uint32_t>(n);
        return TcpIoResult::kDone;
    }
    if (n == 0) return TcpIoResult::kClosed;
    const int code = WSAGetLastError();
    if (code == WSAEWOULDBLOCK) return TcpIoResult::kDone;
    if (PeerGone(code)) return TcpIoResult::kClosed;
    error = static_cast<uint32_t>(code);
    return TcpIoResult::kFailed;
}

TcpIoResult HostTcpConnection::Peek(uint32_t& error) {
    char      byte = 0;
    const int n    = recv(impl_->socket, &byte, 1, MSG_PEEK);
    if (n > 0) return TcpIoResult::kDone;
    if (n == 0) return TcpIoResult::kClosed;
    const int code = WSAGetLastError();
    if (code == WSAEWOULDBLOCK) return TcpIoResult::kDone;
    if (PeerGone(code)) return TcpIoResult::kClosed;
    error = static_cast<uint32_t>(code);
    return TcpIoResult::kFailed;
}

TcpIoResult HostTcpConnection::Write(const uint8_t* data, uint32_t size, uint32_t timeout_ms, uint32_t& wrote,
                                     uint32_t& error) {
    wrote = 0;
    while (wrote < size) {
        const TcpWaitResult wait = WaitFor(impl_->socket, kPollWritable, timeout_ms, error);
        if (wait == TcpWaitResult::kTimeout) return TcpIoResult::kDone;
        if (wait == TcpWaitResult::kFailed) return TcpIoResult::kFailed;
        const int n = send(impl_->socket, reinterpret_cast<const char*>(data + wrote), static_cast<int>(size - wrote), 0);
        if (n >= 0) {
            wrote += static_cast<uint32_t>(n);
            continue;
        }
        const int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) continue;
        if (PeerGone(code)) return TcpIoResult::kClosed;
        error = static_cast<uint32_t>(code);
        return TcpIoResult::kFailed;
    }
    return TcpIoResult::kDone;
}

void HostTcpConnection::Shutdown() {
    if (impl_->socket != INVALID_SOCKET) shutdown(impl_->socket, SD_BOTH);
}

std::string HostTcpConnection::PeerName() const { return impl_->peer; }

struct HostTcpListener::Impl {
    SOCKET socket = INVALID_SOCKET;
};

HostTcpListener::HostTcpListener() : impl_(std::make_unique<Impl>()) {}

HostTcpListener::~HostTcpListener() { Close(); }

bool HostTcpListener::Listen(const std::string& address, uint16_t port, uint32_t& error) {
    Close();
    sockaddr_in bound{};
    bound.sin_family = AF_INET;
    bound.sin_port   = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &bound.sin_addr) != 1) {
        error = static_cast<uint32_t>(WSAEINVAL);
        return false;
    }
    SOCKET socket = NonInheritableStreamSocket();
    if (socket == INVALID_SOCKET) {
        error = static_cast<uint32_t>(WSAGetLastError());
        return false;
    }
    BOOL   exclusive = TRUE;
    u_long nonblock  = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                   sizeof exclusive) != 0 ||
        ioctlsocket(socket, FIONBIO, &nonblock) != 0 ||
        bind(socket, reinterpret_cast<const sockaddr*>(&bound), sizeof bound) != 0 || listen(socket, 1) != 0) {
        error = static_cast<uint32_t>(WSAGetLastError());
        CloseSocket(socket);
        return false;
    }
    impl_->socket = socket;
    return true;
}

void HostTcpListener::Close() { CloseSocket(impl_->socket); }

bool HostTcpListener::IsListening() const { return impl_->socket != INVALID_SOCKET; }

TcpWaitResult HostTcpListener::WaitAcceptable(uint32_t timeout_ms, uint32_t& error) {
    return WaitFor(impl_->socket, kPollReadable, timeout_ms, error);
}

std::unique_ptr<HostTcpConnection> HostTcpListener::Accept(uint32_t& error) {
    error = 0;
    sockaddr_in peer{};
    int         peer_size = sizeof peer;
    SOCKET      socket    = accept(impl_->socket, reinterpret_cast<sockaddr*>(&peer), &peer_size);
    if (socket == INVALID_SOCKET) {
        const int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK && code != WSAECONNRESET) error = static_cast<uint32_t>(code);
        return nullptr;
    }
    BOOL   no_delay = TRUE;
    u_long nonblock = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblock) != 0 ||
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof no_delay) != 0) {
        error = static_cast<uint32_t>(WSAGetLastError());
        CloseSocket(socket);
        return nullptr;
    }
    auto connection = std::make_unique<HostTcpConnection>();
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &peer.sin_addr, text, sizeof text) != nullptr)
        connection->impl_->peer = std::string(text) + ":" + std::to_string(ntohs(peer.sin_port));
    connection->impl_->socket = socket;
    return connection;
}

}

}
