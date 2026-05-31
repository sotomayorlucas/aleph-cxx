#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

import aleph.cpu;

namespace aleph_bench {

// Run `f(iters)` repeatedly, measuring true core cycles per iteration via a
// perf_event counter (frequency-invariant), and report the median across
// `n_samples`.
template<typename F>
void bench(std::string_view name, F&& f, int n_samples = 50, std::uint64_t iters = 100000) {
    std::vector<double> cycles_per_op;
    cycles_per_op.reserve(static_cast<std::size_t>(n_samples));

    aleph::cpu::CycleCounter ctr;

    // Warm-up to populate caches and ramp the core frequency.
    for (int w = 0; w < 5; ++w) (void)f(iters);

    for (int s = 0; s < n_samples; ++s) {
        ctr.start();
        auto sink = f(iters);
        const std::uint64_t cyc_total = ctr.stop();
        // Force `sink` to be live so the compiler doesn't elide the work.
        asm volatile("" :: "r"(&sink) : "memory");
        const double cyc = static_cast<double>(cyc_total) / static_cast<double>(iters);
        cycles_per_op.push_back(cyc);
    }
    std::sort(cycles_per_op.begin(), cycles_per_op.end());
    const double median = cycles_per_op[static_cast<std::size_t>(n_samples) / 2];
    std::printf("  %-40.40s  %7.2f cyc/op  (median of %d samples)\n",
                std::string{name}.c_str(), median, n_samples);
}

}  // namespace aleph_bench
