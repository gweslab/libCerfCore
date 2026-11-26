#include "host_tcp_stream.h"

#include "host_network.h"

#include <cerfcore/platform/host_descriptor.h>

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

TcpWaitResult WaitFor(int fd, int events, uint32_t timeout_ms, uint32_t& error) {
    HostPollSet poll_set;
    poll_set.Add(static_cast<uintptr_t>(fd), events);
    for (;;) {
        const int ready = poll_set.Wait(static_cast<int>(timeout_ms));
        if (ready > 0) return TcpWaitResult::kReady;
        if (ready == 0) return TcpWaitResult::kTimeout;
        if (errno == EINTR) continue;
        error = static_cast<uint32_t>(errno);
        return TcpWaitResult::kFailed;
    }
}

bool PeerGone(int code) { return code == EPIPE || code == ECONNRESET || code == ENOTCONN || code == ETIMEDOUT; }

void CloseDescriptor(int& fd) {
    if (fd < 0) return;
    ::close(fd);
    fd = -1;
}

}

struct HostTcpConnection::Impl {
    int         fd = -1;
    std::string peer;
};

HostTcpConnection::HostTcpConnection() : impl_(std::make_unique<Impl>()) {}

HostTcpConnection::~HostTcpConnection() { CloseDescriptor(impl_->fd); }

TcpWaitResult HostTcpConnection::WaitReadable(uint32_t timeout_ms, uint32_t& error) {
    return WaitFor(impl_->fd, kPollReadable, timeout_ms, error);
}

TcpIoResult HostTcpConnection::Read(uint8_t* buf, uint32_t size, uint32_t& got, uint32_t& error) {
    got = 0;
    for (;;) {
        const ssize_t n = ::recv(impl_->fd, buf, size, 0);
        if (n > 0) {
            got = static_cast<uint32_t>(n);
            return TcpIoResult::kDone;
        }
        if (n == 0) return TcpIoResult::kClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return TcpIoResult::kDone;
        if (PeerGone(errno)) return TcpIoResult::kClosed;
        error = static_cast<uint32_t>(errno);
        return TcpIoResult::kFailed;
    }
}

TcpIoResult HostTcpConnection::Peek(uint32_t& error) {
    uint8_t byte = 0;
    for (;;) {
        const ssize_t n = ::recv(impl_->fd, &byte, 1, MSG_PEEK);
        if (n > 0) return TcpIoResult::kDone;
        if (n == 0) return TcpIoResult::kClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return TcpIoResult::kDone;
        if (PeerGone(errno)) return TcpIoResult::kClosed;
        error = static_cast<uint32_t>(errno);
        return TcpIoResult::kFailed;
    }
}

TcpIoResult HostTcpConnection::Write(const uint8_t* data, uint32_t size, uint32_t timeout_ms, uint32_t& wrote,
                                     uint32_t& error) {
    wrote = 0;
    while (wrote < size) {
        const TcpWaitResult wait = WaitFor(impl_->fd, kPollWritable, timeout_ms, error);
        if (wait == TcpWaitResult::kTimeout) return TcpIoResult::kDone;
        if (wait == TcpWaitResult::kFailed) return TcpIoResult::kFailed;
        const ssize_t n = ::send(impl_->fd, data + wrote, size - wrote, kSendFlags);
        if (n >= 0) {
            wrote += static_cast<uint32_t>(n);
            continue;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
        if (PeerGone(errno)) return TcpIoResult::kClosed;
        error = static_cast<uint32_t>(errno);
        return TcpIoResult::kFailed;
    }
    return TcpIoResult::kDone;
}

void HostTcpConnection::Shutdown() {
    if (impl_->fd >= 0) ::shutdown(impl_->fd, SHUT_RDWR);
}

std::string HostTcpConnection::PeerName() const { return impl_->peer; }

struct HostTcpListener::Impl {
    int fd = -1;
};

HostTcpListener::HostTcpListener() : impl_(std::make_unique<Impl>()) {}

HostTcpListener::~HostTcpListener() { Close(); }

bool HostTcpListener::Listen(const std::string& address, uint16_t port, uint32_t& error) {
    Close();
    sockaddr_in bound{};
    bound.sin_family = AF_INET;
    bound.sin_port   = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &bound.sin_addr) != 1) {
        error = static_cast<uint32_t>(EINVAL);
        return false;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        error = static_cast<uint32_t>(errno);
        return false;
    }
    const int on = 1;
    if (!SetCloseOnExecNonBlocking(fd) || ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) != 0 ||
        ::bind(fd, reinterpret_cast<const sockaddr*>(&bound), sizeof bound) != 0 || ::listen(fd, 1) != 0) {
        error = static_cast<uint32_t>(errno);
        CloseDescriptor(fd);
        return false;
    }
    impl_->fd = fd;
    return true;
}

void HostTcpListener::Close() { CloseDescriptor(impl_->fd); }

bool HostTcpListener::IsListening() const { return impl_->fd >= 0; }

TcpWaitResult HostTcpListener::WaitAcceptable(uint32_t timeout_ms, uint32_t& error) {
    return WaitFor(impl_->fd, kPollReadable, timeout_ms, error);
}

std::unique_ptr<HostTcpConnection> HostTcpListener::Accept(uint32_t& error) {
    error = 0;
    sockaddr_in peer{};
    socklen_t   peer_size = sizeof peer;
    int         fd        = -1;
    do {
        fd = ::accept(impl_->fd, reinterpret_cast<sockaddr*>(&peer), &peer_size);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) error = static_cast<uint32_t>(errno);
        return nullptr;
    }
    const int on = 1;
    if (!SetCloseOnExecNonBlocking(fd) || ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on) != 0) {
        error = static_cast<uint32_t>(errno);
        CloseDescriptor(fd);
        return nullptr;
    }
#ifdef SO_NOSIGPIPE
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on) != 0) {
        error = static_cast<uint32_t>(errno);
        CloseDescriptor(fd);
        return nullptr;
    }
#endif
    auto connection = std::make_unique<HostTcpConnection>();
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &peer.sin_addr, text, sizeof text) != nullptr)
        connection->impl_->peer = std::string(text) + ":" + std::to_string(ntohs(peer.sin_port));
    connection->impl_->fd = fd;
    return connection;
}

}

}
