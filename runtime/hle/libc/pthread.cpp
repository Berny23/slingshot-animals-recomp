// SPDX-License-Identifier: GPL-3.0-or-later
// <pthread.h> on host threads.
//
// Bionic's 32-bit synchronisation objects are tiny (a mutex or condition
// variable is one int), so they are mapped by guest address to host objects
// created on first use. Every mutex is recursive, whatever its type (the
// initial guest value: 0 = normal, 0x4000 = recursive, 0x8000 = error checking).
#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "libc.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::hle {
namespace {

using u32 = uint32_t;

template <typename T>
class ObjectMap {
public:
    T& get(u32 addr) {
        {
            std::shared_lock lk(lock_);
            auto it = map_.find(addr);
            if (it != map_.end()) return *it->second;
        }
        std::unique_lock lk(lock_);
        auto& p = map_[addr];
        if (!p) p = std::make_unique<T>();
        return *p;
    }
    void erase(u32 addr) {
        std::unique_lock lk(lock_);
        map_.erase(addr);
    }

private:
    std::shared_mutex lock_;
    std::unordered_map<u32, std::unique_ptr<T>> map_;
};

// Never destroyed: guest threads still block on these objects while the
// process exits.
ObjectMap<std::recursive_mutex>& g_mutexes = *new ObjectMap<std::recursive_mutex>;
ObjectMap<std::condition_variable_any>& g_conds = *new ObjectMap<std::condition_variable_any>;
ObjectMap<std::shared_mutex>& g_rwlocks = *new ObjectMap<std::shared_mutex>;

// ---- mutexes
int h_pthread_mutex_init(u32 m, u32 attr) {
    g_mutexes.erase(m);
    st32(m, attr ? ld32(attr) : 0);
    return 0;
}
int h_pthread_mutexattr_init(u32 a) {
    st32(a, 0);
    return 0;
}
int h_pthread_mutexattr_settype(u32 a, int type) {  // stored as the initial mutex value
    st32(a, type == 1 ? 0x4000 : type == 2 ? 0x8000 : 0);
    return 0;
}
int h_pthread_mutexattr_destroy(u32) { return 0; }
int h_pthread_mutex_destroy(u32 m) {
    g_mutexes.erase(m);
    return 0;
}
int h_pthread_mutex_lock(u32 m) {
    g_mutexes.get(m).lock();
    return 0;
}
int h_pthread_mutex_trylock(u32 m) { return g_mutexes.get(m).try_lock() ? 0 : EBUSY; }
int h_pthread_mutex_unlock(u32 m) {
    g_mutexes.get(m).unlock();
    return 0;
}

// ---- condition variables
int h_pthread_cond_init(u32 c, u32) {
    g_conds.erase(c);
    return 0;
}
int h_pthread_cond_destroy(u32 c) {
    g_conds.erase(c);
    return 0;
}
int h_pthread_cond_signal(u32 c) {
    g_conds.get(c).notify_one();
    return 0;
}
int h_pthread_cond_broadcast(u32 c) {
    g_conds.get(c).notify_all();
    return 0;
}
int h_pthread_cond_wait(u32 c, u32 m) {
    g_conds.get(c).wait(g_mutexes.get(m));
    return 0;
}
int h_pthread_cond_timedwait_monotonic(u32 c, u32 m, u32 abstime) {
    using namespace std::chrono;
    const auto deadline = steady_clock::time_point(seconds(int32_t(ld32(abstime))) + nanoseconds(int32_t(ld32(abstime + 4))));
    return g_conds.get(c).wait_until(g_mutexes.get(m), deadline) == std::cv_status::timeout ? ETIMEDOUT : 0;
}
int h_pthread_cond_timedwait(u32 c, u32 m, u32 abstime) {
    using namespace std::chrono;
    const auto deadline = system_clock::time_point(seconds(int32_t(ld32(abstime))) + duration_cast<system_clock::duration>(nanoseconds(int32_t(ld32(abstime + 4)))));
    return g_conds.get(c).wait_until(g_mutexes.get(m), deadline) == std::cv_status::timeout ? ETIMEDOUT : 0;
}
int h_pthread_condattr_init(u32 a) {
    st32(a, 0);
    return 0;
}
int h_pthread_condattr_destroy(u32) { return 0; }

// ---- rwlocks
int h_pthread_rwlock_init(u32 l, u32) {
    g_rwlocks.erase(l);
    return 0;
}
int h_pthread_rwlock_destroy(u32 l) {
    g_rwlocks.erase(l);
    return 0;
}
int h_pthread_rwlock_rdlock(u32 l) {
    g_rwlocks.get(l).lock_shared();
    return 0;
}
int h_pthread_rwlock_wrlock(u32 l) {
    g_rwlocks.get(l).lock();
    return 0;
}
// The host doesn't report which mode is held, so remember it per thread.
thread_local std::unordered_map<u32, int> t_rw_mode;
int h_pthread_rwlock_unlock(u32 l) {
    // Writers are exclusive: if this thread wrote, unlock exclusive; else shared.
    auto& lock = g_rwlocks.get(l);
    auto it = t_rw_mode.find(l);
    if (it != t_rw_mode.end() && it->second > 0) {
        --it->second;
        lock.unlock();
    } else {
        lock.unlock_shared();
    }
    return 0;
}
int h_pthread_rwlock_wrlock_tracked(u32 l) {
    h_pthread_rwlock_wrlock(l);
    ++t_rw_mode[l];
    return 0;
}

// ---- thread-specific data
struct Key {
    bool used = false;
    u32 destructor = 0;
};
std::mutex g_key_lock;
std::vector<Key> g_keys(1);  // key 0 is reserved
thread_local std::unordered_map<u32, u32> t_values;

int h_pthread_key_create(u32 out, u32 destructor) {
    std::lock_guard lk(g_key_lock);
    g_keys.push_back({true, destructor});
    st32(out, u32(g_keys.size() - 1));
    return 0;
}
int h_pthread_key_delete(u32 k) {
    std::lock_guard lk(g_key_lock);
    if (k < g_keys.size()) g_keys[k] = {};
    return 0;
}
u32 h_pthread_getspecific(u32 k) {
    auto it = t_values.find(k);
    return it == t_values.end() ? 0 : it->second;
}
int h_pthread_setspecific(u32 k, u32 v) {
    t_values[k] = v;
    return 0;
}
void run_key_destructors() {
    for (int round = 0; round < 4 && !t_values.empty(); ++round) {
        auto values = std::move(t_values);
        t_values.clear();
        for (auto [k, v] : values) {
            u32 d = 0;
            {
                std::lock_guard lk(g_key_lock);
                if (k < g_keys.size() && g_keys[k].used) d = g_keys[k].destructor;
            }
            if (d && v) guest_call(d, {v});
        }
    }
}

// ---- once
std::recursive_mutex g_once_lock;
int h_pthread_once(u32 once, u32 fn) {
    if (__atomic_load_n(reinterpret_cast<u32*>(g_mem + once), __ATOMIC_ACQUIRE) == 2) return 0;
    std::lock_guard lk(g_once_lock);
    if (ld32(once) == 0) {
        st32(once, 1);
        guest_call(fn);
        __atomic_store_n(reinterpret_cast<u32*>(g_mem + once), 2u, __ATOMIC_RELEASE);
    }
    return 0;
}

// ---- threads
// Host threads are detached right away; join waits for `done`. This keeps the
// pthread semantics std::thread lacks (a thread may detach itself) and allows
// the self-join handling below.
struct Thread {
    std::mutex lock;
    std::condition_variable cv;
    bool done = false;
    u32 result = 0;
};
std::mutex g_thread_lock;
std::unordered_map<u32, std::shared_ptr<Thread>> g_threads;  // joinable threads
std::atomic<u32> g_next_thread{2};
thread_local u32 t_self = 1;  // main thread

int h_pthread_create(u32 out, u32 attr, u32 start, u32 arg) {
    u32 stack = 1u << 20;
    if (attr && ld32(attr + 8) >= 64 * 1024) stack = ld32(attr + 8);
    const bool detached = attr && (ld32(attr) & 1);  // PTHREAD_ATTR_FLAG_DETACHED
    auto t = std::make_shared<Thread>();
    const u32 id = g_next_thread++;
    if (!detached) {
        std::lock_guard lk(g_thread_lock);
        g_threads[id] = t;
    }
    st32(out, id);  // before the thread runs, as bionic does
    std::thread([t, id, start, arg, stack] {
        CPU* c = create_cpu(stack);
        bind_cpu(c);
        t_self = id;
        const u32 result = guest_call(c, start, {arg});
        run_key_destructors();
        bind_cpu(nullptr);
        destroy_cpu(c);
        std::lock_guard lk(t->lock);
        t->result = result;
        t->done = true;
        t->cv.notify_all();
    }).detach();
    return 0;
}
int h_pthread_join(u32 id, u32 retval) {
    if (id == t_self) {
        // bionic returns EDEADLK, which std::thread::join turns into std::terminate.
        // A network worker's completion callback can drop the last reference to its
        // owner. Detach instead: the thread ends right after.
        std::fprintf(stderr, "host: thread %u joined itself; detached it instead\n", id);
        std::lock_guard lk(g_thread_lock);
        g_threads.erase(id);
        return 0;
    }
    std::shared_ptr<Thread> t;
    {
        std::lock_guard lk(g_thread_lock);
        auto it = g_threads.find(id);
        if (it == g_threads.end()) return ESRCH;
        t = it->second;
        g_threads.erase(it);
    }
    std::unique_lock lk(t->lock);
    t->cv.wait(lk, [&] { return t->done; });
    if (retval) st32(retval, t->result);
    return 0;
}
int h_pthread_detach(u32 id) {
    std::lock_guard lk(g_thread_lock);
    return g_threads.erase(id) ? 0 : ESRCH;
}
u32 h_pthread_self() { return t_self; }
int h_pthread_equal(u32 a, u32 b) { return a == b; }
int h_pthread_setname_np(u32, u32 name) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%s", mem::str(name));
    ::pthread_setname_np(::pthread_self(), buf);
    return 0;
}
int h_pthread_getschedparam(u32, u32 policy, u32 param) {
    if (policy) st32(policy, 0);
    if (param) st32(param, 0);
    return 0;
}
int h_pthread_setschedparam(u32, int, u32) { return 0; }

