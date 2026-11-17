// SPDX-License-Identifier: GPL-3.0-or-later
#include "android_res.hpp"

#include <algorithm>

#include "zip.hpp"

namespace apk {
namespace {

using Bytes = std::vector<uint8_t>;

uint16_t le16(const Bytes& b, size_t p) { return p + 2 <= b.size() ? uint16_t(b[p] | b[p + 1] << 8) : 0; }
uint32_t le32(const Bytes& b, size_t p) {
    return p + 4 <= b.size() ? uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24
                             : 0;
}

constexpr uint16_t kStringPool = 0x0001, kTable = 0x0002, kXml = 0x0003, kXmlStart = 0x0102, kXmlResMap = 0x0180;
constexpr uint16_t kPackage = 0x0200, kType = 0x0201;
constexpr uint32_t kAttrIcon = 0x01010002;  // android:icon
constexpr uint8_t kTypeReference = 0x01, kTypeString = 0x03;

// A ResStringPool chunk at `p`.
std::vector<std::string> string_pool(const Bytes& b, size_t p) {
    std::vector<std::string> out;
    const uint32_t count = le32(b, p + 8), flags = le32(b, p + 16), start = le32(b, p + 20);
    const bool utf8 = flags & 0x100;
    for (uint32_t i = 0; i < count; ++i) {
        size_t s = p + start + le32(b, p + 28 + 4 * i);
        std::string str;
        if (utf8) {
            s += (b[s] & 0x80) ? 2 : 1;  // length in UTF-16 units
            size_t n = b[s];
            if (n & 0x80) n = (n & 0x7f) << 8 | b[++s];
            ++s;
            if (s + n <= b.size()) str.assign(reinterpret_cast<const char*>(&b[s]), n);
        } else {
            size_t n = le16(b, s);
            if (n & 0x8000) {
                n = (n & 0x7fff) << 16 | le16(b, s + 2);
                s += 2;
            }
            s += 2;
            for (size_t k = 0; k < n && s + 2 * k + 1 < b.size(); ++k) {
                const uint16_t c = le16(b, s + 2 * k);
                if (c < 0x80) str += char(c);
                else str += '?';  // resource paths are ASCII
            }
        }
        out.push_back(std::move(str));
    }
    return out;
}

// android:icon of <application> in a binary AndroidManifest.xml (a resource id).
uint32_t manifest_icon(const Bytes& b) {
    if (le16(b, 0) != kXml) return 0;
    std::vector<std::string> strings;
    std::vector<uint32_t> res_ids;
    for (size_t p = le16(b, 2); p + 8 <= b.size();) {
        const uint16_t type = le16(b, p);
        const uint32_t size = le32(b, p + 4);
        if (size < 8) break;
        if (type == kStringPool) {
            strings = string_pool(b, p);
        } else if (type == kXmlResMap) {
            for (size_t i = p + 8; i + 4 <= p + size; i += 4) res_ids.push_back(le32(b, i));
        } else if (type == kXmlStart) {
            const size_t ext = p + le16(b, p + 2);  // ResXMLTree_attrExt
            const uint32_t name = le32(b, ext + 4);
            if (name < strings.size() && strings[name] == "application") {
                const uint16_t attr_start = le16(b, ext + 8), attr_size = le16(b, ext + 10), count = le16(b, ext + 12);
                for (uint16_t i = 0; i < count; ++i) {
                    const size_t a = ext + attr_start + size_t(i) * attr_size;
                    const uint32_t an = le32(b, a + 4);
                    const bool is_icon = (an < res_ids.size() && res_ids[an] == kAttrIcon) ||
                                         (an < strings.size() && strings[an] == "icon");
                    if (is_icon && b[a + 15] == kTypeReference) return le32(b, a + 16);
                }
                return 0;
            }
        }
        p += size;
    }
    return 0;
}

// Files (one per configuration) of resource `id` in resources.arsc.
std::vector<std::pair<std::string, uint16_t>> resource_files(const Bytes& b, uint32_t id) {
    std::vector<std::pair<std::string, uint16_t>> out;
    if (le16(b, 0) != kTable) return out;
    std::vector<std::string> values;
    for (size_t p = le16(b, 2); p + 8 <= b.size();) {
        const uint16_t type = le16(b, p);
        const uint32_t size = le32(b, p + 4);
        if (size < 8) break;
        if (type == kStringPool) {
            values = string_pool(b, p);
        } else if (type == kPackage && le32(b, p + 8) == (id >> 24)) {
            for (size_t q = p + le16(b, p + 2); q + 8 <= p + size;) {
                const uint16_t t = le16(b, q);
                const uint32_t s = le32(b, q + 4);
                if (s < 8) break;
                if (t == kType && b[q + 8] == ((id >> 16) & 0xff)) {
                    const uint8_t flags = b[q + 9];
                    const uint32_t count = le32(b, q + 12), entries = le32(b, q + 16);
                    const size_t config = q + 20;
                    const uint16_t density = le16(b, config + 14);
                    const uint32_t index = id & 0xffff;
                    if (!(flags & 0x01) && index < count) {  // not sparse
                        const uint32_t off = le32(b, q + le16(b, q + 2) + 4 * index);
                        if (off != 0xffffffffu) {
                            const size_t e = q + entries + off;
                            const uint16_t entry_size = le16(b, e), entry_flags = le16(b, e + 2);
                            if (!(entry_flags & 0x0001) && b.size() > e + entry_size + 7 &&
                                b[e + entry_size + 3] == kTypeString) {
                                const uint32_t str = le32(b, e + entry_size + 4);
                                if (str < values.size()) out.emplace_back(values[str], density);
                            }
                        }
                    }
                }
                q += s;
            }
        }
        p += size;
    }
    return out;
}

}  // namespace

std::optional<std::pair<uint32_t, uint32_t>> png_size(const std::vector<uint8_t>& d) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (d.size() < 24 || !std::equal(sig, sig + 8, d.begin())) return std::nullopt;
    auto be = [&](size_t p) { return uint32_t(d[p]) << 24 | uint32_t(d[p + 1]) << 16 | uint32_t(d[p + 2]) << 8 | d[p + 3]; };
    return std::make_pair(be(16), be(20));
}

