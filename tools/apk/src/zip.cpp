// SPDX-License-Identifier: GPL-3.0-or-later
#include "zip.hpp"
#include "extract.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace apk {
namespace {

std::FILE* open_file(const std::filesystem::path& p, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}
bool seek(std::FILE* f, uint64_t off) {
#ifdef _WIN32
    return _fseeki64(f, int64_t(off), SEEK_SET) == 0;
#else
    return fseeko(f, off_t(off), SEEK_SET) == 0;
#endif
}
uint64_t tell(std::FILE* f) {
#ifdef _WIN32
    return uint64_t(_ftelli64(f));
#else
    return uint64_t(ftello(f));
#endif
}

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t le64(const uint8_t* p) { return le32(p) | uint64_t(le32(p + 4)) << 32; }
void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xffff); put16(v, x >> 16); }

}  // namespace

uint32_t crc32(const uint8_t* data, size_t n) {
    uLong c = ::crc32(0L, Z_NULL, 0);
    while (n) {
        const uInt k = uInt(std::min<size_t>(n, 1u << 30));
        c = ::crc32(c, data, k);
        data += k;
        n -= k;
    }
    return uint32_t(c);
}

ZipReader::~ZipReader() {
    if (f_) std::fclose(f_);
}

bool ZipReader::open(const std::filesystem::path& path) {
    f_ = open_file(path, "rb");
    if (!f_) {
        error_ = "cannot open " + path.string();
        return false;
    }
    std::fseek(f_, 0, SEEK_END);
    const uint64_t size = tell(f_);
    // The end of central directory record is within the last 64 KiB + 22 bytes.
    const uint64_t tail = std::min<uint64_t>(size, 65536 + 22);
    std::vector<uint8_t> buf(tail);
    if (!seek(f_, size - tail) || std::fread(buf.data(), 1, tail, f_) != tail) {
        error_ = "cannot read " + path.string();
        return false;
    }
    int64_t eocd = -1;
    for (int64_t i = int64_t(tail) - 22; i >= 0; --i)
        if (le32(&buf[size_t(i)]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        error_ = path.string() + " is not a zip archive";
        return false;
    }
    const uint8_t* e = &buf[size_t(eocd)];
    uint64_t count = le16(e + 10), dir_size = le32(e + 12), dir_off = le32(e + 16);
    // Data before the archive (a zip appended to a program): its offsets count
    // from the archive's start, which is where the directory ends minus its size.
    uint64_t base = 0;
    const uint64_t eocd_pos = size - tail + uint64_t(eocd);
    if (dir_off != 0xffffffffu && eocd_pos > dir_off + dir_size) base = eocd_pos - dir_off - dir_size;
    dir_off += base;
    if (dir_off == 0xffffffffu && eocd >= 20 && le32(&buf[size_t(eocd - 20)]) == 0x07064b50) {
        // Zip64 end of central directory locator.
        const uint64_t rec = le64(&buf[size_t(eocd - 20) + 8]);
        uint8_t z[56];
        if (!seek(f_, rec) || std::fread(z, 1, sizeof z, f_) != sizeof z || le32(z) != 0x06064b50) {
            error_ = "bad zip64 directory in " + path.string();
            return false;
        }
        count = le64(z + 32);
        dir_size = le64(z + 40);
        dir_off = le64(z + 48);
    }
    std::vector<uint8_t> dir(dir_size);
    if (!seek(f_, dir_off) || std::fread(dir.data(), 1, dir.size(), f_) != dir.size()) {
        error_ = "cannot read the directory of " + path.string();
        return false;
    }
    size_t p = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (p + 46 > dir.size() || le32(&dir[p]) != 0x02014b50) {
            error_ = "damaged directory in " + path.string();
            return false;
        }
        const uint8_t* c = &dir[p];
        ZipEntry z;
        z.method = le16(c + 10);
        z.crc = le32(c + 16);
        z.compressed = le32(c + 20);
        z.size = le32(c + 24);
        const size_t name_len = le16(c + 28), extra_len = le16(c + 30), comment_len = le16(c + 32);
        z.header_offset = le32(c + 42);
        if (c[5] == 3) z.unix_mode = uint16_t(le32(c + 38) >> 16);  // made on Unix
        if (p + 46 + name_len + extra_len > dir.size()) {
            error_ = "damaged directory in " + path.string();
            return false;
        }
        z.name.assign(reinterpret_cast<const char*>(c + 46), name_len);
        // Zip64 extra field: the 0xffffffff fields in order.
        for (size_t x = 46 + name_len; x + 4 <= 46 + name_len + extra_len;) {
            const uint16_t id = le16(c + x), len = le16(c + x + 2);
            if (id == 1) {
                size_t q = x + 4;
                if (z.size == 0xffffffffu) { z.size = le64(c + q); q += 8; }
                if (z.compressed == 0xffffffffu) { z.compressed = le64(c + q); q += 8; }
                if (z.header_offset == 0xffffffffu) z.header_offset = le64(c + q);
            }
            x += 4 + len;
        }
        z.header_offset += base;
        entries_.push_back(std::move(z));
        p += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    for (const auto& e : entries_)
        if (e.name == name) return &e;
    return nullptr;
}

std::optional<std::vector<uint8_t>> ZipReader::read(const std::string& name) {
    const ZipEntry* e = find(name);
    if (!e) {
        error_ = "no " + name + " in the archive";
        return std::nullopt;
    }
    return read(*e);
}

std::optional<std::vector<uint8_t>> ZipReader::read(const ZipEntry& e) {
    uint8_t h[30];
    if (!seek(f_, e.header_offset) || std::fread(h, 1, sizeof h, f_) != sizeof h || le32(h) != 0x04034b50) {
        error_ = "damaged entry " + e.name;
        return std::nullopt;
    }
    if (!seek(f_, e.header_offset + 30 + le16(h + 26) + le16(h + 28))) {
        error_ = "damaged entry " + e.name;
        return std::nullopt;
    }
    std::vector<uint8_t> raw(e.compressed);
    if (std::fread(raw.data(), 1, raw.size(), f_) != raw.size()) {
        error_ = "truncated entry " + e.name;
        return std::nullopt;
    }
    std::vector<uint8_t> out;
    if (e.method == 0) {
        out = std::move(raw);
    } else if (e.method == 8) {
        out.resize(e.size);
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
            error_ = "zlib error";
            return std::nullopt;
        }
        z.next_in = raw.data();
        z.avail_in = uInt(raw.size());
        z.next_out = out.data();
        z.avail_out = uInt(out.size());
        const int r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        if (r != Z_STREAM_END || z.total_out != out.size()) {
            error_ = "cannot inflate " + e.name;
            return std::nullopt;
        }
    } else {
        error_ = "unsupported compression in " + e.name;
        return std::nullopt;
    }
    if (crc32(out.data(), out.size()) != e.crc) {
        error_ = "checksum mismatch in " + e.name;
        return std::nullopt;
    }
    return out;
}

