// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/node_helper.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Attribute;
using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Register;

namespace {
u32 GetUniformTypeElementsCount(Tegra::Shader::UniformType uniform_type) {
    switch (uniform_type) {
    case Tegra::Shader::UniformType::Single:
        return 1;
    case Tegra::Shader::UniformType::Double:
        return 2;
    case Tegra::Shader::UniformType::Quad:
    case Tegra::Shader::UniformType::UnsignedQuad:
        return 4;
    default:
        UNIMPLEMENTED_MSG("Unimplemented size={}!", static_cast<u32>(uniform_type));
        return 1;
    }
}
} // Anonymous namespace

u32 ShaderIR::DecodeMemory(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::LD_A: {
        // Note: Shouldn't this be interp mode flat? As in no interpolation made.
        UNIMPLEMENTED_IF_MSG(instr.gpr8.Value() != Register::ZeroIndex,
                             "Indirect attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) != 0,
                             "Unaligned attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG(instr.attribute.fmt20.IsPhysical() &&
                                 instr.attribute.fmt20.size != Tegra::Shader::AttributeSize::Word,
                             "Non-32 bits PHYS reads are not implemented");

        const Node buffer{GetRegister(instr.gpr39)};

        u64 next_element = instr.attribute.fmt20.element;
        auto next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

        const auto LoadNextElement = [&](u32 reg_offset) {
            const Node attribute{instr.attribute.fmt20.IsPhysical()
                                     ? GetPhysicalInputAttribute(instr.gpr8, buffer)
                                     : GetInputAttribute(static_cast<Attribute::Index>(next_index),
                                                         next_element, buffer)};

            SetRegister(bb, instr.gpr0.Value() + reg_offset, attribute);

            // Load the next attribute element into the following register. If the element
            // to load goes beyond the vec4 size, load the first element of the next
            // attribute.
            next_element = (next_element + 1) % 4;
            next_index = next_index + (next_element == 0 ? 1 : 0);
        };

        const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
        for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
            LoadNextElement(reg_offset);
        }
        break;
    }
    case OpCode::Id::LD_C: {
        UNIMPLEMENTED_IF(instr.ld_c.unknown != 0);

        Node index = GetRegister(instr.gpr8);

        const Node op_a =
            GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.GetOffset() + 0, index);

        switch (instr.ld_c.type.Value()) {
        case Tegra::Shader::UniformType::Single:
            SetRegister(bb, instr.gpr0, op_a);
            break;

        case Tegra::Shader::UniformType::Double: {
            const Node op_b =
                GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.GetOffset() + 4, index);

            SetTemporary(bb, 0, op_a);
            SetTemporary(bb, 1, op_b);
            SetRegister(bb, instr.gpr0, GetTemporary(0));
            SetRegister(bb, instr.gpr0.Value() + 1, GetTemporary(1));
            break;
        }
        default:
            UNIMPLEMENTED_MSG("Unhandled type: {}", static_cast<unsigned>(instr.ld_c.type.Value()));
        }
        break;
    }
    case OpCode::Id::LD_L:
        LOG_DEBUG(HW_GPU, "LD_L cache management mode: {}", static_cast<u64>(instr.ld_l.unknown));
        [[fallthrough]];
    case OpCode::Id::LD_S: {
        const auto GetMemory = [&](s32 offset) {
            ASSERT(offset % 4 == 0);
            const Node immediate_offset = Immediate(static_cast<s32>(instr.smem_imm) + offset);
            const Node address = Operation(OperationCode::IAdd, NO_PRECISE, GetRegister(instr.gpr8),
                                           immediate_offset);
            return opcode->get().GetId() == OpCode::Id::LD_S ? GetSharedMemory(address)
                                                             : GetLocalMemory(address);
        };

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bits32:
        case Tegra::Shader::StoreType::Bits64:
        case Tegra::Shader::StoreType::Bits128: {
            const u32 count = [&]() {
                switch (instr.ldst_sl.type.Value()) {
                case Tegra::Shader::StoreType::Bits32:
                    return 1;
                case Tegra::Shader::StoreType::Bits64:
                    return 2;
                case Tegra::Shader::StoreType::Bits128:
                    return 4;
                default:
                    UNREACHABLE();
                    return 0;
                }
            }();
            for (u32 i = 0; i < count; ++i) {
                SetTemporary(bb, i, GetMemory(i * 4));
            }
            for (u32 i = 0; i < count; ++i) {
                SetRegister(bb, instr.gpr0.Value() + i, GetTemporary(i));
            }
            break;
        }
        default:
            UNIMPLEMENTED_MSG("{} Unhandled type: {}", opcode->get().GetName(),
                              static_cast<u32>(instr.ldst_sl.type.Value()));
        }
        break;
    }
    case OpCode::Id::LD:
    case OpCode::Id::LDG: {
        const auto type = [instr, &opcode]() -> Tegra::Shader::UniformType {
            switch (opcode->get().GetId()) {
            case OpCode::Id::LD:
                UNIMPLEMENTED_IF_MSG(!instr.generic.extended, "Unextended LD is not implemented");
                return instr.generic.type;
            case OpCode::Id::LDG:
                return instr.ldg.type;
            default:
                UNREACHABLE();
                return {};
            }
        }();

        const auto [real_address_base, base_address, descriptor] =
            TrackGlobalMemory(bb, instr, false);

        const u32 count = GetUniformTypeElementsCount(type);
        if (!real_address_base || !base_address) {
            // Tracking failed, load zeroes.
            for (u32 i = 0; i < count; ++i) {
                SetRegister(bb, instr.gpr0.Value() + i, Immediate(0.0f));
            }
            break;
        }

        for (u32 i = 0; i < count; ++i) {
            const Node it_offset = Immediate(i * 4);
            const Node real_address =
                Operation(OperationCode::UAdd, NO_PRECISE, real_address_base, it_offset);
            const Node gmem = MakeNode<GmemNode>(real_address, base_address, descriptor);

            SetTemporary(bb, i, gmem);
        }
        for (u32 i = 0; i < count; ++i) {
            SetRegister(bb, instr.gpr0.Value() + i, GetTemporary(i));
        }
        break;
    }
    case OpCode::Id::ST_A: {
        UNIMPLEMENTED_IF_MSG(instr.gpr8.Value() != Register::ZeroIndex,
                             "Indirect attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) != 0,
                             "Unaligned attribute loads are not supported");

        u64 next_element = instr.attribute.fmt20.element;
        auto next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

        const auto StoreNextElement = [&](u32 reg_offset) {
            const auto dest = GetOutputAttribute(static_cast<Attribute::Index>(next_index),
                                                 next_element, GetRegister(instr.gpr39));
            const auto src = GetRegister(instr.gpr0.Value() + reg_offset);

            bb.push_back(Operation(OperationCode::Assign, dest, src));

            // Load the next attribute element into the following register. If the element
            // to load goes beyond the vec4 size, load the first element of the next
            // attribute.
            next_element = (next_element + 1) % 4;
            next_index = next_index + (next_element == 0 ? 1 : 0);
        };

        const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
        for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
            StoreNextElement(reg_offset);
        }

        break;
    }
    case OpCode::Id::ST_L:
        LOG_DEBUG(HW_GPU, "ST_L cache management mode: {}",
                  static_cast<u64>(instr.st_l.cache_management.Value()));
        [[fallthrough]];
    case OpCode::Id::ST_S: {
        const auto GetAddress = [&](s32 offset) {
            ASSERT(offset % 4 == 0);
            const Node immediate = Immediate(static_cast<s32>(instr.smem_imm) + offset);
            return Operation(OperationCode::IAdd, NO_PRECISE, GetRegister(instr.gpr8), immediate);
        };

        const auto set_memory = opcode->get().GetId() == OpCode::Id::ST_L
                                    ? &ShaderIR::SetLocalMemory
                                    : &ShaderIR::SetSharedMemory;

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bits128:
            (this->*set_memory)(bb, GetAddress(12), GetRegister(instr.gpr0.Value() + 3));
            (this->*set_memory)(bb, GetAddress(8), GetRegister(instr.gpr0.Value() + 2));
            [[fallthrough]];
        case Tegra::Shader::StoreType::Bits64:
            (this->*set_memory)(bb, GetAddress(4), GetRegister(instr.gpr0.Value() + 1));
            [[fallthrough]];
        case Tegra::Shader::StoreType::Bits32:
            (this->*set_memory)(bb, GetAddress(0), GetRegister(instr.gpr0));
            break;
        default:
            UNIMPLEMENTED_MSG("{} unhandled type: {}", opcode->get().GetName(),
                              static_cast<u32>(instr.ldst_sl.type.Value()));
        }
        break;
    }
    case OpCode::Id::ST:
    case OpCode::Id::STG: {
        const auto type = [instr, &opcode]() -> Tegra::Shader::UniformType {
            switch (opcode->get().GetId()) {
            case OpCode::Id::ST:
                UNIMPLEMENTED_IF_MSG(!instr.generic.extended, "Unextended ST is not implemented");
                return instr.generic.type;
            case OpCode::Id::STG:
                return instr.stg.type;
            default:
                UNREACHABLE();
                return {};
            }
        }();

        const auto [real_address_base, base_address, descriptor] =
            TrackGlobalMemory(bb, instr, true);
        if (!real_address_base || !base_address) {
            // Tracking failed, skip the store.
            break;
        }

        const u32 count = GetUniformTypeElementsCount(type);
        for (u32 i = 0; i < count; ++i) {
            const Node it_offset = Immediate(i * 4);
            const Node real_address = Operation(OperationCode::UAdd, real_address_base, it_offset);
            const Node gmem = MakeNode<GmemNode>(real_address, base_address, descriptor);
            const Node value = GetRegister(instr.gpr0.Value() + i);
            bb.push_back(Operation(OperationCode::Assign, gmem, value));
        }
        break;
    }
    case OpCode::Id::AL2P: {
        // Ignore al2p.direction since we don't care about it.

        // Calculate emulation fake physical address.
        const Node fixed_address{Immediate(static_cast<u32>(instr.al2p.address))};
        const Node reg{GetRegister(instr.gpr8)};
        const Node fake_address{Operation(OperationCode::IAdd, NO_PRECISE, reg, fixed_address)};

        // Set the fake address to target register.
        SetRegister(bb, instr.gpr0, fake_address);

        // Signal the shader IR to declare all possible attributes and varyings
        uses_physical_attributes = true;
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled memory instruction: {}", opcode->get().GetName());
    }

    return pc;
}