std::vector<uint8_t> png_to_ico(const std::vector<uint8_t>& png) {
    const auto size = png_size(png);
    std::vector<uint8_t> ico = {0, 0, 1, 0, 1, 0};  // ICONDIR: an icon with one image
    auto put = [&](uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) ico.push_back(uint8_t(v >> (8 * i)));
    };
    put(size && size->first < 256 ? size->first : 0, 1);  // 0 means 256
    put(size && size->second < 256 ? size->second : 0, 1);
    put(0, 1);  // palette
    put(0, 1);
    put(1, 2);   // planes
    put(32, 2);  // bits per pixel
    put(uint32_t(png.size()), 4);
    put(22, 4);  // offset of the image
    ico.insert(ico.end(), png.begin(), png.end());
    return ico;
}

std::vector<IconFile> app_icons(ZipReader& apk) {
    std::vector<IconFile> out;
    const auto manifest = apk.read("AndroidManifest.xml");
    const auto table = apk.read("resources.arsc");
    if (!manifest || !table) return out;
    const uint32_t id = manifest_icon(*manifest);
    if (!id) return out;
    for (const auto& [path, density] : resource_files(*table, id)) {
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".png")) continue;
        const auto data = apk.read(path);
        if (!data) continue;
        if (const auto size = png_size(*data))
            out.push_back({path, density, size->first, size->second});
    }
    std::stable_sort(out.begin(), out.end(), [](const IconFile& a, const IconFile& b) {
        return uint64_t(a.width) * a.height > uint64_t(b.width) * b.height;
    });
    return out;
}

}  // namespace apk
