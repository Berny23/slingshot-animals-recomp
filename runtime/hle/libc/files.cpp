// SPDX-License-Identifier: GPL-3.0-or-later
// POSIX file and directory functions, and <stdio.h> streams.
//
// Guest file descriptors are host descriptors. Guest FILE* and DIR* are small
// opaque guest allocations mapped to host objects.
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <libgen.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utime.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "libc.hpp"
#include "recomp/game.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::hle {
namespace {

using u32 = uint32_t;
using mem::str;

int fail(int e) {
    set_guest_errno(e);
    return -1;
}
int ret(int v) {
    if (v < 0) set_guest_errno(errno);
    return v;
}

// Guest path -> host path, or fail with ENOENT/EACCES.
bool map_path(u32 guest, std::string& out) {
    out = host_path(str(guest));
    if (out.empty()) {
        if (trace_enabled("files")) std::fprintf(stderr, "recomp: denied path %s\n", str(guest));
        set_guest_errno(ENOENT);
        return false;
    }
    if (trace_enabled("files")) std::fprintf(stderr, "recomp: path %s -> %s\n", str(guest), out.c_str());
    return true;
}

// ARM open(2) flags differ from x86-64 for a few bits.
int host_open_flags(u32 f) {
    int h = int(f & (O_ACCMODE | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | O_APPEND | O_NONBLOCK | O_SYNC | O_CLOEXEC));
    if (f & 040000) h |= O_DIRECTORY;
    if (f & 0100000) h |= O_NOFOLLOW;
    return h;
}

// bionic (ARM) struct stat: 104 bytes.
void put_stat(u32 g, const struct stat& s) {
    std::memset(g_mem + g, 0, 104);
    st64(g + 0, uint64_t(s.st_dev));
    st32(g + 12, u32(s.st_ino));
    st32(g + 16, u32(s.st_mode));
    st32(g + 20, u32(s.st_nlink));
    st32(g + 24, 10123);
    st32(g + 28, 10123);
    st64(g + 32, uint64_t(s.st_rdev));
    st64(g + 48, uint64_t(s.st_size));
    st32(g + 56, u32(s.st_blksize));
    st64(g + 64, uint64_t(s.st_blocks));
    st32(g + 72, u32(s.st_atim.tv_sec));
    st32(g + 76, u32(s.st_atim.tv_nsec));
    st32(g + 80, u32(s.st_mtim.tv_sec));
    st32(g + 84, u32(s.st_mtim.tv_nsec));
    st32(g + 88, u32(s.st_ctim.tv_sec));
    st32(g + 92, u32(s.st_ctim.tv_nsec));
    st64(g + 96, uint64_t(s.st_ino));
}

// Android's alarm driver, which older devices have: the engine's clock reads the
// elapsed time from it and only falls back to CLOCK_BOOTTIME without it.
std::atomic<int> g_alarm_fd{-1};

// ------------------------------------------------------------------ fd level
void h_open(CPU* c) {
    ArgReader a(c);
    const u32 path = a.u32(), flags = a.u32(), mode = (flags & O_CREAT) ? a.u32() : 0;
    if (!std::strcmp(str(path), "/dev/alarm")) {
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) g_alarm_fd = fd;
        c->r[0] = u32(ret(fd));
        return;
    }
    std::string hp;
    if (!map_path(path, hp)) { c->r[0] = u32(-1); return; }
    c->r[0] = u32(ret(::open(hp.c_str(), host_open_flags(flags), mode)));
}
int h_close(int fd) {
    if (fd <= 2) return 0;
    int alarm = fd;
    g_alarm_fd.compare_exchange_strong(alarm, -1);
    return ret(::close(fd));
}
int h_read(int fd, u32 buf, u32 n) { return ret(int(::read(fd, g_mem + buf, n))); }
int h_write(int fd, u32 buf, u32 n) { return ret(int(::write(fd, g_mem + buf, n))); }
int h_writev(int fd, u32 iov, int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        const u32 b = ld32(iov + 8 * u32(i)), l = ld32(iov + 8 * u32(i) + 4);
        const ssize_t w = ::write(fd, g_mem + b, l);
        if (w < 0) return ret(-1);
        total += int(w);
    }
    return total;
}
int h_lseek(int fd, int off, int whence) { return ret(int(::lseek(fd, off, whence))); }
int h_fsync(int fd) { return ret(::fsync(fd)); }
int h_ftruncate(int fd, int len) { return ret(::ftruncate(fd, len)); }
int h_dup(int fd) { return ret(::dup(fd)); }
int h_pipe(u32 fds) {
    int f[2];
    if (::pipe(f) < 0) return ret(-1);
    st32(fds, u32(f[0]));
    st32(fds + 4, u32(f[1]));
    return 0;
}
void h_fcntl(CPU* c) {
    ArgReader a(c);
    const int fd = int(a.u32()), cmd = int(a.u32());
    const u32 arg = a.u32();
    int r;
    switch (cmd) {
        case F_GETFL: {
            r = ::fcntl(fd, F_GETFL);
            break;
        }
        case F_SETFL: r = ::fcntl(fd, F_SETFL, host_open_flags(arg)); break;
        case F_GETFD: case F_SETFD: case F_DUPFD: r = ::fcntl(fd, cmd, int(arg)); break;
        default: r = 0; break;  // locks etc.: pretend success
    }
    c->r[0] = u32(ret(r));
}
void h_ioctl(CPU* c) {
    ArgReader a(c);
    const int fd = int(a.u32());
    const u32 request = a.u32(), arg = a.u32();
    if (::fcntl(fd, F_GETFD) < 0) {
        c->r[0] = u32(fail(EBADF));
        return;
    }
    // ANDROID_ALARM_GET_TIME(type) = _IOW('a', 4 | type << 4, struct timespec)
    if (fd == g_alarm_fd && (request & ~0xf0u) == 0x40086104u) {
        const u32 type = (request >> 4) & 0xf;
        const clockid_t id = type <= 1 ? CLOCK_REALTIME : type <= 3 ? CLOCK_BOOTTIME : CLOCK_MONOTONIC;
        timespec ts{};
        ::clock_gettime(id, &ts);
        st32(arg, u32(ts.tv_sec));
        st32(arg + 4, u32(ts.tv_nsec));
        c->r[0] = 0;
        return;
    }
    c->r[0] = u32(fail(ENOTTY));
}
int h_poll(u32 fds, u32 n, int timeout) {
    std::vector<pollfd> p(n);
    for (u32 i = 0; i < n; ++i) {
        p[i].fd = int(ld32(fds + 8 * i));
        p[i].events = short(ld16(fds + 8 * i + 4));
    }
    const int r = ::poll(p.data(), n, timeout);
    for (u32 i = 0; i < n; ++i) st16(fds + 8 * i + 6, u32(uint16_t(p[i].revents)));
    return ret(r);
}
int h_fstat(int fd, u32 out) {
    struct stat s;
    if (::fstat(fd, &s) < 0) return ret(-1);
    put_stat(out, s);
    return 0;
}
int h_stat(u32 path, u32 out) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    struct stat s;
    if (::stat(hp.c_str(), &s) < 0) return ret(-1);
    put_stat(out, s);
    return 0;
}
int h_access(u32 path, int mode) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::access(hp.c_str(), mode));
}
int h_mkdir(u32 path, u32 mode) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::mkdir(hp.c_str(), mode));
}
int h_rmdir(u32 path) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::rmdir(hp.c_str()));
}
int h_unlink(u32 path) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::unlink(hp.c_str()));
}
int h_remove(u32 path) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::remove(hp.c_str()));
}
int h_rename(u32 a, u32 b) {
    std::string ha, hb;
    if (!map_path(a, ha) || !map_path(b, hb)) return -1;
    return ret(::rename(ha.c_str(), hb.c_str()));
}
int h_chmod(u32 path, u32 mode) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    return ret(::chmod(hp.c_str(), mode));
}
int h_utime(u32 path, u32 times) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    if (!times) return ret(::utime(hp.c_str(), nullptr));
    utimbuf t{time_t(int32_t(ld32(times))), time_t(int32_t(ld32(times + 4)))};
    return ret(::utime(hp.c_str(), &t));
}
int h_statfs(u32 path, u32 out) {
    std::string hp;
    if (!map_path(path, hp)) return -1;
    struct statfs s;
    if (::statfs(hp.c_str(), &s) < 0) return ret(-1);
    std::memset(g_mem + out, 0, 84);
    st32(out + 0, u32(s.f_type));
    st32(out + 4, u32(s.f_bsize));
    st64(out + 8, s.f_blocks);
    st64(out + 16, s.f_bfree);
    st64(out + 24, s.f_bavail);
    st64(out + 32, s.f_files);
    st64(out + 40, s.f_ffree);
    st32(out + 56, 255);
    st32(out + 60, u32(s.f_frsize));
    return 0;
}
int h_pathconf(u32, int) { return 255; }
u32 h_getcwd(u32 buf, u32 n) {
    const std::string cwd = game_data_dir();
    if (n < cwd.size() + 1) return u32(fail(ERANGE));
    std::memcpy(g_mem + buf, cwd.c_str(), cwd.size() + 1);
    return buf;
}
int h_mkstemp(u32 tmpl) {
    std::string hp;
    if (!map_path(tmpl, hp)) return -1;
    const int fd = ::mkstemp(hp.data());
    if (fd >= 0) {
        // Write the chosen name back (same suffix length).
        const size_t n = std::strlen(str(tmpl));
        std::memcpy(str(tmpl) + n - 6, hp.data() + hp.size() - 6, 6);
    }
    return ret(fd);
}
u32 h_basename(u32 path) {
    static thread_local u32 buf = 0;
    if (!buf) buf = mem::alloc(4096);
    std::string s = str(path);
    std::snprintf(str(buf), 4096, "%s", ::basename(s.data()));
    return buf;
}
int h_fnmatch(u32 pat, u32 s, int flags) { return ::fnmatch(str(pat), str(s), flags); }

