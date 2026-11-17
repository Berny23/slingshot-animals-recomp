// SPDX-License-Identifier: GPL-3.0-or-later
#include "image.hpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace apk {
namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

bool inflate_all(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t expected) {
    out.assign(expected, 0);
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = uInt(in.size());
    z.next_out = out.data();
    z.avail_out = uInt(out.size());
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return (r == Z_STREAM_END || r == Z_BUF_ERROR) && z.avail_out == 0;
}

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

}  // namespace

std::optional<Image> decode_png(const std::vector<uint8_t>& d) {
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (d.size() < 33 || std::memcmp(d.data(), kSig, 8)) return std::nullopt;
    uint32_t w = 0, h = 0;
    int depth = 0, type = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    for (size_t p = 8; p + 12 <= d.size();) {
        const uint32_t len = be32(&d[p]);
        if (p + 12 + size_t(len) > d.size()) return std::nullopt;
        const char* t = reinterpret_cast<const char*>(&d[p + 4]);
        const uint8_t* c = &d[p + 8];
        if (!std::memcmp(t, "IHDR", 4) && len >= 13) {
            w = be32(c);
            h = be32(c + 4);
            depth = c[8];
            type = c[9];
            interlace = c[12];
        } else if (!std::memcmp(t, "PLTE", 4)) {
            palette.assign(c, c + len);
        } else if (!std::memcmp(t, "tRNS", 4)) {
            trns.assign(c, c + len);
        } else if (!std::memcmp(t, "IDAT", 4)) {
            idat.insert(idat.end(), c, c + len);
        } else if (!std::memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + size_t(len);
    }
    int channels = 0;
    switch (type) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return std::nullopt;
    }
    if (!w || !h || w > 8192 || h > 8192 || interlace || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16))
        return std::nullopt;
    const size_t bits = size_t(channels) * size_t(depth);
    const size_t stride = (size_t(w) * bits + 7) / 8, bpp = std::max<size_t>(1, bits / 8);
    std::vector<uint8_t> raw;
    if (!inflate_all(idat, raw, (stride + 1) * h)) return std::nullopt;
    // Undo the scanline filters in place.
    std::vector<uint8_t> prev(stride, 0);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* line = &raw[y * (stride + 1)];
        const uint8_t f = line[0];
        uint8_t* s = line + 1;
        for (size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? s[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
            switch (f) {
                case 0: break;
                case 1: s[x] = uint8_t(s[x] + a); break;
                case 2: s[x] = uint8_t(s[x] + b); break;
                case 3: s[x] = uint8_t(s[x] + (a + b) / 2); break;
                case 4: s[x] = uint8_t(s[x] + paeth(a, b, c)); break;
                default: return std::nullopt;
            }
        }
        std::memcpy(prev.data(), s, stride);
    }
    Image img;
    img.width = int(w);
    img.height = int(h);
    img.rgba.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = &raw[y * (stride + 1) + 1];
        for (uint32_t x = 0; x < w; ++x) {
            // Sample i of the pixel, scaled to 8 bits.
            auto sample = [&](int i) -> int {
                if (depth == 16) return s[(size_t(x) * channels + i) * 2];
                if (depth == 8) return s[size_t(x) * channels + i];
                const size_t bit = (size_t(x) * channels + i) * depth;
                const int v = (s[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
                return type == 3 ? v : v * 255 / ((1 << depth) - 1);
            };
            uint8_t* o = &img.rgba[(size_t(y) * w + x) * 4];
            if (type == 3) {
                const int i = sample(0);
                if (size_t(i) * 3 + 2 >= palette.size()) return std::nullopt;
                o[0] = palette[i * 3];
                o[1] = palette[i * 3 + 1];
                o[2] = palette[i * 3 + 2];
                o[3] = size_t(i) < trns.size() ? trns[i] : 255;
            } else if (channels <= 2) {
                o[0] = o[1] = o[2] = uint8_t(sample(0));
                o[3] = channels == 2 ? uint8_t(sample(1)) : 255;
            } else {
                o[0] = uint8_t(sample(0));
                o[1] = uint8_t(sample(1));
                o[2] = uint8_t(sample(2));
                o[3] = channels == 4 ? uint8_t(sample(3)) : 255;
            }
        }
    }
    return img;
}

