// SPDX-License-Identifier: GPL-3.0-or-later
#include "codec.hpp"

#include <zlib.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>

namespace slingshot::level_maker {

namespace {

constexpr uint8_t kVersion = 1;
constexpr size_t kMaxObjects = 4000;
constexpr size_t kMaxJoints = 8000;
constexpr size_t kMaxDefinitions = 1000;
constexpr size_t kMaxLimits = 16;
constexpr uint32_t kMaxUses = 10;
constexpr size_t kMaxText = 4 << 20;
constexpr double kMaxValue = 1e7;

// Optional values of objects and joints, by number in the binary form.
const char* const kObjectKeys[] = {
    "strength", "density", "defence", "scaleX", "scaleY", "filterGroup", "startNumber", "destroyedScoreInc",
    "color", "ov.z_order", "ov.destroyedScoreInc", "ov.scaleX", "ov.filterGroup", "ov.strength", "ov.scaleY",
    "ov.density", "ov.defence",
};
const char* const kJointKeys[] = {
    "collideConnected", "breakable", "breakForce", "upperLimit", "lowerLimit", "limit", "motor", "motorSpeed",
    "maxTorque", "backAndForth", "destroyTimer", "dampingRatio", "frequency", "worldAxisX", "worldAxisY",
};
constexpr int kColorKey = 8;

enum Kind : uint8_t { False = 0, True = 1, Int = 2, Float = 3 };

struct Extra {
    int key;
    Kind kind;
    double value;      // Int, Float
    uint8_t rgba[4];   // color
};

struct Object {
    std::string def;
    double x, y, angle, z;
    std::vector<Extra> extras;
};

struct Joint {
    int type, coords;
    size_t a, b;  // objects, from 1
    double p[4];
    std::vector<Extra> extras;
};

struct Level {
    std::string theme = "theme1";
    std::optional<double> gravity;
    std::optional<std::pair<double, double>> sling;
    std::optional<std::pair<uint32_t, uint32_t>> stars;
    std::vector<uint32_t> limits;
    std::string music;
    bool has_particles = false;
    double particles[3] = {};
    std::string particle_name;
    uint32_t particle_amount[2] = {};
    bool has_boom = false;
    double boom[4] = {};
    std::vector<Object> objects;
    std::vector<Joint> joints;
};

bool valid_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool number(const std::string& s, double& out) {
    if (s.empty() || s.size() > 40) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && std::isfinite(out) && std::fabs(out) <= kMaxValue;
}

bool count(const std::string& s, uint32_t& out) {
    double v;
    if (!number(s, v) || v < 0 || v != std::floor(v)) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

std::string format(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

// ---- text -> Level

template <size_t N>
int key_index(const char* const (&keys)[N], const std::string& name) {
    for (size_t i = 0; i < N; ++i)
        if (name == keys[i]) return int(i);
    return -1;
}

template <size_t N>
bool parse_extra(const char* const (&keys)[N], bool objects, const std::string& token, Extra& e) {
    const size_t eq = token.find('=');
    if (eq == std::string::npos) return false;
    e.key = key_index(keys, token.substr(0, eq));
    if (e.key < 0) return false;
    const std::string value = token.substr(eq + 1);
    if (objects && e.key == kColorKey) {
        unsigned v[4];
        char tail;
        if (std::sscanf(value.c_str(), "%u,%u,%u,%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) return false;
        for (int i = 0; i < 4; ++i) {
            if (v[i] > 255) return false;
            e.rgba[i] = uint8_t(v[i]);
        }
        e.kind = Int;
        return true;
    }
    if (value == "true" || value == "false") {
        e.kind = value == "true" ? True : False;
        return true;
    }
    if (!number(value, e.value)) return false;
    e.kind = (e.value == std::floor(e.value) && std::fabs(e.value) < 2e9) ? Int : Float;
    return true;
}

bool parse_text(const std::string& text, Level& level, std::string& error) {
    if (text.size() > kMaxText) return error = "too large", false;
    std::istringstream lines(text);
    std::string line;
    int n = 0;
    while (std::getline(lines, line)) {
        ++n;
        std::istringstream in(line);
        std::vector<std::string> t;
        for (std::string s; in >> s;) t.push_back(s);
        if (t.empty()) continue;
        const std::string& k = t[0];
        auto bad = [&] { error = "line " + std::to_string(n); return false; };
        double v[8];
        auto numbers = [&](size_t from, size_t count) {
            if (t.size() < from + count) return false;
            for (size_t i = 0; i < count; ++i)
                if (!number(t[from + i], v[i])) return false;
            return true;
        };
        if (k == "theme") {
            if (t.size() != 2 || !valid_name(t[1])) return bad();
            level.theme = t[1];
        } else if (k == "gravity") {
            if (t.size() != 2 || !numbers(1, 1)) return bad();
            level.gravity = v[0];
        } else if (k == "sling") {
            if (t.size() != 3 || !numbers(1, 2)) return bad();
            level.sling = {v[0], v[1]};
        } else if (k == "stars") {
            uint32_t a, b;
            if (t.size() != 3 || !count(t[1], a) || !count(t[2], b)) return bad();
            level.stars = {a, b};
        } else if (k == "limits") {
            if (t.size() > kMaxLimits + 1) return bad();
            level.limits.clear();
            for (size_t i = 1; i < t.size(); ++i) {
                uint32_t n;
                if (!count(t[i], n) || n > kMaxUses) return bad();
                level.limits.push_back(n);
            }
        } else if (k == "music") {
            if (t.size() != 2 || !valid_name(t[1])) return bad();
            level.music = t[1];
        } else if (k == "particles") {
            if ((t.size() != 4 && t.size() != 7) || !numbers(1, 3)) return bad();
            level.has_particles = true;
            for (int i = 0; i < 3; ++i) level.particles[i] = v[i];
            if (t.size() == 7) {
                if (!valid_name(t[4]) || !count(t[5], level.particle_amount[0]) || !count(t[6], level.particle_amount[1]))
                    return bad();
                level.particle_name = t[4];
            }
        } else if (k == "boom") {
            if (t.size() != 5 || !numbers(1, 4)) return bad();
            level.has_boom = true;
            for (int i = 0; i < 4; ++i) level.boom[i] = v[i];
        } else if (k == "o") {
            if (t.size() < 6 || !valid_name(t[1]) || !numbers(2, 4)) return bad();
            if (level.objects.size() >= kMaxObjects) return error = "too many objects", false;
            Object o{t[1], v[0], v[1], v[2], v[3], {}};
            for (size_t i = 6; i < t.size(); ++i) {
                Extra e{};
                if (!parse_extra(kObjectKeys, true, t[i], e) || o.extras.size() >= 32) return bad();
                o.extras.push_back(e);
            }
            level.objects.push_back(std::move(o));
        } else if (k == "j") {
            uint32_t type, a, b, coords;
            if (t.size() < 9 || !count(t[1], type) || !count(t[2], a) || !count(t[3], b) || !count(t[4], coords) ||
                !numbers(5, 4) || type > 255 || coords > 255)
                return bad();
            if (level.joints.size() >= kMaxJoints) return error = "too many joints", false;
            Joint j{int(type), int(coords), a, b, {v[0], v[1], v[2], v[3]}, {}};
            for (size_t i = 9; i < t.size(); ++i) {
                Extra e{};
                if (!parse_extra(kJointKeys, false, t[i], e) || j.extras.size() >= 32) return bad();
                j.extras.push_back(e);
            }
            level.joints.push_back(std::move(j));
        } else {
            return bad();
        }
    }
    for (const Joint& j : level.joints)
        if (j.a < 1 || j.b < 1 || j.a > level.objects.size() || j.b > level.objects.size())
            return error = "joint without its objects", false;
    return true;
}

// ---- Level -> text

template <size_t N>
void write_extras(const char* const (&keys)[N], bool objects, const std::vector<Extra>& extras, std::string& out) {
    for (const Extra& e : extras) {
        out += ' ';
        out += keys[e.key];
        out += '=';
        if (objects && e.key == kColorKey) {
            out += std::to_string(e.rgba[0]) + ',' + std::to_string(e.rgba[1]) + ',' + std::to_string(e.rgba[2]) + ',' +
                   std::to_string(e.rgba[3]);
        } else if (e.kind == True || e.kind == False) {
            out += e.kind == True ? "true" : "false";
        } else {
            out += format(e.value);
        }
    }
}

std::string write_text(const Level& level) {
    std::string out = "theme " + level.theme + "\n";
    if (level.gravity) out += "gravity " + format(*level.gravity) + "\n";
    if (level.sling) out += "sling " + format(level.sling->first) + " " + format(level.sling->second) + "\n";
    if (level.stars) out += "stars " + std::to_string(level.stars->first) + " " + std::to_string(level.stars->second) + "\n";
    if (!level.limits.empty()) {
        out += "limits";
        for (uint32_t n : level.limits) out += " " + std::to_string(n);
        out += "\n";
    }
    if (!level.music.empty()) out += "music " + level.music + "\n";
    if (level.has_particles) {
        out += "particles " + format(level.particles[0]) + " " + format(level.particles[1]) + " " + format(level.particles[2]);
        if (!level.particle_name.empty())
            out += " " + level.particle_name + " " + std::to_string(level.particle_amount[0]) + " " +
                   std::to_string(level.particle_amount[1]);
        out += "\n";
    }
    if (level.has_boom)
        out += "boom " + format(level.boom[0]) + " " + format(level.boom[1]) + " " + format(level.boom[2]) + " " +
               format(level.boom[3]) + "\n";
    for (const Object& o : level.objects) {
        out += "o " + o.def + " " + format(o.x) + " " + format(o.y) + " " + format(o.angle) + " " + format(o.z);
        write_extras(kObjectKeys, true, o.extras, out);
        out += "\n";
    }
    for (const Joint& j : level.joints) {
        out += "j " + std::to_string(j.type) + " " + std::to_string(j.a) + " " + std::to_string(j.b) + " " +
               std::to_string(j.coords);
        for (double p : j.p) out += " " + format(p);
        write_extras(kJointKeys, false, j.extras, out);
        out += "\n";
    }
    return out;
}

// ---- binary

class Writer {
public:
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void varint(uint64_t v) {
        while (v >= 0x80) {
            u8(uint8_t(v | 0x80));
            v >>= 7;
        }
        u8(uint8_t(v));
    }
    void zigzag(int64_t v) { varint((uint64_t(v) << 1) ^ uint64_t(v >> 63)); }
    void f32(double v) {
        const float f = float(v);
        uint32_t u;
        std::memcpy(&u, &f, 4);
        for (int i = 0; i < 4; ++i) u8(uint8_t(u >> (8 * i)));
    }
    void str(const std::string& s) {
        varint(s.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
};

class Reader {
public:
    Reader(const std::vector<uint8_t>& b) : b_(b) {}
    bool ok = true;
    bool done() const { return pos_ >= b_.size(); }
    uint8_t u8() {
        if (pos_ >= b_.size()) return ok = false, 0;
        return b_[pos_++];
    }
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const uint8_t c = u8();
            v |= uint64_t(c & 0x7f) << shift;
            if (!(c & 0x80)) return v;
        }
        return ok = false, 0;
    }
    int64_t zigzag() {
        const uint64_t v = varint();
        return int64_t(v >> 1) ^ -int64_t(v & 1);
    }
    double f32() {
        uint32_t u = 0;
        for (int i = 0; i < 4; ++i) u |= uint32_t(u8()) << (8 * i);
        float f;
        std::memcpy(&f, &u, 4);
        if (!std::isfinite(f) || std::fabs(f) > kMaxValue) ok = false;
        return f;
    }
    std::string str() {
        const uint64_t n = varint();
        if (n > 64 || pos_ + n > b_.size()) return ok = false, std::string();
        std::string s(b_.begin() + pos_, b_.begin() + pos_ + n);
        pos_ += n;
        if (!valid_name(s)) ok = false;
        return s;
    }

private:
    const std::vector<uint8_t>& b_;
    size_t pos_ = 0;
};

constexpr double kPi = 3.14159265358979323846;
int64_t fixed(double v, double scale) { return std::llround(v * scale); }
uint32_t centidegrees(double radians) {
    int64_t c = std::llround(radians * 18000.0 / kPi) % 36000;
    if (c < 0) c += 36000;
    return uint32_t(c);
}

void write_extra_values(const std::vector<Extra>& extras, bool objects, Writer& w) {
    w.u8(uint8_t(extras.size()));
    for (const Extra& e : extras) {
        w.u8(uint8_t(e.key | (e.kind << 6)));
        if (objects && e.key == kColorKey) {
            for (uint8_t c : e.rgba) w.u8(c);
        } else if (e.kind == Int) {
            w.zigzag(int64_t(e.value));
        } else if (e.kind == Float) {
            w.f32(e.value);
        }
    }
}

bool read_extra_values(std::vector<Extra>& extras, bool objects, Reader& r) {
    const size_t limit = objects ? std::size(kObjectKeys) : std::size(kJointKeys);
    const uint8_t n = r.u8();
    if (n > 32) return false;
    for (int i = 0; i < n && r.ok; ++i) {
        const uint8_t tag = r.u8();
        Extra e{};
        e.key = tag & 63;
        e.kind = Kind(tag >> 6);
        if (size_t(e.key) >= limit) return false;
        if (objects && e.key == kColorKey) {
            if (e.kind != Int) return false;
            for (uint8_t& c : e.rgba) c = r.u8();
        } else if (e.kind == Int) {
            const int64_t v = r.zigzag();
            if (v > int64_t(kMaxValue) || v < -int64_t(kMaxValue)) return false;
            e.value = double(v);
        } else if (e.kind == Float) {
            e.value = r.f32();
        }
        extras.push_back(e);
    }
    return r.ok;
}

std::vector<uint8_t> write_binary(const Level& level) {
    Writer w;
    w.u8('L');
    w.u8('M');
    w.u8(kVersion);
    w.u8('T');
    w.str(level.theme);
    if (level.gravity) {
        w.u8('G');
        w.f32(*level.gravity);
    }
    if (level.sling) {
        w.u8('S');
        w.zigzag(fixed(level.sling->first, 100));
        w.zigzag(fixed(level.sling->second, 100));
    }
    if (level.stars) {
        w.u8('R');
        w.varint(level.stars->first);
        w.varint(level.stars->second);
    }
    if (!level.limits.empty()) {
        w.u8('U');
        w.varint(level.limits.size());
        for (uint32_t n : level.limits) w.u8(uint8_t(n));
    }
    if (!level.music.empty()) {
        w.u8('M');
        w.str(level.music);
    }
    if (level.has_particles) {
        w.u8('Q');
        for (double v : level.particles) w.f32(v);
        w.u8(level.particle_name.empty() ? 0 : 1);
        if (!level.particle_name.empty()) {
            w.str(level.particle_name);
            w.varint(level.particle_amount[0]);
            w.varint(level.particle_amount[1]);
        }
    }
    if (level.has_boom) {
        w.u8('B');
        for (double v : level.boom) w.f32(v);
    }
    if (!level.objects.empty()) {
        w.u8('O');
        std::map<std::string, size_t> index;
        std::vector<std::string> defs;
        for (const Object& o : level.objects)
            if (index.emplace(o.def, defs.size()).second) defs.push_back(o.def);
        w.varint(defs.size());
        for (const std::string& d : defs) w.str(d);
        w.varint(level.objects.size());
        int64_t px = 0, py = 0;
        for (const Object& o : level.objects) {
            const int64_t x = fixed(o.x, 1000), y = fixed(o.y, 1000);
            w.varint(index[o.def]);
            w.zigzag(x - px);
            w.zigzag(y - py);
            px = x;
            py = y;
            w.varint(centidegrees(o.angle));
            w.zigzag(fixed(o.z, 1));
            write_extra_values(o.extras, true, w);
        }
    }
    if (!level.joints.empty()) {
        w.u8('J');
        w.varint(level.joints.size());
        for (const Joint& j : level.joints) {
            w.u8(uint8_t(j.type));
            w.varint(j.a);
            w.varint(j.b);
            w.u8(uint8_t(j.coords));
            for (double p : j.p) w.zigzag(fixed(p, 1000));
            write_extra_values(j.extras, false, w);
        }
    }
    return w.bytes;
}

bool read_binary(const std::vector<uint8_t>& bytes, Level& level, std::string& error) {
    Reader r(bytes);
    if (r.u8() != 'L' || r.u8() != 'M') return error = "not a level", false;
    if (r.u8() != kVersion) return error = "newer version", false;
    auto bound = [](int64_t v, double scale) { return std::fabs(double(v) / scale) <= kMaxValue; };
    while (r.ok && !r.done()) {
        const uint8_t tag = r.u8();
        switch (tag) {
            case 'T': level.theme = r.str(); break;
            case 'G': level.gravity = r.f32(); break;
            case 'S': {
                const int64_t x = r.zigzag(), y = r.zigzag();
                if (!bound(x, 100) || !bound(y, 100)) return error = "bad value", false;
                level.sling = {double(x) / 100, double(y) / 100};
                break;
            }
            case 'R': {
                const uint64_t a = r.varint(), b = r.varint();
                if (a > uint64_t(kMaxValue) || b > uint64_t(kMaxValue)) return error = "bad value", false;
                level.stars = {uint32_t(a), uint32_t(b)};
                break;
            }
            case 'U': {
                const uint64_t n = r.varint();
                if (n > kMaxLimits) return error = "bad value", false;
                level.limits.clear();
                for (uint64_t i = 0; i < n; ++i) {
                    const uint8_t v = r.u8();
                    if (v > kMaxUses) return error = "bad value", false;
                    level.limits.push_back(v);
                }
                break;
            }
            case 'M':
                level.music = r.str();
                if (!valid_name(level.music)) return error = "bad value", false;
                break;
            case 'Q':
                level.has_particles = true;
                for (double& v : level.particles) v = r.f32();
                if (r.u8()) {
                    level.particle_name = r.str();
                    for (uint32_t& a : level.particle_amount) {
                        const uint64_t v = r.varint();
                        if (v > uint64_t(kMaxValue)) return error = "bad value", false;
                        a = uint32_t(v);
                    }
                }
                break;
            case 'B':
                level.has_boom = true;
                for (double& v : level.boom) v = r.f32();
                break;
            case 'O': {
                const uint64_t ndefs = r.varint();
                if (ndefs > kMaxDefinitions) return error = "too many definitions", false;
                std::vector<std::string> defs;
                for (uint64_t i = 0; i < ndefs && r.ok; ++i) defs.push_back(r.str());
                const uint64_t n = r.varint();
                if (n > kMaxObjects) return error = "too many objects", false;
                int64_t x = 0, y = 0;
                for (uint64_t i = 0; i < n && r.ok; ++i) {
                    Object o;
                    const uint64_t d = r.varint();
                    if (d >= defs.size()) return error = "bad definition", false;
                    o.def = defs[d];
                    x += r.zigzag();
                    y += r.zigzag();
                    if (!bound(x, 1000) || !bound(y, 1000)) return error = "bad value", false;
                    o.x = double(x) / 1000;
                    o.y = double(y) / 1000;
                    const uint64_t a = r.varint();
                    if (a >= 36000) return error = "bad angle", false;
                    o.angle = double(a) * kPi / 18000.0;
                    const int64_t z = r.zigzag();
                    if (!bound(z, 1)) return error = "bad value", false;
                    o.z = double(z);
                    if (!read_extra_values(o.extras, true, r)) return error = "bad object", false;
                    level.objects.push_back(std::move(o));
                }
                break;
            }
            case 'J': {
                const uint64_t n = r.varint();
                if (n > kMaxJoints) return error = "too many joints", false;
                for (uint64_t i = 0; i < n && r.ok; ++i) {
                    Joint j{};
                    j.type = r.u8();
                    j.a = r.varint();
                    j.b = r.varint();
                    j.coords = r.u8();
                    for (double& p : j.p) {
                        const int64_t v = r.zigzag();
                        if (!bound(v, 1000)) return error = "bad value", false;
                        p = double(v) / 1000;
                    }
                    if (!read_extra_values(j.extras, false, r)) return error = "bad joint", false;
                    level.joints.push_back(std::move(j));
                }
                break;
            }
            default: return error = "unknown part", false;
        }
    }
    if (!r.ok) return error = "damaged", false;
    for (const Joint& j : level.joints)
        if (j.a < 1 || j.b < 1 || j.a > level.objects.size() || j.b > level.objects.size())
            return error = "joint without its objects", false;
    return true;
}

// ---- deflate, base64url

std::vector<uint8_t> deflate_raw(const std::vector<uint8_t>& in) {
    z_stream s{};
    deflateInit2(&s, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(deflateBound(&s, uLong(in.size())));
    s.next_in = const_cast<Bytef*>(in.data());
    s.avail_in = uInt(in.size());
    s.next_out = out.data();
    s.avail_out = uInt(out.size());
    deflate(&s, Z_FINISH);
    out.resize(s.total_out);
    deflateEnd(&s);
    return out;
}

bool inflate_raw(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    z_stream s{};
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = const_cast<Bytef*>(in.data());
    s.avail_in = uInt(in.size());
    uint8_t buf[16384];
    int ret;
    do {
        s.next_out = buf;
        s.avail_out = sizeof buf;
        ret = inflate(&s, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
        out.insert(out.end(), buf, buf + (sizeof buf - s.avail_out));
        if (out.size() > kMaxText) break;
    } while (ret != Z_STREAM_END);
    inflateEnd(&s);
    return ret == Z_STREAM_END && s.avail_in == 0;
}

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64(const std::vector<uint8_t>& in) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : in) {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += kBase64[(acc >> bits) & 63];
        }
    }
    if (bits) out += kBase64[(acc << (6 - bits)) & 63];
    return out;
}

bool unbase64(const std::string& in, std::vector<uint8_t>& out) {
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        const char* p = c ? std::strchr(kBase64, c) : nullptr;
        if (!p) return false;
        acc = (acc << 6) | uint32_t(p - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
    }
    return true;
}

}  // namespace

std::optional<std::string> encode(const std::string& text, std::string& error) {
    Level level;
    if (!parse_text(text, level, error)) return std::nullopt;
    return base64(deflate_raw(write_binary(level)));
}

std::optional<std::string> decode(const std::string& code, std::string& error) {
    std::vector<uint8_t> packed, bytes;
    if (code.size() > kMaxText || !unbase64(code, packed)) return error = "not a level code", std::nullopt;
    if (!inflate_raw(packed, bytes)) return error = "not a level code", std::nullopt;
    Level level;
    if (!read_binary(bytes, level, error)) return std::nullopt;
    return write_text(level);
}

}  // namespace slingshot::level_maker
