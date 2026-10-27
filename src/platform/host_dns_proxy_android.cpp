#include "host_dns_proxy.h"

#include "../net/dns_message.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/log.h>

#include <slirp/libslirp.h>

#include <android/multinetwork.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

namespace cerfcore {

namespace host_platform {

namespace {

namespace dns = cerf::dns;

constexpr size_t kTcpLengthSize = 2;

struct Listeners {
    int      udp4 = -1;
    int      tcp4 = -1;
    int      udp6 = -1;
    int      tcp6 = -1;
    uint16_t port = 0;

    void Close() {
        for (int* fd : {&udp4, &tcp4, &udp6, &tcp6}) {
            if (*fd >= 0) ::close(*fd);
            *fd = -1;
        }
    }
};

int OpenBound(int family, int type, uint16_t port) {
    const int fd = ::socket(family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;
    int rc;
    if (family == AF_INET) {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = htons(port);
        rc = ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    } else {
        const int only_v6 = 1;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &only_v6, sizeof(only_v6));
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr   = in6addr_loopback;
        addr.sin6_port   = htons(port);
        rc = ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }
    if (rc == 0 && type == SOCK_STREAM) rc = ::listen(fd, SOMAXCONN);
    if (rc != 0) {
        const int error = errno;
        ::close(fd);
        return -error;
    }
    return fd;
}

int BindListeners(Listeners& out) {
    for (;;) {
        Listeners l;
        l.udp4 = OpenBound(AF_INET, SOCK_DGRAM, 0);
        if (l.udp4 < 0) return l.udp4;
        sockaddr_in bound{};
        socklen_t   size = sizeof(bound);
        if (::getsockname(l.udp4, reinterpret_cast<sockaddr*>(&bound), &size) != 0) {
            const int error = errno;
            l.Close();
            return -error;
        }
        l.port = ntohs(bound.sin_port);
        l.tcp4 = OpenBound(AF_INET, SOCK_STREAM, l.port);
        l.udp6 = l.tcp4 < 0 ? l.tcp4 : OpenBound(AF_INET6, SOCK_DGRAM, l.port);
        l.tcp6 = l.udp6 < 0 ? l.udp6 : OpenBound(AF_INET6, SOCK_STREAM, l.port);
        if (l.tcp6 >= 0) {
            out = l;
            return 0;
        }
        const int error = l.tcp6;
        l.Close();
        if (error != -EADDRINUSE) return error;
    }
}

class AndroidDnsProxy final : public HostDnsProxy {
public:
    AndroidDnsProxy(const Listeners& listeners, int stop_read, int stop_write)
        : listeners_(listeners), stop_read_(stop_read), stop_write_(stop_write), thread_([this] { Run(); }) {}

    ~AndroidDnsProxy() override {
        const uint8_t byte = 1;
        while (::write(stop_write_, &byte, 1) < 0 && errno == EINTR) {}
        thread_.join();
        for (const auto& [fd, query] : pending_) android_res_cancel(fd);
        for (const auto& [id, conn] : connections_) ::close(conn.fd);
        listeners_.Close();
        ::close(stop_read_);
        ::close(stop_write_);
    }

    void Configure(SlirpConfig& config) const override { config.host_dns_port = listeners_.port; }

    uint16_t Port() const { return listeners_.port; }

private:
    struct Connection {
        int                  fd = -1;
        std::vector<uint8_t> in;
        std::vector<uint8_t> out;
        bool                 eof = false;
    };

    struct Query {
        std::vector<uint8_t> message;
        int                  udp_socket = -1;
        sockaddr_storage     peer{};
        socklen_t            peer_size  = 0;
        uint64_t             connection = 0;
    };

    void Run();
    void ReceiveDatagrams(int socket);
    void Accept(int listener);
    bool ReadConnection(Connection& conn, uint64_t id);
    bool WriteConnection(Connection& conn);
    void Submit(Query query);
    void Complete(int fd);
    void Fail(const Query& query, const char* what, int error);
    void Deliver(const Query& query, std::vector<uint8_t> answer);
    void DropFinishedConnections();

