module;
#include <cstdint>
#include <vector>
#include <algorithm>

export module aleph.render.sw:span_buffer;

import aleph.math;

export namespace aleph::render::sw {

// SpanDrawFn kept as a convenience alias for callers that want type-erased storage.
// emit() itself is templated to avoid std::function<> instantiation at the module
// boundary (GCC 16 requires <typeinfo> in the TU that instantiates std::function).
template<class Fn>
concept SpanDrawCallable = requires(Fn f, int y, int x0, int x1) { f(y, x0, x1); };

class SpanBuffer {
public:
    SpanBuffer() = default;
    SpanBuffer(int w, int h)
        : covered_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0u),
          w_{w}, h_{h} {}

    void clear() noexcept {
        std::fill(covered_.begin(), covered_.end(), aleph::math::u8{0});
        pixels_drawn_ = 0;
        pixels_skipped_ = 0;
    }

    template<SpanDrawCallable Fn>
    void emit(int y, int x0, int x1, Fn&& fn) {
        if (y < 0 || y >= h_) return;
        if (x0 < 0) x0 = 0;
        if (x1 > w_) x1 = w_;
        if (x0 >= x1) return;

        aleph::math::u8* row =
            covered_.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(w_);
        int run_start = -1;
        for (int x = x0; x < x1; ++x) {
            if (!row[x]) {
                if (run_start < 0) run_start = x;
                row[x] = 1;
            } else {
                ++pixels_skipped_;
                if (run_start >= 0) {
                    fn(y, run_start, x);
                    pixels_drawn_ += x - run_start;
                    run_start = -1;
                }
            }
        }
        if (run_start >= 0) {
            fn(y, run_start, x1);
            pixels_drawn_ += x1 - run_start;
        }
    }

    int pixels_drawn()   const noexcept { return pixels_drawn_; }
    int pixels_skipped() const noexcept { return pixels_skipped_; }

private:
    std::vector<aleph::math::u8> covered_;
    int w_{0}, h_{0};
    int pixels_drawn_{0};
    int pixels_skipped_{0};
};

}  // namespace aleph::render::sw
