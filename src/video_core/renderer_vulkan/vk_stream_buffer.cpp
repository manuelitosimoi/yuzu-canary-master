// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "common/assert.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_memory_manager.h"
#include "video_core/renderer_vulkan/vk_resource_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace Vulkan {

constexpr u64 WATCHES_INITIAL_RESERVE = 0x4000;
constexpr u64 WATCHES_RESERVE_CHUNK = 0x1000;

VKStreamBuffer::VKStreamBuffer(const VKDevice& device, VKMemoryManager& memory_manager,
                               VKScheduler& scheduler, u64 size, vk::BufferUsageFlags usage,
                               vk::AccessFlags access, vk::PipelineStageFlags pipeline_stage)
    : device{device}, scheduler{scheduler}, buffer_size{size}, access{access}, pipeline_stage{
                                                                                   pipeline_stage} {
    CreateBuffers(memory_manager, usage);
    ReserveWatches(WATCHES_INITIAL_RESERVE);
}

VKStreamBuffer::~VKStreamBuffer() = default;

std::tuple<u8*, u64, bool> VKStreamBuffer::Reserve(u64 size) {
    ASSERT(size <= buffer_size);
    mapped_size = size;

    if (offset + size > buffer_size) {
        // The buffer would overflow, save the amount of used buffers, signal an invalidation and
        // reset the state.
        invalidation_mark = used_watches;
        used_watches = 0;
        offset = 0;
    }

    return {mapped_pointer + offset, offset, invalidation_mark.has_value()};
}

void VKStreamBuffer::Send(u64 size) {
    ASSERT_MSG(size <= mapped_size, "Reserved size is too small");

    if (invalidation_mark) {
        // TODO(Rodrigo): Find a better way to invalidate than waiting for all watches to finish.
        scheduler.Flush();
        std::for_each(watches.begin(), watches.begin() + *invalidation_mark,
                      [&](auto& resource) { resource->Wait(); });
        invalidation_mark = std::nullopt;
    }

    if (used_watches + 1 >= watches.size()) {
        // Ensure that there are enough watches.
        ReserveWatches(WATCHES_RESERVE_CHUNK);
    }
    // Add a watch for this allocation.
    watches[used_watches++]->Watch(scheduler.GetFence());

    offset += size;
}

void VKStreamBuffer::CreateBuffers(VKMemoryManager& memory_manager, vk::BufferUsageFlags usage) {
    const vk::BufferCreateInfo buffer_ci({}, buffer_size, usage, vk::SharingMode::eExclusive, 0,
                                         nullptr);

    const auto dev = device.GetLogical();
    const auto& dld = device.GetDispatchLoader();
    buffer = dev.createBufferUnique(buffer_ci, nullptr, dld);
    commit = memory_manager.Commit(*buffer, true);
    mapped_pointer = commit->GetData();
}

void VKStreamBuffer::ReserveWatches(std::size_t grow_size) {
    const std::size_t previous_size = watches.size();
    watches.resize(previous_size + grow_size);
    std::generate(watches.begin() + previous_size, watches.end(),
                  []() { return std::make_unique<VKFenceWatch>(); });
}

} // namespace Vulkan
