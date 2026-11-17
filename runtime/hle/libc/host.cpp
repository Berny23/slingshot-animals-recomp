// SPDX-License-Identifier: GPL-3.0-or-later
#include "host.hpp"

#include <dirent.h>
#include <fcntl.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <random>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#include <sys/mount.h>
#elif defined(__SWITCH__)
#include <switch.h>
#include <sys/statvfs.h>
#else
#include <sys/statfs.h>
#endif
#endif

namespace recomp::hle::host {

int linux_errno(int e) {
#ifdef __linux__
    return e;
#else
    switch (e) {
        case 0: return 0;
        case EPERM: return 1;
        case ENOENT: return 2;
        case ESRCH: return 3;
        case EINTR: return 4;
        case EIO: return 5;
        case ENXIO: return 6;
        case E2BIG: return 7;
        case ENOEXEC: return 8;
        case EBADF: return 9;
        case ECHILD: return 10;
        case EAGAIN: return 11;
        case ENOMEM: return 12;
        case EACCES: return 13;
        case EFAULT: return 14;
        case EBUSY: return 16;
        case EEXIST: return 17;
        case EXDEV: return 18;
        case ENODEV: return 19;
        case ENOTDIR: return 20;
        case EISDIR: return 21;
        case EINVAL: return 22;
        case ENFILE: return 23;
        case EMFILE: return 24;
        case ENOTTY: return 25;
        case EFBIG: return 27;
        case ENOSPC: return 28;
        case ESPIPE: return 29;
        case EROFS: return 30;
        case EMLINK: return 31;
        case EPIPE: return 32;
        case EDOM: return 33;
        case ERANGE: return 34;
        case EDEADLK: return 35;
        case ENAMETOOLONG: return 36;
        case ENOLCK: return 37;
        case ENOSYS: return 38;
        case ENOTEMPTY: return 39;
        case ELOOP: return 40;
        case EOVERFLOW: return 75;
        case ENOTSOCK: return 88;
        case EOPNOTSUPP: return 95;
        case EADDRINUSE: return 98;
        case ENETDOWN: return 100;
        case ENETUNREACH: return 101;
        case ECONNRESET: return 104;
        case ENOBUFS: return 105;
        case EISCONN: return 106;
        case ENOTCONN: return 107;
        case ETIMEDOUT: return 110;
        case ECONNREFUSED: return 111;
        case EHOSTUNREACH: return 113;
        case EALREADY: return 114;
        case EINPROGRESS: return 115;
        default: return 5;  // EIO
    }
#endif
}

namespace {
// Linux ARM open(2) flags.
constexpr uint32_t kAccMode = 03, kCreat = 0100, kExcl = 0200, kTrunc = 01000, kAppend = 02000, kNonBlock = 04000,
                   kDirectory = 040000, kNoFollow = 0100000, kCloExec = 02000000;
}  // namespace

int open(const char* path, uint32_t f, uint32_t mode) {
    int h = 0;
    switch (f & kAccMode) {
        case 0: h = O_RDONLY; break;
        case 1: h = O_WRONLY; break;
        default: h = O_RDWR; break;
    }
    if (f & kCreat) h |= O_CREAT;
    if (f & kExcl) h |= O_EXCL;
    if (f & kTrunc) h |= O_TRUNC;
    if (f & kAppend) h |= O_APPEND;
#ifdef _WIN32
    (void)kNonBlock; (void)kDirectory; (void)kNoFollow; (void)kCloExec;
    h |= O_BINARY | O_NOINHERIT;
    if (f & kDirectory) {
        errno = std::filesystem::is_directory(path) ? EACCES : ENOTDIR;  // no descriptors for directories
        return -1;
    }
    return ::_open(path, h, int(mode & 0200 ? (_S_IREAD | _S_IWRITE) : _S_IREAD));
#else
    if (f & kNonBlock) h |= O_NONBLOCK;
    if (f & kDirectory) h |= O_DIRECTORY;
    if (f & kNoFollow) h |= O_NOFOLLOW;
    if (f & kCloExec) h |= O_CLOEXEC;
    return ::open(path, h, mode_t(mode));
#endif
}

int open_null() {
#ifdef _WIN32
    return ::_open("NUL", O_RDONLY | O_BINARY | O_NOINHERIT);
#else
    return ::open("/dev/null", O_RDONLY | O_CLOEXEC);
#endif
}

timespec clock(int id) {
    timespec t{};
#ifdef __linux__
    ::clock_gettime(clockid_t(id), &t);
#else
    // Linux ids: 0 REALTIME, 2 PROCESS_CPUTIME_ID, 3 THREAD_CPUTIME_ID, the rest monotonic or boot time.
    clockid_t h = CLOCK_MONOTONIC;
    if (id == 0 || id == 8 /*REALTIME_ALARM*/) h = CLOCK_REALTIME;
#ifndef __SWITCH__  // no CPU-time clocks: monotonic
    else if (id == 2) h = CLOCK_PROCESS_CPUTIME_ID;
    else if (id == 3) h = CLOCK_THREAD_CPUTIME_ID;
#endif
    ::clock_gettime(h, &t);
#endif
    return t;
}

int process_id() {
#ifdef _WIN32
    return ::_getpid();
#else
    return int(::getpid());
#endif
}

int thread_id() {
#ifdef _WIN32
    return int(GetCurrentThreadId());
#elif defined(__APPLE__)
    uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return int(id);
#elif defined(__SWITCH__)
    uint64_t id = 0;
    svcGetThreadId(&id, CUR_THREAD_HANDLE);
    return int(id);
#else
    return int(::syscall(SYS_gettid));
#endif
}

int mkdir(const char* path, uint32_t mode) {
#ifdef _WIN32
    (void)mode;
    return ::_mkdir(path);
#else
    return ::mkdir(path, mode_t(mode));
#endif
}

int access(const char* path, int mode) {
#ifdef _WIN32
    return ::_access(path, mode & 06);  // no execute bit
#else
    return ::access(path, mode);
#endif
}

int rename(const char* from, const char* to) {
#ifdef _WIN32
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 0;
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: errno = EEXIST; break;
        case ERROR_DIR_NOT_EMPTY: errno = ENOTEMPTY; break;
        case ERROR_NOT_SAME_DEVICE: errno = EXDEV; break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION: errno = EACCES; break;
        default: errno = EIO; break;
    }
    return -1;
#elif defined(__SWITCH__)
    // Horizon's file system refuses an existing target too (the game saves through
    // <file>.tmp and a rename, so nothing would be saved after the first time).
    if (::rename(from, to) == 0) return 0;
    if (errno != EEXIST) return -1;
    struct stat src{}, dst{};
    if (::stat(from, &src) != 0 || ::stat(to, &dst) != 0) return -1;
    if (S_ISDIR(src.st_mode) != S_ISDIR(dst.st_mode)) {
        errno = S_ISDIR(dst.st_mode) ? EISDIR : ENOTDIR;
        return -1;
    }
    if ((S_ISDIR(dst.st_mode) ? ::rmdir(to) : ::unlink(to)) != 0) return -1;  // rmdir: ENOTEMPTY as in POSIX
    return ::rename(from, to);
#else
    return ::rename(from, to);
#endif
}