std::tuple<Node, Node, GlobalMemoryBase> ShaderIR::TrackGlobalMemory(NodeBlock& bb,
                                                                     Instruction instr,
                                                                     bool is_write) {
    const auto addr_register{GetRegister(instr.gmem.gpr)};
    const auto immediate_offset{static_cast<u32>(instr.gmem.offset)};

    const auto [base_address, index, offset] =
        TrackCbuf(addr_register, global_code, static_cast<s64>(global_code.size()));
    ASSERT_OR_EXECUTE_MSG(base_address != nullptr,
                          { return std::make_tuple(nullptr, nullptr, GlobalMemoryBase{}); },
                          "Global memory tracking failed");

    bb.push_back(Comment(fmt::format("Base address is c[0x{:x}][0x{:x}]", index, offset)));

    const GlobalMemoryBase descriptor{index, offset};
    const auto& [entry, is_new] = used_global_memory.try_emplace(descriptor);
    auto& usage = entry->second;
    if (is_write) {
        usage.is_written = true;
    } else {
        usage.is_read = true;
    }

    const auto real_address =
        Operation(OperationCode::UAdd, NO_PRECISE, Immediate(immediate_offset), addr_register);

    return {real_address, base_address, descriptor};
}

} // namespace VideoCommon::Shader
