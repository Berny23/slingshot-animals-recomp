// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's asset format: \x89LZMA\r\n\x1a\n and an .lzma ("LZMA alone")
// stream with the size in its header, or in later games a 7z archive of one
// LZMA-packed file, encrypted. Encryption and decryption are
// the game's own code (its cipher object and script key, games/<id>/game.json),
// called in guest memory; the host only (de)compresses.
#include <lzma.h>

#include <cstdio>
#include <cstring>

#include "recomp/fusion.hpp"
#include "recomp/game.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::hle {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint8_t kLzmaMagic[9] = {0x89, 'L', 'Z', 'M', 'A', '\r', '\n', 0x1a, '\n'};
constexpr uint32_t kCipherSize = 0x200;  // the engine keeps it in a stack slot of at most this size

// Runs a whole buffer through an LZMA coder set up in `s`.
std::optional<Bytes> run_lzma(lzma_stream& s, const uint8_t* data, size_t size, Bytes out) {
    s.next_in = data;
    s.avail_in = size;
    uint8_t buf[65536];
    lzma_ret r = LZMA_OK;
    do {
        s.next_out = buf;
        s.avail_out = sizeof buf;
        r = lzma_code(&s, LZMA_FINISH);
        out.insert(out.end(), buf, buf + (sizeof buf - s.avail_out));
    } while (r == LZMA_OK);
    lzma_end(&s);
    if (r != LZMA_STREAM_END) return std::nullopt;
    return out;
}

// 7z numbers: the leading one bits of the first byte count the bytes that follow
// (little endian); the rest of the first byte is the top part.
struct SevenZipReader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    uint8_t byte() {
        if (p == end) return ok = false, 0;
        return *p++;
    }
    uint64_t number() {
        const uint8_t first = byte();
        uint64_t v = 0;
        for (int i = 0, mask = 0x80; i < 8; ++i, mask >>= 1) {
            if (!(first & mask)) return v | uint64_t(first & (mask - 1)) << (8 * i);
            v |= uint64_t(byte()) << (8 * i);
        }
        return v;
    }
    bool expect(uint8_t b) { return byte() == b && ok; }
};

constexpr uint8_t k7zMagic[6] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
constexpr uint8_t k7zLzma[3] = {3, 1, 1};

// The file in a 7z archive of one LZMA stream, as the game's scripts are; nullopt
// for other archives.
std::optional<Bytes> unpack_7z(const Bytes& a) {
    if (a.size() < 32 || std::memcmp(a.data(), k7zMagic, sizeof k7zMagic)) return std::nullopt;
    uint64_t at = 0, size = 0;
    for (int i = 0; i < 8; ++i) at |= uint64_t(a[12 + i]) << (8 * i), size |= uint64_t(a[20 + i]) << (8 * i);
    if (at > a.size() - 32 || size > a.size() - 32 - at) return std::nullopt;
    SevenZipReader r{a.data() + 32 + at, a.data() + 32 + at + size};
    // Header, MainStreamsInfo, PackInfo: position, one stream and its size.
    if (!r.expect(0x01) || !r.expect(0x04) || !r.expect(0x06)) return std::nullopt;
    const uint64_t pack_pos = r.number();
    if (r.number() != 1 || !r.expect(0x09)) return std::nullopt;
    const uint64_t pack_size = r.number();
    while (r.ok && r.p != r.end && *r.p != 0) r.byte();  // pack CRCs
    // UnpackInfo: one folder of one LZMA coder, and its unpacked size.
    if (!r.expect(0x00) || !r.expect(0x07) || !r.expect(0x0b) || r.number() != 1 || !r.expect(0x00) ||
        r.number() != 1 || !r.expect(0x23) || !r.expect(k7zLzma[0]) || !r.expect(k7zLzma[1]) ||
        !r.expect(k7zLzma[2]) || r.number() != 5)
        return std::nullopt;
    uint8_t props[5];
    for (uint8_t& b : props) b = r.byte();
    if (!r.expect(0x0c)) return std::nullopt;
    const uint64_t unpack_size = r.number();
    if (!r.ok || pack_pos > a.size() - 32 || pack_size > a.size() - 32 - pack_pos) return std::nullopt;
    // As an "LZMA alone" stream: the coder's properties, then the size.
    Bytes alone(props, props + 5);
    for (int i = 0; i < 8; ++i) alone.push_back(uint8_t(unpack_size >> (8 * i)));
    alone.insert(alone.end(), a.begin() + long(32 + pack_pos), a.begin() + long(32 + pack_pos + pack_size));
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_alone_decoder(&s, UINT64_MAX) != LZMA_OK) return std::nullopt;
    return run_lzma(s, alone.data(), alone.size(), {});
}

