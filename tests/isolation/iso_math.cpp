import aleph.math;

int main() {
    aleph::math::Vec3 v{1, 2, 3};
    return static_cast<int>(dot(v, v)) - 14;
}