int64_t pread(int fd, void* buf, size_t n, int64_t off) {
#ifdef _WIN32
    const int64_t pos = ::_lseeki64(fd, 0, SEEK_CUR);
    if (pos < 0 || ::_lseeki64(fd, off, SEEK_SET) < 0) return -1;
    const int r = ::_read(fd, buf, unsigned(n));
    ::_lseeki64(fd, pos, SEEK_SET);
    return r;
#elif defined(__SWITCH__)  // newlib has no pread
    const off_t pos = ::lseek(fd, 0, SEEK_CUR);
    if (pos < 0 || ::lseek(fd, off_t(off), SEEK_SET) < 0) return -1;
    const ssize_t r = ::read(fd, buf, n);
    ::lseek(fd, pos, SEEK_SET);
    return r;
#else
    return ::pread(fd, buf, n, off_t(off));
#endif
}

int fsync(int fd) {
#ifdef _WIN32
    return ::_commit(fd);
#else
    return ::fsync(fd);
#endif
}

int truncate(int fd, int64_t size) {
#ifdef _WIN32
    return ::_chsize_s(fd, size) == 0 ? 0 : -1;
#else
    return ::ftruncate(fd, off_t(size));
#endif
}

int pipe(int fds[2]) {
#ifdef _WIN32
    return ::_pipe(fds, 65536, O_BINARY | O_NOINHERIT);
#elif defined(__SWITCH__)
    (void)fds;
    errno = ENOSYS;  // no pipes on the console
    return -1;
#else
    return ::pipe(fds);
#endif
}

bool valid_fd(int fd) {
#ifdef _WIN32
    return ::_get_osfhandle(fd) != -1;
#else
    return ::fcntl(fd, F_GETFD) >= 0;
#endif
}