Image icon_square(const Image& src, int size) {
    // Fitted into the square, sampled with a tent filter as wide as a source or
    // target pixel (whichever is larger), with alpha weighting, over white.
    Image out;
    out.width = out.height = size;
    out.rgba.assign(size_t(size) * size * 4, 255);
    const float scale = float(std::max(src.width, src.height)) / float(size);
    const float ox = (float(size) * scale - float(src.width)) / 2, oy = (float(size) * scale - float(src.height)) / 2;
    const float radius = std::max(1.0f, scale);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float cx = (float(x) + 0.5f) * scale - ox, cy = (float(y) + 0.5f) * scale - oy;
            float sum[3] = {}, alpha = 0, weight = 0;
            for (int sy = int(std::floor(cy - radius)); sy <= int(std::ceil(cy + radius)); ++sy) {
                const float wy = 1 - std::fabs(float(sy) + 0.5f - cy) / radius;
                if (wy <= 0) continue;
                for (int sx = int(std::floor(cx - radius)); sx <= int(std::ceil(cx + radius)); ++sx) {
                    const float wx = 1 - std::fabs(float(sx) + 0.5f - cx) / radius;
                    if (wx <= 0) continue;
                    const float wgt = wx * wy;
                    weight += wgt;
                    if (sx < 0 || sy < 0 || sx >= src.width || sy >= src.height) continue;
                    const uint8_t* p = &src.rgba[(size_t(sy) * src.width + sx) * 4];
                    const float a = p[3] / 255.0f * wgt;
                    for (int c = 0; c < 3; ++c) sum[c] += p[c] * a;
                    alpha += a;
                }
            }
            uint8_t* o = &out.rgba[(size_t(y) * size + x) * 4];
            const float a = weight > 0 ? alpha / weight : 0;
            for (int c = 0; c < 3; ++c) {
                const float colour = alpha > 0 ? sum[c] / alpha : 255;
                o[c] = uint8_t(std::lround(std::clamp(colour * a + 255 * (1 - a), 0.0f, 255.0f)));
            }
        }
    }
    return out;
}

namespace {

constexpr uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
// ITU T.81 Annex K: quantisation tables (natural order) and Huffman tables.
constexpr uint8_t kLumaQ[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr uint8_t kChromaQ[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                  24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
constexpr uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr uint8_t kDcValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr uint8_t kAcLumaValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
constexpr uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr uint8_t kAcChromaValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
    0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct Huffman {
    uint16_t code[256] = {};
    uint8_t length[256] = {};
    Huffman(const uint8_t* bits, const uint8_t* values) {
        uint16_t c = 0;
        int k = 0;
        for (int len = 1; len <= 16; ++len, c <<= 1)
            for (int i = 0; i < bits[len - 1]; ++i, ++c, ++k) {
                code[values[k]] = c;
                length[values[k]] = uint8_t(len);
            }
    }
};

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}
    void put(uint32_t bits, int n) {
        acc_ = acc_ << n | (bits & ((1u << n) - 1));
        count_ += n;
        while (count_ >= 8) {
            const uint8_t b = uint8_t(acc_ >> (count_ - 8));
            out_.push_back(b);
            if (b == 0xff) out_.push_back(0);  // byte stuffing
            count_ -= 8;
        }
    }
    void flush() {
        if (count_) put(0x7f, 8 - count_);  // pad with ones
    }

private:
    std::vector<uint8_t>& out_;
    uint32_t acc_ = 0;
    int count_ = 0;
};

// Magnitude category and the value's bits as JPEG codes them.
int category(int v) {
    int a = v < 0 ? -v : v, n = 0;
    while (a) {
        ++n;
        a >>= 1;
    }
    return n;
}
uint32_t magnitude_bits(int v, int n) { return v < 0 ? uint32_t(v + (1 << n) - 1) : uint32_t(v); }

void encode_block(BitWriter& bw, const float in[64], const uint8_t q[64], int& dc_prev, const Huffman& dc,
                  const Huffman& ac) {
    static float cosines[8][8];
    static bool ready = false;
    if (!ready) {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u) cosines[x][u] = std::cos(float((2 * x + 1) * u) * 3.14159265358979f / 16);
        ready = true;
    }
    int coef[64];
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            float s = 0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) s += in[y * 8 + x] * cosines[x][u] * cosines[y][v];
            const float cu = u ? 1 : 1 / std::sqrt(2.0f), cv = v ? 1 : 1 / std::sqrt(2.0f);
            coef[v * 8 + u] = int(std::lround(0.25f * cu * cv * s / q[v * 8 + u]));
        }
    const int diff = coef[0] - dc_prev;
    dc_prev = coef[0];
    const int dn = category(diff);
    bw.put(dc.code[dn], dc.length[dn]);
    if (dn) bw.put(magnitude_bits(diff, dn), dn);
    int run = 0;
    for (int k = 1; k < 64; ++k) {
        const int c = coef[kZigzag[k]];
        if (!c) {
            ++run;
            continue;
        }
        while (run > 15) {
            bw.put(ac.code[0xf0], ac.length[0xf0]);
            run -= 16;
        }
        const int n = category(c), sym = run << 4 | n;
        bw.put(ac.code[sym], ac.length[sym]);
        bw.put(magnitude_bits(c, n), n);
        run = 0;
    }
    if (run) bw.put(ac.code[0], ac.length[0]);  // end of block
}

}  // namespace