// mmap: anonymous or private file mappings become guest heap copies.
u32 h_mmap(u32 addr, u32 len, int prot, int flags, int fd, int off) {
    (void)addr; (void)prot;
    const u32 a = mem::alloc(len, 4096);
    if (!a) return u32(fail(ENOMEM));
    std::memset(g_mem + a, 0, len);
    if (!(flags & 0x20 /*MAP_ANONYMOUS*/) && fd >= 0) {
        if (::pread(fd, g_mem + a, len, off) < 0) {
            mem::free(a);
            return u32(ret(-1));
        }
    }
    return a;
}
int h_munmap(u32 addr, u32) {
    mem::free(addr);
    return 0;
}

// ------------------------------------------------------------------ directories
constexpr u32 kDirentSize = 280;
std::mutex g_dir_lock;
std::unordered_map<u32, DIR*> g_dirs;

u32 h_opendir(u32 path) {
    std::string hp;
    if (!map_path(path, hp)) return 0;
    DIR* d = ::opendir(hp.c_str());
    if (!d) return u32(ret(0));
    const u32 h = mem::alloc_zeroed(16 + kDirentSize);  // and readdir's entry
    std::lock_guard lk(g_dir_lock);
    g_dirs[h] = d;
    return h;
}
int h_closedir(u32 h) {
    std::lock_guard lk(g_dir_lock);
    auto it = g_dirs.find(h);
    if (it == g_dirs.end()) return fail(EBADF);
    ::closedir(it->second);
    g_dirs.erase(it);
    mem::free(h);
    return 0;
}
int h_readdir_r(u32 h, u32 entry, u32 result) {
    DIR* d;
    {
        std::lock_guard lk(g_dir_lock);
        auto it = g_dirs.find(h);
        if (it == g_dirs.end()) return EBADF;
        d = it->second;
    }
    errno = 0;
    dirent* e = ::readdir(d);
    if (!e) {
        st32(result, 0);
        return errno;
    }
    // bionic struct dirent: u64 d_ino, s64 d_off, u16 d_reclen, u8 d_type, char d_name[256]
    std::memset(g_mem + entry, 0, kDirentSize);
    st64(entry, e->d_ino);
    st64(entry + 8, uint64_t(e->d_off));
    st16(entry + 16, kDirentSize);
    st8(entry + 18, e->d_type);
    std::snprintf(str(entry + 19), 256, "%s", e->d_name);
    st32(result, entry);
    return 0;
}
u32 h_readdir(u32 h) {
    const u32 entry = h + 16, result = mem::alloc(4);
    const int err = h_readdir_r(h, entry, result);
    const u32 r = ld32(result);
    mem::free(result);
    if (err) errno = err;
    return err ? 0 : r;
}

