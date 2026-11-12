// SPDX-License-Identifier: GPL-3.0-or-later
// Object model shared by the JNI modules.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "recomp/jni.hpp"

namespace recomp::jni {

struct MethodDef {
    ClassDef* owner;
    std::string name, sig;
    bool is_static = false;
    MethodFn fn;  // empty: not implemented
    uint32_t id;
};

struct FieldDef {
    ClassDef* owner;
    std::string name, sig;
    bool is_static = false;
    Value static_value = 0;
    uint32_t id;
};

struct ClassDef {
    std::string name;
    ClassDef* super = nullptr;
    std::map<std::string, MethodDef*> methods;  // key: name + sig
    std::map<std::string, FieldDef*> fields;    // key: name
    std::map<std::string, uint32_t> natives;    // RegisterNatives: name -> guest function
    Ref class_ref = 0;                          // the java.lang.Class object (global)
};

struct Object {
    ClassDef* cls = nullptr;
    ClassDef* class_value = nullptr;  // set for java.lang.Class objects
    // java.lang.String
    bool is_string = false;
    std::u16string str;
    // arrays
    char elem = 0;                    // 0: not an array, 'L': object array
    uint32_t length = 0;
    uint32_t data = 0;                // guest memory for primitive elements
    std::vector<Ref> elems;           // object arrays
    // instances
    std::unordered_map<uint32_t, Value> fields;  // FieldDef::id -> value
    std::shared_ptr<void> host;
    // direct byte buffers
    uint32_t buffer = 0, capacity = 0;
    std::unique_ptr<std::recursive_mutex> monitor;

    ~Object();
};

// All JNI state is guarded by one recursive lock: the game's Java traffic is
// light and this keeps reference counting simple.
std::recursive_mutex& lock();
Object* object(Ref r);  // nullptr for null / stale references
Ref add_object(std::unique_ptr<Object> o);  // returns a new local reference
Ref add_local(Ref r);                       // another local reference to r
void delete_local(Ref r);

MethodDef* find_method(ClassDef* c, const std::string& name, const std::string& sig, bool is_static);
MethodDef* method_by_id(uint32_t id);
FieldDef* find_field(ClassDef* c, const std::string& name, const std::string& sig, bool is_static);
FieldDef* field_by_id(uint32_t id);
// Calls a method (unimplemented methods log once and return 0).
Value invoke(MethodDef* m, Call& call);

std::u16string utf8_to_utf16(std::string_view s);
std::string utf16_to_utf8(std::u16string_view s);  // Java "modified UTF-8"

void push_frame_mark(size_t& mark);
void pop_frame_to(size_t mark);

}  // namespace recomp::jni