uint32_t fd_flags(int fd) {
#ifdef _WIN32
    (void)fd;
    return 2;  // O_RDWR
#else
    const int h = ::fcntl(fd, F_GETFL);
    if (h < 0) return uint32_t(-1);
    uint32_t f = (h & O_ACCMODE) == O_WRONLY ? 1 : (h & O_ACCMODE) == O_RDWR ? 2 : 0;
    if (h & O_APPEND) f |= kAppend;
    if (h & O_NONBLOCK) f |= kNonBlock;
    return f;
#endif
}

int mkstemp(std::string& path) {
    if (path.size() < 6 || path.compare(path.size() - 6, 6, "XXXXXX")) {
        errno = EINVAL;
        return -1;
    }
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (size_t i = path.size() - 6; i < path.size(); ++i) path[i] = chars[rd() % 62];
        const int fd = open(path.c_str(), 2 | kCreat | kExcl | kCloExec, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}

std::string file_mode(const char* mode) {
    std::string m = mode;
#ifdef _WIN32
    if (m.find('b') == std::string::npos) m += 'b';
#endif
    return m;
}

std::FILE* memory_stream(const std::string& data) {
#ifdef _WIN32
    std::FILE* f = std::tmpfile();
    if (!f) return nullptr;
    std::fwrite(data.data(), 1, data.size(), f);
#else
    std::FILE* f = ::fmemopen(nullptr, data.size() + 1, "w+");  // the buffer goes with fclose
    if (!f) return nullptr;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fflush(f);
    // fmemopen's size is the buffer's: reads end at the contents' end.
#endif
    std::rewind(f);
    return f;
}

int64_t ftell(std::FILE* f) {
#ifdef _WIN32
    return ::_ftelli64(f);
#else
    return ::ftello(f);
#endif
}

int seek(std::FILE* f, int64_t off, int whence) {
#ifdef _WIN32
    return ::_fseeki64(f, off, whence);
#else
    return ::fseeko(f, off_t(off), whence);
#endif
}

namespace {
#ifdef _WIN32
using HostStat = struct _stat64;
void convert(const HostStat& s, Stat& o) {
    o.dev = uint64_t(s.st_dev);
    o.ino = uint64_t(s.st_ino);
    o.mode = uint32_t(s.st_mode);
    o.nlink = uint32_t(s.st_nlink);
    o.rdev = uint64_t(s.st_rdev);
    o.size = uint64_t(s.st_size);
    o.blocks = (o.size + 511) / 512;
    o.atime = s.st_atime;
    o.mtime = s.st_mtime;
    o.ctime = s.st_ctime;
}
#else
using HostStat = struct ::stat;
void convert(const HostStat& s, Stat& o) {
    o.dev = uint64_t(s.st_dev);
    o.ino = uint64_t(s.st_ino);
    o.mode = uint32_t(s.st_mode);
    o.nlink = uint32_t(s.st_nlink);
    o.rdev = uint64_t(s.st_rdev);
    o.size = uint64_t(s.st_size);
    o.blksize = uint32_t(s.st_blksize);
    o.blocks = uint64_t(s.st_blocks);
#ifdef __APPLE__
    const timespec a = s.st_atimespec, m = s.st_mtimespec, c = s.st_ctimespec;
#else
    const timespec a = s.st_atim, m = s.st_mtim, c = s.st_ctim;
#endif
    o.atime = a.tv_sec; o.atime_ns = uint32_t(a.tv_nsec);
    o.mtime = m.tv_sec; o.mtime_ns = uint32_t(m.tv_nsec);
    o.ctime = c.tv_sec; o.ctime_ns = uint32_t(c.tv_nsec);
}
#endif
}  // namespace

bool stat(const char* path, Stat& out) {
    HostStat s;
#ifdef _WIN32
    if (::_stat64(path, &s) != 0) return false;
#else
    if (::stat(path, &s) != 0) return false;
#endif
    convert(s, out);
    return true;
}

bool fstat(int fd, Stat& out) {
    HostStat s;
#ifdef _WIN32
    if (::_fstat64(fd, &s) != 0) return false;
#else
    if (::fstat(fd, &s) != 0) return false;
#endif
    convert(s, out);
    return true;
}

bool statfs(const char* path, DiskSpace& out) {
#ifdef _WIN32
    ULARGE_INTEGER avail, total, free;
    if (!GetDiskFreeSpaceExA(path, &avail, &total, &free)) {
        errno = ENOENT;
        return false;
    }
    out.blocks = total.QuadPart / out.bsize;
    out.bfree = free.QuadPart / out.bsize;
    out.bavail = avail.QuadPart / out.bsize;
    return true;
#elif defined(__SWITCH__)
    struct ::statvfs s;
    if (::statvfs(path, &s) != 0) return false;
    out.bsize = uint32_t(s.f_frsize ? s.f_frsize : s.f_bsize);
    out.blocks = uint64_t(s.f_blocks);
    out.bfree = uint64_t(s.f_bfree);
    out.bavail = uint64_t(s.f_bavail);
    out.files = uint64_t(s.f_files);
    out.ffree = uint64_t(s.f_ffree);
    return true;
#else
    struct ::statfs s;
    if (::statfs(path, &s) != 0) return false;
    out.bsize = uint32_t(s.f_bsize);
    out.blocks = uint64_t(s.f_blocks);
    out.bfree = uint64_t(s.f_bfree);
    out.bavail = uint64_t(s.f_bavail);
    out.files = uint64_t(s.f_files);
    out.ffree = uint64_t(s.f_ffree);
    return true;
#endif
}

struct Dir {
    DIR* d;
    std::string path;
};

Dir* opendir(const char* path) {
    DIR* d = ::opendir(path);
    return d ? new Dir{d, path} : nullptr;
}

bool readdir(Dir* d, DirEntry& out) {
    errno = 0;
    dirent* e = ::readdir(d->d);
    if (!e) return false;
    out.name = e->d_name;
#ifdef _WIN32
    out.ino = 0;
    Stat s;
    out.type = stat((d->path + "/" + out.name).c_str(), s) ? ((s.mode & S_IFMT) == S_IFDIR ? 4 : 8) : 0;
#else
    out.ino = uint64_t(e->d_ino);
    out.type = e->d_type;  // DT_* values are the same on Linux, macOS and Android
#endif
    return true;
}

void closedir(Dir* d) {
    ::closedir(d->d);
    delete d;
}

int fnmatch(const char* p, const char* s, int flags) {
    constexpr int kNoEscape = 1, kPathName = 2, kPeriod = 4, kCaseFold = 16, kNoMatch = 1;
    const bool fold = flags & kCaseFold;
    auto eq = [&](char a, char b) { return fold ? std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)) : a == b; };
    const char* start = s;
    auto leading_period = [&](const char* at) {
        return (flags & kPeriod) && *at == '.' && (at == start || ((flags & kPathName) && at[-1] == '/'));
    };
    // Iterative matching with backtracking to the last '*'.
    const char *star_p = nullptr, *star_s = nullptr;
    while (*s) {
        if (*p == '*' && !leading_period(s)) {
            while (*p == '*') ++p;
            star_p = p;
            star_s = s;
            continue;
        }
        bool ok = false;
        const char* next = p + 1;
        if (*p == '?') {
            ok = !((flags & kPathName) && *s == '/') && !leading_period(s);
        } else if (*p == '[') {
            const char* q = p + 1;
            const bool negate = *q == '!' || *q == '^';
            if (negate) ++q;
            bool in = false;
            bool closed = false;
            for (bool first = true; *q && (first || *q != ']'); first = false) {
                char lo = *q++;
                if (lo == '\\' && !(flags & kNoEscape) && *q) lo = *q++;
                char hi = lo;
                if (*q == '-' && q[1] && q[1] != ']') {
                    hi = q[1];
                    q += 2;
                }
                const char c = fold ? char(std::tolower(uint8_t(*s))) : *s;
                if ((c >= (fold ? char(std::tolower(uint8_t(lo))) : lo) && c <= (fold ? char(std::tolower(uint8_t(hi))) : hi)))
                    in = true;
            }
            if (*q == ']') {
                closed = true;
                next = q + 1;
            }
            ok = closed && in != negate && !((flags & kPathName) && *s == '/');
            if (!closed) ok = eq(*p, *s);
        } else if (*p == '\\' && !(flags & kNoEscape) && p[1]) {
            ok = eq(p[1], *s);
            next = p + 2;
        } else if (*p) {
            ok = eq(*p, *s);
        }
        if (ok) {
            p = next;
            ++s;
        } else if (star_p && !((flags & kPathName) && *star_s == '/')) {
            p = star_p;
            s = ++star_s;
        } else {
            return kNoMatch;
        }
    }
    while (*p == '*') ++p;
    return *p ? kNoMatch : 0;
}

}  // namespace recomp::hle::host
