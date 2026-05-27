module;
#include <array>
#include <cmath>

export module aleph.math:mat;

import :types;
import :vec;

export namespace aleph::math {

// 3x3 col-major. Padded to 3x Vec3 = 48 B with alignment 16.
struct alignas(16) Mat3 {
    std::array<f32, 12> m{};   // 3 columns x 4 floats (last = pad)

    static constexpr Mat3 identity() noexcept {
        Mat3 r{};
        r.m[0] = r.m[5] = r.m[10] = 1.0f;
        return r;
    }
};

[[nodiscard]] constexpr Vec3 operator*(const Mat3& a, Vec3 v) noexcept {
    return {
        a.m[0]*v.x + a.m[4]*v.y + a.m[8] *v.z,
        a.m[1]*v.x + a.m[5]*v.y + a.m[9] *v.z,
        a.m[2]*v.x + a.m[6]*v.y + a.m[10]*v.z,
    };
}

// 4x4 col-major. Element (row r, col c) at m[c*4 + r]. 64 B = 1 cache line.
struct alignas(64) Mat4 {
    std::array<f32, 16> m{};

    constexpr f32&       operator()(int r, int c)       noexcept { return m[static_cast<usize>(c*4 + r)]; }
    constexpr const f32& operator()(int r, int c) const noexcept { return m[static_cast<usize>(c*4 + r)]; }

    static constexpr Mat4 zero() noexcept { return Mat4{}; }

    static constexpr Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static constexpr Mat4 translate(Vec3 t) noexcept {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static constexpr Mat4 scale(Vec3 s) noexcept {
        Mat4 r{};
        r.m[0]  = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 rotate_x(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[5] = c;  r.m[6]  = s;
        r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotate_y(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c;  r.m[2]  = -s;
        r.m[8] = s;  r.m[10] = c;
        return r;
    }

    static Mat4 rotate_z(f32 a) noexcept {
        Mat4 r = identity();
        const f32 c = std::cos(a), s = std::sin(a);
        r.m[0] = c;  r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 perspective(f32 fov_y, f32 aspect, f32 near, f32 far) noexcept {
        Mat4 r{};
        const f32 fy = 1.0f / std::tan(fov_y * 0.5f);
        r.m[0]  = fy / aspect;
        r.m[5]  = fy;
        r.m[10] = (far + near) / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far * near) / (near - far);
        return r;
    }

    static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
        const Vec3 f = normalize(target - eye);
        const Vec3 s = normalize(cross(f, up));
        const Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0]  =  s.x; r.m[4]  =  s.y; r.m[8]   =  s.z;
        r.m[1]  =  u.x; r.m[5]  =  u.y; r.m[9]   =  u.z;
        r.m[2]  = -f.x; r.m[6]  = -f.y; r.m[10]  = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] =  dot(f, eye);
        return r;
    }
};

[[nodiscard]] constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r{};
    for (usize j = 0; j < 4; ++j)
        for (usize i = 0; i < 4; ++i) {
            f32 s = 0.0f;
            for (usize k = 0; k < 4; ++k) s += a.m[k*4 + i] * b.m[j*4 + k];
            r.m[j*4 + i] = s;
        }
    return r;
}

[[nodiscard]] constexpr Vec4 operator*(const Mat4& a, Vec4 v) noexcept {
    return {
        a.m[0]*v.x + a.m[4]*v.y + a.m[8] *v.z + a.m[12]*v.w,
        a.m[1]*v.x + a.m[5]*v.y + a.m[9] *v.z + a.m[13]*v.w,
        a.m[2]*v.x + a.m[6]*v.y + a.m[10]*v.z + a.m[14]*v.w,
        a.m[3]*v.x + a.m[7]*v.y + a.m[11]*v.z + a.m[15]*v.w,
    };
}

}  // namespace aleph::math
