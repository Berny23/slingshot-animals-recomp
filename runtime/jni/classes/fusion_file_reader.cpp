// SPDX-License-Identifier: GPL-3.0-or-later
// com.rovio.fusion.FileReader: the APK's assets as older games read them, through
// the Java AssetManager. readFile(name) gives the asset in a direct ByteBuffer,
// or, when there is an asset <name>.zip, the file packed in it. The engine reads
// straight from the buffer while it holds a reference to it, so each buffer has
// its own guest memory, freed with the object.
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "recomp/fusion.hpp"
#include "recomp/jni.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"
#include "recomp/vfs.hpp"

namespace recomp::java {
namespace {

using namespace recomp::jni;
using Bytes = std::vector<uint8_t>;

constexpr const char* kReader = "com/rovio/fusion/FileReader";

uint32_t le16(const Bytes& d, size_t at) { return d[at] | d[at + 1] << 8; }
uint32_t le32(const Bytes& d, size_t at) { return le16(d, at) | le16(d, at + 2) << 16; }

// The first file of a zip archive (stored or deflated), from its central directory.
std::optional<Bytes> first_zip_entry(const Bytes& zip) {
    if (zip.size() < 22) return std::nullopt;
    size_t end = zip.size() - 22;
    while (le32(zip, end) != 0x06054b50)
        if (end-- == 0 || zip.size() - end > 22 + 0xffff) return std::nullopt;
    const size_t dir = le32(zip, end + 16);
    if (dir + 46 > zip.size() || le32(zip, dir) != 0x02014b50) return std::nullopt;
    const uint32_t method = le16(zip, dir + 10), packed = le32(zip, dir + 20), size = le32(zip, dir + 24);
    const size_t local = le32(zip, dir + 42);
    if (local + 30 > zip.size() || le32(zip, local) != 0x04034b50) return std::nullopt;
    const size_t data = local + 30 + le16(zip, local + 26) + le16(zip, local + 28);
    if (data + packed > zip.size()) return std::nullopt;
    if (method == 0) return Bytes(zip.begin() + long(data), zip.begin() + long(data + packed));
    if (method != 8) return std::nullopt;
    Bytes out(size);
    // zlib refuses a null output buffer, which an empty vector may have.
    uint8_t none = 0;
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return std::nullopt;
    s.next_in = const_cast<Bytef*>(zip.data() + data);
    s.avail_in = packed;
    s.next_out = out.empty() ? &none : out.data();
    s.avail_out = size;
    const int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END || s.total_out != size) return std::nullopt;
    return out;
}

// `bytes` in a direct ByteBuffer that owns its guest memory.
Ref buffer_of(const Bytes& bytes) {
    const uint32_t size = uint32_t(bytes.size());
    const uint32_t data = mem::alloc(size ? size : 1, 16);
    if (!data) return 0;
    std::memcpy(g_mem + data, bytes.data(), size);
    const Ref r = new_direct_buffer(data, size);
    set_host_data(r, std::shared_ptr<void>(nullptr, [data](void*) { mem::free(data); }));
    return r;
}

Ref read(const std::string& name, bool compressed) {
    std::optional<Bytes> bytes = vfs_read_asset(compressed ? name + ".zip" : name);
    if (bytes && compressed) bytes = first_zip_entry(*bytes);
    // Plain scripts (patched ones) in the engine's format.
    if (bytes && name.size() > 4 && !name.compare(name.size() - 4, 4, ".lua") && hle::is_plain_asset(*bytes))
        if (auto encoded = hle::encode_script(*bytes, name)) bytes = std::move(encoded);
    if (trace_enabled("assets"))
        std::fprintf(stderr, "assets: read %s%s %s\n", name.c_str(), compressed ? ".zip" : "", bytes ? "ok" : "missing");
    return bytes ? buffer_of(*bytes) : 0;
}

std::filesystem::path host_path(const std::string& name) { return std::filesystem::path(vfs_asset_root()) / name; }
bool is_file(const std::string& name) { return std::filesystem::is_regular_file(host_path(name)); }
bool is_directory(const std::string& name) { return std::filesystem::is_directory(host_path(name)); }

RECOMP_JAVA_METHOD(kReader, "readFile", "(Ljava/lang/String;)Ljava/nio/ByteBuffer;", [](Call& c) {
    const std::string name = string_utf8(c.l(0));
    return ref(is_file(name + ".zip") ? read(name, true) : read(name, false));
});
RECOMP_JAVA_METHOD(kReader, "readFileCompressed", "(Ljava/lang/String;)Ljava/nio/ByteBuffer;",
                   [](Call& c) { return ref(read(string_utf8(c.l(0)), true)); });
RECOMP_JAVA_METHOD(kReader, "readFileUncompressed", "(Ljava/lang/String;)Ljava/nio/ByteBuffer;",
                   [](Call& c) { return ref(read(string_utf8(c.l(0)), false)); });
RECOMP_JAVA_METHOD(kReader, "isFile", "(Ljava/lang/String;)Z",
                   [](Call& c) { return boolean(is_file(string_utf8(c.l(0)))); });
RECOMP_JAVA_METHOD(kReader, "isDirectory", "(Ljava/lang/String;)Z",
                   [](Call& c) { return boolean(is_directory(string_utf8(c.l(0)))); });
RECOMP_JAVA_METHOD(kReader, "exists", "(Ljava/lang/String;)Z", [](Call& c) {
    const std::string name = string_utf8(c.l(0));
    return boolean(is_file(name) || is_directory(name));
});
// AssetManager.list: the names in an asset directory.
RECOMP_JAVA_METHOD(kReader, "enumerate", "(Ljava/lang/String;)[Ljava/lang/String;", [](Call& c) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(host_path(string_utf8(c.l(0))), ec))
        names.push_back(e.path().filename().string());
    const Ref a = new_object_array("java/lang/String", uint32_t(names.size()));
    for (size_t i = 0; i < names.size(); ++i) array_set(a, uint32_t(i), new_string(names[i]));
    return ref(a);
});

}  // namespace
}  // namespace recomp::java