    Listeners                      listeners_;
    int                            stop_read_;
    int                            stop_write_;
    std::map<int, Query>           pending_;
    std::map<uint64_t, Connection> connections_;
    uint64_t                       next_connection_ = 1;
    std::vector<uint8_t>           buffer_ = std::vector<uint8_t>(dns::kMaxMessageSize);
    std::thread                    thread_;
};

void AndroidDnsProxy::Run() {
    std::vector<pollfd>   fds;
    std::vector<uint64_t> ids;
    for (;;) {
        fds.clear();
        ids.clear();
        for (int fd : {stop_read_, listeners_.udp4, listeners_.udp6, listeners_.tcp4, listeners_.tcp6})
            fds.push_back({fd, POLLIN, 0});
        for (const auto& [id, conn] : connections_) {
            const short events = static_cast<short>((conn.eof ? 0 : POLLIN) | (conn.out.empty() ? 0 : POLLOUT));
            fds.push_back({conn.fd, events, 0});
            ids.push_back(id);
        }
        for (const auto& [fd, query] : pending_) fds.push_back({fd, POLLIN, 0});

        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) continue;
            LOG(Net, "DNS proxy: poll failed (errno=%d)\n", errno);
            return;
        }
        if (fds[0].revents != 0) return;
        if (fds[1].revents != 0) ReceiveDatagrams(listeners_.udp4);
        if (fds[2].revents != 0) ReceiveDatagrams(listeners_.udp6);
        if (fds[3].revents != 0) Accept(listeners_.tcp4);
        if (fds[4].revents != 0) Accept(listeners_.tcp6);

