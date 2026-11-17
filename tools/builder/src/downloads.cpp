// SPDX-License-Identifier: GPL-3.0-or-later
#include "downloads.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSysInfo>

#include "paths.hpp"
#include "zip.hpp"

namespace builder {
namespace {

const QString kGoogle = "https://dl.google.com/android/repository/";
const QString kCMake = "https://github.com/Kitware/CMake/releases/download/v4.5.0/";
const QString kNinja = "https://github.com/ninja-build/ninja/releases/download/v1.13.2/";
const QString kJdk = "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/";

// Android SDK packages: Google's repository (repository2-3.xml); JDK: Eclipse
// Temurin 21; CMake 4.5.0; Ninja 1.13.2; llvm-mingw 20261104 (UCRT); Zig 0.17.1.
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
const Download kCMakeDl{"CMake", kCMake + "cmake-4.5.0-linux-x86_64.tar.gz",
                        "81f0d5b0960fb6adc8197030ffc827fba937640d51e73e39e0e9e40d88c66f6f", 57'000'000, "cmake"};
const Download kNinjaDl{"Ninja", kNinja + "ninja-linux.zip",
                        "5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6", 130'000, "ninja"};
const Download kNdkDl{"Android NDK", kGoogle + "android-ndk-r30-linux.zip",
                      "753611f410d002cfcd3f3dc2ef49aad532089d3180b436c060a90bf0fcb64df2", 738'633'529, "android-ndk"};
const Download kBuildToolsDl{"Android build tools", kGoogle + "build-tools_r37_linux.zip",
                             "01af179347cbcd9c208b7f8171f7b21f6dd1d2f85bcd15e88caa51d5d7b86060", 63'000'000,
                             "android-sdk/build-tools/37.0.0"};
const Download kPlatformToolsDl{"Android platform tools", kGoogle + "platform-tools_r37.0.1-linux.zip",
                                "d230f13842f60f782a8645f9c813f8f845bf36089ea7289f28c48f17979313f1", 9'054'187,
                                "android-sdk/platform-tools"};
const Download kJdkDl{"Java", kJdk + "OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz",
                      "d7f5d5cf865a2902b70ff549b97586a7d3f234e016f041ab447ea740c7d66d1c", 207'858'352, "jdk"};
// Zig 0.17.1: clang with glibc headers and libc++, when no compiler is installed
const Download kZigDl{"Zig (C++ compiler)", "https://ziglang.org/download/0.17.1/zig-x86_64-linux-0.17.1.tar.xz",
                      "3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a", 57'411'036, "zig"};
#define SLINGSHOT_HAVE_DOWNLOADS 1
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
const Download kCMakeDl{"CMake", kCMake + "cmake-4.5.0-windows-x86_64.zip",
                        "301b1728317ccd84e984b02e366a8293735bc5ac618757d3d8f58b578f062411", 48'000'000, "cmake"};
const Download kNinjaDl{"Ninja", kNinja + "ninja-win.zip",
                        "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65", 300'000, "ninja"};
const Download kLlvmMingwDl{"LLVM MinGW",
                            "https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/"
                            "llvm-mingw-20261104-ucrt-x86_64.zip",
                            "552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f", 180'000'000,
                            "llvm-mingw"};
const Download kNdkDl{"Android NDK", kGoogle + "android-ndk-r30-windows.zip",
                      "b830098aaf18b67a42eb831c404e15e5f2990a474f054ac145b0bc957ac6d729", 727'980'788, "android-ndk"};
const Download kBuildToolsDl{"Android build tools", kGoogle + "build-tools_r37_windows.zip",
                             "68075aa319ed8a01cf1a565ed1e61a3c1a801dd49191c35851248dc293c33b1a", 61'077'164,
                             "android-sdk/build-tools/37.0.0"};
const Download kPlatformToolsDl{"Android platform tools", kGoogle + "platform-tools_r37.0.1-win.zip",
                                "45f4d63113e895ebde0c90f194099a4676b6ac653bd28d54314a9e022bbc1a99", 8'044'989,
                                "android-sdk/platform-tools"};
const Download kJdkDl{"Java", kJdk + "OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip",
                      "a4dbaa05fca4bd89d84584e58c459bd732efebb1722af80ab13c6516832004f9", 205'000'000, "jdk"};
#define SLINGSHOT_HAVE_DOWNLOADS 1
#elif defined(Q_OS_MACOS)
const Download kCMakeDl{"CMake", kCMake + "cmake-4.5.0-macos-universal.tar.gz",
                        "a645d69fd21e521759cc0c64e7c60462d43faf0c7214ac05f75c80f7c5037dde", 80'000'000, "cmake"};
const Download kNinjaDl{"Ninja", kNinja + "ninja-mac.zip",
                        "c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b", 300'000, "ninja"};
const Download kNdkDl{"Android NDK", kGoogle + "android-ndk-r30-darwin.zip",
                      "d125634de97b26deb1e1bb1a562f9d839aa5803d1784a1e414485f5ccbe6739f", 974'984'488, "android-ndk"};
const Download kBuildToolsDl{"Android build tools", kGoogle + "build-tools_r37_macosx.zip",
                             "b5b1ac529028a49f11b596b89d9b34252e0f39388ee7dbd16ae3110f1c9c5722", 81'933'386,
                             "android-sdk/build-tools/37.0.0"};
const Download kPlatformToolsDl{"Android platform tools", kGoogle + "platform-tools_r37.0.1-darwin.zip",
                                "ee39ad5967e95c2a07f04dbcbde96b1a0c916ba376096db5d2f498b7727a5d1d", 16'110'554,
                                "android-sdk/platform-tools"};
#if defined(Q_PROCESSOR_ARM_64)
const Download kJdkDl{"Java", kJdk + "OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.13_9.tar.gz",
                      "bf036b35b071bfb2f54f2c0af8ee8d88afa95ee6eb3dce02c0f94cfc2524d04a", 195'000'000, "jdk"};
#else
const Download kJdkDl{"Java", kJdk + "OpenJDK21U-jdk_x64_mac_hotspot_21.0.13_9.tar.gz",
                      "9aa1d1c17ae5467a022679e7e0af5c00797d94ac462ed1a46f4db07a8a34a6dd", 195'000'000, "jdk"};
#endif
#define SLINGSHOT_HAVE_DOWNLOADS 1
#endif

#ifdef SLINGSHOT_HAVE_DOWNLOADS
// Every system: the platform's android.jar.
const Download kPlatformDl{"Android platform", kGoogle + "platform-36_r02.zip",
                           "37607369a28c5b640b3a7998868d45898ebcb777565a0e85f9acf36f29631d2e", 65'878'410,
                           "android-sdk/platforms/android-36"};
#endif

const QString kStamp = ".slingshot-sha256";

QString dest_dir(const Download& d) { return cache_dir() + "/tools/" + d.dest; }

// The only folder in `dir` (an archive's top folder), else `dir`.
QString single_root(const QString& dir) {
    const QDir d(dir);
    const QStringList entries = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (entries.size() == 1 && QFileInfo(d.filePath(entries[0])).isDir()) return d.filePath(entries[0]);
    return dir;
}

}  // namespace

const Download* download_for(Tool tool) {
#ifdef SLINGSHOT_HAVE_DOWNLOADS
    switch (tool) {
        case Tool::CMake: return &kCMakeDl;
        case Tool::Ninja: return &kNinjaDl;
        case Tool::Ndk: return &kNdkDl;
        case Tool::BuildTools: return &kBuildToolsDl;
        case Tool::Platform: return &kPlatformDl;
        case Tool::PlatformTools: return &kPlatformToolsDl;
        case Tool::Jdk: return &kJdkDl;
        case Tool::Zig:
#if defined(Q_OS_LINUX)
            return &kZigDl;
#else
            return nullptr;
#endif
        case Tool::LlvmMingw:
#ifdef Q_OS_WIN
            return &kLlvmMingwDl;
#else
            return nullptr;
#endif
    }
#else
    (void)tool;
#endif
    return nullptr;
}

QString cached(const Download& d) {
    QFile stamp(dest_dir(d) + "/" + kStamp);
    if (stamp.open(QIODevice::ReadOnly) && QString::fromLatin1(stamp.readAll()).trimmed() == d.sha256)
        return dest_dir(d);
    return {};
}

QString fetch(const Download& d, const FetchContext& ctx, QString& error) {
    if (QString dir = cached(d); !dir.isEmpty()) return dir;
    const QString dl_dir = cache_dir() + "/downloads";
    QDir().mkpath(dl_dir);
    const QString archive = dl_dir + "/" + QUrl(d.url).fileName();
    if (ctx.log) ctx.log("Downloading " + d.url);

    // Download with the hash computed on the way.
    {
        QFile out(archive + ".part");
        if (!out.open(QIODevice::WriteOnly)) {
            error = "Cannot write " + out.fileName();
            return {};
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QNetworkAccessManager net;
        QNetworkRequest request{QUrl(d.url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, "slingshot-builder");
        QNetworkReply* reply = net.get(request);
        QEventLoop loop;
        qint64 done = 0;
        bool write_failed = false;
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            const QByteArray chunk = reply->readAll();
            hash.addData(chunk);
            done += chunk.size();
            if (out.write(chunk) != chunk.size()) {
                write_failed = true;
                reply->abort();
            }
            if (ctx.progress) {
                const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                ctx.progress(done, total > 0 ? total : d.size);
            }
            if (ctx.cancel && *ctx.cancel) reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QByteArray rest = reply->readAll();
        hash.addData(rest);
        out.write(rest);
        out.close();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const QString net_error = reply->errorString();
        reply->deleteLater();
        if (ctx.cancel && *ctx.cancel) {
            QFile::remove(out.fileName());
            error = "Cancelled";
            return {};
        }
        if (write_failed || failed) {
            QFile::remove(out.fileName());
            error = write_failed ? "Cannot write " + out.fileName() + " (disk full?)"
                                 : "Download of " + d.name + " failed: " + net_error;
            return {};
        }
        if (QString::fromLatin1(hash.result().toHex()) != d.sha256) {
            QFile::remove(out.fileName());
            error = "The download of " + d.name + " is not the expected file (SHA-256 differs). Try again later.";
            return {};
        }
        QFile::remove(archive);
        QFile::rename(out.fileName(), archive);
    }

    // Unpack into a temporary folder, then move its top folder into place.
    if (ctx.log) ctx.log("Unpacking " + d.name);
    const QString dest = dest_dir(d);
    const QString tmp = dest + ".tmp";
    QDir(tmp).removeRecursively();
    QDir(dest).removeRecursively();
    QDir().mkpath(tmp);
    if (archive.endsWith(".zip")) {
        apk::ZipReader zip;
        std::string e;
        if (!zip.open(to_path(archive)) || !apk::unpack(zip, to_path(tmp), e)) {
            error = "Cannot unpack " + d.name + ": " + QString::fromStdString(e.empty() ? zip.error() : e);
            return {};
        }
    } else {
        QProcess tar;
        tar.start("tar", {"-xf", archive, "-C", tmp});
        if (!tar.waitForFinished(-1) || tar.exitCode() != 0) {
            error = "Cannot unpack " + d.name + ": " + QString::fromLocal8Bit(tar.readAllStandardError());
            return {};
        }
    }
    QDir().mkpath(QFileInfo(dest).path());
    if (!QDir().rename(single_root(tmp), dest)) {
        error = "Cannot move " + d.name + " into " + dest;
        return {};
    }
    QDir(tmp).removeRecursively();
    QFile::remove(archive);
    QFile stamp(dest + "/" + kStamp);
    if (stamp.open(QIODevice::WriteOnly)) stamp.write(d.sha256.toLatin1());
    return dest;
}

}  // namespace builder
