import aleph.render.rt;
import aleph.render.common;
import aleph.scene;
import aleph.math;

int main() {
    aleph::scene::Scene s;
    aleph::render::common::Pcg32 rng(0, 0);
    auto v = aleph::render::rt::ray_color(s, aleph::math::Ray{}, 0,
        aleph::render::common::Sky{aleph::math::Vec3{}, aleph::math::Vec3{}}, true, rng);
    return aleph::math::length_sq(v) == 0.0f ? 0 : 1;
}
