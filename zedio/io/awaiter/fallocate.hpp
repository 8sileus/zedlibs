#pragma once

#include "zedio/io/base/registrator.hpp"

namespace zedio::io {

namespace detail {

    class FAllocate : public IORegistrator<FAllocate> {
    private:
        using Super = IORegistrator<FAllocate>;

    public:
        FAllocate(int fd, int mode, __u64 offset, __u64 len)
            : Super{io_uring_prep_fallocate, fd, mode, offset, len} {}

        auto await_resume() const noexcept -> Result<void> {
            if (this->cb_.result_ >= 0) [[likely]] {
                return {};
            } else {
                return std::unexpected{make_sys_error(-this->cb_.result_)};
            }
        }
    };

} // namespace detail

[[REMEMBER_CO_AWAIT]]
static inline auto fallocate(int fd, int mode, __u64 offset, __u64 len) {
    return detail::FAllocate{fd, mode, offset, len};
}

} // namespace zedio::io
