// SPDX-License-Identifier: GPL-3.0-or-later
// slingshot-apk extract <game.apk> <out-dir> <library> <sha256>
//   Extracts the library, assets/ and the app icon of a supported APK (extract.hpp).
// slingshot-apk icons <game.apk>
//   Lists the app icon's files, the largest first.
// slingshot-apk pack <out.apk> <base.apk> <classes.dex> <abi> <game-dir> <lib.so>...
//   Builds an unsigned Android package (pack.hpp).
#include <cstdio>
#include <cstring>

#include "android_res.hpp"
#include "extract.hpp"
#include "pack.hpp"
#include "zip.hpp"

namespace {

int usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s extract <game.apk> <out-dir> <library> <sha256>\n"
                 "       %s icons <game.apk>\n"
                 "       %s pack <out.apk> <base.apk> <classes.dex> <abi> <game-dir> <lib.so>...\n",
                 argv0, argv0, argv0);
    return 2;
}

int fail(const std::string& what) {
    std::fprintf(stderr, "error: %s\n", what.c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage(argv[0]);
    const std::string cmd = argv[1];
    if (cmd == "icons" && argc == 3) {
        apk::ZipReader zip;
        if (!zip.open(argv[2])) return fail(zip.error());
        for (const auto& i : apk::app_icons(zip))
            std::printf("%ux%u %u dpi %s\n", i.width, i.height, i.density ? i.density : 160, i.path.c_str());
        return 0;
    }
    if (cmd == "extract" && argc == 6) {
        apk::ZipReader zip;
        if (!zip.open(argv[2])) return fail(zip.error());
        const apk::GameSpec game{"", argv[4], argv[5]};
        std::string error;
        if (!apk::extract(zip, game, argv[3], error)) return fail(std::string(argv[2]) + ": " + error);
        return 0;
    }
    if (cmd == "pack" && argc >= 8) {
        apk::PackInput in;
        in.base = argv[3];
        in.dex = argv[4];
        in.abi = argv[5];
        in.game = argv[6];
        for (int i = 7; i < argc; ++i) in.libs.emplace_back(argv[i]);
        std::string error;
        if (!apk::pack(in, argv[2], error)) return fail(error);
        return 0;
    }
    return usage(argv[0]);
}
