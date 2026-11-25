// SPDX-License-Identifier: GPL-3.0-or-later
// Host differences the libc HLE hides: the guest uses Linux (ARM) numbers for
// errno and open(2) flags, and some POSIX calls are missing on Windows. All
// paths are UTF-8 (Windows: the executable's code page is UTF-8).
#pragma once

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace recomp::hle::host {

// Host errno value -> Linux errno value.
int linux_errno(int e);

// open(2) with Linux ARM flags (O_CREAT 0100, O_DIRECTORY 040000, ...).
int open(const char* path, uint32_t linux_flags, uint32_t mode);
int open_null();  // a descriptor that reads nothing (/dev/null)
int mkdir(const char* path, uint32_t mode);
int access(const char* path, int mode);
// rename(2): replaces an existing target (Windows' rename refuses to).
int rename(const char* from, const char* to);
int64_t pread(int fd, void* buf, size_t n, int64_t off);
// read(2), write(2), fread and fwrite (in bytes) for buffers in guest memory. The
// Switch refuses guest memory mapped as code memory as an IPC buffer: there the data
// passes through a host buffer.
int64_t read(int fd, void* buf, size_t n);
int64_t write(int fd, const void* buf, size_t n);
size_t fread(void* buf, size_t n, FILE* f);
size_t fwrite(const void* buf, size_t n, FILE* f);
int fsync(int fd);
int truncate(int fd, int64_t size);
// pipe(2) with a non-blocking write end. The games' HTTP clients write a wake-up
// byte per request, and the client pumped without a worker thread never reads
// them: a full pipe must fail the write, not block the game.
int pipe(int fds[2]);
bool valid_fd(int fd);
// poll(2) with Linux event bits. Windows: pipes are ready to read when they hold
// data or the writer is gone, other descriptors are always ready.
struct PollFd {
    int fd;
    uint16_t events, revents;
};
int poll(PollFd* fds, size_t n, int timeout_ms);
// fcntl F_GETFL as Linux flags.
uint32_t fd_flags(int fd);
// A temporary file from a template ending in XXXXXX (replaced in place).
int mkstemp(std::string& path);
// fopen modes always binary.
std::string file_mode(const char* mode);
// A read-only stream of `data`.
std::FILE* memory_stream(const std::string& data);
int64_t ftell(std::FILE* f);
int seek(std::FILE* f, int64_t off, int whence);

struct Stat {
    uint64_t dev = 0, ino = 0, rdev = 0, size = 0, blocks = 0;
    uint32_t mode = 0, nlink = 1, blksize = 4096;
    int64_t atime = 0, mtime = 0, ctime = 0;
    uint32_t atime_ns = 0, mtime_ns = 0, ctime_ns = 0;
};
bool stat(const char* path, Stat& out);
bool fstat(int fd, Stat& out);

struct DiskSpace {
    uint32_t type = 0xef53, bsize = 4096;  // ext4
    uint64_t blocks = 0, bfree = 0, bavail = 0, files = 1 << 20, ffree = 1 << 19;
};
bool statfs(const char* path, DiskSpace& out);

struct DirEntry {
    uint64_t ino;
    uint8_t type;  // Linux DT_* (4 directory, 8 regular file, 0 unknown)
    std::string name;
};
// Opens a directory listing; nullptr with errno set if it cannot be read.
struct Dir;
Dir* opendir(const char* path);
bool readdir(Dir* d, DirEntry& out);  // false at the end
void closedir(Dir* d);

// clock_gettime for a Linux clock id (CLOCK_BOOTTIME and the like fall back to
// the monotonic clock where the host has no such clock).
timespec clock(int linux_id);

int process_id();
int thread_id();  // the kernel's id of the calling thread (gettid)

// fnmatch(3) with the Linux flag values.
int fnmatch(const char* pattern, const char* s, int flags);

}  // namespace recomp::hle::host
