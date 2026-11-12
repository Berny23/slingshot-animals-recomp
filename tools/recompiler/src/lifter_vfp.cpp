// SPDX-License-Identifier: GPL-3.0-or-later
// VFP (scalar floating point) and a small NEON subset.
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "lifter.hpp"

namespace recomp {
namespace {

const cs_arm_op* op(const Insn& in, unsigned i) { return i < in.arm.op_count ? &in.arm.operands[i] : nullptr; }
bool is_reg(const cs_arm_op* o) { return o && o->type == ARM_OP_REG; }
uint32_t pc_of(const Insn& in) { return in.addr + (in.mode == Mode::Thumb ? 4 : 8); }
bool is_sreg(int r) { return r >= ARM_REG_S0 && r <= ARM_REG_S31; }
bool is_dreg(int r) { return r >= ARM_REG_D0 && r <= ARM_REG_D31; }
bool is_qreg(int r) { return r >= ARM_REG_Q0 && r <= ARM_REG_Q15; }
unsigned sidx(int r) { return unsigned(r - ARM_REG_S0); }
unsigned didx(int r) { return unsigned(r - ARM_REG_D0); }
unsigned qidx(int r) { return unsigned(r - ARM_REG_Q0); }

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// Float-valued expression of an S or D register.
std::string fval(int r) {
    if (is_sreg(r)) return fmt("recomp::f32(recomp::s_get(ctx, %u))", sidx(r));
    return fmt("recomp::f64(ctx->d[%u])", didx(r));
}
// Statement writing a float-valued expression to an S or D register.
std::string fset(int r, const std::string& e) {
    if (is_sreg(r)) return fmt("recomp::s_set(ctx, %u, recomp::b32(%s)); ", sidx(r), e.c_str());
    return fmt("ctx->d[%u] = recomp::b64(%s); ", didx(r), e.c_str());
}
// Raw bits.
std::string bits(int r) {
    if (is_sreg(r)) return fmt("recomp::s_get(ctx, %u)", sidx(r));
    return fmt("ctx->d[%u]", didx(r));
}
std::string bset(int r, const std::string& e) {
    if (is_sreg(r)) return fmt("recomp::s_set(ctx, %u, %s); ", sidx(r), e.c_str());
    return fmt("ctx->d[%u] = %s; ", didx(r), e.c_str());
}

// D-register index range [first, first+count) covered by a D or Q operand.
bool dspan(int r, unsigned& first, unsigned& count) {
    if (is_dreg(r)) { first = didx(r); count = 1; return true; }
    if (is_qreg(r)) { first = 2 * qidx(r); count = 2; return true; }
    return false;
}

bool all_scalar(const Insn& in, bool& dbl) {
    // Scalar VFP: all FP register operands are S, or all are D with an F64 data type.
    bool any_s = false, any_d = false;
    for (unsigned i = 0; i < in.arm.op_count; ++i) {
        const cs_arm_op* o = op(in, i);
        if (o->type != ARM_OP_REG) continue;
        if (is_sreg(o->reg)) any_s = true;
        else if (is_dreg(o->reg)) any_d = true;
        else if (is_qreg(o->reg)) return false;
    }
    if (any_s && !any_d) { dbl = false; return true; }
    if (any_d && !any_s && in.arm.vector_data == ARM_VECTORDATA_F64) { dbl = true; return true; }
    return false;
}

}  // namespace

bool Lifter::lift_vfp_load_store(const Insn& in, LiftResult& out) {
    const cs_arm_op* rt = op(in, 0);
    const cs_arm_op* m = op(in, 1);
    if (!is_reg(rt) || !m || m->type != ARM_OP_MEM || m->mem.index != ARM_REG_INVALID) return false;
    const bool load = in.id == ARM_INS_VLDR;
    const bool dbl = is_dreg(rt->reg);
    if (!dbl && !is_sreg(rt->reg)) return false;
    const unsigned size = dbl ? 8 : 4;
    if (load && gpr(m->mem.base) == 15) {
        uint64_t v;
        const uint32_t ea = (pc_of(in) & ~3u) + uint32_t(m->mem.disp);
        if (literal(ea, size, v)) {
            out.code += bset(rt->reg, dbl ? fmt("0x%llxull", (unsigned long long)v) : hex(uint32_t(v)));
            return true;
        }
    }
    const std::string ea = fmt("%s + %s", read(in, m->mem.base, true).c_str(), hex(uint32_t(m->mem.disp)).c_str());
    if (load) out.code += bset(rt->reg, fmt("recomp::%s(%s)", dbl ? "ld64" : "ld32", ea.c_str()));
    else out.code += fmt("recomp::%s(%s, %s); ", dbl ? "st64" : "st32", ea.c_str(), bits(rt->reg).c_str());
    return true;
}

bool Lifter::lift_vfp_multiple(const Insn& in, LiftResult& out) {
    bool load, wb = in.arm.writeback, db = false;
    unsigned first = 1;
    int base = -1;
    switch (in.id) {
        case ARM_INS_VPUSH: load = false; db = true; wb = true; first = 0; base = 13; break;
        case ARM_INS_VPOP: load = true; wb = true; first = 0; base = 13; break;
        case ARM_INS_VLDMIA: case ARM_INS_FLDMIAX: load = true; break;
        case ARM_INS_VLDMDB: load = true; db = true; break;
        case ARM_INS_VSTMIA: case ARM_INS_FSTMIAX: load = false; break;
        case ARM_INS_VSTMDB: load = false; db = true; break;
        default: return false;
    }
    if (first == 1) {
        if (!is_reg(op(in, 0))) return false;
        base = gpr(op(in, 0)->reg);
        if (base < 0) return false;
    }
    std::vector<int> regs;
    unsigned bytes = 0;
    for (unsigned i = first; i < in.arm.op_count; ++i) {
        const cs_arm_op* o = op(in, i);
        if (!is_reg(o) || !(is_sreg(o->reg) || is_dreg(o->reg))) return false;
        regs.push_back(o->reg);
        bytes += is_dreg(o->reg) ? 8 : 4;
    }
    if (in.id == ARM_INS_FLDMIAX || in.id == ARM_INS_FSTMIAX) bytes += 4;  // FLDMX/FSTMX: extra word
    std::string c = fmt("{ const uint32_t b = ctx->r[%d]%s; ", base, db ? fmt(" - 0x%xu", bytes).c_str() : "");
    unsigned off = 0;
    for (int r : regs) {
        const bool d = is_dreg(r);
        if (load) c += bset(r, fmt("recomp::%s(b + 0x%xu)", d ? "ld64" : "ld32", off));
        else c += fmt("recomp::%s(b + 0x%xu, %s); ", d ? "st64" : "st32", off, bits(r).c_str());
        off += d ? 8 : 4;
    }
    if (wb) c += db ? fmt("ctx->r[%d] = b; ", base) : fmt("ctx->r[%d] = b + 0x%xu; ", base, bytes);
    out.code += c + "} ";
    return true;
}

bool Lifter::lift_vfp(const Insn& in, LiftResult& out) {
    const unsigned n = in.arm.op_count;
    auto R = [&](unsigned i) { return op(in, i)->reg; };
    bool dbl = false;

    switch (in.id) {
        case ARM_INS_VLDR: case ARM_INS_VSTR:
            return lift_vfp_load_store(in, out);
        case ARM_INS_VPUSH: case ARM_INS_VPOP: case ARM_INS_VLDMIA: case ARM_INS_VLDMDB:
        case ARM_INS_VSTMIA: case ARM_INS_VSTMDB: case ARM_INS_FLDMIAX: case ARM_INS_FSTMIAX:
            return lift_vfp_multiple(in, out);
        case ARM_INS_FMSTAT:
            out.code += "recomp::vmrs_apsr(ctx); ";
            return true;
        case ARM_INS_VMRS:
            if (n == 2 && R(0) == ARM_REG_APSR_NZCV) { out.code += "recomp::vmrs_apsr(ctx); "; return true; }
            if (n == 2 && gpr(R(0)) >= 0 && R(1) == ARM_REG_FPSCR) {
                out.code += fmt("%s = (uint32_t(ctx->fp_n) << 31) | (uint32_t(ctx->fp_z) << 30) | (uint32_t(ctx->fp_c) << 29) | "
                                "(uint32_t(ctx->fp_v) << 28) | (ctx->fpscr & 0x0fffffffu); ", reg_lv(R(0)).c_str());
                return true;
            }
            return false;
        case ARM_INS_VMSR:
            if (n == 2 && R(0) == ARM_REG_FPSCR && gpr(R(1)) >= 0) {
                out.code += fmt("{ const uint32_t v = %s; ctx->fpscr = v & 0x0fffffffu; ctx->fp_n = v >> 31; ctx->fp_z = (v >> 30) & 1; "
                                "ctx->fp_c = (v >> 29) & 1; ctx->fp_v = (v >> 28) & 1; } ", read(in, R(1)).c_str());
                return true;
            }
            return false;
        case ARM_INS_FCONSTS: case ARM_INS_FCONSTD:
            if (n == 2 && op(in, 1)->type == ARM_OP_FP) {
                const double v = op(in, 1)->fp;
                if (is_sreg(R(0))) out.code += bset(R(0), hex(std::bit_cast<uint32_t>(float(v))));
                else out.code += bset(R(0), fmt("0x%llxull", (unsigned long long)std::bit_cast<uint64_t>(v)));
                return true;
            }
            return false;
        case ARM_INS_FMDHR: case ARM_INS_FMDLR:
            // vmov.32 dN[1|0], rT
            if (n == 2 && is_dreg(R(0)) && gpr(R(1)) >= 0) {
                const unsigned d = didx(R(0));
                out.code += in.id == ARM_INS_FMDHR
                    ? fmt("ctx->d[%u] = (ctx->d[%u] & 0xffffffffull) | (uint64_t(%s) << 32); ", d, d, read(in, R(1)).c_str())
                    : fmt("ctx->d[%u] = (ctx->d[%u] & ~0xffffffffull) | %s; ", d, d, read(in, R(1)).c_str());
                return true;
            }
            return false;
        case ARM_INS_VMOV: {
            const cs_arm_op* o0 = op(in, 0);
            const cs_arm_op* o1 = op(in, 1);
            if (n == 2 && is_reg(o0) && o1->type == ARM_OP_FP) {
                const double v = o1->fp;
                if (is_sreg(o0->reg)) { out.code += bset(o0->reg, hex(std::bit_cast<uint32_t>(float(v)))); return true; }
                if (is_dreg(o0->reg) && in.arm.vector_data == ARM_VECTORDATA_F64) {
                    out.code += bset(o0->reg, fmt("0x%llxull", (unsigned long long)std::bit_cast<uint64_t>(v)));
                    return true;
                }
                return false;  // NEON vmov.f32 dN, #imm
            }
            if (n == 2 && is_reg(o0) && is_reg(o1)) {
                const int a = o0->reg, b = o1->reg;
                // Scalar lane moves (vmov.32 rT, dN[x] / vmov.32 dN[x], rT).
                if (gpr(a) >= 0 && is_dreg(b) && o1->vector_index >= 0) {
                    const int lane = o1->vector_index, sz = in.arm.vector_size ? in.arm.vector_size : 32;
                    std::string e = fmt("(ctx->d[%u] >> %d)", didx(b), lane * sz);
                    const char* t = sz == 32 ? "uint32_t" : sz == 16 ? "uint16_t" : "uint8_t";
                    const bool sgn = in.arm.vector_data == ARM_VECTORDATA_S8 || in.arm.vector_data == ARM_VECTORDATA_S16;
                    e = sgn ? fmt("uint32_t(int32_t(%s(%s(%s))))", sz == 16 ? "int16_t" : "int8_t", t, e.c_str())
                            : fmt("uint32_t(%s(%s))", t, e.c_str());
                    out.code += fmt("%s = %s; ", reg_lv(a).c_str(), e.c_str());
                    return true;
                }
                if (is_dreg(a) && o0->vector_index >= 0 && gpr(b) >= 0) {
                    const int lane = o0->vector_index, sz = in.arm.vector_size ? in.arm.vector_size : 32;
                    const uint64_t mask = (sz == 32 ? 0xffffffffull : sz == 16 ? 0xffffull : 0xffull) << (lane * sz);
                    out.code += fmt("ctx->d[%u] = (ctx->d[%u] & 0x%llxull) | ((uint64_t(%s) << %d) & 0x%llxull); ", didx(a), didx(a),
                                    (unsigned long long)~mask, read(in, b).c_str(), lane * sz, (unsigned long long)mask);
                    return true;
                }
                if (is_sreg(a) && gpr(b) >= 0) { out.code += bset(a, read(in, b)); return true; }
                if (gpr(a) >= 0 && is_sreg(b)) { out.code += fmt("%s = %s; ", reg_lv(a).c_str(), bits(b).c_str()); return true; }
                if ((is_sreg(a) && is_sreg(b)) || (is_dreg(a) && is_dreg(b))) { out.code += bset(a, bits(b)); return true; }
                if (is_qreg(a) && is_qreg(b)) {
                    out.code += fmt("ctx->d[%u] = ctx->d[%u]; ctx->d[%u] = ctx->d[%u]; ", 2 * qidx(a), 2 * qidx(b), 2 * qidx(a) + 1,
                                    2 * qidx(b) + 1);
                    return true;
                }
                return false;
            }
            if (n == 3 && gpr(R(0)) >= 0 && gpr(R(1)) >= 0 && is_dreg(R(2))) {
                out.code += fmt("{ const uint64_t v = ctx->d[%u]; %s = uint32_t(v); %s = uint32_t(v >> 32); } ", didx(R(2)),
                                reg_lv(R(0)).c_str(), reg_lv(R(1)).c_str());
                return true;
            }
            if (n == 3 && is_dreg(R(0)) && gpr(R(1)) >= 0 && gpr(R(2)) >= 0) {
                out.code += fmt("ctx->d[%u] = (uint64_t(%s) << 32) | %s; ", didx(R(0)), read(in, R(2)).c_str(), read(in, R(1)).c_str());
                return true;
            }
            if (n == 4 && gpr(R(0)) >= 0 && gpr(R(1)) >= 0 && is_sreg(R(2)) && is_sreg(R(3))) {
                out.code += fmt("%s = %s; %s = %s; ", reg_lv(R(0)).c_str(), bits(R(2)).c_str(), reg_lv(R(1)).c_str(), bits(R(3)).c_str());
                return true;
            }
            if (n == 4 && is_sreg(R(0)) && is_sreg(R(1)) && gpr(R(2)) >= 0 && gpr(R(3)) >= 0) {
                out.code += bset(R(0), read(in, R(2))) + bset(R(1), read(in, R(3)));
                return true;
            }
            return false;
        }
        case ARM_INS_VCMP: case ARM_INS_VCMPE: {
            if (!all_scalar(in, dbl)) return false;
            const std::string a = fval(R(0));
            std::string b;
            if (n == 2 && is_reg(op(in, 1))) b = fval(R(1));
            else b = dbl ? "0.0" : "0.0f";
            out.code += fmt("recomp::vcmp(ctx, %s, %s); ", a.c_str(), b.c_str());
            return true;
        }
        case ARM_INS_VADD: case ARM_INS_VSUB: case ARM_INS_VMUL: case ARM_INS_VDIV: case ARM_INS_VNMUL:
        case ARM_INS_VMLA: case ARM_INS_VMLS: case ARM_INS_VNMLA: case ARM_INS_VNMLS: {
            if (!all_scalar(in, dbl)) return false;
            const unsigned d = 0, a = n == 3 ? 1 : 0, b = n == 3 ? 2 : 1;
            const std::string D = fval(R(d)), A = fval(R(a)), B = fval(R(b));
            std::string e;
            switch (in.id) {
                case ARM_INS_VADD: e = A + " + " + B; break;
                case ARM_INS_VSUB: e = A + " - " + B; break;
                case ARM_INS_VMUL: e = A + " * " + B; break;
                case ARM_INS_VDIV: e = A + " / " + B; break;
                case ARM_INS_VNMUL: e = "-(" + A + " * " + B + ")"; break;
                // Non-fused: the product is rounded first (generated code is built with -ffp-contract=off).
                case ARM_INS_VMLA: e = D + " + (" + A + " * " + B + ")"; break;
                case ARM_INS_VMLS: e = D + " - (" + A + " * " + B + ")"; break;
                case ARM_INS_VNMLA: e = "-" + D + " - (" + A + " * " + B + ")"; break;
                case ARM_INS_VNMLS: e = "-" + D + " + (" + A + " * " + B + ")"; break;
            }
            out.code += "{ " + fset(R(d), e) + "} ";
            return true;
        }
        case ARM_INS_VNEG: case ARM_INS_VABS: case ARM_INS_VSQRT: {
            if (!all_scalar(in, dbl) || n != 2) return false;
            const std::string A = fval(R(1));
            std::string e = in.id == ARM_INS_VNEG ? "-" + A : in.id == ARM_INS_VABS ? "std::fabs(" + A + ")" : "std::sqrt(" + A + ")";
            if (in.id == ARM_INS_VNEG || in.id == ARM_INS_VABS) {
                // Sign-bit operations: exact on NaNs too.
                const int d = R(0), m = R(1);
                if (is_sreg(d)) out.code += bset(d, fmt("%s %s", bits(m).c_str(), in.id == ARM_INS_VNEG ? "^ 0x80000000u" : "& 0x7fffffffu"));
                else out.code += bset(d, fmt("%s %s", bits(m).c_str(), in.id == ARM_INS_VNEG ? "^ 0x8000000000000000ull" : "& 0x7fffffffffffffffull"));
                return true;
            }
            out.code += fset(R(0), e);
            return true;
        }
        case ARM_INS_VCVT: case ARM_INS_VCVTR: {
            if (n < 2 || !is_reg(op(in, 0)) || !is_reg(op(in, 1))) return false;
            const int d = R(0), m = R(1);
            if (is_qreg(d) || is_qreg(m) || (is_dreg(d) && is_dreg(m))) return false;  // NEON / fixed-point D forms
            const bool round = in.id == ARM_INS_VCVTR;
            const int fbits = n == 3 && op(in, 2)->type == ARM_OP_IMM ? op(in, 2)->imm : 0;
            const double scale = fbits ? double(uint64_t(1) << fbits) : 1.0;
            std::string e;
            switch (in.arm.vector_data) {
                case ARM_VECTORDATA_F64F32: e = fset(d, "double(" + fval(m) + ")"); break;
                case ARM_VECTORDATA_F32F64: e = fset(d, "float(" + fval(m) + ")"); break;
                case ARM_VECTORDATA_S32F32: case ARM_VECTORDATA_S32F64:
                    e = bset(d, fmt("recomp::%s(%s%s)", round ? "cvtr_s32" : "cvt_s32", fval(m).c_str(),
                                    fbits ? fmt(" * %.1f", scale).c_str() : ""));
                    break;
                case ARM_VECTORDATA_U32F32: case ARM_VECTORDATA_U32F64:
                    e = bset(d, fmt("recomp::%s(%s%s)", round ? "cvtr_u32" : "cvt_u32", fval(m).c_str(),
                                    fbits ? fmt(" * %.1f", scale).c_str() : ""));
                    break;
                case ARM_VECTORDATA_F32S32:
                    e = fset(d, fmt("float(int32_t(%s))%s", bits(m).c_str(), fbits ? fmt(" / %.1ff", scale).c_str() : ""));
                    break;
                case ARM_VECTORDATA_F32U32:
                    e = fset(d, fmt("float(uint32_t(%s))%s", bits(m).c_str(), fbits ? fmt(" / %.1ff", scale).c_str() : ""));
                    break;
                case ARM_VECTORDATA_F64S32:
                    e = fset(d, fmt("double(int32_t(%s))%s", bits(m).c_str(), fbits ? fmt(" / %.1f", scale).c_str() : ""));
                    break;
                case ARM_VECTORDATA_F64U32:
                    e = fset(d, fmt("double(uint32_t(%s))%s", bits(m).c_str(), fbits ? fmt(" / %.1f", scale).c_str() : ""));
                    break;
                default:
                    return false;
            }
            out.code += e;
            return true;
        }
        default:
            break;
    }

    // ---- NEON subset: bitwise ops and moves on whole registers.
    unsigned df = 0, dc = 0, nf = 0, nc = 0, mf = 0, mc = 0;
    switch (in.id) {
        case ARM_INS_VAND: case ARM_INS_VORR: case ARM_INS_VEOR: case ARM_INS_VBIC: case ARM_INS_VORN:
        case ARM_INS_VBSL: case ARM_INS_VBIT: case ARM_INS_VBIF: {
            if (n != 3 || !is_reg(op(in, 2)) || !dspan(R(0), df, dc) || !dspan(R(1), nf, nc) || !dspan(R(2), mf, mc)) return false;
            std::string c = "{ ";
            for (unsigned i = 0; i < dc; ++i) {
                const std::string D = fmt("ctx->d[%u]", df + i), N = fmt("ctx->d[%u]", nf + i), M = fmt("ctx->d[%u]", mf + i);
                std::string e;
                switch (in.id) {
                    case ARM_INS_VAND: e = N + " & " + M; break;
                    case ARM_INS_VORR: e = N + " | " + M; break;
                    case ARM_INS_VEOR: e = N + " ^ " + M; break;
                    case ARM_INS_VBIC: e = N + " & ~" + M; break;
                    case ARM_INS_VORN: e = N + " | ~" + M; break;
                    case ARM_INS_VBSL: e = "(" + N + " & " + D + ") | (" + M + " & ~" + D + ")"; break;
                    case ARM_INS_VBIT: e = "(" + N + " & " + M + ") | (" + D + " & ~" + M + ")"; break;
                    case ARM_INS_VBIF: e = "(" + D + " & " + M + ") | (" + N + " & ~" + M + ")"; break;
                }
                c += fmt("const uint64_t r%u = %s; ", i, e.c_str());
            }
            for (unsigned i = 0; i < dc; ++i) c += fmt("ctx->d[%u] = r%u; ", df + i, i);
            out.code += c + "} ";
            return true;
        }
        case ARM_INS_VLD1: case ARM_INS_VST1: {
            // Multiple-register form only: {dA, dB, ...}, [rN(:align)] [!| , rM]
            std::vector<unsigned> regs;
            unsigned i = 0;
            for (; i < n && is_reg(op(in, i)) && is_dreg(R(i)); ++i) {
                if (op(in, i)->neon_lane != -1) return false;
                regs.push_back(didx(R(i)));
            }
            const cs_arm_op* m = op(in, i);
            if (regs.empty() || !m || m->type != ARM_OP_MEM) return false;
            const cs_arm_op* post = op(in, i + 1);
            const std::string b = read(in, m->mem.base);
            std::string c = fmt("{ const uint32_t b = %s; ", b.c_str());
            for (unsigned k = 0; k < regs.size(); ++k)
                c += in.id == ARM_INS_VLD1 ? fmt("ctx->d[%u] = recomp::ld64(b + %u); ", regs[k], 8 * k)
                                           : fmt("recomp::st64(b + %u, ctx->d[%u]); ", 8 * k, regs[k]);
            if (post && is_reg(post)) c += fmt("%s = b + %s; ", reg_lv(m->mem.base).c_str(), read(in, post->reg).c_str());
            else if (in.arm.writeback) c += fmt("%s = b + %zu; ", reg_lv(m->mem.base).c_str(), 8 * regs.size());
            out.code += c + "} ";
            return true;
        }
        default:
            return false;
    }
}

}  // namespace recomp
