// SPDX-License-Identifier: GPL-3.0-or-later
// The single-file Windows release: this program with the builder's folder
// appended as a zip (cat launcher.exe builder.zip > Slingshot-Builder.exe).
// It unpacks the folder once per version into slingshot-builder-data\app\ next
// to itself (%LOCALAPPDATA%\slingshot\app\ where that cannot be written or the
// path is too long) and starts the builder from there with the same arguments
// and SLINGSHOT_BUILDER_HOME set to its own folder; a small window shows the
// progress meanwhile.
#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "sha256.hpp"
#include "zip.hpp"

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTitle[] = L"Slingshot Builder";
constexpr wchar_t kProgram[] = L"slingshot-builder.exe";

int fail(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
    return 1;
}

std::wstring widen(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}

// The arguments after the program name, as typed.
const wchar_t* arguments() {
    const wchar_t* p = GetCommandLineW();
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p) ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p == L' ' || *p == L'\t') ++p;
    return p;
}

// A name for this payload: changes whenever any file in it changes.
std::string payload_id(const apk::ZipReader& zip) {
    apk::Sha256 h;
    for (const apk::ZipEntry& e : zip.entries()) {
        h.update(e.name.data(), e.name.size() + 1);
        h.update(&e.crc, sizeof e.crc);
        h.update(&e.size, sizeof e.size);
    }
    return h.hex().substr(0, 16);
}

// The folder holding kProgram: the payload's top folder or its root.
fs::path program_dir(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / kProgram, ec)) return dir;
    for (const auto& d : fs::directory_iterator(dir, ec))
        if (d.is_directory(ec) && fs::exists(d.path() / kProgram, ec)) return d.path();
    return {};
}

// Whether files can be made in dir (made if missing).
bool writable_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path probe = dir / L".write-test";
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

// "Unpacking…" with a progress bar.
class ProgressWindow {
public:
    ProgressWindow() {
        INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&icc);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);  // the colour STATIC text is drawn on
        wc.lpszClassName = L"SlingshotLauncher";
        RegisterClassW(&wc);

        HDC screen = GetDC(nullptr);
        const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
        auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
        RECT r{0, 0, px(380), px(96)};
        const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&r, style, FALSE);
        const int w = r.right - r.left, h = r.bottom - r.top;
        window_ = CreateWindowExW(0, wc.lpszClassName, kTitle, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, wc.hInstance, nullptr);
        NONCLIENTMETRICSW m{};
        m.cbSize = sizeof m;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
        font_ = CreateFontIndirectW(&m.lfMessageFont);
        HWND label = CreateWindowExW(0, L"STATIC", L"Unpacking\u2026",
                                     WS_CHILD | WS_VISIBLE, px(20), px(20), px(340), px(22), window_, nullptr,
                                     wc.hInstance, nullptr);
        SendMessageW(label, WM_SETFONT, WPARAM(font_), TRUE);
        bar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, px(20), px(52), px(340), px(20),
                               window_, nullptr, wc.hInstance, nullptr);
        SendMessageW(bar_, PBM_SETRANGE32, 0, 1000);
        ShowWindow(window_, SW_SHOWNORMAL);
        pump();
    }
    ~ProgressWindow() {
        DestroyWindow(window_);
        DeleteObject(font_);
    }
    void set(size_t done, size_t total) {
        if (total) SendMessageW(bar_, PBM_SETPOS, WPARAM(done * 1000 / total), 0);
        pump();
    }

private:
    void pump() {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    HWND window_ = nullptr, bar_ = nullptr;
    HFONT font_ = nullptr;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    if (n == 0 || n >= std::size(self)) return fail(L"Cannot find this program's path.");

    apk::ZipReader zip;
    if (!zip.open(fs::path(self))) return fail(L"This download is damaged: " + widen(zip.error()));

    wchar_t local[MAX_PATH];
    const DWORD m = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (m == 0 || m >= MAX_PATH) return fail(L"LOCALAPPDATA is not set.");
    const fs::path system_apps = fs::path(local) / L"slingshot" / L"app";
    // The unpacked files are up to about 100 characters below the version folder.
    const fs::path home = fs::path(self).parent_path();
    const fs::path portable_apps = home / L"slingshot-builder-data" / L"app";
    const bool portable = portable_apps.wstring().size() <= 130 && writable_dir(portable_apps);
    const fs::path apps = portable ? portable_apps : system_apps;
    SetEnvironmentVariableW(L"SLINGSHOT_BUILDER_HOME", home.c_str());
    const fs::path dir = apps / payload_id(zip);
    const fs::path done = dir / L".complete";

    std::error_code ec;
    if (!fs::exists(done, ec)) {
        // Unpack next to the target, then move it in place: a cancelled or
        // failed run leaves no half folder behind.
        const fs::path tmp = apps / (dir.filename().wstring() + L".tmp" + std::to_wstring(GetCurrentProcessId()));
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        std::string error;
        bool ok;
        {
            ProgressWindow progress;
            ok = apk::unpack(zip, tmp, error, [&](size_t done, size_t total) { progress.set(done, total); });
        }
        if (!ok) {
            fs::remove_all(tmp, ec);
            return fail(L"Cannot unpack Slingshot Builder: " + widen(error));
        }
        HANDLE f = CreateFileW((tmp / L".complete").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        fs::remove_all(dir, ec);
        fs::rename(tmp, dir, ec);
        if (ec && !fs::exists(done)) {  // another start may have finished first
            fs::remove_all(tmp, ec);
            return fail(L"Cannot unpack Slingshot Builder into " + dir.wstring());
        }
        fs::remove_all(tmp, ec);
        // Older versions are no longer needed, nor those unpacked before the portable folder.
        std::vector<fs::path> folders{apps};
        if (portable) folders.push_back(system_apps);
        for (const fs::path& folder : folders) {
            for (const auto& d : fs::directory_iterator(folder, ec))
                if (d.path() != dir && d.path().filename().wstring().find(L".tmp") == std::wstring::npos)
                    fs::remove_all(d.path(), ec);
        }
    }

    const fs::path bin = program_dir(dir);
    if (bin.empty()) return fail(L"The download does not contain the builder.");
    std::wstring command = L"\"" + (bin / kProgram).wstring() + L"\" " + arguments();
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, bin.c_str(), &si, &pi))
        return fail(L"Cannot start " + (bin / kProgram).wstring());
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
