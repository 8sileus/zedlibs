#pragma once

#include <thread>

namespace zedio::async::detail {

struct Config {
    /// worker

    // num of worker
    std::size_t num_worker_{std::thread::hardware_concurrency()};
    // How many ticks worker to poll finished I/O operation
    uint32_t check_io_interval_{61};
    // How many ticks worker to pop global queue
    uint32_t check_global_interval_{61};

    /// poller

    // size of io_uring_queue entries
    std::size_t ring_entries_{1024};
    // io_uring flags
    uint32_t io_uring_flags_{0};

    // static
    static constexpr std::size_t LOCAL_QUEUE_CAPACITY{256};
    static constexpr std::size_t FIXED_FILES_NUM{10};
};

} // namespace zedio::async::detail