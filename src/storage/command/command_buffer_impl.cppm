module;

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

export module arche:command.detail.impl;

import :command;

export namespace arche {

// =============================================================================
// ParallelCommandBuffer Implementations
// =============================================================================

namespace internal {

ParallelCommandBuffer::ParallelCommandBuffer(uint32_t thread_count)
    : buffers_(thread_count) {}

uint32_t ParallelCommandBuffer::thread_count() const noexcept {
  return static_cast<uint32_t>(buffers_.size());
}

CommandBuffer &ParallelCommandBuffer::get(uint32_t thread_id) noexcept {
  assert(thread_id < buffers_.size() && "Worker thread ID out of range");
  return buffers_[thread_id];
}

std::span<CommandBuffer> ParallelCommandBuffer::buffers() noexcept {
  return buffers_;
}

std::span<const CommandBuffer> ParallelCommandBuffer::buffers() const noexcept {
  return buffers_;
}

void ParallelCommandBuffer::clear() noexcept {
  for (auto &buf : buffers_) {
    buf.clear();
  }
}

} // namespace internal

} // namespace arche
