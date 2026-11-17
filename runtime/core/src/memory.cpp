// SPDX-License-Identifier: GPL-3.0-or-later
#include "recomp/memory.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

#include "os.hpp"

namespace recomp {
uint8_t* g_mem = nullptr;
}

namespace recomp::mem {
namespace {

constexpr uint32_t kPage = 4096;
constexpr uint32_t kSmallMax = 2048;   // larger requests are page-granular
constexpr uint32_t kSlabPages = 16;    // 64 KiB slabs for small size classes
constexpr unsigned kClasses = kSmallMax / 16;
constexpr uint8_t kLargeHead = 0xfe;   // page_class_ markers
constexpr uint8_t kLargeTail = 0xfd;
constexpr uint8_t kUnused = 0xff;

std::mutex g_lock;
uint32_t g_bump = kHeapBase;                 // next never-used heap address
uint32_t g_free_small[kClasses + 1] = {};    // singly linked free lists (guest addresses)
std::vector<uint8_t> g_page_class;           // per heap page: size class, large marker or unused
std::map<uint32_t, uint32_t> g_large;        // large allocation start -> pages
std::map<uint32_t, uint32_t> g_free_runs;    // free page run start -> pages
std::multimap<uint32_t, uint32_t> g_free_by_size;  // pages -> start
uint32_t g_static_next = kHleDataBase;

uint32_t page_index(uint32_t a) { return (a - kHeapBase) / kPage; }

uint32_t bump_pages(uint32_t pages) {
    const uint64_t end = uint64_t(g_bump) + uint64_t(pages) * kPage;
    if (end > kHeapEnd) return 0;
    const uint32_t a = g_bump;
    g_bump = uint32_t(end);
    return a;
}

void erase_run(uint32_t start) {
    auto it = g_free_runs.find(start);
    const uint32_t pages = it->second;
    for (auto [b, e] = g_free_by_size.equal_range(pages); b != e; ++b)
        if (b->second == start) { g_free_by_size.erase(b); break; }
    g_free_runs.erase(it);
}

void insert_run(uint32_t start, uint32_t pages) {
    // Coalesce with neighbours.
    auto next = g_free_runs.find(start + pages * kPage);
    if (next != g_free_runs.end()) {
        pages += next->second;
        erase_run(next->first);
    }
    auto prev = g_free_runs.lower_bound(start);
    if (prev != g_free_runs.begin()) {
        --prev;
        if (prev->first + prev->second * kPage == start) {
            start = prev->first;
            pages += prev->second;
            erase_run(prev->first);
        }
    }
    g_free_runs[start] = pages;
    g_free_by_size.emplace(pages, start);
}

uint32_t find_pages(uint32_t pages) {
    auto it = g_free_by_size.lower_bound(pages);
    if (it != g_free_by_size.end()) {
        const uint32_t start = it->second, have = it->first;
        erase_run(start);
        if (have > pages) insert_run(start + pages * kPage, have - pages);
        return start;
    }
    return bump_pages(pages);
}

uint32_t alloc_pages(uint32_t pages) {
    const uint32_t a = find_pages(pages);
    if (a && !os::commit(g_mem + a, size_t(pages) * kPage)) return 0;
    return a;
}

uint32_t alloc_small_locked(unsigned cls) {
    if (!g_free_small[cls]) {
        const uint32_t slab = alloc_pages(kSlabPages);
        if (!slab) return 0;
        for (uint32_t p = 0; p < kSlabPages; ++p) g_page_class[page_index(slab) + p] = uint8_t(cls);
        const uint32_t sz = cls * 16;
        // Thread the new slab onto the free list.
        uint32_t head = 0;
        for (uint32_t a = slab + (kSlabPages * kPage / sz - 1) * sz;; a -= sz) {
            st32(a, head);
            head = a;
            if (a == slab) break;
        }
        g_free_small[cls] = head;
    }
    const uint32_t a = g_free_small[cls];
    g_free_small[cls] = ld32(a);
    return a;
}

uint32_t alloc_large_locked(uint32_t size) {
    const uint32_t pages = (size + kPage - 1) / kPage;
    const uint32_t a = alloc_pages(pages);
    if (!a) return 0;
    g_large[a] = pages;
    g_page_class[page_index(a)] = kLargeHead;
    for (uint32_t p = 1; p < pages; ++p) g_page_class[page_index(a) + p] = kLargeTail;
    return a;
}

}  // namespace

bool init() {
    const size_t size = (size_t(1) << 32) + 2 * kPage;  // + guard for accesses straddling 4 GiB
    // Everything except the null page is read/write; the heap is committed as it grows.
    g_mem = os::reserve_arena(size, kNullPageEnd, size_t(1) << 32);
    if (!g_mem || !os::commit(g_mem + kNullPageEnd, kHeapBase - kNullPageEnd) ||
        !os::commit(g_mem + kHeapEnd, (size_t(1) << 32) - kHeapEnd))
        return false;
    g_page_class.assign((kHeapEnd - kHeapBase) / kPage, kUnused);
    return true;
}

bool is_guest(const void* p) {
    const auto* b = static_cast<const uint8_t*>(p);
    return b >= g_mem && b < g_mem + (size_t(1) << 32);
}

uint32_t alloc(uint32_t size, uint32_t align) {
    if (size == 0) size = 1;
    std::lock_guard lk(g_lock);
    if (size <= kSmallMax && align <= 16) return alloc_small_locked((size + 15) / 16);
    if (align <= kPage) return alloc_large_locked(size);
    // Rare: alignment beyond a page. Over-allocate and leak the slack.
    const uint32_t a = alloc_large_locked(size + align);
    return a ? (a + align - 1) & ~(align - 1) : 0;
}

uint32_t alloc_zeroed(uint32_t size) {
    const uint32_t a = alloc(size);
    if (a) std::memset(g_mem + a, 0, size);
    return a;
}

uint32_t usable_size(uint32_t a) {
    if (a < kHeapBase || a >= kHeapEnd) return 0;
    std::lock_guard lk(g_lock);
    const uint8_t cls = g_page_class[page_index(a)];
    if (cls <= kClasses) return cls * 16;
    if (cls == kLargeHead) {
        auto it = g_large.find(a & ~(kPage - 1));
        if (it != g_large.end()) return it->second * kPage - (a - it->first);
    }
    return 0;
}

void free(uint32_t a) {
    if (!a) return;
    if (a < kHeapBase || a >= kHeapEnd) {
        std::fprintf(stderr, "recomp: free of non-heap address 0x%08x\n", a);
        return;
    }
    std::lock_guard lk(g_lock);
    const uint8_t cls = g_page_class[page_index(a)];
    if (cls >= 1 && cls <= kClasses) {
        st32(a, g_free_small[cls]);
        g_free_small[cls] = a;
        return;
    }
    if (cls == kLargeHead) {
        auto it = g_large.find(a);
        if (it == g_large.end()) return;
        const uint32_t pages = it->second;
        g_large.erase(it);
        for (uint32_t p = 0; p < pages; ++p) g_page_class[page_index(a) + p] = kUnused;
        // Give the memory back to the OS; the range stays reserved.
        os::discard(g_mem + a, size_t(pages) * kPage);
        insert_run(a, pages);
        return;
    }
    std::fprintf(stderr, "recomp: invalid free 0x%08x\n", a);
}

uint32_t realloc(uint32_t a, uint32_t size) {
    if (!a) return alloc(size);
    if (size == 0) {
        free(a);
        return 0;
    }
    const uint32_t old = usable_size(a);
    if (size <= old && (old <= kSmallMax || size > old / 2)) return a;
    const uint32_t n = alloc(size);
    if (!n) return 0;
    std::memcpy(g_mem + n, g_mem + a, std::min(old, size));
    free(a);
    return n;
}

uint32_t static_alloc(uint32_t size, uint32_t align) {
    std::lock_guard lk(g_lock);
    const uint32_t a = (g_static_next + align - 1) & ~(align - 1);
    if (a + size > kHleDataBase + kHleDataSize) {
        std::fprintf(stderr, "recomp: HLE data area exhausted\n");
        std::abort();
    }
    g_static_next = a + size;
    return a;
}

uint32_t dup_string(std::string_view s) {
    const uint32_t a = alloc(uint32_t(s.size() + 1));
    std::memcpy(g_mem + a, s.data(), s.size());
    g_mem[a + s.size()] = 0;
    return a;
}

uint32_t static_string(std::string_view s) {
    const uint32_t a = static_alloc(uint32_t(s.size() + 1), 1);
    std::memcpy(g_mem + a, s.data(), s.size());
    g_mem[a + s.size()] = 0;
    return a;
}

}  // namespace recomp::mem
