// Brick 10 (part 1) — Cross-platform memory-mapped file.
// On POSIX we use mmap + MAP_POPULATE/MADV_SEQUENTIAL; on Windows we use
// CreateFileMappingA / MapViewOfFile. Both expose the same (data, size) view.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nanomatch {

class MappedFile {
public:
    MappedFile() = default;
    explicit MappedFile(const std::string& path);
    ~MappedFile();

    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& o) noexcept;
    MappedFile& operator=(MappedFile&& o) noexcept;

    const std::byte* data() const noexcept { return data_; }
    std::size_t      size() const noexcept { return size_; }
    bool             ok()   const noexcept { return data_ != nullptr; }

private:
    void close_();
    const std::byte* data_ = nullptr;
    std::size_t      size_ = 0;
#if defined(_WIN32)
    void* file_   = nullptr;   // HANDLE
    void* mapping_= nullptr;   // HANDLE
#else
    int  fd_ = -1;
#endif
};

} // namespace nanomatch
