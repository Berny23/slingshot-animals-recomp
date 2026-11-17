// SPDX-License-Identifier: GPL-3.0-or-later
#include "switch_host.hpp"

#include <arpa/inet.h>  // before switch.h: nxlink's address type
#include <switch.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace slingshot::nx {
namespace {
bool g_nxlink = false;
}

void init() {
    romfsInit();
    // Started with `nxlink -s`: stdout and stderr go to the computer.
    if (__nxlink_host.s_addr && R_SUCCEEDED(socketInitializeDefault())) g_nxlink = nxlinkStdio() >= 0;
    // newlib knows only POSIX TZ strings: the console's current offset, without DST rules.
    u64 now = 0;
    TimeCalendarTime cal{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &now)) &&
        R_SUCCEEDED(timeToCalendarTimeWithMyRule(now, &cal, &info))) {
        const int west = -info.offset;  // POSIX counts west of UTC as positive
        const int a = west < 0 ? -west : west;
        char tz[32];
        std::snprintf(tz, sizeof tz, "LCL%c%02d:%02d:%02d", west < 0 ? '-' : '+', a / 3600, a / 60 % 60, a % 60);
        setenv("TZ", tz, 1);
        tzset();
    }
}

void exit() {
    if (g_nxlink) socketExit();
    romfsExit();
}

std::string font_dir() {
    const std::string dir = "sdmc:/switch/slingshot-animals-recomp/fonts";
    if (R_FAILED(plInitialize(PlServiceType_User))) return dir;
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/slingshot-animals-recomp", 0777);
    mkdir(dir.c_str(), 0777);
    // Numbered by priority: the standard font (Latin, Japanese) first.
    static const char* const kNames[] = {"standard", "chinese-simplified", "chinese-simplified-ext",
                                         "chinese-traditional", "korean", "nintendo-ext"};
    for (int type = 0; type < PlSharedFontType_Total; ++type) {
        PlFontData font{};
        if (R_FAILED(plGetSharedFontByType(&font, PlSharedFontType(type))) || !font.address) continue;
        char path[128];
        std::snprintf(path, sizeof path, "%s/%d-%s.ttf", dir.c_str(), type, kNames[type]);
        struct stat st{};
        if (stat(path, &st) == 0 && st.st_size == off_t(font.size)) continue;
        if (FILE* f = std::fopen(path, "wb")) {
            const bool ok = std::fwrite(font.address, 1, font.size, f) == font.size;
            std::fclose(f);
            if (!ok) std::remove(path);
        }
    }
    plExit();
    return dir;
}

}  // namespace slingshot::nx
