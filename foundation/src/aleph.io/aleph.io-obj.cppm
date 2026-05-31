module;
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>

export module aleph.io:obj;

export namespace aleph::io {

struct Vec3f { float x{}, y{}, z{}; };

struct ObjMesh {
    std::vector<Vec3f>              verts;
    std::vector<std::array<int, 3>> tris;
};

inline std::expected<ObjMesh, std::string>
load_obj(std::span<const std::byte> bytes) noexcept {
    ObjMesh mesh;
    std::size_t i = 0;
    while (i < bytes.size()) {
        std::size_t j = i;
        while (j < bytes.size() && static_cast<char>(bytes[j]) != '\n') ++j;
        std::string_view line{
            reinterpret_cast<const char*>(bytes.data()) + i, j - i};
        i = j + 1;

        // Copy line into a null-terminated stack buffer before sscanf.
        // sscanf requires null-terminated input; line.data() points into an
        // mmap-backed region which may NOT be null-terminated, and overreading
        // could fault into an unmapped page.
        char buf[256];
        const std::size_t len = std::min(line.size(), sizeof(buf) - 1);
        std::memcpy(buf, line.data(), len);
        buf[len] = '\0';

        if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
            Vec3f v;
            if (std::sscanf(buf, "v %f %f %f", &v.x, &v.y, &v.z) == 3)
                mesh.verts.push_back(v);
        } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
            std::array<int, 16> idx{};
            std::size_t n_idx = 0;
            const char* p   = buf + 2;
            const char* end = buf + len;
            while (p < end && n_idx < 16) {
                while (p < end && (*p == ' ' || *p == '\t')) ++p;
                if (p >= end) break;
                int v = 0;
                if (std::sscanf(p, "%d", &v) != 1) break;
                if (v < 0) v = static_cast<int>(mesh.verts.size()) + v + 1;
                idx[n_idx++] = v - 1;
                while (p < end && *p != ' ' && *p != '\t') ++p;
            }
            // Fan-triangulate: need at least 3 vertices.
            if (n_idx < 3) continue;
            for (std::size_t k = 1; k < n_idx - 1; ++k) {
                const int a = idx[0], b = idx[k], c = idx[k + 1];
                const int n = static_cast<int>(mesh.verts.size());
                if (a < 0 || a >= n || b < 0 || b >= n || c < 0 || c >= n) continue;
                mesh.tris.push_back({a, b, c});
            }
        }
    }
    return mesh;
}

}  // namespace aleph::io
