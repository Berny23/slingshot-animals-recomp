// SPDX-License-Identifier: GPL-3.0-or-later
#include "watchdog.hpp"

#include <chrono>
#include <cstdio>

#include "recomp/runtime.hpp"

namespace slingshot {
namespace {

constexpr int64_t kLimitMs = 10000;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Watchdog::Watchdog() : game_thread_(pthread_self()), thread_([this] { run(); }) {}

Watchdog::~Watchdog() {
    {
        std::lock_guard lk(lock_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void Watchdog::frame_started() { started_ms_ = now_ms(); }
void Watchdog::frame_finished() { started_ms_ = 0; }

void Watchdog::run() {
    std::unique_lock lk(lock_);
    int64_t reported = 0;  // start of the frame last reported
    while (!cv_.wait_for(lk, std::chrono::seconds(1), [this] { return stop_; })) {
        const int64_t start = started_ms_;
        if (!start || now_ms() - start < kLimitMs || now_ms() - reported < kLimitMs) continue;
        std::fprintf(stderr, "host: the game has not finished a frame for %lld s; game thread report:\n",
                     static_cast<long long>((now_ms() - start) / 1000));
        recomp::request_thread_report(game_thread_);
        reported = now_ms();
    }
}

}  // namespace slingshot