ZipWriter::~ZipWriter() {
    if (f_) std::fclose(f_);
}

bool ZipWriter::open(const std::filesystem::path& path) {
    f_ = open_file(path, "wb");
    if (!f_) error_ = "cannot write " + path.string();
    return f_ != nullptr;
}

bool ZipWriter::add(const std::string& name, const std::vector<uint8_t>& data, bool deflate, uint32_t align) {
    ZipEntry z;
    z.name = name;
    z.size = data.size();
    z.crc = crc32(data.data(), data.size());
    z.header_offset = tell(f_);
    std::vector<uint8_t> body;
    if (deflate) {
        z.method = 8;
        z_stream s{};
        deflateInit2(&s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        body.resize(deflateBound(&s, uLong(data.size())));
        s.next_in = const_cast<Bytef*>(data.data());
        s.avail_in = uInt(data.size());
        s.next_out = body.data();
        s.avail_out = uInt(body.size());
        const int r = ::deflate(&s, Z_FINISH);
        body.resize(s.total_out);
        deflateEnd(&s);
        if (r != Z_STREAM_END) {
            error_ = "cannot deflate " + name;
            return false;
        }
    }
    const std::vector<uint8_t>& payload = deflate ? body : data;
    z.compressed = payload.size();
    if (z.compressed >= 0xffffffffu || z.header_offset >= 0xffffffffu) {
        error_ = "archive too large";
        return false;
    }
    // Padding in the extra field puts stored data on the alignment.
    size_t pad = 0;
    if (!deflate && align > 1) pad = (align - (z.header_offset + 30 + name.size()) % align) % align;
    std::vector<uint8_t> h;
    put32(h, 0x04034b50);
    put16(h, deflate ? 20 : 10);
    put16(h, 0x0800);  // UTF-8 names
    put16(h, z.method);
    put16(h, 0);  // time
    put16(h, 0x21);  // date: 1980-01-01
    put32(h, z.crc);
    put32(h, uint32_t(z.compressed));
    put32(h, uint32_t(z.size));
    put16(h, uint32_t(name.size()));
    put16(h, uint32_t(pad));
    h.insert(h.end(), name.begin(), name.end());
    h.insert(h.end(), pad, 0);
    if (std::fwrite(h.data(), 1, h.size(), f_) != h.size() ||
        std::fwrite(payload.data(), 1, payload.size(), f_) != payload.size()) {
        error_ = "cannot write " + name;
        return false;
    }
    entries_.push_back(std::move(z));
    return true;
}

bool ZipWriter::close() {
    if (!f_) return false;
    const uint64_t dir_off = tell(f_);
    std::vector<uint8_t> d;
    for (const auto& z : entries_) {
        put32(d, 0x02014b50);
        put16(d, 20);
        put16(d, z.method ? 20 : 10);
        put16(d, 0x0800);
        put16(d, z.method);
        put16(d, 0);
        put16(d, 0x21);
        put32(d, z.crc);
        put32(d, uint32_t(z.compressed));
        put32(d, uint32_t(z.size));
        put16(d, uint32_t(z.name.size()));
        put16(d, 0);
        put16(d, 0);
        put16(d, 0);
        put16(d, 0);
        put32(d, 0);
        put32(d, uint32_t(z.header_offset));
        d.insert(d.end(), z.name.begin(), z.name.end());
    }
    const uint32_t dir_size = uint32_t(d.size());
    put32(d, 0x06054b50);
    put16(d, 0);
    put16(d, 0);
    put16(d, uint32_t(entries_.size()));
    put16(d, uint32_t(entries_.size()));
    put32(d, dir_size);
    put32(d, uint32_t(dir_off));
    put16(d, 0);
    const bool ok = std::fwrite(d.data(), 1, d.size(), f_) == d.size();
    const bool closed = std::fclose(f_) == 0;
    f_ = nullptr;
    if (!ok || !closed) error_ = "cannot write the archive directory";
    return ok && closed;
}

bool unpack(ZipReader& zip, const std::filesystem::path& dir, std::string& error,
            const std::function<void(size_t, size_t)>& progress) {
    namespace fs = std::filesystem;
    const auto& entries = zip.entries();
    std::error_code ec;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        const fs::path rel = fs::path(e.name).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == "..") {
            error = "unsafe name in archive: " + e.name;
            return false;
        }
        const fs::path out = dir / rel;
        if (!e.name.empty() && e.name.back() == '/') {
            fs::create_directories(out, ec);
        } else {
            const auto data = zip.read(e);
            if (!data) {
                error = zip.error();
                return false;
            }
            fs::create_directories(out.parent_path(), ec);
            fs::remove(out, ec);
            const uint16_t type = e.unix_mode & 0170000;
#ifndef _WIN32
            if (type == 0120000) {  // symbolic link: the data is its target
                const fs::path target(std::string(data->begin(), data->end()));
                if (target.is_absolute() || (rel.parent_path() / target).lexically_normal().begin()->string() == "..") {
                    error = "unsafe link in archive: " + e.name;
                    return false;
                }
                fs::create_symlink(target, out, ec);
                if (ec) {
                    error = "cannot create " + out.string() + ": " + ec.message();
                    return false;
                }
                if (progress) progress(i + 1, entries.size());
                continue;
            }
#endif
            if (!write_file(out, *data)) {
                error = "cannot write " + out.string();
                return false;
            }
#ifndef _WIN32
            if (type == 0100000 && (e.unix_mode & 0111))
                fs::permissions(out, fs::perms(e.unix_mode & 0777), fs::perm_options::replace, ec);
#endif
        }
        if (progress) progress(i + 1, entries.size());
    }
    return true;
}

}  // namespace apk