// ------------------------------------------------------------------ stdio streams
constexpr u32 kFileSize = 84;  // sizeof(FILE) in 32-bit bionic
std::mutex g_file_lock;
std::unordered_map<u32, FILE*> g_files;
u32 g_sF = 0;

FILE* host_file(u32 f) {
    std::lock_guard lk(g_file_lock);
    auto it = g_files.find(f);
    return it == g_files.end() ? nullptr : it->second;
}
// Code compiled against old bionic headers reads some FILE fields directly
// (fileno() is a macro for fp->_file): keep _flags and _file meaningful.
void fill_guest_file(u32 h, FILE* f) {
    st16(h + 12, 0x0004 | 0x0008 | 0x0010);  // _flags: __SRD | __SWR | __SRW
    st16(h + 14, u32(::fileno(f)));         // _file
}
u32 wrap_file(FILE* f) {
    if (!f) return 0;
    const u32 h = mem::alloc_zeroed(kFileSize);
    fill_guest_file(h, f);
    std::lock_guard lk(g_file_lock);
    g_files[h] = f;
    return h;
}

void init_stdio() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // the game's log (Lua print) in order with stderr's
    g_sF = mem::static_alloc(3 * kFileSize, 8);
    FILE* std_files[3] = {stdin, stdout, stderr};
    for (u32 i = 0; i < 3; ++i) {
        g_files[g_sF + i * kFileSize] = std_files[i];
        fill_guest_file(g_sF + i * kFileSize, std_files[i]);
    }
    register_data("__sF", g_sF);
}
RECOMP_INIT_HOOK(init_stdio);

