#include "nanomatch/file_map.hpp"

#include <utility>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace nanomatch {

#if defined(_WIN32)
MappedFile::MappedFile(const std::string& path) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart == 0) { CloseHandle(f); return; }
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) { CloseHandle(f); return; }
    void* p = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!p) { CloseHandle(m); CloseHandle(f); return; }
    file_    = f;
    mapping_ = m;
    data_    = static_cast<const std::byte*>(p);
    size_    = static_cast<std::size_t>(sz.QuadPart);
}
void MappedFile::close_() {
    if (data_)    UnmapViewOfFile(const_cast<std::byte*>(data_));
    if (mapping_) CloseHandle(mapping_);
    if (file_)    CloseHandle(file_);
    data_ = nullptr; mapping_ = nullptr; file_ = nullptr; size_ = 0;
}
#else
MappedFile::MappedFile(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return;
    struct stat st{};
    if (::fstat(fd_, &st) != 0 || st.st_size == 0) { ::close(fd_); fd_ = -1; return; }
    void* p = ::mmap(nullptr, st.st_size, PROT_READ,
                     MAP_PRIVATE | MAP_POPULATE, fd_, 0);
    if (p == MAP_FAILED) { ::close(fd_); fd_ = -1; return; }
    ::madvise(p, st.st_size, MADV_SEQUENTIAL);
    data_ = static_cast<const std::byte*>(p);
    size_ = static_cast<std::size_t>(st.st_size);
}
void MappedFile::close_() {
    if (data_) ::munmap(const_cast<std::byte*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
    data_ = nullptr; size_ = 0; fd_ = -1;
}
#endif

MappedFile::~MappedFile() { close_(); }

MappedFile::MappedFile(MappedFile&& o) noexcept { *this = std::move(o); }
MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
    if (this != &o) {
        close_();
        data_ = o.data_; size_ = o.size_;
#if defined(_WIN32)
        file_ = o.file_; mapping_ = o.mapping_;
        o.file_ = nullptr; o.mapping_ = nullptr;
#else
        fd_ = o.fd_; o.fd_ = -1;
#endif
        o.data_ = nullptr; o.size_ = 0;
    }
    return *this;
}

} // namespace nanomatch