std::vector<uint8_t> encode_jpeg(const Image& img, int quality) {
    quality = std::clamp(quality, 1, 100);
    const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    uint8_t q[2][64];
    for (int i = 0; i < 64; ++i) {
        q[0][i] = uint8_t(std::clamp((kLumaQ[i] * scale + 50) / 100, 1, 255));
        q[1][i] = uint8_t(std::clamp((kChromaQ[i] * scale + 50) / 100, 1, 255));
    }
    std::vector<uint8_t> out = {0xff, 0xd8,  // SOI, JFIF APP0
                                0xff, 0xe0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    auto u16 = [&](int v) {
        out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v));
    };
    for (int t = 0; t < 2; ++t) {  // DQT
        out.insert(out.end(), {0xff, 0xdb});
        u16(67);
        out.push_back(uint8_t(t));
        for (int k = 0; k < 64; ++k) out.push_back(q[t][kZigzag[k]]);
    }
    out.insert(out.end(), {0xff, 0xc0});  // SOF0: 8 bits, 3 components without subsampling
    u16(17);
    out.push_back(8);
    u16(img.height);
    u16(img.width);
    out.insert(out.end(), {3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1});
    auto dht = [&](int cls_id, const uint8_t* bits, const uint8_t* values, int n) {
        out.insert(out.end(), {0xff, 0xc4});
        u16(3 + 16 + n);
        out.push_back(uint8_t(cls_id));
        out.insert(out.end(), bits, bits + 16);
        out.insert(out.end(), values, values + n);
    };
    dht(0x00, kDcLumaBits, kDcValues, 12);
    dht(0x10, kAcLumaBits, kAcLumaValues, 162);
    dht(0x01, kDcChromaBits, kDcValues, 12);
    dht(0x11, kAcChromaBits, kAcChromaValues, 162);
    out.insert(out.end(), {0xff, 0xda});  // SOS
    u16(12);
    out.insert(out.end(), {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0});

    static const Huffman dc_luma(kDcLumaBits, kDcValues), ac_luma(kAcLumaBits, kAcLumaValues);
    static const Huffman dc_chroma(kDcChromaBits, kDcValues), ac_chroma(kAcChromaBits, kAcChromaValues);
    BitWriter bw(out);
    int prev[3] = {};
    for (int by = 0; by < img.height; by += 8) {
        for (int bx = 0; bx < img.width; bx += 8) {
            float blocks[3][64];
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    // Edge blocks repeat the last row and column.
                    const int sx = std::min(bx + x, img.width - 1), sy = std::min(by + y, img.height - 1);
                    const uint8_t* p = &img.rgba[(size_t(sy) * img.width + sx) * 4];
                    const float r = p[0], g = p[1], b = p[2];
                    blocks[0][y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128;
                    blocks[1][y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                    blocks[2][y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
                }
            encode_block(bw, blocks[0], q[0], prev[0], dc_luma, ac_luma);
            encode_block(bw, blocks[1], q[1], prev[1], dc_chroma, ac_chroma);
            encode_block(bw, blocks[2], q[1], prev[2], dc_chroma, ac_chroma);
        }
    }
    bw.flush();
    out.insert(out.end(), {0xff, 0xd9});  // EOI
    return out;
}

}  // namespace apk