// A virtual file (vfs_add_virtual_file): its contents in a memory stream.
FILE* open_virtual(u32 path, u32 mode) {
    if (!path || !mode || str(mode)[0] != 'r') return nullptr;
    auto text = vfs_read_virtual_file(str(path));
    if (!text) return nullptr;
    FILE* f = ::fmemopen(nullptr, text->size() + 1, "w+");  // the buffer goes with fclose
    if (!f) return nullptr;
    std::fwrite(text->data(), 1, text->size(), f);
    std::fflush(f);
    // fmemopen's size is the buffer's: reads end at the contents' end.
    std::rewind(f);
    return f;
}

u32 h_fopen(u32 path, u32 mode) {
    if (FILE* v = open_virtual(path, mode)) return wrap_file(v);
    std::string hp;
    if (!map_path(path, hp)) return 0;
    FILE* f = std::fopen(hp.c_str(), str(mode));
    if (!f) set_guest_errno(errno);
    return wrap_file(f);
}
u32 h_fdopen(int fd, u32 mode) { return wrap_file(::fdopen(fd, str(mode))); }
u32 h_freopen(u32 path, u32 mode, u32 f) {
    FILE* hf = host_file(f);
    std::string hp;
    if (!hf || !map_path(path, hp)) return 0;
    FILE* nf = std::freopen(hp.c_str(), str(mode), hf);
    if (nf) fill_guest_file(f, nf);
    std::lock_guard lk(g_file_lock);
    g_files[f] = nf;
    return nf ? f : 0;
}
u32 h_tmpfile() { return wrap_file(std::tmpfile()); }
int h_fclose(u32 f) {
    FILE* hf;
    {
        std::lock_guard lk(g_file_lock);
        auto it = g_files.find(f);
        if (it == g_files.end()) return fail(EBADF);
        hf = it->second;
        if (f >= g_sF && f < g_sF + 3 * kFileSize) return 0;
        g_files.erase(it);
    }
    mem::free(f);
    return std::fclose(hf);
}
#define FILE_OR(f, v) FILE* hf = host_file(f); if (!hf) { set_guest_errno(EBADF); return v; }
u32 h_fread(u32 buf, u32 sz, u32 n, u32 f) { FILE_OR(f, 0) return u32(std::fread(g_mem + buf, sz, n, hf)); }
u32 h_fwrite(u32 buf, u32 sz, u32 n, u32 f) { FILE_OR(f, 0) return u32(std::fwrite(g_mem + buf, sz, n, hf)); }
int h_fseek(u32 f, int off, int whence) { FILE_OR(f, -1) return std::fseek(hf, off, whence); }
int h_fseeko(u32 f, int off, int whence) { FILE_OR(f, -1) return ::fseeko(hf, off, whence); }
int h_ftell(u32 f) { FILE_OR(f, -1) return int(std::ftell(hf)); }
int h_ftello(u32 f) { FILE_OR(f, -1) return int(::ftello(hf)); }
int h_fflush(u32 f) {
    if (!f) return std::fflush(nullptr);
    FILE_OR(f, -1) return std::fflush(hf);
}
int h_feof(u32 f) { FILE_OR(f, 1) return std::feof(hf); }
int h_ferror(u32 f) { FILE_OR(f, 1) return std::ferror(hf); }
u32 h_fgets(u32 buf, int n, u32 f) { FILE_OR(f, 0) return std::fgets(str(buf), n, hf) ? buf : 0; }
int h_fputs(u32 s, u32 f) { FILE_OR(f, -1) return std::fputs(str(s), hf); }
int h_fputc(int ch, u32 f) { FILE_OR(f, -1) return std::fputc(ch, hf); }
int h_putc(int ch, u32 f) { return h_fputc(ch, f); }
int h_getc(u32 f) { FILE_OR(f, -1) return std::getc(hf); }
// Old bionic's getc() macro reads the FILE's buffer and calls this when it is
// empty; the guest FILE has no buffer (_r is 0), so every character comes here.
int h___srget(u32 f) { return h_getc(f); }
int h_ungetc(int ch, u32 f) { FILE_OR(f, -1) return std::ungetc(ch, hf); }
u32 h_getwc(u32 f) { FILE_OR(f, 0xffffffffu) return u32(std::fgetwc(hf)); }
u32 h_putwc(u32 ch, u32 f) { FILE_OR(f, 0xffffffffu) return u32(std::fputwc(wchar_t(ch), hf)); }
u32 h_fputwc(u32 ch, u32 f) { return h_putwc(ch, f); }
u32 h_ungetwc(u32 ch, u32 f) { FILE_OR(f, 0xffffffffu) return u32(std::ungetwc(wint_t(ch), hf)); }
int h_setvbuf(u32 f, u32, int mode, u32 size) { FILE_OR(f, -1) return std::setvbuf(hf, nullptr, mode, size); }
int h_puts(u32 s) { return std::puts(str(s)); }
void h_perror(u32 s) {
    const int e = int(ld32(guest_errno_addr()));
    std::fprintf(stderr, "%s%s%s\n", s ? str(s) : "", s ? ": " : "", std::strerror(e));
}

