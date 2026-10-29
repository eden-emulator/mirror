// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.
#pragma once

#include <array>
#include <bit>
#include <optional>
#include <span>
#include <utility>

#include "common/common_types.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/classify.h"
#include "shader_recompiler/frontend/ampere/flow.h"
#include "shader_recompiler/frontend/ampere/native.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

namespace Shader::Ampere {

// A source operand as selected by the form field (bits 9..11) of an ALU-style instruction.
struct Operand {
    enum class Kind { Reg, Ureg, Imm, Cbuf } kind{Kind::Reg};
    u32 reg{};
    u32 imm{};
    u32 cbuf_index{};
    u32 cbuf_offset{};
    bool abs{};
    bool negate{};
};

// A packed half source: lanes are selected by `swizzle` (0 both lanes, 2 low lane twice, 3 high lane
// twice) after which abs / negate apply to both lanes.
struct HalfOperand {
    IR::U32 value;
    u32 swizzle{};
    bool abs{};
    bool negate{};
};

// Translates one native instruction into IR. Every bit of the instruction must be either read by
// the handler (Take) or explicitly ignored (Ignore); bits that are set but neither read nor ignored
// make the program unsupported instead of being silently dropped.
class Visitor {
public:
    // `program_info` is set during validation to collect program-wide environment requirements.
    // A probe visitor records the registers a program touches instead of rejecting high GPRs.
    Visitor(Environment& env_, IR::Block& block, const Instruction& instruction, u32 index_,
            ProgramInfo* program_info_, const UniformLayout& layout_ = {}, bool probe_ = false);

    // Translates the instruction; throws UnsupportedError.
    void Run();

private:
    // Field access -----------------------------------------------------------------------------
    u32 Take(u32 lo, u32 count);
    void Ignore(u32 lo, u32 count);
    [[noreturn]] void Unsupported(std::string_view reason) const;
    void Require(bool condition, std::string_view reason) const {
        if (!condition) Unsupported(reason);
    }
    [[nodiscard]] u32 Form() const {
        return FormOf(op);
    }

    // Uniform registers live in the top of the IR register file: UR0..UR62 are slots 192..254 and the
    // uniform predicates UP0..UP6 are the bits of slot 191. Guest GPRs in that range are rejected.
    [[nodiscard]] IR::U32 UR(u32 reg);
    void NoteGpr(u32 reg);
    [[nodiscard]] IR::Reg UniformSlot(u32 reg);
    [[nodiscard]] IR::Reg PredicateSlot();
    void UR(u32 reg, const IR::U32& value);
    [[nodiscard]] IR::U1 UPred(u32 index, bool negated);
    void SetUPred(u32 index, const IR::U1& value);
    // Destination register / predicate access that follows the datapath of the instruction.
    [[nodiscard]] u32 Dst();
    void WriteDst(u32 reg, const IR::U32& value);
    [[nodiscard]] IR::U1 PredIn(u32 index, bool negated);
    void PredOut(u32 index, const IR::U1& value);

    // Registers and predicates -----------------------------------------------------------------
    [[nodiscard]] IR::U32 X(u32 reg);
    [[nodiscard]] IR::F32 F(u32 reg);
    void X(u32 reg, const IR::U32& value);
    void F(u32 reg, const IR::F32& value);
    [[nodiscard]] IR::U1 Pred(u32 index, bool negated);
    void SetPred(u32 index, const IR::U1& value);
    [[nodiscard]] IR::U32 Cbuf(u32 index, u32 byte_offset);

    // Operands ---------------------------------------------------------------------------------
    // Logical source n (0..2) of an instruction with up to three register-or-wide sources.
    // `modifiers` selects whether the abs/neg bits belong to the instruction (floating point
    // and signed-integer ops) or must stay clear.
    [[nodiscard]] Operand Source(u32 which, bool modifiers);
    // The wide (bits 32..63) slot selected by the form: register, immediate or constant buffer.
    [[nodiscard]] Operand WideSlot(bool modifiers);
    [[nodiscard]] IR::U32 Value(const Operand& operand);
    [[nodiscard]] IR::F32 FloatValue(const Operand& operand, bool apply_modifiers = true);

    [[nodiscard]] HalfOperand HalfSource(u32 which);
    [[nodiscard]] std::pair<IR::F16F32F64, IR::F16F32F64> HalfLanes(const HalfOperand& operand);

    // Instruction handlers ---------------------------------------------------------------------
    void MOV();
    void FMUL();
    void MUFU();
    void IPA();
    void EXIT();
    void TEX();
    void TLD();
    void SUST();
    void LDG();
    void R2UR();
    void LDS();
    void ATOMG();
    void LDL();
    void STL();
    void MEMBAR();
    void BAR();
    void FSWZADD();
    void VOTEU();
    void VOTE();
    void LDGSTS();
    void TXQ();
    void ATOMS_POPC();
    void BREV();
    void FSET();
    void HMNMX2();
    void SHFL();
    void STS();
    [[nodiscard]] IR::U32 SharedOffset(u32 reg, u32 stride, u32 offset);
    void STG();
    [[nodiscard]] IR::U64 GlobalAddress(u32 reg, bool reg_pair, bool use_uniform, u32 uniform,
                                        bool uniform_pair, u32 offset);
    void SULD();
    void SUATOM();
    void TLD4();
    void TXD();
    void TMML();
    void UseDescriptorBuffer(u32 cbuf_index);
    void FADD();
    void FFMA();
    void FMNMX();
    void SEL(bool floating);
    void FSETP();
    void ISETP();
    void IADD3();
    void HADD2();
    void HMUL2();
    void HFMA2();
    void LOP3();
    void SHF();
    void LEA();
    void IMNMX();
    void PRMT();
    void IMAD();
    void IMAD_HI();
    void IMAD_WIDE();
    void IABS();
    void IDP();
    void ATOMS();
    void REDUX();
    void MATCH();
    void FLO();
    void POPC();
    void SGXT();
    void ISBERD();
    void ISBEWR();
    void HSETP2();
    void LDSM();
    void MOVM();
    void HMMA();
    [[nodiscard]] IR::U32 TransposeB16(const IR::U32& value);
    [[nodiscard]] IR::U32 ShuffleLane(const IR::U32& value, const IR::U32& lane);
    [[nodiscard]] IR::U32 MultiplyHigh(const IR::U32& a, const IR::U32& b, bool is_signed);
    void F2FP();
    void PLOP3();
    void P2R();
    void R2P();
    void F2I();
    void I2F();
    void FRND();
    void F2F();
    void ALD();
    void AST();
    void LDC();
    void CS2R();
    void S2R();
    void UMOV();
    void ULDC();
    void CheckConsumed();

    Environment& env;
    IR::IREmitter ir;
    const Instruction& insn;
    u32 index;
    ProgramInfo* program_info;
    UniformLayout layout;
    bool probe;
    u32 op;
    bool uniform_op{};
    std::optional<IR::U1> guard_select; // set when a guarded pure instruction is written as a select
    std::array<u32, 4> consumed{};
};

} // namespace Shader::Ampere
