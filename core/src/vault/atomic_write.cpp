#include "vault/atomic_write.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace nightlock::io {

namespace {

bool writeAll(int fd, std::span<const std::uint8_t> data) {
    const std::uint8_t* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const ssize_t written = ::write(fd, p, left);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += written;
        left -= static_cast<std::size_t>(written);
    }
    return true;
}

bool flushToDisk(int fd) {
#if defined(F_FULLFSYNC)
    // fsync on APFS stops at the drive cache; F_FULLFSYNC forces the
    // platter. Some filesystems reject it — fall back to fsync there.
    if (::fcntl(fd, F_FULLFSYNC) == 0)
        return true;
#endif
    return ::fsync(fd) == 0;
}

}  // namespace

bool atomicReplace(const std::filesystem::path& path,
                   std::span<const std::uint8_t> head,
                   std::span<const std::uint8_t> tail) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return false;
    bool ok = writeAll(fd, head) && writeAll(fd, tail) && flushToDisk(fd);
    ok = (::close(fd) == 0) && ok;
    if (!ok) {
        ::unlink(tmp.c_str());
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::path bak = path;
        bak += ".bak";
        // Best-effort: a failed backup must not block the save itself.
        ::rename(path.c_str(), bak.c_str());
    }

    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }

    // Persist the rename. Required on Linux; harmless elsewhere. The
    // parent may be empty for relative paths — best-effort by design.
    const std::filesystem::path dir = path.parent_path();
    if (!dir.empty()) {
        const int dfd = ::open(dir.c_str(), O_RDONLY);
        if (dfd >= 0) {
            ::fsync(dfd);
            ::close(dfd);
        }
    }
    return true;
}

}  // namespace nightlock::io
