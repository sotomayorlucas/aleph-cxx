module;
#include <cstdint>

export module aleph.render.common:film;

import aleph.math;
import aleph.alloc;

export namespace aleph::render::common {

struct Film {
    aleph::math::Vec3* pixels;
    int width, height;
    int stride_pixels;
};

inline Film film_alloc(aleph::alloc::Arena& arena, int w, int h, int stride = 0) noexcept {
    const int s = (stride > 0) ? stride : w;
    const std::size_t bytes = static_cast<std::size_t>(s) * static_cast<std::size_t>(h)
                              * sizeof(aleph::math::Vec3);
    void* mem = arena.allocate(bytes, alignof(aleph::math::Vec3));
    Film f{};
    f.pixels = static_cast<aleph::math::Vec3*>(mem);
    f.width  = w;
    f.height = h;
    f.stride_pixels = s;
    return f;
}

}  // namespace aleph::render::common