// ---- formatted output
void h_printf(CPU* c) {
    ArgReader a(c);
    const std::string s = guest_format(str(a.u32()), a);
    c->r[0] = u32(std::fwrite(s.data(), 1, s.size(), stdout));
}
void h_vprintf(CPU* c) {
    ArgReader va = ArgReader::from_memory(c, c->r[1]);
    const std::string s = guest_format(str(c->r[0]), va);
    c->r[0] = u32(std::fwrite(s.data(), 1, s.size(), stdout));
}
void h_vfprintf(CPU* c) {
    FILE* hf = host_file(c->r[0]);
    ArgReader va = ArgReader::from_memory(c, c->r[2]);
    const std::string s = guest_format(str(c->r[1]), va);
    c->r[0] = hf ? u32(std::fwrite(s.data(), 1, s.size(), hf)) : u32(-1);
}
void h_fprintf(CPU* c) {
    ArgReader a(c);
    FILE* hf = host_file(a.u32());
    const std::string s = guest_format(str(a.u32()), a);
    c->r[0] = hf ? u32(std::fwrite(s.data(), 1, s.size(), hf)) : u32(-1);
}
void store(u32 buf, u32 n, const std::string& s) {
    if (n == 0) return;
    const size_t k = std::min<size_t>(s.size(), n - 1);
    std::memcpy(g_mem + buf, s.data(), k);
    g_mem[buf + k] = 0;
}
void h_sprintf(CPU* c) {
    ArgReader a(c);
    const u32 buf = a.u32();
    const std::string s = guest_format(str(a.u32()), a);
    store(buf, u32(s.size() + 1), s);
    c->r[0] = u32(s.size());
}
void h_snprintf(CPU* c) {
    ArgReader a(c);
    const u32 buf = a.u32(), n = a.u32();
    const std::string s = guest_format(str(a.u32()), a);
    store(buf, n, s);
    c->r[0] = u32(s.size());
}
void h_vsprintf(CPU* c) {
    ArgReader va = ArgReader::from_memory(c, c->r[2]);
    const std::string s = guest_format(str(c->r[1]), va);
    store(c->r[0], u32(s.size() + 1), s);
    c->r[0] = u32(s.size());
}
void h_vsnprintf(CPU* c) {
    ArgReader va = ArgReader::from_memory(c, c->r[3]);
    const std::string s = guest_format(str(c->r[2]), va);
    store(c->r[0], c->r[1], s);
    c->r[0] = u32(s.size());
}
void h_vasprintf(CPU* c) {
    ArgReader va = ArgReader::from_memory(c, c->r[2]);
    const std::string s = guest_format(str(c->r[1]), va);
    st32(c->r[0], mem::dup_string(s));
    c->r[0] = u32(s.size());
}
void h_vsscanf(CPU* c) {
    ArgReader va = ArgReader::from_memory(c, c->r[2]);
    c->r[0] = u32(guest_scan(str(c->r[0]), str(c->r[1]), va));
}
void h_sscanf(CPU* c) {
    ArgReader a(c);
    const u32 input = a.u32(), fmt = a.u32();
    c->r[0] = u32(guest_scan(str(input), str(fmt), a));
}

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
#define REGRAW(name) RECOMP_REGISTER_IMPORT(#name, h_##name)
REGRAW(open); REG(close); REG(read); REG(write); REG(writev); REG(lseek); REG(fsync); REG(ftruncate); REG(dup);
REG(pipe); REGRAW(fcntl); REGRAW(ioctl); REG(poll); REG(fstat); REG(stat); REG(access); REG(mkdir); REG(rmdir);
REG(unlink); REG(remove); REG(rename); REG(chmod); REG(utime); REG(statfs); REG(pathconf); REG(getcwd);
REG(mkstemp); REG(basename); REG(fnmatch); REG(mmap); REG(munmap); REG(opendir); REG(closedir); REG(readdir_r); REG(readdir);
REG(fopen); REG(fdopen); REG(freopen); REG(tmpfile); REG(fclose); REG(fread); REG(fwrite); REG(fseek); REG(fseeko);
REG(ftell); REG(ftello); REG(fflush); REG(feof); REG(ferror); REG(fgets); REG(fputs); REG(fputc); REG(putc);
REG(getc); REG(__srget); REG(ungetc); REG(getwc); REG(putwc); REG(fputwc); REG(ungetwc); REG(setvbuf); REG(puts); REG(perror);
REGRAW(printf); REGRAW(vprintf); REGRAW(fprintf); REGRAW(vfprintf); REGRAW(sprintf); REGRAW(snprintf); REGRAW(vsprintf);
REGRAW(vsnprintf); REGRAW(vasprintf); REGRAW(sscanf); REGRAW(vsscanf);

}  // namespace
}  // namespace recomp::hle
