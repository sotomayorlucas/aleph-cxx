import aleph.editor;
import aleph.math;

int main() {
    aleph::editor::OrbitCam c{aleph::math::Vec3{0,0,0}, 0.0f, 0.0f, 5.0f};
    auto e = aleph::editor::orbit_eye(c);
    return e.z >= 4.9f && e.z <= 5.1f ? 0 : 1;
}
