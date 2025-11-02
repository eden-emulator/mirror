#pragma once

#undef NDEBUG

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <ranges>
#include <bit>
#include <utility>

//#ifndef __cpp_lib_unreachable
namespace std {
[[noreturn]] inline void unreachable() {
#if defined(_MSC_VER) && !defined(__clang__) // MSVC
    __assume(false);
#else // GCC, Clang
    __builtin_unreachable();
#endif
}
}
//#endif

namespace powah {

// Symbolic conditions
enum class Cond : uint8_t {
    LT, LE, NG, EQ, GE, NL, GT, NE, SO, UN, NS, NU
};

struct GPR { uint32_t index; };
struct FPR { uint32_t index; };
struct CPR { uint32_t index; };
struct Label { uint32_t index; };

constexpr inline GPR R0{0};
constexpr inline GPR R1{1};
constexpr inline GPR R2{2};
constexpr inline GPR R3{3};
constexpr inline GPR R4{4};
constexpr inline GPR R5{5};
constexpr inline GPR R6{6};
constexpr inline GPR R7{7};
constexpr inline GPR R8{8};
constexpr inline GPR R9{9};
constexpr inline GPR R10{10};
constexpr inline GPR R11{11};
constexpr inline GPR R12{12};
constexpr inline GPR R13{13};
constexpr inline GPR R14{14};
constexpr inline GPR R15{15};
constexpr inline GPR R16{16};
constexpr inline GPR R17{17};
constexpr inline GPR R18{18};
constexpr inline GPR R19{19};
constexpr inline GPR R20{20};
constexpr inline GPR R21{21};
constexpr inline GPR R22{22};
constexpr inline GPR R23{23};
constexpr inline GPR R24{24};
constexpr inline GPR R25{25};
constexpr inline GPR R26{26};
constexpr inline GPR R27{27};
constexpr inline GPR R28{28};
constexpr inline GPR R29{29};
constexpr inline GPR R30{30};
constexpr inline GPR R31{31};

constexpr inline FPR FR0{0};
constexpr inline FPR FR1{1};
constexpr inline FPR FR2{2};
constexpr inline FPR FR3{3};
constexpr inline FPR FR4{4};
constexpr inline FPR FR5{5};
constexpr inline FPR FR6{6};
constexpr inline FPR FR7{7};
constexpr inline FPR FR8{8};
constexpr inline FPR FR9{9};
constexpr inline FPR FR10{10};
constexpr inline FPR FR11{11};
constexpr inline FPR FR12{12};
constexpr inline FPR FR13{13};
constexpr inline FPR FR14{14};
constexpr inline FPR FR15{15};
constexpr inline FPR FR16{16};
constexpr inline FPR FR17{17};
constexpr inline FPR FR18{18};
constexpr inline FPR FR19{19};
constexpr inline FPR FR20{20};
constexpr inline FPR FR21{21};
constexpr inline FPR FR22{22};
constexpr inline FPR FR23{23};
constexpr inline FPR FR24{24};
constexpr inline FPR FR25{25};
constexpr inline FPR FR26{26};
constexpr inline FPR FR27{27};
constexpr inline FPR FR28{28};
constexpr inline FPR FR29{29};
constexpr inline FPR FR30{30};
constexpr inline FPR FR31{31};

// They call it CPR because when programmers see this code they-
constexpr inline CPR CR0{0};
constexpr inline CPR CR1{4};
constexpr inline CPR CR2{8};
constexpr inline CPR CR3{12};
constexpr inline CPR CR4{16};
constexpr inline CPR CR5{20};
constexpr inline CPR CR6{24};
constexpr inline CPR CR7{28};

struct Context {
    Context() = default;
    Context(void* ptr, size_t size)
        : base{reinterpret_cast<uint32_t*>(ptr)}
        , offset{0}
        , size{uint32_t(size)} {

    }
    ~Context() = default;

    std::vector<uint32_t> labels;

    Label DefineLabel() {
        labels.push_back(0);
        return Label{ uint32_t(labels.size()) };
    }

    void LABEL(Label l) {
        assert(labels[l.index] == 0);
        labels[l.index] = offset;
    }

    static constexpr uint32_t cond2offset(Cond c) noexcept {
        switch (c) {
        case Cond::LT: return 1;
        case Cond::LE: return 2;
        case Cond::NG: return 2;
        case Cond::EQ: return 3;
        case Cond::GE: return 1;
        case Cond::NL: return 1;
        case Cond::GT: return 2;
        case Cond::NE: return 3;
        case Cond::SO: return 4;
        case Cond::UN: return 4;
        case Cond::NS: return 4;
        case Cond::NU: return 4;
        default: return 0; //hopefully icc isn't stupid
        }
    }

