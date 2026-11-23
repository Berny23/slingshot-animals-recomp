// SPDX-License-Identifier: GPL-3.0-or-later
#include "recomp_achievements.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(__SWITCH__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "common/game_scripts.hpp"
#include "common/script_link.hpp"
#include "mod.hpp"
#include "recomp/game.hpp"
#include "recomp/vfs.hpp"

namespace slingshot::mods {
namespace {

constexpr const char* kPrefix = "cache/slingshot-achievements/";

// The Lua side: what to do comes from the virtual file <prefix>mode; messages to
// the host are 's' (save loaded), 'e<key>' (unlocked), 'l<json>' (an achievement
// for the list) and 'L' (the list is complete).
constexpr const char* kReporterLua = R"(
local PREFIX = 'cache/slingshot-achievements/'
local mode = read(PREFIX .. 'mode')
if G.type(mode) == 'table' and (mode.report or mode.list) then
    local function quote(s)
        return '"' .. G.tostring(s or ''):gsub('[%c"\\]', function(c)
            return G.string.format('\\u%04x', c:byte())
        end) .. '"'
    end
    local function key(a)
        return a.key or (G.tostring(a.name):lower():gsub('[^%w]+', '_'):gsub('^_+', ''):gsub('_+$', ''))
    end
    local reported, ticks, listed, started, waited = {}, 0, false, false, 0
    each_frame(function()
        ticks = ticks + 1
        if ticks % 30 ~= 1 then return end
        local list = G.rawget(G, 'slingshotAchievements')
        if G.type(list) ~= 'table' then return end
        waited = waited + 1
        -- The list once the game's texts are there (some load after the table).
        if mode.list and not listed and waited > 10 then
            listed = true
            for _, a in G.ipairs(list) do
                local ok, title, description = G.pcall(a.describe)
                if not ok then
                    if G.native and G.native.logError then G.native.logError(G.tostring(title)) end
                    title, description = a.name or key(a), ''
                end
                local fields = {'"title":' .. quote(title), '"description":' .. quote(description),
                                '"points":' .. G.tostring(a.points or 10)}
                if a.type then fields[#fields + 1] = '"type":' .. quote(a.type) end
                if a.bonus then fields[#fields + 1] = '"bonus":true' end
                fields[#fields + 1] = '"event":' .. quote(key(a))
                send(PREFIX, 'l{' .. G.table.concat(fields, ',') .. '}')
            end
            send(PREFIX, 'L')
        end
        if not mode.report then return end
        if not started then
            started = true
            send(PREFIX, 's')
        end
        for _, a in G.ipairs(list) do
            local k = key(a)
            if not reported[k] then
                local ok, done = G.pcall(a.unlocked)
                if ok and done then
                    reported[k] = true
                    send(PREFIX, 'e' .. k)
                end
            end
        end
    end)
end
)";

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += char(c);
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else out += char(c);
    }
    return out + "\"";
}

// RecompLauncher's end of the channel: a named pipe on Windows, a Unix socket elsewhere.
// Only written to; the launcher's messages (quiet, and lists with memory rules) are not
// needed for events.
class Channel {
public:
    bool open(const char* name) {
#if defined(_WIN32)
        const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        std::wstring wide(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wide.data(), n);
        for (int attempt = 0; attempt < 5; ++attempt) {
            pipe_ = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) return true;
            if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(wide.c_str(), 1000)) break;
        }
        return false;
#elif defined(__SWITCH__)
        (void)name;
        return false;
#else
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (std::strlen(name) >= sizeof addr.sun_path) return false;
        std::strcpy(addr.sun_path, name);
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
#ifdef SO_NOSIGPIPE
        const int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) return true;
        close(fd_);
        fd_ = -1;
        return false;
#endif
    }

    // One message: a JSON object on a line. A channel that fails is closed.
    void send(const std::string& json) {
        const std::string line = json + "\n";
#if defined(_WIN32)
        if (pipe_ == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        if (!WriteFile(pipe_, line.data(), DWORD(line.size()), &written, nullptr) || written != line.size()) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
#elif !defined(__SWITCH__)
        if (fd_ < 0) return;
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        size_t done = 0;
        while (done < line.size()) {
            const ssize_t n = ::send(fd_, line.data() + done, line.size() - done, flags);
            if (n <= 0) {
                close(fd_);
                fd_ = -1;
                return;
            }
            done += size_t(n);
        }
#else
        (void)line;
#endif
    }

private:
#if defined(_WIN32)
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

}  // namespace

void report_achievements() {
    static bool done = false;  // one channel, whichever mod asks
    if (done) return;
    done = true;
    static Channel channel;
    bool report = false;
    if (const char* session = std::getenv("RL_SESSION"); session && *session && channel.open(session)) {
        report = true;
        channel.send(std::string(R"({"t":"hello","sdk":1,"console":"","memory":false,"game":)") +
                     json_string(std::string("slingshot-animals-recomp--") + recomp::game.id) + "}");
    }
    static std::string list_file;
    if (const std::string* f = slingshot::mod_option("game-statistics", "achievement-list")) list_file = *f;
    if (!report && list_file.empty()) return;

    const std::string mode = std::string("return {report = ") + (report ? "true" : "false") +
                             ", list = " + (list_file.empty() ? "false" : "true") + "}";
    recomp::vfs_add_virtual_file(std::string(kPrefix) + "mode", [mode] { return mode; });
    static std::vector<std::string> entries;
    on_script_message(kPrefix, [](const std::string& m) {
        if (m == "s") {
            channel.send(R"({"t":"save_loaded"})");
        } else if (m.size() > 1 && m[0] == 'e') {
            channel.send(R"({"t":"event","name":)" + json_string(m.substr(1)) + "}");
        } else if (m.size() > 2 && m[0] == 'l' && m[1] == '{') {
            entries.push_back("{\"id\":" + std::to_string(entries.size() + 1) + "," + m.substr(2));
        } else if (m == "L" && !list_file.empty()) {
            std::ofstream out(list_file, std::ios::binary | std::ios::trunc);
            out << "{\n  \"format\": 1,\n  \"game\": "
                << json_string(std::string("slingshot-animals-recomp--") + recomp::game.id)
                << ",\n  \"source\": \"events\",\n  \"achievements\": [\n";
            for (size_t i = 0; i < entries.size(); ++i) out << "    " << entries[i] << (i + 1 < entries.size() ? ",\n" : "\n");
            out << "  ]\n}\n";
            std::fprintf(stderr, "game-statistics: %zu achievements written to %s\n", entries.size(), list_file.c_str());
        }
    });
    append_lua(recomp::game.main_script, std::string(kEachFrameLua) + kReporterLua);
}

}  // namespace slingshot::mods
