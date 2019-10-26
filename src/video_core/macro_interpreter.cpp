// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/macro_interpreter.h"

MICROPROFILE_DEFINE(MacroInterp, "GPU", "Execute macro interpreter", MP_RGB(128, 128, 192));

namespace Tegra {
namespace {
enum class Operation : u32 {
    ALU = 0,
    AddImmediate = 1,
    ExtractInsert = 2,
    ExtractShiftLeftImmediate = 3,
    ExtractShiftLeftRegister = 4,
    Read = 5,
    Unused = 6, // This operation doesn't seem to be a valid encoding.
    Branch = 7,
};
} // Anonymous namespace

enum class MacroInterpreter::ALUOperation : u32 {
    Add = 0,
    AddWithCarry = 1,
    Subtract = 2,
    SubtractWithBorrow = 3,
    // Operations 4-7 don't seem to be valid encodings.
    Xor = 8,
    Or = 9,
    And = 10,
    AndNot = 11,
    Nand = 12
};

enum class MacroInterpreter::ResultOperation : u32 {
    IgnoreAndFetch = 0,
    Move = 1,
    MoveAndSetMethod = 2,
    FetchAndSend = 3,
    MoveAndSend = 4,
    FetchAndSetMethod = 5,
    MoveAndSetMethodFetchAndSend = 6,
    MoveAndSetMethodSend = 7
};

enum class MacroInterpreter::BranchCondition : u32 {
    Zero = 0,
    NotZero = 1,
};

union MacroInterpreter::Opcode {
    u32 raw;
    BitField<0, 3, Operation> operation;
    BitField<4, 3, ResultOperation> result_operation;
    BitField<4, 1, BranchCondition> branch_condition;
    // If set on a branch, then the branch doesn't have a delay slot.
    BitField<5, 1, u32> branch_annul;
    BitField<7, 1, u32> is_exit;
    BitField<8, 3, u32> dst;
    BitField<11, 3, u32> src_a;
    BitField<14, 3, u32> src_b;
    // The signed immediate overlaps the second source operand and the alu operation.
    BitField<14, 18, s32> immediate;

    BitField<17, 5, ALUOperation> alu_operation;

    // Bitfield instructions data
    BitField<17, 5, u32> bf_src_bit;
    BitField<22, 5, u32> bf_size;
    BitField<27, 5, u32> bf_dst_bit;

    u32 GetBitfieldMask() const {
        return (1 << bf_size) - 1;
    }

    s32 GetBranchTarget() const {
        return static_cast<s32>(immediate * sizeof(u32));
    }
};

MacroInterpreter::MacroInterpreter(Engines::Maxwell3D& maxwell3d) : maxwell3d(maxwell3d) {}

void MacroInterpreter::Execute(u32 offset, std::size_t num_parameters, const u32* parameters) {
    MICROPROFILE_SCOPE(MacroInterp);
    Reset();

    registers[1] = parameters[0];

    if (num_parameters > parameters_capacity) {
        parameters_capacity = num_parameters;
        this->parameters = std::make_unique<u32[]>(num_parameters);
    }
    std::memcpy(this->parameters.get(), parameters, num_parameters * sizeof(u32));
    this->num_parameters = num_parameters;

    // Execute the code until we hit an exit condition.
    bool keep_executing = true;
    while (keep_executing) {
        keep_executing = Step(offset, false);
    }

    // Assert the the macro used all the input parameters
    ASSERT(next_parameter_index == num_parameters);
}

void MacroInterpreter::Reset() {
    registers = {};
    pc = 0;
    delayed_pc = {};
    method_address.raw = 0;
    num_parameters = 0;
    // The next parameter index starts at 1, because $r1 already has the value of the first
    // parameter.
    next_parameter_index = 1;
    carry_flag = false;
}

bool MacroInterpreter::Step(u32 offset, bool is_delay_slot) {
    u32 base_address = pc;

    Opcode opcode = GetOpcode(offset);
    pc += 4;

    // Update the program counter if we were delayed
    if (delayed_pc) {
        ASSERT(is_delay_slot);
        pc = *delayed_pc;
        delayed_pc = {};
    }

    switch (opcode.operation) {
    case Operation::ALU: {
        u32 result = GetALUResult(opcode.alu_operation, GetRegister(opcode.src_a),
                                  GetRegister(opcode.src_b));
        ProcessResult(opcode.result_operation, opcode.dst, result);
        break;
    }
    case Operation::AddImmediate: {
        ProcessResult(opcode.result_operation, opcode.dst,
                      GetRegister(opcode.src_a) + opcode.immediate);
        break;
    }
    case Operation::ExtractInsert: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        src = (src >> opcode.bf_src_bit) & opcode.GetBitfieldMask();
        dst &= ~(opcode.GetBitfieldMask() << opcode.bf_dst_bit);
        dst |= src << opcode.bf_dst_bit;
        ProcessResult(opcode.result_operation, opcode.dst, dst);
        break;
    }
    case Operation::ExtractShiftLeftImmediate: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        u32 result = ((src >> dst) & opcode.GetBitfieldMask()) << opcode.bf_dst_bit;

        ProcessResult(opcode.result_operation, opcode.dst, result);
        break;
    }
    case Operation::ExtractShiftLeftRegister: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        u32 result = ((src >> opcode.bf_src_bit) & opcode.GetBitfieldMask()) << dst;

