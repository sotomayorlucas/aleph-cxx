import aleph.render.common;
import aleph.math;

int main() {
    auto c = aleph::render::common::make_camera(
        aleph::math::Vec3{0,0,5}, aleph::math::Vec3{0,0,0}, aleph::math::Vec3{0,1,0},
        60.0f, 100, 100, 0.0f, 1.0f);
    return c.has_defocus ? 1 : 0;
}
