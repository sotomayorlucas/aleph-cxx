module;
#include <cstddef>
#include <span>
#include <vector>
#include <string>
#include <expected>
#include <cstring>
#include <charconv>
#include <utility>

export module aleph.io:ppm;

export namespace aleph::io {

struct Image {
    int                    width{0};
    int                    height{0};
    std::vector<std::byte> pixels;   // RGB8 interleaved, row-major
};

namespace detail {

inline std::size_t skip_ws(std::span<const std::byte> b, std::size_t i) {
    while (i < b.size()) {
        char c = static_cast<char>(b[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
        if (c == '#') { while (i < b.size() && static_cast<char>(b[i]) != '\n') ++i; continue; }
        return i;
    }
    return i;
}

inline std::expected<std::pair<int, std::size_t>, std::string>
parse_int(std::span<const std::byte> b, std::size_t i) {
    std::size_t j = i;
    while (j < b.size()) {
        char c = static_cast<char>(b[j]);
        if (c < '0' || c > '9') break;
        ++j;
    }
    if (i == j) return std::unexpected("expected integer");
    int v = 0;
    auto [_, ec] = std::from_chars(
        reinterpret_cast<const char*>(b.data()) + i,
        reinterpret_cast<const char*>(b.data()) + j, v);
    if (ec != std::errc{}) return std::unexpected("integer parse error");
    return std::pair{v, j};
}

}  // namespace detail

inline std::expected<Image, std::string>
load_ppm(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 3 ||
        static_cast<char>(bytes[0]) != 'P' || static_cast<char>(bytes[1]) != '6')
        return std::unexpected("PPM: expected P6 magic");

    std::size_t i = 2;
    i = detail::skip_ws(bytes, i);
    auto w = detail::parse_int(bytes, i);
    if (!w) return std::unexpected(w.error());
    i = detail::skip_ws(bytes, w->second);

    auto h = detail::parse_int(bytes, i);
    if (!h) return std::unexpected(h.error());
    i = detail::skip_ws(bytes, h->second);

    auto m = detail::parse_int(bytes, i);
    if (!m) return std::unexpected(m.error());
    if (m->first != 255) return std::unexpected("PPM: maxval must be 255");
    i = m->second;
    if (i >= bytes.size()) return std::unexpected("PPM: truncated");
    ++i;   // consume the mandatory whitespace
    // Also consume any optional extra whitespace some writers emit
    // (e.g. ImageMagick / gimp write \n\n between maxval and pixels).
    // Note: do NOT use skip_ws here — that also skips '#' comment lines,
    // which are NOT valid between maxval and pixel data per P6 spec.
    while (i < bytes.size()) {
        const char c = static_cast<char>(bytes[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
        else break;
    }

    const std::size_t n = static_cast<std::size_t>(w->first) *
                          static_cast<std::size_t>(h->first) * 3u;
    if (bytes.size() - i < n) return std::unexpected("PPM: truncated pixel data");

    Image img;
    img.width  = w->first;
    img.height = h->first;
    img.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(i),
                      bytes.begin() + static_cast<std::ptrdiff_t>(i + n));
    return img;
}

}  // namespace aleph::io
