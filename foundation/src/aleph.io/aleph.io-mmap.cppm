module;
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <expected>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

export module aleph.io:mmap;

export namespace aleph::io {

class MappedFile {
public:
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept
        : data_{o.data_}, size_{o.size_} { o.data_ = nullptr; o.size_ = 0; }
    MappedFile& operator=(MappedFile&& o) noexcept {
        if (this != &o) { release(); data_ = o.data_; size_ = o.size_;
                           o.data_ = nullptr; o.size_ = 0; }
        return *this;
    }
    ~MappedFile() { release(); }

    static std::expected<MappedFile, std::string>
    open_read(std::string_view path) noexcept {
        std::string p{path};
        const int fd = ::open(p.c_str(), O_RDONLY);
        if (fd < 0) return std::unexpected("open failed: " + p);
        struct stat st{};
        if (::fstat(fd, &st) < 0) { ::close(fd);
                                     return std::unexpected("fstat failed: " + p); }
        const std::size_t sz = static_cast<std::size_t>(st.st_size);
        void* map = ::mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED) return std::unexpected("mmap failed: " + p);
        MappedFile mf;
        mf.data_ = static_cast<const std::byte*>(map);
        mf.size_ = sz;
        return mf;
    }

    std::span<const std::byte> bytes() const noexcept { return {data_, size_}; }

private:
    void release() noexcept {
        if (data_) { ::munmap(const_cast<std::byte*>(data_), size_); data_ = nullptr; size_ = 0; }
    }
    const std::byte* data_{nullptr};
    std::size_t      size_{0};
};

}  // namespace aleph::io
