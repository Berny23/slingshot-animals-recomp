// SPDX-License-Identifier: GPL-3.0-or-later
// Class registry, object handles and reference counting.
#include <bit>
#include <cstdio>
#include <set>

#include "jni_internal.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::jni {
namespace {

constexpr Ref kRefBase = 0x10000000;  // makes references easy to spot in logs

struct Slot {
    std::unique_ptr<Object> obj;
    uint32_t refs = 0;
};

struct State {
    std::recursive_mutex lock;
    std::map<std::string, std::unique_ptr<ClassDef>, std::less<>> classes;
    std::vector<std::unique_ptr<MethodDef>> methods;
    std::vector<std::unique_ptr<FieldDef>> fields;
    std::vector<Slot> slots;
    std::vector<uint32_t> free_slots;
};
State& st() {
    static State* s = new State;  // never destroyed: used by static registrations and at exit
    return *s;
}
thread_local std::vector<Ref> t_locals;

Slot* slot(Ref r) {
    if (r < kRefBase || (r - kRefBase) % 4) return nullptr;
    const uint32_t i = (r - kRefBase) / 4;
    auto& s = st().slots;
    return i < s.size() && s[i].obj ? &s[i] : nullptr;
}
void retain(Ref r) {
    if (Slot* s = slot(r)) ++s->refs;
}
void release(Ref r) {
    Slot* s = slot(r);
    if (!s || --s->refs) return;
    std::unique_ptr<Object> dead = std::move(s->obj);
    st().free_slots.push_back((r - kRefBase) / 4);
    // ~Object may release further objects.
}
bool is_ref_sig(std::string_view sig) { return !sig.empty() && (sig[0] == 'L' || sig[0] == '['); }

}  // namespace

Object::~Object() {
    if (data) mem::free(data);
    for (Ref r : elems) release(r);
    if (cls) {
        for (auto& [id, v] : fields) {
            FieldDef* f = field_by_id(id);
            if (f && is_ref_sig(f->sig)) release(Ref(v));
        }
    }
}

float Call::f(size_t n) const { return std::bit_cast<float>(uint32_t(args.at(n))); }
double Call::d(size_t n) const { return std::bit_cast<double>(args.at(n)); }
Value float_(float v) { return std::bit_cast<uint32_t>(v); }
Value double_(double v) { return std::bit_cast<uint64_t>(v); }

std::recursive_mutex& lock() { return st().lock; }

Object* object(Ref r) {
    std::lock_guard lk(lock());
    Slot* s = slot(r);
    return s ? s->obj.get() : nullptr;
}

Ref add_object(std::unique_ptr<Object> o) {
    std::lock_guard lk(lock());
    auto& s = st();
    uint32_t i;
    if (!s.free_slots.empty()) {
        i = s.free_slots.back();
        s.free_slots.pop_back();
    } else {
        i = uint32_t(s.slots.size());
        s.slots.emplace_back();
    }
    s.slots[i].obj = std::move(o);
    s.slots[i].refs = 1;
    const Ref r = kRefBase + 4 * i;
    t_locals.push_back(r);
    return r;
}

Ref add_local(Ref r) {
    if (!r) return 0;
    std::lock_guard lk(lock());
    if (!slot(r)) return 0;
    retain(r);
    t_locals.push_back(r);
    return r;
}
Ref new_local(Ref r) { return add_local(r); }

void delete_local(Ref r) {
    if (!r) return;
    std::lock_guard lk(lock());
    for (size_t i = t_locals.size(); i-- > 0;) {
        if (t_locals[i] == r) {
            t_locals.erase(t_locals.begin() + long(i));
            release(r);
            return;
        }
    }
}

Ref new_global(Ref r) {
    std::lock_guard lk(lock());
    if (!slot(r)) return 0;
    retain(r);
    return r;
}
void delete_global(Ref r) {
    std::lock_guard lk(lock());
    release(r);
}

void push_frame_mark(size_t& mark) { mark = t_locals.size(); }
void pop_frame_to(size_t mark) {
    std::lock_guard lk(lock());
    while (t_locals.size() > mark) {
        const Ref r = t_locals.back();
        t_locals.pop_back();
        release(r);
    }
}
NativeFrame::NativeFrame() { push_frame_mark(mark_); }
NativeFrame::~NativeFrame() { pop_frame_to(mark_); }

// ---- classes
ClassDef* find_class(std::string_view name, bool create) {
    std::lock_guard lk(lock());
    auto& m = st().classes;
    auto it = m.find(name);
    if (it != m.end()) return it->second.get();
    if (!create) return nullptr;
    auto c = std::make_unique<ClassDef>();
    c->name = std::string(name);
    if (name != "java/lang/Object") c->super = find_class("java/lang/Object");
    ClassDef* p = c.get();
    m.emplace(std::string(name), std::move(c));
    return p;
}
const std::string& class_name(const ClassDef* c) { return c->name; }

void set_superclass(std::string_view cls, std::string_view super) { find_class(cls)->super = find_class(super); }