#define REG(name) RECOMP_REGISTER_IMPORT(#name, RECOMP_HLE(h_##name))
REG(pthread_mutex_init); REG(pthread_mutex_destroy); REG(pthread_mutex_lock); REG(pthread_mutex_trylock);
REG(pthread_mutex_unlock); REG(pthread_cond_init); REG(pthread_cond_destroy); REG(pthread_cond_signal);
REG(pthread_mutexattr_init); REG(pthread_mutexattr_settype); REG(pthread_mutexattr_destroy);
REG(pthread_cond_broadcast); REG(pthread_cond_wait); REG(pthread_cond_timedwait); REG(pthread_cond_timedwait_monotonic); REG(pthread_condattr_init);
REG(pthread_condattr_destroy); REG(pthread_rwlock_init); REG(pthread_rwlock_destroy); REG(pthread_rwlock_rdlock);
RECOMP_REGISTER_IMPORT("pthread_rwlock_wrlock", RECOMP_HLE(h_pthread_rwlock_wrlock_tracked));
REG(pthread_rwlock_unlock); REG(pthread_key_create); REG(pthread_key_delete); REG(pthread_getspecific);
REG(pthread_setspecific); REG(pthread_once); REG(pthread_create); REG(pthread_join); REG(pthread_detach);
REG(pthread_self); REG(pthread_equal); REG(pthread_setname_np); REG(pthread_getschedparam); REG(pthread_setschedparam);

}  // namespace
}  // namespace recomp::hle
