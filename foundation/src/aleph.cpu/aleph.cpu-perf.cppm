module;
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

export module aleph.cpu:perf;

export namespace aleph::cpu {

// Frequency-invariant per-thread core-cycle counter backed by a single
// perf_event (PERF_COUNT_HW_CPU_CYCLES). Unlike rdtscp — which counts the
// fixed-rate TSC and therefore conflates frequency with work — this counts
// actual CPU_CLK_UNHALTED cycles regardless of turbo/DVFS, the correct unit
// for microbenchmarks. Linux only; requires perf_event_paranoid <= 2.
// Hard-errors (throws) if the event cannot be opened — this is a local
// development gate, not portable infrastructure.
// NOTE (hybrid CPUs): a hardware-cycles event reads 0 while the calling thread
// runs on a different core type than the one it was scheduled on at enable time
// (e.g. the E-cores on Meteor Lake). Pin the measuring thread to a consistent
// P-core for valid counts — the bench harness and run-baselines.sh do this.
class CycleCounter {
public:
    CycleCounter() {
        perf_event_attr attr{};
        attr.type           = PERF_TYPE_HARDWARE;
        attr.size           = sizeof(attr);
        attr.config         = PERF_COUNT_HW_CPU_CYCLES;
        attr.disabled       = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv     = 1;
        fd_ = static_cast<int>(
            ::syscall(__NR_perf_event_open, &attr, /*pid=*/0, /*cpu=*/-1,
                      /*group_fd=*/-1, /*flags=*/0UL));
        if (fd_ < 0) {
            const int err = errno;
            throw std::runtime_error(
                std::string{"CycleCounter: perf_event_open failed: "} +
                std::strerror(err));
        }
    }

    CycleCounter(const CycleCounter&)            = delete;
    CycleCounter& operator=(const CycleCounter&) = delete;

    ~CycleCounter() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Reset the counter to 0 and begin counting.
    void start() noexcept {
        ::ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
        ::ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
    }

    // Stop counting and return cycles elapsed since the last start().
    std::uint64_t stop() noexcept {
        ::ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
        std::uint64_t count = 0;
        const ssize_t n = ::read(fd_, &count, sizeof(count));
        return (n == static_cast<ssize_t>(sizeof(count))) ? count : 0;
    }

private:
    int fd_{-1};
};

}  // namespace aleph::cpu
