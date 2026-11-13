// SPDX-License-Identifier: GPL-3.0-or-later
// The guest-visible JNIEnv and JavaVM: function tables of host thunks.
//
// Table indices follow jni.h (JNI 1.6). Argument and return conventions are
// the guest's AAPCS softfp ones: jlong/jdouble use register pairs, variadic
// float arguments arrive promoted to double, va_list is a pointer to the
// argument area and jvalue arrays are 8 bytes per element.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "jni_internal.hpp"
#include "recomp/memory.hpp"
#include "recomp/runtime.hpp"

namespace recomp::jni {
namespace {

using hle::ArgReader;
using u32 = uint32_t;
using u64 = uint64_t;

bool trace() {
    static const bool t = trace_enabled("jni");
    return t;
}

void ret(CPU* c, Value v) {
    c->r[0] = u32(v);
    c->r[1] = u32(v >> 32);
}

// Parameter type characters of a method signature ('L' for any reference).
std::vector<char> param_types(const std::string& sig) {
    std::vector<char> out;
    size_t i = sig.find('(');
    if (i == std::string::npos) return out;
    for (++i; i < sig.size() && sig[i] != ')';) {
        bool array = false;
        while (sig[i] == '[') array = true, ++i;
        if (sig[i] == 'L') {
            const size_t e = sig.find(';', i);
            if (e == std::string::npos) break;
            i = e + 1;
            out.push_back('L');
            continue;
        }
        out.push_back(array ? 'L' : sig[i]);
        ++i;
    }
    return out;
}

// Variadic arguments and va_list share the promotion rules.
void read_varargs(const std::string& sig, ArgReader& a, std::vector<Value>& out) {
    for (char t : param_types(sig)) {
        switch (t) {
            case 'J': out.push_back(a.u64()); break;
            case 'D': out.push_back(a.u64()); break;
            case 'F': out.push_back(float_(float(a.f64()))); break;
            default: out.push_back(a.u32()); break;
        }
    }
}

void read_jvalues(const std::string& sig, u32 p, std::vector<Value>& out) {
    for (char t : param_types(sig)) {
        switch (t) {
            case 'Z': out.push_back(g_mem[p]); break;
            case 'B': out.push_back(u32(int32_t(int8_t(g_mem[p])))); break;
            case 'C': out.push_back(ld16(p)); break;
            case 'S': out.push_back(u32(int32_t(int16_t(ld16(p))))); break;
            case 'J':
            case 'D': out.push_back(ld64(p)); break;
            default: out.push_back(ld32(p)); break;
        }
        p += 8;
    }
}

ClassDef* class_of_ref(Ref clazz) {
    Object* o = object(clazz);
    return o ? o->class_value : nullptr;
}

// Virtual dispatch: the most derived implementation of name+sig.
MethodDef* resolve_virtual(Ref self, MethodDef* m) {
    Object* o = object(self);
    if (!o) return m;
    const std::string key = m->name + m->sig;
    for (ClassDef* k = o->cls; k; k = k->super) {
        auto it = k->methods.find(key);
        if (it != k->methods.end() && it->second->fn) return it->second;
    }
    return m;
}

enum class Kind { Virtual, Nonvirtual, Static };
enum class Var { Dots, V, A };

template <Kind K, Var V>
void call_method(CPU* c) {
    ArgReader a(c);
    a.u32();  // env
    Call call;
    call.self = a.u32();
    if constexpr (K == Kind::Nonvirtual) a.u32();  // clazz
    MethodDef* m = method_by_id(a.u32());
    if (!m) {
        std::fprintf(stderr, "jni: call with invalid method ID (lr=0x%08x)\n", c->r[14]);
        ret(c, 0);
        return;
    }
    if constexpr (V == Var::Dots) read_varargs(m->sig, a, call.args);
    else if constexpr (V == Var::V) {
        ArgReader va = ArgReader::from_memory(c, a.u32());
        read_varargs(m->sig, va, call.args);
    } else {
        read_jvalues(m->sig, a.u32(), call.args);
    }
    if constexpr (K == Kind::Static) call.cls = class_of_ref(call.self);
    else call.cls = object_class(call.self);
    if constexpr (K == Kind::Virtual) m = resolve_virtual(call.self, m);
    if (trace()) std::fprintf(stderr, "jni: call %s.%s%s\n", m->owner->name.c_str(), m->name.c_str(), m->sig.c_str());
    ret(c, invoke(m, call));
}

template <Var V>
void new_object_impl(CPU* c) {
    ArgReader a(c);
    a.u32();
    const Ref clazz = a.u32();
    MethodDef* m = method_by_id(a.u32());
    ClassDef* cls = class_of_ref(clazz);
    if (!cls) {
        ret(c, 0);
        return;
    }
    Call call;
    call.cls = cls;
    call.self = new_object(cls->name);
    if (m) {
        if constexpr (V == Var::Dots) read_varargs(m->sig, a, call.args);
        else if constexpr (V == Var::V) {
            ArgReader va = ArgReader::from_memory(c, a.u32());
            read_varargs(m->sig, va, call.args);
        } else {
            read_jvalues(m->sig, a.u32(), call.args);
        }
        if (trace()) std::fprintf(stderr, "jni: new %s%s\n", cls->name.c_str(), m->sig.c_str());
        invoke(m, call);
    }
    ret(c, call.self);
}

// ---- fields
bool is_ref_type(char t) { return t == 'L' || t == '['; }

template <char T>
void get_field_impl(CPU* c) {  // (env, obj, fieldID)
    std::lock_guard lk(lock());
    Object* o = object(c->r[1]);
    FieldDef* f = field_by_id(c->r[2]);
    Value v = 0;
    if (o && f) {
        auto it = o->fields.find(f->id);
        if (it != o->fields.end()) v = it->second;
    }
    ret(c, is_ref_type(T) ? add_local(Ref(v)) : v);
}
template <char T>
void set_field_impl(CPU* c) {  // (env, obj, fieldID, value)
    ArgReader a(c);
    a.u32();
    const Ref obj = a.u32();
    FieldDef* f = field_by_id(a.u32());
    const Value v = (T == 'J' || T == 'D') ? a.u64() : a.u32();
    if (!f) return;
    std::lock_guard lk(lock());
    if (Object* o = object(obj)) {
        if (is_ref_type(T)) {
            new_global(Ref(v));
            delete_global(Ref(o->fields[f->id]));
        }
        o->fields[f->id] = v;
    }
}
template <char T>
void get_static_field_impl(CPU* c) {  // (env, clazz, fieldID)
    FieldDef* f = field_by_id(c->r[2]);
    const Value v = f ? f->static_value : 0;
    ret(c, is_ref_type(T) ? add_local(Ref(v)) : v);
}
template <char T>
void set_static_field_impl(CPU* c) {
    ArgReader a(c);
    a.u32();
    a.u32();
    FieldDef* f = field_by_id(a.u32());
    const Value v = (T == 'J' || T == 'D') ? a.u64() : a.u32();
    if (!f) return;
    std::lock_guard lk(lock());
    if (is_ref_type(T)) {
        new_global(Ref(v));
        delete_global(Ref(f->static_value));
    }
    f->static_value = v;
}

// ---- primitive arrays
uint32_t elem_size(char t) {
    switch (t) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'J': case 'D': return 8;
        default: return 4;
    }
}
template <char T>
void new_array_impl(CPU* c) { ret(c, new_array(T, c->r[1])); }
void get_array_elements(CPU* c) {  // (env, array, isCopy)
    if (c->r[2]) g_mem[c->r[2]] = 0;
    c->r[0] = array_data(c->r[1]);
}
void release_array_elements(CPU*) {}  // elements are never copied
template <char T>
void get_array_region(CPU* c) {  // (env, array, start, len, buf)
    ArgReader a(c);
    a.u32();
    Object* o = object(a.u32());
    const u32 start = a.u32(), len = a.u32(), buf = a.u32();
    if (!o || !o->data || u64(start) + len > o->length) return;
    std::memmove(g_mem + buf, g_mem + o->data + start * elem_size(T), size_t(len) * elem_size(T));
}
template <char T>
void set_array_region(CPU* c) {
    ArgReader a(c);
    a.u32();
    Object* o = object(a.u32());
    const u32 start = a.u32(), len = a.u32(), buf = a.u32();
    if (!o || !o->data || u64(start) + len > o->length) return;
    std::memmove(g_mem + o->data + start * elem_size(T), g_mem + buf, size_t(len) * elem_size(T));
}

// ---- the rest, as typed HLE functions
u32 j_GetVersion(u32) { return 0x00010006; }
u32 j_DefineClass(u32, u32, u32, u32, u32) { return 0; }
u32 j_FindClass(u32, u32 name) {
    const char* n = mem::str(name);
    if (!n) return 0;
    if (trace()) std::fprintf(stderr, "jni: FindClass %s\n", n);
    return add_local(class_object(find_class(n)));
}
u32 j_FromReflected(u32, u32) { return 0; }
u32 j_ToReflected(u32, u32, u32, u32) { return 0; }
u32 j_GetSuperclass(u32, u32 clazz) {
    ClassDef* c = class_of_ref(clazz);
    return c && c->super ? add_local(class_object(c->super)) : 0;
}
bool derives(ClassDef* c, ClassDef* base) {
    for (; c; c = c->super)
        if (c == base) return true;
    return false;
}
u32 j_IsAssignableFrom(u32, u32 a, u32 b) { return derives(class_of_ref(a), class_of_ref(b)); }
int j_Throw(u32, u32 obj) {
    std::fprintf(stderr, "jni: guest throws %s\n", object_class(obj) ? object_class(obj)->name.c_str() : "?");
    return 0;
}
int j_ThrowNew(u32, u32 clazz, u32 msg) {
    ClassDef* c = class_of_ref(clazz);
    std::fprintf(stderr, "jni: guest throws %s: %s\n", c ? c->name.c_str() : "?", msg ? mem::str(msg) : "");
    return 0;
}
u32 j_Zero0(u32) { return 0; }
void j_Nop0(u32) {}
void j_FatalError(u32, u32 msg) {
    std::fprintf(stderr, "jni: FatalError: %s\n", mem::str(msg));
    std::abort();
}
thread_local std::vector<size_t> t_frames;
int j_PushLocalFrame(u32, int) {
    size_t mark;
    push_frame_mark(mark);
    t_frames.push_back(mark);
    return 0;
}
u32 j_PopLocalFrame(u32, u32 result) {
    if (t_frames.empty()) return result;
    const Ref keep = new_global(result);
    pop_frame_to(t_frames.back());
    t_frames.pop_back();
    const Ref r = add_local(keep);
    delete_global(keep);
    return r;
}
u32 j_NewGlobalRef(u32, u32 r) { return new_global(r); }
void j_DeleteGlobalRef(u32, u32 r) { delete_global(r); }
void j_DeleteLocalRef(u32, u32 r) { delete_local(r); }
int j_IsSameObject(u32, u32 a, u32 b) { return a == b; }
u32 j_NewLocalRef(u32, u32 r) { return add_local(r); }
int j_EnsureLocalCapacity(u32, int) { return 0; }
u32 j_AllocObject(u32, u32 clazz) {
    ClassDef* c = class_of_ref(clazz);
    return c ? new_object(c->name) : 0;
}
u32 j_GetObjectClass(u32, u32 obj) {
    ClassDef* c = object_class(obj);
    return c ? add_local(class_object(c)) : 0;
}
int j_IsInstanceOf(u32, u32 obj, u32 clazz) { return !obj || derives(object_class(obj), class_of_ref(clazz)); }

u32 method_id(u32 clazz, u32 name, u32 sig, bool is_static) {
    ClassDef* c = class_of_ref(clazz);
    if (!c || !name || !sig) return 0;
    MethodDef* m = find_method(c, mem::str(name), mem::str(sig), is_static);
    if (trace())
        std::fprintf(stderr, "jni: Get%sMethodID %s.%s%s%s\n", is_static ? "Static" : "", c->name.c_str(), mem::str(name),
                     mem::str(sig), m->fn ? "" : " (unimplemented)");
    return m->id;
}
u32 j_GetMethodID(u32, u32 clazz, u32 name, u32 sig) { return method_id(clazz, name, sig, false); }
u32 j_GetStaticMethodID(u32, u32 clazz, u32 name, u32 sig) { return method_id(clazz, name, sig, true); }
u32 field_id(u32 clazz, u32 name, u32 sig, bool is_static) {
    ClassDef* c = class_of_ref(clazz);
    if (!c || !name || !sig) return 0;
    return find_field(c, mem::str(name), mem::str(sig), is_static)->id;
}
u32 j_GetFieldID(u32, u32 clazz, u32 name, u32 sig) { return field_id(clazz, name, sig, false); }
u32 j_GetStaticFieldID(u32, u32 clazz, u32 name, u32 sig) { return field_id(clazz, name, sig, true); }

// ---- strings
Object* string_obj(u32 s) {
    Object* o = object(s);
    return o && o->is_string ? o : nullptr;
}
u32 j_NewString(u32, u32 chars, u32 len) {
    std::u16string s(len, u'\0');
    if (len) std::memcpy(s.data(), g_mem + chars, size_t(len) * 2);
    return new_string_utf16(s);
}
u32 j_GetStringLength(u32, u32 s) {
    std::lock_guard lk(lock());
    Object* o = string_obj(s);
    return o ? u32(o->str.size()) : 0;
}
u32 j_GetStringChars(u32, u32 s, u32 is_copy) {
    std::lock_guard lk(lock());
    Object* o = string_obj(s);
    const size_t n = o ? o->str.size() : 0;
    const u32 p = mem::alloc(u32(n * 2 + 2), 4);
    if (n) std::memcpy(g_mem + p, o->str.data(), n * 2);
    st16(p + u32(n * 2), 0);
    if (is_copy) g_mem[is_copy] = 1;
    return p;
}
void j_ReleaseChars(u32, u32, u32 chars) {
    if (chars) mem::free(chars);
}
u32 j_NewStringUTF(u32, u32 chars) { return chars ? new_string_utf16(utf8_to_utf16(mem::str(chars))) : 0; }
u32 j_GetStringUTFLength(u32, u32 s) {
    std::lock_guard lk(lock());
    Object* o = string_obj(s);
    return o ? u32(utf16_to_utf8(o->str).size()) : 0;
}
u32 j_GetStringUTFChars(u32, u32 s, u32 is_copy) {
    std::string u;
    {
        std::lock_guard lk(lock());
        if (Object* o = string_obj(s)) u = utf16_to_utf8(o->str);
    }
    if (is_copy) g_mem[is_copy] = 1;
    return mem::dup_string(u);
}
void j_GetStringRegion(u32, u32 s, u32 start, u32 len, u32 buf) {
    std::lock_guard lk(lock());
    Object* o = string_obj(s);
    if (!o || u64(start) + len > o->str.size()) return;
    std::memcpy(g_mem + buf, o->str.data() + start, size_t(len) * 2);
}
void j_GetStringUTFRegion(u32, u32 s, u32 start, u32 len, u32 buf) {
    std::lock_guard lk(lock());
    Object* o = string_obj(s);
    if (!o || u64(start) + len > o->str.size()) return;
    const std::string u = utf16_to_utf8(std::u16string_view(o->str).substr(start, len));
    std::memcpy(g_mem + buf, u.c_str(), u.size() + 1);
}

// ---- arrays
u32 j_GetArrayLength(u32, u32 a) { return array_length(a); }
u32 j_NewObjectArray(u32, u32 len, u32 clazz, u32 init) {
    ClassDef* c = class_of_ref(clazz);
    const Ref a = new_object_array(c ? c->name : "java/lang/Object", len);
    if (init)
        for (u32 i = 0; i < len; ++i) array_set(a, i, init);
    return a;
}
u32 j_GetObjectArrayElement(u32, u32 a, u32 i) { return array_get(a, i); }
void j_SetObjectArrayElement(u32, u32 a, u32 i, u32 v) { array_set(a, i, v); }

int j_RegisterNatives(u32, u32 clazz, u32 methods, int n) {
    ClassDef* c = class_of_ref(clazz);
    if (!c) return -1;
    std::lock_guard lk(lock());
    for (int i = 0; i < n; ++i) {
        const u32 e = methods + 12 * u32(i);
        c->natives[mem::str(ld32(e))] = ld32(e + 8);
        if (trace()) std::fprintf(stderr, "jni: RegisterNatives %s.%s%s\n", c->name.c_str(), mem::str(ld32(e)), mem::str(ld32(e + 4)));
    }
    return 0;
}
int j_UnregisterNatives(u32, u32 clazz) {
    if (ClassDef* c = class_of_ref(clazz)) {
        std::lock_guard lk(lock());
        c->natives.clear();
    }
    return 0;
}
std::recursive_mutex* monitor(u32 obj) {
    std::lock_guard lk(lock());
    Object* o = object(obj);
    if (!o) return nullptr;
    if (!o->monitor) o->monitor = std::make_unique<std::recursive_mutex>();
    return o->monitor.get();
}
int j_MonitorEnter(u32, u32 obj) {
    if (auto* m = monitor(obj)) m->lock();
    return 0;
}
int j_MonitorExit(u32, u32 obj) {
    if (auto* m = monitor(obj)) m->unlock();
    return 0;
}
int j_GetJavaVM(u32, u32 out) {
    st32(out, vm());
    return 0;
}
u32 j_GetPrimitiveArrayCritical(u32, u32 a, u32 is_copy) {
    if (is_copy) g_mem[is_copy] = 0;
    return array_data(a);
}
void j_ReleaseCritical(u32, u32, u32, int) {}
u32 j_NewWeakGlobalRef(u32, u32 r) { return new_global(r); }
u32 j_NewDirectByteBuffer(u32, u32 addr, int64_t cap) { return new_direct_buffer(addr, u32(cap)); }
u32 j_GetDirectBufferAddress(u32, u32 buf) {
    Object* o = object(buf);
    return o ? o->buffer : 0;
}
int64_t j_GetDirectBufferCapacity(u32, u32 buf) {
    Object* o = object(buf);
    return o ? o->capacity : -1;
}
int j_GetObjectRefType(u32, u32 r) { return r ? 1 : 0; }

// ---- JavaVM
int vm_Nop(u32) { return 0; }
int vm_AttachCurrentThread(u32, u32 penv, u32) {
    if (penv) st32(penv, env());
    return 0;
}
int vm_DetachCurrentThread(u32) {
    pop_frame_to(0);
    return 0;
}
int vm_GetEnv(u32, u32 penv, int) {
    if (penv) st32(penv, env());
    return 0;
}

void unimplemented_fn(CPU* c) {
    std::fprintf(stderr, "jni: unimplemented JNIEnv function (lr=0x%08x)\n", c->r[14]);
    ret(c, 0);
}

#define H(fn) RECOMP_HLE(fn)

struct Tables {
    u32 env = 0, vm = 0;
};

Tables build() {
    std::vector<hle::HostFn> f(233, unimplemented_fn);
    f[4] = H(j_GetVersion);
    f[5] = H(j_DefineClass);
    f[6] = H(j_FindClass);
    f[7] = H(j_FromReflected);
    f[8] = H(j_FromReflected);
    f[9] = H(j_ToReflected);
    f[10] = H(j_GetSuperclass);
    f[11] = H(j_IsAssignableFrom);
    f[12] = H(j_ToReflected);
    f[13] = H(j_Throw);
    f[14] = H(j_ThrowNew);
    f[15] = H(j_Zero0);  // ExceptionOccurred
    f[16] = H(j_Nop0);   // ExceptionDescribe
    f[17] = H(j_Nop0);   // ExceptionClear
    f[18] = H(j_FatalError);
    f[19] = H(j_PushLocalFrame);
    f[20] = H(j_PopLocalFrame);
    f[21] = H(j_NewGlobalRef);
    f[22] = H(j_DeleteGlobalRef);
    f[23] = H(j_DeleteLocalRef);
    f[24] = H(j_IsSameObject);
    f[25] = H(j_NewLocalRef);
    f[26] = H(j_EnsureLocalCapacity);
    f[27] = H(j_AllocObject);
    f[28] = new_object_impl<Var::Dots>;
    f[29] = new_object_impl<Var::V>;
    f[30] = new_object_impl<Var::A>;
    f[31] = H(j_GetObjectClass);
    f[32] = H(j_IsInstanceOf);
    f[33] = H(j_GetMethodID);
    // Call<Type>Method{,V,A} for Object, Boolean, Byte, Char, Short, Int, Long, Float, Double, Void.
    // The return value is passed in r0/r1 whatever the type, so one thunk serves all ten.
    for (int t = 0; t < 10; ++t) {
        f[34 + 3 * t] = call_method<Kind::Virtual, Var::Dots>;
        f[35 + 3 * t] = call_method<Kind::Virtual, Var::V>;
        f[36 + 3 * t] = call_method<Kind::Virtual, Var::A>;
        f[64 + 3 * t] = call_method<Kind::Nonvirtual, Var::Dots>;
        f[65 + 3 * t] = call_method<Kind::Nonvirtual, Var::V>;
        f[66 + 3 * t] = call_method<Kind::Nonvirtual, Var::A>;
        f[114 + 3 * t] = call_method<Kind::Static, Var::Dots>;
        f[115 + 3 * t] = call_method<Kind::Static, Var::V>;
        f[116 + 3 * t] = call_method<Kind::Static, Var::A>;
    }
    f[94] = H(j_GetFieldID);
    // Get/Set<Type>Field for Object, Boolean, Byte, Char, Short, Int, Long, Float, Double.
    const hle::HostFn getf[9] = {get_field_impl<'L'>, get_field_impl<'Z'>, get_field_impl<'B'>,
                                 get_field_impl<'C'>, get_field_impl<'S'>, get_field_impl<'I'>,
                                 get_field_impl<'J'>, get_field_impl<'F'>, get_field_impl<'D'>};
    const hle::HostFn setf[9] = {set_field_impl<'L'>, set_field_impl<'Z'>, set_field_impl<'B'>,
                                 set_field_impl<'C'>, set_field_impl<'S'>, set_field_impl<'I'>,
                                 set_field_impl<'J'>, set_field_impl<'F'>, set_field_impl<'D'>};
    const hle::HostFn gets[9] = {get_static_field_impl<'L'>, get_static_field_impl<'Z'>, get_static_field_impl<'B'>,
                                 get_static_field_impl<'C'>, get_static_field_impl<'S'>, get_static_field_impl<'I'>,
                                 get_static_field_impl<'J'>, get_static_field_impl<'F'>, get_static_field_impl<'D'>};
    const hle::HostFn sets[9] = {set_static_field_impl<'L'>, set_static_field_impl<'Z'>, set_static_field_impl<'B'>,
                                 set_static_field_impl<'C'>, set_static_field_impl<'S'>, set_static_field_impl<'I'>,
                                 set_static_field_impl<'J'>, set_static_field_impl<'F'>, set_static_field_impl<'D'>};
    for (int t = 0; t < 9; ++t) {
        f[95 + t] = getf[t];
        f[104 + t] = setf[t];
        f[145 + t] = gets[t];
        f[154 + t] = sets[t];
    }
    f[113] = H(j_GetStaticMethodID);
    f[144] = H(j_GetStaticFieldID);
    f[163] = H(j_NewString);
    f[164] = H(j_GetStringLength);
    f[165] = H(j_GetStringChars);
    f[166] = H(j_ReleaseChars);
    f[167] = H(j_NewStringUTF);
    f[168] = H(j_GetStringUTFLength);
    f[169] = H(j_GetStringUTFChars);
    f[170] = H(j_ReleaseChars);
    f[171] = H(j_GetArrayLength);
    f[172] = H(j_NewObjectArray);
    f[173] = H(j_GetObjectArrayElement);
    f[174] = H(j_SetObjectArrayElement);
    // New/Get/Release/GetRegion/SetRegion for Boolean, Byte, Char, Short, Int, Long, Float, Double.
    const hle::HostFn newa[8] = {new_array_impl<'Z'>, new_array_impl<'B'>, new_array_impl<'C'>, new_array_impl<'S'>,
                                 new_array_impl<'I'>, new_array_impl<'J'>, new_array_impl<'F'>, new_array_impl<'D'>};
    const hle::HostFn getr[8] = {get_array_region<'Z'>, get_array_region<'B'>, get_array_region<'C'>,
                                 get_array_region<'S'>, get_array_region<'I'>, get_array_region<'J'>,
                                 get_array_region<'F'>, get_array_region<'D'>};
    const hle::HostFn setr[8] = {set_array_region<'Z'>, set_array_region<'B'>, set_array_region<'C'>,
                                 set_array_region<'S'>, set_array_region<'I'>, set_array_region<'J'>,
                                 set_array_region<'F'>, set_array_region<'D'>};
    for (int t = 0; t < 8; ++t) {
        f[175 + t] = newa[t];
        f[183 + t] = get_array_elements;
        f[191 + t] = release_array_elements;
        f[199 + t] = getr[t];
        f[207 + t] = setr[t];
    }
    f[215] = H(j_RegisterNatives);
    f[216] = H(j_UnregisterNatives);
    f[217] = H(j_MonitorEnter);
    f[218] = H(j_MonitorExit);
    f[219] = H(j_GetJavaVM);
    f[220] = H(j_GetStringRegion);
    f[221] = H(j_GetStringUTFRegion);
    f[222] = H(j_GetPrimitiveArrayCritical);
    f[223] = H(j_ReleaseCritical);
    f[224] = H(j_GetStringChars);  // GetStringCritical
    f[225] = H(j_ReleaseChars);    // ReleaseStringCritical
    f[226] = H(j_NewWeakGlobalRef);
    f[227] = H(j_DeleteGlobalRef);  // DeleteWeakGlobalRef
    f[228] = H(j_Zero0);            // ExceptionCheck
    f[229] = H(j_NewDirectByteBuffer);
    f[230] = H(j_GetDirectBufferAddress);
    f[231] = H(j_GetDirectBufferCapacity);
    f[232] = H(j_GetObjectRefType);

    Tables t;
    const u32 env_table = mem::static_alloc(u32(f.size()) * 4, 4);
    for (size_t i = 0; i < f.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "JNIEnv[%zu]", i);
        st32(env_table + 4 * u32(i), i < 4 ? 0 : make_thunk(f[i], mem::str(mem::static_string(name))));
    }
    t.env = mem::static_alloc(4, 4);
    st32(t.env, env_table);

