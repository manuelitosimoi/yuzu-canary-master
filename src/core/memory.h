// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <string>
#include "common/common_types.h"

namespace Kernel {
class Process;
}

namespace Memory {

/**
 * Page size used by the ARM architecture. This is the smallest granularity with which memory can
 * be mapped.
 */
constexpr std::size_t PAGE_BITS = 12;
constexpr u64 PAGE_SIZE = 1ULL << PAGE_BITS;
constexpr u64 PAGE_MASK = PAGE_SIZE - 1;

/// Virtual user-space memory regions
enum : VAddr {
    /// TLS (Thread-Local Storage) related.
    TLS_ENTRY_SIZE = 0x200,

    /// Application stack
    DEFAULT_STACK_SIZE = 0x100000,

    /// Kernel Virtual Address Range
    KERNEL_REGION_VADDR = 0xFFFFFF8000000000,
    KERNEL_REGION_SIZE = 0x7FFFE00000,
    KERNEL_REGION_END = KERNEL_REGION_VADDR + KERNEL_REGION_SIZE,
};

/// Changes the currently active page table to that of
/// the given process instance.
void SetCurrentPageTable(Kernel::Process& process);

/// Determines if the given VAddr is valid for the specified process.
bool IsValidVirtualAddress(const Kernel::Process& process, VAddr vaddr);
bool IsValidVirtualAddress(VAddr vaddr);
/// Determines if the given VAddr is a kernel address
bool IsKernelVirtualAddress(VAddr vaddr);

u8 Read8(VAddr addr);
u16 Read16(VAddr addr);
u32 Read32(VAddr addr);
u64 Read64(VAddr addr);

void Write8(VAddr addr, u8 data);
void Write16(VAddr addr, u16 data);
void Write32(VAddr addr, u32 data);
void Write64(VAddr addr, u64 data);

void ReadBlock(const Kernel::Process& process, VAddr src_addr, void* dest_buffer, std::size_t size);
void ReadBlock(VAddr src_addr, void* dest_buffer, std::size_t size);
void WriteBlock(const Kernel::Process& process, VAddr dest_addr, const void* src_buffer,
                std::size_t size);
void WriteBlock(VAddr dest_addr, const void* src_buffer, std::size_t size);
void ZeroBlock(const Kernel::Process& process, VAddr dest_addr, std::size_t size);
void CopyBlock(VAddr dest_addr, VAddr src_addr, std::size_t size);

u8* GetPointer(VAddr vaddr);

std::string ReadCString(VAddr vaddr, std::size_t max_length);

/**
 * Mark each page touching the region as cached.
 */
void RasterizerMarkRegionCached(VAddr vaddr, u64 size, bool cached);

} // namespace Memory
