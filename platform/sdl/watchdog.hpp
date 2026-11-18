// SPDX-License-Identifier: GPL-3.0-or-later
// Reports a hanging game thread: if one frame (the game's update and render)
// takes longer than a few seconds, its guest registers, guest stack and host
// backtrace go to stderr (recomp::request_thread_report) and the thread goes on.
// The report repeats every few seconds while the frame does not finish.
#pragma once

#include <pthread.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace slingshot {

class Watchdog {
public:
    Watchdog();  // watches the calling thread
    ~Watchdog();
    void frame_started();
    void frame_finished();

private:
    void run();

    pthread_t game_thread_;
    std::atomic<int64_t> started_ms_{0};  // 0: not in a frame
    std::mutex lock_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace slingshot
