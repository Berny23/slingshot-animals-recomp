// SPDX-License-Identifier: GPL-3.0-or-later
#include "qr.hpp"

#include <SDL3/SDL.h>

#include <cstring>

extern "C" {
#include <qrcodegen.h>
#include <quirc.h>
}

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

namespace slingshot::level_maker {

std::vector<uint8_t> qr_modules(const std::string& text, int& size) {
    std::vector<uint8_t> code(qrcodegen_BUFFER_LEN_MAX), temp(qrcodegen_BUFFER_LEN_MAX);
    size = 0;
    if (!qrcodegen_encodeText(text.c_str(), temp.data(), code.data(), qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return {};
    size = qrcodegen_getSize(code.data());
    std::vector<uint8_t> modules(size_t(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) modules[size_t(y) * size + x] = qrcodegen_getModule(code.data(), x, y) ? 1 : 0;
    return modules;
}

bool save_qr_png(const std::string& text, const std::string& path) {
    int size;
    const std::vector<uint8_t> modules = qr_modules(text, size);
    if (!size) return false;
    constexpr int kScale = 8, kQuiet = 4;
    const int side = (size + 2 * kQuiet) * kScale;
    std::vector<uint8_t> pixels(size_t(side) * side, 255);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (modules[size_t(y) * size + x])
                for (int dy = 0; dy < kScale; ++dy)
                    std::memset(&pixels[size_t((y + kQuiet) * kScale + dy) * side + (x + kQuiet) * kScale], 0, kScale);
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
    if (!io) return false;
    const bool ok = stbi_write_png_to_func(
                        [](void* context, void* data, int n) {
                            SDL_WriteIO(static_cast<SDL_IOStream*>(context), data, size_t(n));
                        },
                        io, side, side, 1, pixels.data(), side) != 0;
    return SDL_CloseIO(io) && ok;
}

std::optional<std::string> read_qr_grey(const uint8_t* grey, int w, int h, int pitch) {
    std::optional<std::string> result;
    quirc* q = quirc_new();
    if (q && quirc_resize(q, w, h) >= 0) {
        int qw, qh;
        uint8_t* buffer = quirc_begin(q, &qw, &qh);
        for (int y = 0; y < h; ++y) std::memcpy(buffer + size_t(y) * w, grey + size_t(y) * pitch, size_t(w));
        quirc_end(q);
        for (int i = 0; i < quirc_count(q) && !result; ++i) {
            quirc_code code;
            quirc_data data;
            quirc_extract(q, i, &code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
                quirc_flip(&code);  // a mirrored picture (front cameras)
                if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;
            }
            result = std::string(reinterpret_cast<const char*>(data.payload), size_t(data.payload_len));
        }
    }
    if (q) quirc_destroy(q);
    return result;
}

std::optional<std::string> read_qr_picture(const std::string& path) {
    size_t length = 0;
    void* file = SDL_LoadFile(path.c_str(), &length);
    if (!file) return std::nullopt;
    int w, h, channels;
    uint8_t* grey = stbi_load_from_memory(static_cast<const stbi_uc*>(file), int(length), &w, &h, &channels, 1);
    SDL_free(file);
    if (!grey) return std::nullopt;
    auto result = read_qr_grey(grey, w, h, w);
    stbi_image_free(grey);
    return result;
}

}  // namespace slingshot::level_maker