Ref class_object(ClassDef* c) {
    if (!c) return 0;
    std::lock_guard lk(lock());
    if (!c->class_ref) {
        auto o = std::make_unique<Object>();
        o->cls = find_class("java/lang/Class");
        o->class_value = c;
        const Ref r = add_object(std::move(o));
        t_locals.pop_back();  // class objects are permanent (refs stays 1)
        c->class_ref = r;
    }
    return c->class_ref;
}

ClassDef* object_class(Ref r) {
    Object* o = object(r);
    return o ? o->cls : nullptr;
}

MethodDef* find_method(ClassDef* c, const std::string& name, const std::string& sig, bool is_static) {
    std::lock_guard lk(lock());
    const std::string key = name + sig;
    for (ClassDef* k = c; k; k = k->super) {
        auto it = k->methods.find(key);
        if (it != k->methods.end()) return it->second;
    }
    // Unknown: create an unimplemented entry so the ID stays valid.
    auto m = std::make_unique<MethodDef>();
    m->owner = c;
    m->name = name;
    m->sig = sig;
    m->is_static = is_static;
    m->id = uint32_t(st().methods.size()) + 1;
    MethodDef* p = m.get();
    c->methods.emplace(key, p);
    st().methods.push_back(std::move(m));
    return p;
}
MethodDef* method_by_id(uint32_t id) {
    std::lock_guard lk(lock());
    return id && id <= st().methods.size() ? st().methods[id - 1].get() : nullptr;
}

void define_method(std::string_view cls, std::string_view name, std::string_view sig, MethodFn fn) {
    std::lock_guard lk(lock());
    ClassDef* c = find_class(cls);
    const std::string key = std::string(name) + std::string(sig);
    auto it = c->methods.find(key);
    MethodDef* m = it != c->methods.end() ? it->second : find_method(c, std::string(name), std::string(sig), false);
    if (m->owner != c) m = find_method(c, std::string(name), std::string(sig), false);  // override in subclass
    m->fn = std::move(fn);
}

FieldDef* find_field(ClassDef* c, const std::string& name, const std::string& sig, bool is_static) {
    std::lock_guard lk(lock());
    for (ClassDef* k = c; k; k = k->super) {
        auto it = k->fields.find(name);
        if (it != k->fields.end()) return it->second;
    }
    auto f = std::make_unique<FieldDef>();
    f->owner = c;
    f->name = name;
    f->sig = sig;
    f->is_static = is_static;
    f->id = uint32_t(st().fields.size()) + 1;
    FieldDef* p = f.get();
    c->fields.emplace(name, p);
    st().fields.push_back(std::move(f));
    if (trace_enabled("jni")) std::fprintf(stderr, "jni: unknown field %s.%s %s\n", c->name.c_str(), name.c_str(), sig.c_str());
    return p;
}
FieldDef* field_by_id(uint32_t id) {
    std::lock_guard lk(lock());
    return id && id <= st().fields.size() ? st().fields[id - 1].get() : nullptr;
}

void define_static_field(std::string_view cls, std::string_view name, std::string_view sig, Value v) {
    std::lock_guard lk(lock());
    ClassDef* c = find_class(cls);
    auto it = c->fields.find(std::string(name));
    FieldDef* f;
    if (it != c->fields.end()) {
        f = it->second;
    } else {
        auto nf = std::make_unique<FieldDef>();
        nf->owner = c;
        nf->name = std::string(name);
        nf->sig = std::string(sig);
        nf->id = uint32_t(st().fields.size()) + 1;
        f = nf.get();
        c->fields.emplace(f->name, f);
        st().fields.push_back(std::move(nf));
    }
    f->is_static = true;
    if (is_ref_sig(f->sig)) retain(Ref(v));
    f->static_value = v;
}

Value get_field(Ref obj, std::string_view name) {
    std::lock_guard lk(lock());
    Object* o = object(obj);
    if (!o) return 0;
    FieldDef* f = find_field(o->cls, std::string(name), "", false);
    auto it = o->fields.find(f->id);
    return it == o->fields.end() ? 0 : it->second;
}
void set_field(Ref obj, std::string_view name, std::string_view sig, Value v) {
    std::lock_guard lk(lock());
    Object* o = object(obj);
    if (!o) return;
    FieldDef* f = find_field(o->cls, std::string(name), std::string(sig), false);
    if (f->sig.empty()) f->sig = std::string(sig);
    Value& slot_value = o->fields[f->id];
    if (is_ref_sig(f->sig)) {
        retain(Ref(v));
        release(Ref(slot_value));
    }
    slot_value = v;
}

Value invoke(MethodDef* m, Call& call) {
    if (m->fn) return m->fn(call);
    static std::mutex seen_lock;
    static std::set<MethodDef*> seen;
    bool first;
    {
        std::lock_guard lk(seen_lock);
        first = seen.insert(m).second;
    }
    if (first && (m->name != "<init>" || trace_enabled("jni")))
        std::fprintf(stderr, "jni: unimplemented %s%s.%s%s\n", m->is_static ? "static " : "", m->owner->name.c_str(),
                     m->name.c_str(), m->sig.c_str());
    return 0;
}