    const hle::HostFn v[8] = {nullptr, nullptr, nullptr, H(vm_Nop), H(vm_AttachCurrentThread),
                              H(vm_DetachCurrentThread), H(vm_GetEnv), H(vm_AttachCurrentThread)};
    const char* vnames[8] = {"", "", "", "DestroyJavaVM", "AttachCurrentThread", "DetachCurrentThread", "GetEnv",
                             "AttachCurrentThreadAsDaemon"};
    const u32 vm_table = mem::static_alloc(8 * 4, 4);
    for (int i = 0; i < 8; ++i) st32(vm_table + 4 * u32(i), v[i] ? make_thunk(v[i], vnames[i]) : 0);
    t.vm = mem::static_alloc(4, 4);
    st32(t.vm, vm_table);
    return t;
}

const Tables& tables() {
    static const Tables t = build();
    return t;
}

std::string mangle(std::string_view s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '/': out += '_'; break;
            case '_': out += "_1"; break;
            case ';': out += "_2"; break;
            case '[': out += "_3"; break;
            default: out += ch;
        }
    }
    return out;
}

}  // namespace

Ref new_direct_buffer(uint32_t addr, uint32_t capacity) {
    const Ref r = new_object("java/nio/DirectByteBuffer");
    Object* o = object(r);
    o->buffer = addr;
    o->capacity = capacity;
    return r;
}

uint32_t env() { return tables().env; }
uint32_t vm() { return tables().vm; }

uint32_t native_method(std::string_view cls, std::string_view name) {
    if (ClassDef* c = find_class(cls, false)) {
        std::lock_guard lk(lock());
        auto it = c->natives.find(std::string(name));
        if (it != c->natives.end()) return it->second;
    }
    return lib_symbol(("Java_" + mangle(cls) + "_" + mangle(name)).c_str());
}

}  // namespace recomp::jni