        ProcessResult(opcode.result_operation, opcode.dst, result);
        break;
    }
    case Operation::Read: {
        u32 result = Read(GetRegister(opcode.src_a) + opcode.immediate);
        ProcessResult(opcode.result_operation, opcode.dst, result);
        break;
    }
    case Operation::Branch: {
        ASSERT_MSG(!is_delay_slot, "Executing a branch in a delay slot is not valid");
        u32 value = GetRegister(opcode.src_a);
        bool taken = EvaluateBranchCondition(opcode.branch_condition, value);
        if (taken) {
            // Ignore the delay slot if the branch has the annul bit.
            if (opcode.branch_annul) {
                pc = base_address + opcode.GetBranchTarget();
                return true;
            }

            delayed_pc = base_address + opcode.GetBranchTarget();
            // Execute one more instruction due to the delay slot.
            return Step(offset, true);
        }
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unimplemented macro operation {}",
                          static_cast<u32>(opcode.operation.Value()));
    }

    // An instruction with the Exit flag will not actually
    // cause an exit if it's executed inside a delay slot.
    if (opcode.is_exit && !is_delay_slot) {
        // Exit has a delay slot, execute the next instruction
        Step(offset, true);
        return false;
    }

    return true;
}

MacroInterpreter::Opcode MacroInterpreter::GetOpcode(u32 offset) const {
    const auto& macro_memory{maxwell3d.GetMacroMemory()};
    ASSERT((pc % sizeof(u32)) == 0);
    ASSERT((pc + offset) < macro_memory.size() * sizeof(u32));
    return {macro_memory[offset + pc / sizeof(u32)]};
}

u32 MacroInterpreter::GetALUResult(ALUOperation operation, u32 src_a, u32 src_b) {
    switch (operation) {
    case ALUOperation::Add: {
        const u64 result{static_cast<u64>(src_a) + src_b};
        carry_flag = result > 0xffffffff;
        return static_cast<u32>(result);
    }
    case ALUOperation::AddWithCarry: {
        const u64 result{static_cast<u64>(src_a) + src_b + (carry_flag ? 1ULL : 0ULL)};
        carry_flag = result > 0xffffffff;
        return static_cast<u32>(result);
    }
    case ALUOperation::Subtract: {
        const u64 result{static_cast<u64>(src_a) - src_b};
        carry_flag = result < 0x100000000;
        return static_cast<u32>(result);
    }
    case ALUOperation::SubtractWithBorrow: {
        const u64 result{static_cast<u64>(src_a) - src_b - (carry_flag ? 0ULL : 1ULL)};
        carry_flag = result < 0x100000000;
        return static_cast<u32>(result);
    }
    case ALUOperation::Xor:
        return src_a ^ src_b;
    case ALUOperation::Or:
        return src_a | src_b;
    case ALUOperation::And:
        return src_a & src_b;
    case ALUOperation::AndNot:
        return src_a & ~src_b;
    case ALUOperation::Nand:
        return ~(src_a & src_b);

    default:
        UNIMPLEMENTED_MSG("Unimplemented ALU operation {}", static_cast<u32>(operation));
        return 0;
    }
}

void MacroInterpreter::ProcessResult(ResultOperation operation, u32 reg, u32 result) {
    switch (operation) {
    case ResultOperation::IgnoreAndFetch:
        // Fetch parameter and ignore result.
        SetRegister(reg, FetchParameter());
        break;
    case ResultOperation::Move:
        // Move result.
        SetRegister(reg, result);
        break;
    case ResultOperation::MoveAndSetMethod:
        // Move result and use as Method Address.
        SetRegister(reg, result);
        SetMethodAddress(result);
        break;
    case ResultOperation::FetchAndSend:
        // Fetch parameter and send result.
        SetRegister(reg, FetchParameter());
        Send(result);
        break;
    case ResultOperation::MoveAndSend:
        // Move and send result.
        SetRegister(reg, result);
        Send(result);
        break;
    case ResultOperation::FetchAndSetMethod:
        // Fetch parameter and use result as Method Address.
        SetRegister(reg, FetchParameter());
        SetMethodAddress(result);
        break;
    case ResultOperation::MoveAndSetMethodFetchAndSend:
        // Move result and use as Method Address, then fetch and send parameter.
        SetRegister(reg, result);
        SetMethodAddress(result);
        Send(FetchParameter());
        break;
    case ResultOperation::MoveAndSetMethodSend:
        // Move result and use as Method Address, then send bits 12:17 of result.
        SetRegister(reg, result);
        SetMethodAddress(result);
        Send((result >> 12) & 0b111111);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented result operation {}", static_cast<u32>(operation));
    }
}

u32 MacroInterpreter::FetchParameter() {
    ASSERT(next_parameter_index < num_parameters);
    return parameters[next_parameter_index++];
}

u32 MacroInterpreter::GetRegister(u32 register_id) const {
    return registers.at(register_id);
}

void MacroInterpreter::SetRegister(u32 register_id, u32 value) {
    // Register 0 is hardwired as the zero register.
    // Ensure no writes to it actually occur.
    if (register_id == 0) {
        return;
    }

    registers.at(register_id) = value;
}

void MacroInterpreter::SetMethodAddress(u32 address) {
    method_address.raw = address;
}

void MacroInterpreter::Send(u32 value) {
    maxwell3d.CallMethodFromMME({method_address.address, value});
    // Increment the method address by the method increment.
    method_address.address.Assign(method_address.address.Value() +
                                  method_address.increment.Value());
}

u32 MacroInterpreter::Read(u32 method) const {
    return maxwell3d.GetRegisterValue(method);
}

bool MacroInterpreter::EvaluateBranchCondition(BranchCondition cond, u32 value) const {
    switch (cond) {
    case BranchCondition::Zero:
        return value == 0;
    case BranchCondition::NotZero:
        return value != 0;
    }
    UNREACHABLE();
    return true;
}

} // namespace Tegra
