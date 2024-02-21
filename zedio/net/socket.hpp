#pragma once

#include "zedio/async/operation.hpp"
#include "zedio/common/concepts.hpp"
#include "zedio/common/error.hpp"
#include "zedio/common/util/noncopyable.hpp"
// Linux
#include <netdb.h>
// C++
#include <chrono>

namespace zedio::net {

enum class SHUTDOWN_OPTION : int {
    READ = SHUT_RD,
    WRITE = SHUT_WR,
    READ_WRITE = SHUT_RDWR,
};

class Socket {
private:
    explicit Socket(int fd)
        : fd_{fd} {}

public:
    ~Socket() {
        if (fd_ >= 0) {
            sync_close(fd_);
        }
    }

    Socket(Socket &&other)
        : fd_{other.fd_} {
        other.fd_ = -1;
    }

    auto operator=(Socket &&other) -> Socket & {
        if (fd_ >= 0) {
            sync_close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }

    [[nodiscard]]
    auto close() noexcept {
        auto fd = fd_;
        fd_ = -1;
        return async::close(fd);
    }

    [[nodiscard]]
    auto shutdown(SHUTDOWN_OPTION how) const noexcept -> Result<void> {
        if (::shutdown(fd_, static_cast<int>(how)) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
    }

    [[nodiscard]]
    auto read(void *buf, std::size_t len) const noexcept {
        return async::read(fd_, buf, len, 0);
    }

    [[nodiscard]]
    auto read(std::span<char> buf) const noexcept {
        return this->read(buf.data(), buf.size_bytes());
    }

    [[nodiscard]]
    auto read_vectored(struct iovec *iovecs, int nr_vecs) const noexcept {
        return async::readv(fd_, iovecs, nr_vecs, 0);
    }

    template <typename... Ts>
    [[nodiscard]]
    auto read_vectored(Ts &...bufs) const noexcept -> async::Task<Result<std::size_t>> {
        constexpr auto              N = sizeof...(Ts);
        std::array<struct iovec, N> iovecs{
            iovec{
                  .iov_base = std::span<char>(bufs).data(),
                  .iov_len = std::span<char>(bufs).size_bytes(),
                  }
            ...
        };
        co_return co_await read_vectored(iovecs.data(), iovecs.size());
    }

    [[nodiscard]]
    auto write(const void *buf, std::size_t len) const noexcept {
        return async::write(fd_, buf, len, 0);
    }

    [[nodiscard]]
    auto write(std::span<const char> buf) const noexcept {
        return this->write(buf.data(), buf.size_bytes());
    }

    [[nodiscard]]
    auto write_all(std::span<const char> buf) const noexcept -> async::Task<Result<void>> {
        std::size_t has_written_bytes = 0;
        std::size_t remaining_bytes = buf.size_bytes();
        while (remaining_bytes > 0) {
            auto ret = co_await this->write(buf.data() + has_written_bytes, remaining_bytes);
            if (!ret) [[unlikely]] {
                co_return std::unexpected{ret.error()};
            }
            has_written_bytes += ret.value();
            remaining_bytes -= ret.value();
        }
        co_return Result<void>{};
    }

    [[nodiscard]]
    auto write_vectored(const struct iovec *iovecs, int nr_vecs) const noexcept {
        return async::writev(this->fd_, iovecs, nr_vecs, 0);
    }

    template <typename... Ts>
    [[nodiscard]]
    auto write_vectored(Ts &...bufs) const noexcept -> async::Task<Result<std::size_t>> {
        constexpr auto                          N = sizeof...(Ts);
        const std::array<const struct iovec, N> iovecs{
            iovec{
                  .iov_base = std::span<char>(bufs).data(),
                  .iov_len = std::span<char>(bufs).size_bytes(),
                  }
            ...
        };
        co_return co_await write_vectored(iovecs.data(), iovecs.size());
    }

    [[nodiscard]]
    auto send(std::span<const char> buf) const noexcept {
        return async::send(fd_, buf.data(), buf.size_bytes(), MSG_NOSIGNAL);
    }

    template <typename Addr>
        requires is_socket_address<Addr>
    [[nodiscard]]
    auto send_to(std::span<const char> buf, const Addr &addr) const noexcept {
        return async::sendto(fd_,
                             buf.data(),
                             buf.size_bytes(),
                             MSG_NOSIGNAL,
                             addr.sockaddr(),
                             addr.length());
    }

    [[nodiscard]]
    auto recv(std::span<char> buf) const noexcept {
        return read(buf);
    }

    [[nodiscard]]
    auto fd() const noexcept -> int {
        return fd_;
    }

    template <typename Addr>
        requires is_socket_address<Addr>
    [[nodiscard]]
    auto connect(const Addr &addr) const noexcept {
        return async::connect(fd_, addr.sockaddr(), addr.length());
    }

    template <typename Addr>
        requires is_socket_address<Addr>
    [[nodiscard]]
    auto bind(const Addr &addr) const noexcept -> Result<void> {
        if (::bind(fd_, addr.sockaddr(), addr.length()) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    [[nodiscard]]
    auto listen(int n) const noexcept -> Result<void> {
        if (::listen(fd_, n) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    template <typename Addr>
        requires is_socket_address<Addr>
    [[nodiscard]]
    auto local_addr() const noexcept -> Result<Addr> {
        struct sockaddr_storage addr {};
        socklen_t               len;
        if (::getsockname(fd_, reinterpret_cast<struct sockaddr *>(&addr), &len) == -1)
            [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return Addr{reinterpret_cast<struct sockaddr *>(&addr), len};
    }

    template <typename Addr>
        requires is_socket_address<Addr>
    [[nodiscard]]
    auto peer_addr() const noexcept -> Result<Addr> {
        struct sockaddr_storage addr {};
        socklen_t               len;
        if (::getpeername(fd_, reinterpret_cast<struct sockaddr *>(&addr), &len) == -1)
            [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return Addr{reinterpret_cast<struct sockaddr *>(&addr), len};
    }

    [[nodiscard]]
    auto set_reuseaddr(bool on) const noexcept -> Result<void> {
        auto optval{on ? 1 : 0};
        return set_sock_opt(SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    }

    [[nodiscard]]
    auto reuseaddr() const noexcept -> Result<bool> {
        auto optval = 0;
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)); ret)
            [[likely]] {
            return optval == 1;
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_reuseport(bool on) const noexcept -> Result<void> {
        auto optval{on ? 1 : 0};
        return set_sock_opt(SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    }

    [[nodiscard]]
    auto reuseport() const noexcept -> Result<bool> {
        auto optval = 0;
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)); ret)
            [[likely]] {
            return optval == 1;
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_ttl(uint32_t ttl) const noexcept -> Result<void> {
        return set_sock_opt(IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    }

    [[nodiscard]]
    auto ttl() const noexcept -> Result<uint32_t> {
        uint32_t optval = 0;
        if (auto ret = get_sock_opt(IPPROTO_IP, IP_TTL, &optval, sizeof(optval)); ret) [[likely]] {
            return optval;
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_linger(std::optional<std::chrono::seconds> duration) const noexcept -> Result<void> {
        struct linger lin {
            .l_onoff{0}, .l_linger{0},
        };
        if (duration.has_value()) {
            lin.l_onoff = 1;
            lin.l_linger = duration.value().count();
        }
        return set_sock_opt(SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    }

    [[nodiscard]]
    auto linger() const noexcept -> Result<std::optional<std::chrono::seconds>> {
        struct linger lin;
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_LINGER, &lin, sizeof(lin)); ret) [[likely]] {
            if (lin.l_onoff == 0) {
                return std::nullopt;
            } else {
                return std::chrono::seconds(lin.l_linger);
            }
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_broadcast(bool on) const noexcept -> Result<void> {
        auto optval{on ? 1 : 0};
        return set_sock_opt(SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
    }

    [[nodiscard]]
    auto broadcast() const noexcept -> Result<bool> {
        auto optval{0};
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)); ret)
            [[likely]] {
            return optval == 1;
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_keepalive(bool on) const noexcept {
        auto optval{on ? 1 : 0};
        return set_sock_opt(SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    }

    [[nodiscard]]
    auto keepalive() const noexcept -> Result<bool> {
        auto optval{0};
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)); ret)
            [[likely]] {
            return optval == 1;
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_recv_buffer_size(int size) const noexcept {
        return set_sock_opt(SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    }

    [[nodiscard]]
    auto recv_buffer_size() const noexcept -> Result<std::size_t> {
        int size = 0;
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)); ret) [[likely]] {
            return static_cast<std::size_t>(size);
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_send_buffer_size(int size) const noexcept {
        return set_sock_opt(SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }

    [[nodiscard]]
    auto send_buffer_size() const noexcept -> Result<std::size_t> {
        int size{0};
        if (auto ret = get_sock_opt(SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)); ret) [[likely]] {
            return static_cast<std::size_t>(size);
        } else {
            return std::unexpected{ret.error()};
        }
    }

    [[nodiscard]]
    auto set_nodelay(bool on) const noexcept -> Result<void> {
        auto flags = ::fcntl(fd_, F_GETFL, 0);
        if (on) {
            flags |= O_NDELAY;
        } else {
            flags &= ~O_NDELAY;
        }
        if (::fcntl(fd_, F_SETFL, flags) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    [[nodiscard]]
    auto nodelay() const noexcept -> Result<bool> {
        auto flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return flags & O_NDELAY;
    }

    [[nodiscard]]
    auto set_nonblocking(bool status) const noexcept -> Result<void> {
        auto flags = ::fcntl(fd_, F_GETFL, 0);
        if (status) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        if (::fcntl(fd_, F_SETFL, flags) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    [[nodiscard]]
    auto nonblocking() const noexcept -> Result<bool> {
        auto flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return flags & O_NONBLOCK;
    }

private:
    [[nodiscard]]
    auto set_sock_opt(int level, int optname, const void *optval, socklen_t optlen) const noexcept
        -> Result<void> {
        if (::setsockopt(fd_, level, optname, optval, optlen) == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    [[nodiscard]]
    auto get_sock_opt(int level, int optname, void *optval, socklen_t optlen) const noexcept
        -> Result<void> {
        if (auto ret = ::getsockopt(fd_, level, optname, optval, &optlen); ret == -1) [[unlikely]] {
            return std::unexpected{make_sys_error(errno)};
        }
        return {};
    }

    static void sync_close(int fd) noexcept {
        int ret = 0;
        int cnt = 3;
        do {
            if (ret = ::close(fd); ret == 0) [[likely]] {
                return;
            }
        } while (ret == EINTR && cnt--);
        LOG_ERROR("sync close {} failed, error: {}", ret, strerror(errno));
    }

public:
    [[nodiscard]]
    static auto build(int domain, int type, int protocol) -> Result<Socket> {
        if (auto fd = ::socket(domain, type, protocol); fd != -1) [[likely]] {
            return Socket{fd};
        } else {
            return std::unexpected{make_sys_error(errno)};
        }
    }

    [[nodiscard]]
    static auto from_fd(int fd) -> Socket {
        return Socket{fd};
    }

private:
    int fd_;
};

} // namespace zedio::net