void put_number(Bytes& out, uint64_t v) {
    int n = 0;  // bytes after the first
    while (n < 8 && v >= uint64_t(1) << (7 * (n + 1))) ++n;
    const uint8_t ones = uint8_t(0xff00 >> n);
    out.push_back(n == 8 ? 0xff : uint8_t(ones | (v >> (8 * n))));
    for (int i = 0; i < n; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

void put32(Bytes& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

// `data` as the file `name` in a 7z archive of one LZMA stream (no end marker).
std::optional<Bytes> pack_7z(const Bytes& data, const std::string& name) {
    lzma_options_lzma opt;
    lzma_lzma_preset(&opt, 6);
    opt.ext_flags = 0;
    opt.ext_size_low = uint32_t(data.size());
    opt.ext_size_high = uint32_t(uint64_t(data.size()) >> 32);
    const lzma_filter filters[] = {{LZMA_FILTER_LZMA1EXT, &opt}, {LZMA_VLI_UNKNOWN, nullptr}};
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_raw_encoder(&s, filters) != LZMA_OK) return std::nullopt;
    auto packed = run_lzma(s, data.data(), data.size(), {});
    uint32_t props_size = 0;
    uint8_t props[5];
    if (!packed || lzma_properties_size(&props_size, &filters[0]) != LZMA_OK || props_size != 5 ||
        lzma_properties_encode(&filters[0], props) != LZMA_OK)
        return std::nullopt;
    Bytes h = {0x01, 0x04, 0x06, 0x00, 0x01, 0x09};  // Header, MainStreamsInfo, PackInfo at 0, one stream
    put_number(h, packed->size());
    h.insert(h.end(), {0x00, 0x07, 0x0b, 0x01, 0x00, 0x01, 0x23, k7zLzma[0], k7zLzma[1], k7zLzma[2], 0x05});
    h.insert(h.end(), props, props + 5);
    h.push_back(0x0c);
    put_number(h, data.size());
    h.insert(h.end(), {0x00, 0x08, 0x0a, 0x01});  // SubStreamsInfo: the file's CRC
    put32(h, lzma_crc32(data.data(), data.size(), 0));
    h.insert(h.end(), {0x00, 0x00, 0x05, 0x01, 0x11});  // FilesInfo: one file, its name
    put_number(h, 1 + 2 * (name.size() + 1));
    h.push_back(0x00);
    for (char c : name) h.insert(h.end(), {uint8_t(c), 0x00});
    h.insert(h.end(), {0x00, 0x00, 0x00, 0x00});
    Bytes out(k7zMagic, k7zMagic + sizeof k7zMagic);
    out.insert(out.end(), {0x00, 0x04});
    Bytes start;
    for (int i = 0; i < 8; ++i) start.push_back(uint8_t(uint64_t(packed->size()) >> (8 * i)));
    for (int i = 0; i < 8; ++i) start.push_back(uint8_t(uint64_t(h.size()) >> (8 * i)));
    put32(start, lzma_crc32(h.data(), h.size(), 0));
    put32(out, lzma_crc32(start.data(), start.size(), 0));
    out.insert(out.end(), start.begin(), start.end());
    out.insert(out.end(), packed->begin(), packed->end());
    out.insert(out.end(), h.begin(), h.end());
    return out;
}

// `in` through the cipher's encrypt or decrypt in guest memory.
std::optional<Bytes> run_cipher(uint32_t fn, const Bytes& in) {
    const GameCipher& g = game.cipher;
    CPU* c = current_cpu();
    if (!c || !game.script_key || !g.init || !g.free || !fn) return std::nullopt;
    const uint32_t key = guest_call(c, game.script_key);
    const uint32_t size = uint32_t(in.size());
    const uint32_t cipher = mem::alloc_zeroed(kCipherSize), vecs = mem::alloc_zeroed(24), data = mem::alloc(size ? size : 1);
    if (!key || !cipher || !vecs || !data) {
        mem::free(cipher), mem::free(vecs), mem::free(data);
        return std::nullopt;
    }
    std::memcpy(g_mem + data, in.data(), size);
    st32(vecs, data);  // in: begin, end, capacity; out: empty
    st32(vecs + 4, data + size);
    st32(vecs + 8, data + size);
    guest_call(c, g.init, {cipher, key, 0, 0});
    const bool ok = guest_call(c, fn, {cipher, vecs, vecs + 12}) != 0 || fn == g.encrypt;
    guest_call(c, g.free, {cipher});
    std::optional<Bytes> out;
    const uint32_t begin = ld32(vecs + 12), end = ld32(vecs + 16);
    if (ok) out = Bytes(g_mem + begin, g_mem + end);
    static const uint32_t operator_delete = lib_symbol("_ZdlPv");
    if (begin && operator_delete) guest_call(c, operator_delete, {begin});
    mem::free(cipher), mem::free(vecs), mem::free(data);
    return out;
}

}  // namespace

bool is_plain_asset(const Bytes& d) {
    if (d.size() % 16) return true;
    if (d.size() >= 4 && (!std::memcmp(d.data(), "\x1bLua", 4) || !std::memcmp(d.data(), "PK\x03\x04", 4))) return true;
    for (size_t i = 0; i < d.size() && i < 256; ++i) {
        const uint8_t c = d[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
    }
    return true;
}

std::optional<Bytes> decode_asset(const Bytes& data) {
    auto plain = run_cipher(game.cipher.decrypt, data);
    if (plain && plain->size() >= sizeof k7zMagic && !std::memcmp(plain->data(), k7zMagic, sizeof k7zMagic))
        return unpack_7z(*plain);
    if (!plain || plain->size() < sizeof kLzmaMagic || std::memcmp(plain->data(), kLzmaMagic, sizeof kLzmaMagic))
        return plain;
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_alone_decoder(&s, UINT64_MAX) != LZMA_OK) return std::nullopt;
    return run_lzma(s, plain->data() + sizeof kLzmaMagic, plain->size() - sizeof kLzmaMagic, {});
}

std::optional<Bytes> encode_script(const Bytes& data, const std::string& name) {
    if (!game.cipher.encrypt) return std::nullopt;
    if (game.scripts_7z) {
        const auto archive = pack_7z(data, name.substr(name.rfind('/') + 1));
        return archive ? run_cipher(game.cipher.encrypt, *archive) : std::nullopt;
    }
    lzma_options_lzma opt;
    lzma_lzma_preset(&opt, 6);
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_alone_encoder(&s, &opt) != LZMA_OK) return std::nullopt;
    auto out = run_lzma(s, data.data(), data.size(), Bytes(kLzmaMagic, kLzmaMagic + sizeof kLzmaMagic));
    if (!out || out->size() < sizeof kLzmaMagic + 13) return std::nullopt;
    // The encoder writes an unknown size (and an end marker, which the size makes unused).
    const uint64_t size = data.size();
    for (int i = 0; i < 8; ++i) (*out)[sizeof kLzmaMagic + 5 + size_t(i)] = uint8_t(size >> (8 * i));
    return run_cipher(game.cipher.encrypt, *out);
}

}  // namespace recomp::hle