    uint32_t bitExt(uint32_t value, uint32_t offs, uint32_t n) {
        uint32_t mask = (1UL << (n + 1)) - 1;
        return (value & mask) << (32 - (n + offs));
    }
    void emit_XO(uint32_t op, GPR const rt, GPR const ra, GPR const rb, bool oe, bool rc) {
        (void)op;
        (void)rt;
        (void)ra;
        (void)rb;
        (void)oe;
        (void)rc;
        std::abort();
    }
    void emit_D(uint32_t op, GPR const rt, GPR const ra, uint32_t d) {
        base[offset++] = __bswap32(op |
            (op == 0x74000000
                ? (bitExt(ra.index, 6, 5) | bitExt(rt.index, 11, 5))
                : (bitExt(rt.index, 6, 5) | bitExt(ra.index, 11, 5)))
            | (d & 0xffff)
        );
    }
    void emit_X(uint32_t op, GPR const ra, GPR const rt, GPR const rb, bool rc) {
        base[offset++] = __bswap32(op |
            bitExt(rt.index, 6, 5)
            | bitExt(ra.index, 11, 5)
            | bitExt(rb.index, 16, 5)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_XS(uint32_t op, GPR const rt, GPR const ra, uint32_t sh, bool rc) {
        base[offset++] = __bswap32(op |
            bitExt(rt.index, 6, 5)
            | bitExt(ra.index, 11, 5)
            | bitExt(sh, 16, 5)
            | bitExt(sh >> 5, 30, 1)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_reloc_I(uint32_t op, Label const& l) {
        (void)op;
        (void)l;
        std::abort();
    }
    void emit_reloc_B(uint32_t op, uint32_t cri, Label const& l, bool lk) {
        (void)op;
        (void)cri;
        (void)l;
        (void)lk;
        std::abort();
    }
    void emit_XL(uint32_t op, uint32_t bt, uint32_t ba, uint32_t bb, bool lk) {
        base[offset++] = __bswap32(op |
            bitExt(bt, 6, 5)
            | bitExt(ba, 11, 5)
            | bitExt(bb, 16, 5)
            | bitExt(lk, 31, 1)
        );
    }
    void emit_A(uint32_t op, FPR const frt, FPR const fra, FPR const frb, FPR const frc, bool rc) {
        base[offset++] = __bswap32(op |
            bitExt(frt.index, 6, 5)
            | bitExt(fra.index, 11, 5)
            | bitExt(frb.index, 16, 5)
            | bitExt(frc.index, 21, 5)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_DS(uint32_t op, GPR const rt, GPR const ra, uint32_t d) {
        //assert(d & 0x03 == 0);
        base[offset++] = __bswap32(op |
            bitExt(rt.index, 6, 5)
            | bitExt(ra.index, 11, 5)
            | bitExt(d >> 2, 16, 14)
        );
    }
    void emit_M(uint32_t op, GPR const rs, GPR const ra, uint32_t sh, uint32_t mb, uint32_t me, bool rc) {
        (void)op;
        (void)rs;
        (void)ra;
        (void)sh;
        (void)mb;
        (void)me;
        (void)rc;
        std::abort();
    }
    void emit_MD(uint32_t op, GPR const rs, GPR const ra, GPR const rb, uint32_t mb, bool rc) {
        assert(mb <= 0x3f);
        base[offset++] = __bswap32(op |
            bitExt(ra.index, 6, 5)
            | bitExt(rs.index, 11, 5)
            | bitExt(rb.index, 16, 5)
            | ((mb & 0x1f) << 6) | (mb & 0x20)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_MD(uint32_t op, GPR const rs, GPR const ra, uint32_t sh, uint32_t mb, bool rc) {
        assert(sh <= 0x3f && mb <= 0x3f);
        base[offset++] = __bswap32(op |
            bitExt(ra.index, 6, 5)
            | bitExt(rs.index, 11, 5)
            | ((mb & 0x1f) << 6) | (mb & 0x20)
            | ((sh & 0x1f) << 11) | ((sh >> 4) & 0x02)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_MDS(uint32_t op, GPR const rs, GPR const ra, GPR const rb, uint32_t mb, bool rc) {
        base[offset++] = __bswap32(op |
            bitExt(ra.index, 6, 5)
            | bitExt(rs.index, 11, 5)
            | bitExt(rb.index, 16, 5)
            | ((mb & 0x1f) << 6) | (mb & 0x20)
            | bitExt(rc, 31, 1)
        );
    }
    void emit_XFL(uint32_t op) {
        (void)op;
        std::abort();
    }
    void emit_XFX(uint32_t op, GPR const rt, uint32_t spr) {
        (void)op;
        (void)rt;
        (void)spr;
        std::abort();
    }
    void emit_SC(uint32_t op, uint32_t lev) {
        (void)op;
        (void)lev;
        std::abort();
    }

    // Extended Memmonics, hand coded :)
    void MR(GPR const ra, GPR const rs) { OR(ra, rs, rs); }
    void NOP() { ORI(R0, R0, 0); }
    void NOT(GPR const ra, GPR const rs) { NOR(ra, rs, rs); }

    void ROTLDI(GPR const ra, GPR const rs, uint32_t n) { RLDICL(ra, rs, n, 0); }
    void ROTRDI(GPR const ra, GPR const rs, uint32_t n) { RLDICL(ra, rs, 64 - n, 0); }

    void ROTLWI(GPR const ra, GPR const rs, uint32_t n) { RLWINM(ra, rs, n, 0, 31); }
    void ROTRWI(GPR const ra, GPR const rs, uint32_t n) { RLWINM(ra, rs, 32 - n, 0, 31); }

    void ROTLW(GPR const ra, GPR const rs, GPR const rb) { RLWNM(ra, rs, rb, 0, 31); }
    void ROTLD(GPR const ra, GPR const rs, GPR const rb) { RLDCL(ra, rs, rb, 0); }

    void EXTLDI(GPR const ra, GPR const rs, uint32_t n, uint32_t b) { RLDICR(ra, rs, b, n - 1); }
    void SLDI(GPR const ra, GPR const rs, uint32_t n) { RLDICR(ra, rs,  n, 63 - n); }
    void CLRLDI(GPR const ra, GPR const rs, uint32_t n) { RLDICL(ra, rs, 0, n); }

    void EXTRDI(GPR const ra, GPR const rs, uint32_t n, uint32_t b) { RLDICL(ra, rs, b + n, 64 - n); }
    void SRDI(GPR const ra, GPR const rs, uint32_t n) { RLDICL(ra, rs, 64 - n, n); }
    void CLLDI(GPR const ra, GPR const rs, uint32_t n) { RLDICR(ra, rs, 0, n); }
    void CLRSLDI(GPR const ra, GPR const rs, uint32_t n, uint32_t b) { RLDICR(ra, rs, n, b - n); }

    void EXTLWI(GPR const ra, GPR const rs, uint32_t n, uint32_t b) { RLWINM(ra, rs, b, 0, n - 1); }
    void SRWI(GPR const ra, GPR const rs, uint32_t n) { RLWINM(ra, rs, 32 - n, n, 31); }
    void CLRRWI(GPR const ra, GPR const rs, uint32_t n) { RLWINM(ra, rs, 0, 0, 31 - n); }

    void CRSET(CPR const bx) { CREQV(bx, bx, bx); }
    void CRCLR(CPR const bx) { CRXOR(bx, bx, bx); }
    void CRMOVE(CPR const bx, CPR const by) { CROR(bx, by, by); }
    void CRNOT(CPR const bx, CPR const by) { CRNOR(bx, by, by); }

    void CMPLDI(GPR const rx, uint32_t v) { CMPLI(0, 1, rx, v); }
    void CMPLWI(GPR const rx, uint32_t v) { CMPLI(0, 0, rx, v); }
    void CMPLD(GPR const rx, GPR const ry) { CMPL(0, 1, rx, ry); }
    void CMPLW(GPR const rx, GPR const ry) { CMPL(0, 0, rx, ry); }

    void CMPLDI(CPR const cr, GPR const rx, uint32_t v) { CMPLI(cr.index / 4, 1, rx, v); }
    void CMPLWI(CPR const cr, GPR const rx, uint32_t v) { CMPLI(cr.index / 4, 0, rx, v); }
    void CMPLD(CPR const cr, GPR const rx, GPR const ry) { CMPL(cr.index / 4, 1, rx, ry); }
    void CMPLW(CPR const cr, GPR const rx, GPR const ry) { CMPL(cr.index / 4, 0, rx, ry); }

    void CMPWI(CPR const cr, GPR const rx, uint32_t si) { CMPI(cr.index / 4, 0, rx, si); }
    void CMPW(CPR const cr, GPR const rx, GPR const ry) { CMP(cr.index / 4, 0, rx, ry); }
    void CMPDI(CPR const cr, GPR const rx, uint32_t si) { CMPI(cr.index / 4, 1, rx, si); }
    void CMPD(CPR const cr, GPR const rx, GPR const ry) { CMP(cr.index / 4, 1, rx, ry); }

    void BLR() { BCLR(R0, CR0, R0); }

    // TODO: PowerPC 11 stuff
    void ISEL(GPR const rd, GPR const ra, GPR const rb, uint32_t d) {
        (void)rd;
        (void)ra;
        (void)rb;
        (void)d;
        std::unreachable();
    }
    void ISELLT(GPR const rd, GPR const ra, GPR const rb) { ISEL(rd, ra, rb, 0); }
    void ISELGT(GPR const rd, GPR const ra, GPR const rb) { ISEL(rd, ra, rb, 1); }
    void ISELEQ(GPR const rd, GPR const ra, GPR const rb) { ISEL(rd, ra, rb, 2); }

    // Rawly pasted because fuck you
#include "powah_gen_base.hpp"

    uint32_t* base = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

}
