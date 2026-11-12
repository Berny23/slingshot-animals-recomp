// SPDX-License-Identifier: GPL-3.0-or-later
// The engine's asset format: \x89LZMA\r\n\x1a\n and an .lzma ("LZMA alone")
// stream with the size in its header, encrypted. Encryption and decryption are
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
    if (!plain || plain->size() < sizeof kLzmaMagic || std::memcmp(plain->data(), kLzmaMagic, sizeof kLzmaMagic))
        return plain;
    lzma_stream s = LZMA_STREAM_INIT;
    if (lzma_alone_decoder(&s, UINT64_MAX) != LZMA_OK) return std::nullopt;
    return run_lzma(s, plain->data() + sizeof kLzmaMagic, plain->size() - sizeof kLzmaMagic, {});
}

std::optional<Bytes> encode_script(const Bytes& data) {
    if (!game.cipher.encrypt) return std::nullopt;
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
