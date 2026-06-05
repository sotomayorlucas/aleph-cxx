module;
export module aleph.render.sw:downsample;
import aleph.math;
import aleph.render.common;   // Film
export namespace aleph::render::sw {
// Average each ss×ss block of `src` (ss*dst.width × ss*dst.height) into `dst`,
// indexing rows by each film's own stride_pixels. Linear Vec3 average (pre-tonemap),
// fixed summation order → deterministic. ss==1 is an exact copy.
inline void downsample_box(const aleph::render::common::Film& src,
                           aleph::render::common::Film& dst, int ss) noexcept {
    const aleph::math::f32 inv = 1.0f / static_cast<aleph::math::f32>(ss * ss);
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            aleph::math::Vec3 acc{0.0f, 0.0f, 0.0f};
            for (int j = 0; j < ss; ++j) {
                const int sy = y * ss + j;
                for (int i = 0; i < ss; ++i)
                    acc = acc + src.pixels[sy * src.stride_pixels + (x * ss + i)];
            }
            dst.pixels[y * dst.stride_pixels + x] = acc * inv;
        }
    }
}
}  // namespace aleph::render::sw
