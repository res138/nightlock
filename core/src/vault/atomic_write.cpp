#include "vault/atomic_write.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <system_error>

namespace nightlock::io {

namespace {

#ifdef _WIN32

bool writeAll(HANDLE file, std::span<const std::uint8_t> data) {
    const std::uint8_t* p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const DWORD chunk =
            left > 0x0FFFFFFFu ? 0x0FFFFFFFu : static_cast<DWORD>(left);
        DWORD written = 0;
        if (!WriteFile(file, p, chunk, &written, nullptr) || written == 0)
            return false;
        p += written;
        left -= written;
    }
    return true;
}

#else

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

#endif

}  // namespace

#ifdef _WIN32

bool atomicReplace(const std::filesystem::path& path,
                   std::span<const std::uint8_t> head,
                   std::span<const std::uint8_t> tail) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    const HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    bool ok = writeAll(file, head) && writeAll(file, tail) &&
              FlushFileBuffers(file) != 0;
    ok = (CloseHandle(file) != 0) && ok;
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::path bak = path;
        bak += ".bak";
        // Best-effort: a failed backup must not block the save itself.
        MoveFileExW(path.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    // WRITE_THROUGH flushes the rename's metadata before returning —
    // the closest Windows gets to POSIX's fsync-the-directory step.
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

#else

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

#endif

}  // namespace nightlock::io