        size_t at = 5;
        for (uint64_t id : ids) {
            const pollfd& p    = fds[at++];
            auto          it   = connections_.find(id);
            bool          keep = true;
            if ((p.revents & (POLLERR | POLLNVAL)) != 0) keep = false;
            if (keep && (p.revents & (POLLIN | POLLHUP)) != 0) keep = ReadConnection(it->second, id);
            if (keep && (p.revents & POLLOUT) != 0) keep = WriteConnection(it->second);
            if (!keep) {
                ::close(it->second.fd);
                connections_.erase(it);
            }
        }
        for (; at < fds.size(); ++at)
            if (fds[at].revents != 0) Complete(fds[at].fd);
        DropFinishedConnections();
    }
}

void AndroidDnsProxy::ReceiveDatagrams(int socket) {
    for (;;) {
        Query         query;
        query.peer_size = sizeof(query.peer);
        const ssize_t n = ::recvfrom(socket, buffer_.data(), buffer_.size(), 0,
                                     reinterpret_cast<sockaddr*>(&query.peer), &query.peer_size);
        if (n < 0) return;
        query.message.assign(buffer_.begin(), buffer_.begin() + n);
        query.udp_socket = socket;
        Submit(std::move(query));
    }
}

void AndroidDnsProxy::Accept(int listener) {
    for (;;) {
        const int fd = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        connections_[next_connection_++].fd = fd;
    }
}

bool AndroidDnsProxy::ReadConnection(Connection& conn, uint64_t id) {
    for (;;) {
        const ssize_t n = ::read(conn.fd, buffer_.data(), buffer_.size());
        if (n > 0) {
            conn.in.insert(conn.in.end(), buffer_.begin(), buffer_.begin() + n);
            continue;
        }
        if (n == 0) conn.eof = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
        break;
    }
    while (conn.in.size() >= kTcpLengthSize) {
        const size_t size = cerf::be::U16(conn.in.data(), 0);
        if (conn.in.size() < kTcpLengthSize + size) break;
        Query query;
        query.message.assign(conn.in.begin() + kTcpLengthSize, conn.in.begin() + kTcpLengthSize + size);
        query.connection = id;
        conn.in.erase(conn.in.begin(), conn.in.begin() + kTcpLengthSize + size);
        Submit(std::move(query));
    }
    return true;
}

bool AndroidDnsProxy::WriteConnection(Connection& conn) {
    const ssize_t n = ::send(conn.fd, conn.out.data(), conn.out.size(), MSG_NOSIGNAL);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK;
    conn.out.erase(conn.out.begin(), conn.out.begin() + n);
    return true;
}

void AndroidDnsProxy::Submit(Query query) {
    if (query.message.size() < dns::kHeaderSize) {
        LOG(Net, "DNS proxy: %zu-byte message dropped\n", query.message.size());
        return;
    }
    const int fd = android_res_nsend(NETWORK_UNSPECIFIED, query.message.data(), query.message.size(), 0);
    if (fd < 0) {
        Fail(query, "query not sent", fd);
        return;
    }
    pending_.emplace(fd, std::move(query));
}

void AndroidDnsProxy::Complete(int fd) {
    auto it = pending_.find(fd);
    if (it == pending_.end()) return;
    const Query query = std::move(it->second);
    pending_.erase(it);
    int       rcode  = 0;
    const int result = android_res_nresult(fd, &rcode, buffer_.data(), buffer_.size());
    if (result < static_cast<int>(dns::kHeaderSize)) {
        Fail(query, "query failed", result);
        return;
    }
    Deliver(query, std::vector<uint8_t>(buffer_.begin(), buffer_.begin() + result));
}

void AndroidDnsProxy::Fail(const Query& query, const char* what, int error) {
    LOG(Net, "DNS proxy: %s (%d)\n", what, error);
    Deliver(query, dns::EmptyAnswer(query.message.data(), query.message.size(),
                                    error == -EINVAL ? dns::kRcodeFormErr : dns::kRcodeServFail));
}

void AndroidDnsProxy::Deliver(const Query& query, std::vector<uint8_t> answer) {
    const bool udp = query.udp_socket >= 0;
    if (udp) {
        const size_t limit = dns::UdpAnswerLimit(query.message.data(), query.message.size());
        if (answer.size() > limit)
            answer = dns::TruncatedAnswer(answer.data(), answer.size(),
                                          dns::HasOpt(query.message.data(), query.message.size()));
        if (answer.empty() || answer.size() > limit) {
            LOG(Net, "DNS proxy: answer does not fit %zu bytes, dropped\n", limit);
            return;
        }
        if (::sendto(query.udp_socket, answer.data(), answer.size(), 0,
                     reinterpret_cast<const sockaddr*>(&query.peer), query.peer_size) < 0) {
            LOG(Net, "DNS proxy: UDP answer not sent (errno=%d)\n", errno);
            return;
        }
    } else {
        auto it = connections_.find(query.connection);
        if (it == connections_.end()) return;
        std::vector<uint8_t>& out = it->second.out;
        const size_t          at  = out.size();
        out.resize(at + kTcpLengthSize + answer.size());
        cerf::be::Put16(out.data() + at, static_cast<uint16_t>(answer.size()));
        std::memcpy(out.data() + at + kTcpLengthSize, answer.data(), answer.size());
    }
    LOG(Net, "DNS proxy: %s %zu-byte query -> %zu-byte answer\n", udp ? "UDP" : "TCP", query.message.size(),
        answer.size());
}

void AndroidDnsProxy::DropFinishedConnections() {
    for (auto it = connections_.begin(); it != connections_.end();) {
        bool waiting = false;
        for (const auto& [fd, query] : pending_) waiting |= query.connection == it->first;
        if (it->second.eof && it->second.out.empty() && !waiting) {
            ::close(it->second.fd);
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

}

int OpenHostDnsProxy(std::unique_ptr<HostDnsProxy>& out) {
    Listeners listeners;
    if (const int rc = BindListeners(listeners); rc != 0) return rc;
    int stop[2];
    if (::pipe2(stop, O_CLOEXEC | O_NONBLOCK) != 0) {
        const int error = errno;
        listeners.Close();
        return -error;
    }
    auto proxy = std::make_unique<AndroidDnsProxy>(listeners, stop[0], stop[1]);
    LOG(Net, "DNS proxy: loopback port %u -> host system resolver\n", static_cast<unsigned>(proxy->Port()));
    out = std::move(proxy);
    return 0;
}

}

}
