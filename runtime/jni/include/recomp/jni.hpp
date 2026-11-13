// SPDX-License-Identifier: GPL-3.0-or-later
// Host-side Java environment for the game's JNI code.
//
// The game talks to its Java half (com.rovio.fusion.*) through JNI. There is
// no JVM here: the guest gets a JavaVM/JNIEnv whose function tables point at
// host code, Java objects live in a host handle table, and the Java methods the
// game calls are implemented in C++ and registered by class:
//
//     RECOMP_JAVA_METHOD("com/rovio/fusion/Globals", "getDeviceName", "()Ljava/lang/String;",
//                        [](jni::Call&) { return jni::ref(jni::new_string("Linux")); });
//
// Methods nobody implemented are logged once and return 0 / null, which is
// also how online services (ads, IAP, social, analytics) are disabled.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace recomp::jni {

using Ref = uint32_t;     // jobject as seen by the guest (0 = null)
using Value = uint64_t;   // jvalue bits: ints/refs in the low word, float as its bit pattern

struct ClassDef;

// Arguments of a Java method call, normalised from varargs / va_list / jvalue[].
struct Call {
    Ref self = 0;             // `this` (or the class object for static methods)
    ClassDef* cls = nullptr;  // class the method was looked up in
    std::vector<Value> args;

    int32_t i(size_t n) const { return int32_t(uint32_t(args.at(n))); }
    bool z(size_t n) const { return uint32_t(args.at(n)) != 0; }
    int64_t j(size_t n) const { return int64_t(args.at(n)); }
    float f(size_t n) const;
    double d(size_t n) const;
    Ref l(size_t n) const { return Ref(args.at(n)); }
};
using MethodFn = std::function<Value(Call&)>;

// Return value helpers.
inline Value ref(Ref r) { return r; }
inline Value boolean(bool b) { return b ? 1 : 0; }
inline Value integer(int32_t v) { return uint32_t(v); }
inline Value long_(int64_t v) { return uint64_t(v); }
Value float_(float v);
Value double_(double v);

// ---- class registry (case-sensitive JNI names, e.g. "java/lang/String")
ClassDef* find_class(std::string_view name, bool create = true);
void define_method(std::string_view cls, std::string_view name, std::string_view sig, MethodFn fn);
void define_static_field(std::string_view cls, std::string_view name, std::string_view sig, Value v);
void set_superclass(std::string_view cls, std::string_view super);
const std::string& class_name(const ClassDef* c);

// ---- objects
// Newly created references are local to the current native call (see
// NativeFrame) unless the guest turns them into global references.
Ref new_string(std::string_view utf8);
Ref new_string_utf16(std::u16string_view s);
std::string string_utf8(Ref s);     // "" for null
Ref new_object(std::string_view cls);
Ref class_object(ClassDef* c);
ClassDef* object_class(Ref r);
// Host state attached to an object (e.g. the C++ side of a wrapper class).
void set_host_data(Ref r, std::shared_ptr<void> data);
std::shared_ptr<void> host_data(Ref r);
Value get_field(Ref obj, std::string_view name);
// `sig` is the JNI type ("I", "Ljava/lang/String;", ...); references are retained.
void set_field(Ref obj, std::string_view name, std::string_view sig, Value v);

// Primitive arrays keep their elements in guest memory.
Ref new_array(char elem_type, uint32_t length);  // 'Z','B','C','S','I','J','F','D'
Ref new_object_array(std::string_view elem_class, uint32_t length);
// java.nio.ByteBuffer over guest memory (what NewDirectByteBuffer makes).
Ref new_direct_buffer(uint32_t addr, uint32_t capacity);
uint32_t array_length(Ref a);
uint32_t array_data(Ref a);  // guest address of primitive elements
Ref array_get(Ref a, uint32_t index);
void array_set(Ref a, uint32_t index, Ref v);

// Handlers return references they own: new_* results can be returned as they
// are, stored objects must be returned as new_local(stored).
Ref new_local(Ref r);
Ref new_global(Ref r);
void delete_global(Ref r);

// ---- calling into the game
// Guest JNIEnv* / JavaVM* values to pass to native methods.
uint32_t env();
uint32_t vm();
// Scope for local references created by host code around a native call.
class NativeFrame {
public:
    NativeFrame();
    ~NativeFrame();
    NativeFrame(const NativeFrame&) = delete;
    NativeFrame& operator=(const NativeFrame&) = delete;

private:
    size_t mark_;
};
// Native method registered with RegisterNatives, or the exported
// Java_<class>_<name> symbol. 0 if neither exists.
uint32_t native_method(std::string_view cls, std::string_view name);

// Static registration helpers.
struct MethodRegistrar {
    MethodRegistrar(const char* cls, const char* name, const char* sig, MethodFn fn) {
        define_method(cls, name, sig, std::move(fn));
    }
};
#define RECOMP_JNI_CONCAT2(a, b) a##b
#define RECOMP_JNI_CONCAT(a, b) RECOMP_JNI_CONCAT2(a, b)
#define RECOMP_JAVA_METHOD(cls, name, sig, fn) \
    static ::recomp::jni::MethodRegistrar RECOMP_JNI_CONCAT(recomp_jm_, __COUNTER__)(cls, name, sig, fn)

}  // namespace recomp::jni