// ---- strings
std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        uint32_t cp;
        size_t n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c & 0xe0) == 0xc0) cp = c & 0x1f, n = 2;
        else if ((c & 0xf0) == 0xe0) cp = c & 0x0f, n = 3;
        else if ((c & 0xf8) == 0xf0) cp = c & 0x07, n = 4;
        else { out.push_back(u'�'); ++i; continue; }
        if (i + n > s.size()) { out.push_back(u'�'); break; }
        for (size_t k = 1; k < n; ++k) cp = cp << 6 | (uint8_t(s[i + k]) & 0x3f);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(char16_t(0xd800 + (cp >> 10)));
            out.push_back(char16_t(0xdc00 + (cp & 0x3ff)));
        } else {
            out.push_back(char16_t(cp));
        }
    }
    return out;
}

std::string utf16_to_utf8(std::u16string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char16_t u : s) {  // each unit separately (surrogates stay 3-byte sequences)
        if (u != 0 && u < 0x80) {
            out.push_back(char(u));
        } else if (u < 0x800) {
            out.push_back(char(0xc0 | (u >> 6)));
            out.push_back(char(0x80 | (u & 0x3f)));
        } else {
            out.push_back(char(0xe0 | (u >> 12)));
            out.push_back(char(0x80 | ((u >> 6) & 0x3f)));
            out.push_back(char(0x80 | (u & 0x3f)));
        }
    }
    return out;
}

Ref new_string_utf16(std::u16string_view s) {
    auto o = std::make_unique<Object>();
    o->cls = find_class("java/lang/String");
    o->is_string = true;
    o->str = std::u16string(s);
    return add_object(std::move(o));
}
Ref new_string(std::string_view utf8) { return new_string_utf16(utf8_to_utf16(utf8)); }

std::string string_utf8(Ref s) {
    std::lock_guard lk(lock());
    Object* o = object(s);
    if (!o || !o->is_string) return {};
    std::string out;  // regular UTF-8 for host code
    for (size_t i = 0; i < o->str.size(); ++i) {
        uint32_t cp = o->str[i];
        if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < o->str.size() && o->str[i + 1] >= 0xdc00 && o->str[i + 1] < 0xe000)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (o->str[++i] - 0xdc00);
        if (cp < 0x80) out.push_back(char(cp));
        else if (cp < 0x800) out += {char(0xc0 | cp >> 6), char(0x80 | (cp & 0x3f))};
        else if (cp < 0x10000) out += {char(0xe0 | cp >> 12), char(0x80 | ((cp >> 6) & 0x3f)), char(0x80 | (cp & 0x3f))};
        else
            out += {char(0xf0 | cp >> 18), char(0x80 | ((cp >> 12) & 0x3f)), char(0x80 | ((cp >> 6) & 0x3f)),
                    char(0x80 | (cp & 0x3f))};
    }
    return out;
}

// ---- objects and arrays
Ref new_object(std::string_view cls) {
    auto o = std::make_unique<Object>();
    o->cls = find_class(cls);
    return add_object(std::move(o));
}

void set_host_data(Ref r, std::shared_ptr<void> data) {
    std::lock_guard lk(lock());
    if (Object* o = object(r)) o->host = std::move(data);
}
std::shared_ptr<void> host_data(Ref r) {
    std::lock_guard lk(lock());
    Object* o = object(r);
    return o ? o->host : nullptr;
}

static uint32_t elem_size(char t) {
    switch (t) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'J': case 'D': return 8;
        default: return 4;
    }
}

Ref new_array(char elem_type, uint32_t length) {
    auto o = std::make_unique<Object>();
    o->cls = find_class(std::string("[") + elem_type);
    o->elem = elem_type;
    o->length = length;
    o->data = mem::alloc_zeroed(length ? length * elem_size(elem_type) : 8);
    return add_object(std::move(o));
}

Ref new_object_array(std::string_view elem_class, uint32_t length) {
    auto o = std::make_unique<Object>();
    o->cls = find_class("[L" + std::string(elem_class) + ";");
    o->elem = 'L';
    o->length = length;
    o->elems.assign(length, 0);
    return add_object(std::move(o));
}

uint32_t array_length(Ref a) {
    Object* o = object(a);
    return o && o->elem ? o->length : 0;
}
uint32_t array_data(Ref a) {
    Object* o = object(a);
    return o ? o->data : 0;
}
Ref array_get(Ref a, uint32_t index) {
    std::lock_guard lk(lock());
    Object* o = object(a);
    if (!o || o->elem != 'L' || index >= o->length) return 0;
    return add_local(o->elems[index]);
}
void array_set(Ref a, uint32_t index, Ref v) {
    std::lock_guard lk(lock());
    Object* o = object(a);
    if (!o || o->elem != 'L' || index >= o->length) return;
    retain(v);
    release(o->elems[index]);
    o->elems[index] = v;
}

}  // namespace recomp::jni
