module;
#include <cstdint>
#include <algorithm>

export module aleph.render.sw:span_buffer;

import aleph.math;

export namespace aleph::render::sw {

// SpanDrawFn kept as a convenience alias for callers that want type-erased storage.
// emit() itself is templated to avoid std::function<> instantiation at the module
// boundary (GCC 16 requires <typeinfo> in the TU that instantiates std::function).
template<class Fn>
concept SpanDrawCallable = requires(Fn f, int y, int x0, int x1) { f(y, x0, x1); };

// A per-scanline span dispatcher: clamps [x0,x1) to the framebuffer width, then
// hands the visible run to the draw callback. It does NOT perform hidden-surface
// removal — that is the per-pixel 1/w z-buffer's job (see :rast_scan).
//
// HISTORY: this used to be a coverage / C-buffer that, paired with a front-to-back
// painter sort by FACE CENTRE depth, skipped pixels already drawn this frame. That
// is correct only when centre-depth order matches the true per-pixel order — which
// FAILS for a large triangle (e.g. the floor quad, whose near half's centroid
// sorts in front of a sphere resting on it). The floor then claimed the sphere's
// lower-half pixels and the sphere's spans were skipped before any depth test ran,
// burying the sphere. Correct occlusion needs per-pixel depth, so HSR moved to the
// z-buffer and this type is now a thin span clamp (kept so callers' signatures and
// the `pixels_drawn` instrumentation are unchanged).
class SpanBuffer {
public:
    SpanBuffer() = default;
    SpanBuffer(int w, int h) : w_{w}, h_{h} {}

    void clear() noexcept { pixels_drawn_ = 0; }

    template<SpanDrawCallable Fn>
    void emit(int y, int x0, int x1, Fn&& fn) {
        if (y < 0 || y >= h_) return;
        if (x0 < 0) x0 = 0;
        if (x1 > w_) x1 = w_;
        if (x0 >= x1) return;
        fn(y, x0, x1);
        pixels_drawn_ += x1 - x0;
    }

    int pixels_drawn() const noexcept { return pixels_drawn_; }

private:
    int w_{0}, h_{0};
    int pixels_drawn_{0};
};

}  // namespace aleph::render::sw
