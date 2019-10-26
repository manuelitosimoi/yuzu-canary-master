// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Service::NVFlinger {
class BufferQueue;
}

namespace Service::VI {

/// Represents a single display layer.
class Layer {
public:
    /// Constructs a layer with a given ID and buffer queue.
    ///
    /// @param id    The ID to assign to this layer.
    /// @param queue The buffer queue for this layer to use.
    ///
    Layer(u64 id, NVFlinger::BufferQueue& queue);
    ~Layer();

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    Layer(Layer&&) = default;
    Layer& operator=(Layer&&) = delete;

    /// Gets the ID for this layer.
    u64 GetID() const {
        return id;
    }

    /// Gets a reference to the buffer queue this layer is using.
    NVFlinger::BufferQueue& GetBufferQueue() {
        return buffer_queue;
    }

    /// Gets a const reference to the buffer queue this layer is using.
    const NVFlinger::BufferQueue& GetBufferQueue() const {
        return buffer_queue;
    }

private:
    u64 id;
    NVFlinger::BufferQueue& buffer_queue;
};

} // namespace Service::VI
