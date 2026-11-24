module;

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

export module arche:chunk_pool.detail.impl;

import :chunk_pool;

export namespace arche {

// =============================================================================
// ChunkPool Lifecycle & Memory Allocation Implementations
// =============================================================================

ChunkPool::ChunkPool(
    size_t chunk_size,
    size_t initial_reserve,
    ChunkAllocFn custom_alloc,
    ChunkFreeFn custom_free
)
    : chunk_size_(Hardware::optimal_chunk_size(chunk_size)),
      alloc_fn_(custom_alloc), free_fn_(custom_free) {
  // 1. Preallocate reserved blocks into free list if requested.
  if (initial_reserve > 0) {
    reserve(initial_reserve);
  }
}

ChunkPool::~ChunkPool() {
  // 1. Release all pooled blocks back to allocator.
  purge();
}

ChunkPool::ChunkPool(ChunkPool &&other) noexcept
    : chunk_size_(other.chunk_size_),
      total_allocated_(other.total_allocated_),
      free_list_(std::move(other.free_list_)), alloc_fn_(other.alloc_fn_),
      free_fn_(other.free_fn_) {
  // 1. Reset source instance telemetry counters.
  other.total_allocated_ = 0;
  other.alloc_fn_ = nullptr;
  other.free_fn_ = nullptr;
}

ChunkPool &ChunkPool::operator=(ChunkPool &&other) noexcept {
  if (this != &other) {
    // 1. Purge existing pooled blocks.
    purge();

    // 2. Transfer ownership of memory and configuration state.
    chunk_size_ = other.chunk_size_;
    total_allocated_ = other.total_allocated_;
    free_list_ = std::move(other.free_list_);
    alloc_fn_ = other.alloc_fn_;
    free_fn_ = other.free_fn_;

    // 3. Clear source telemetry.
    other.total_allocated_ = 0;
    other.alloc_fn_ = nullptr;
    other.free_fn_ = nullptr;
  }
  return *this;
}

// =============================================================================
// Block Allocation & Recycling Implementations
// =============================================================================

void *ChunkPool::acquire() {
  // 1. Fast path: recycle recently freed block from LIFO free list.
  if (!free_list_.empty()) {
    void *ptr = free_list_.back();
    free_list_.pop_back();
    return ptr;
  }

  // 2. Slow path: allocate fresh page-aligned memory block.
  void *ptr = allocate_block();
  total_allocated_++;
  return ptr;
}

void ChunkPool::release(void *ptr) {
  // 1. Ignore null pointers safely.
  if (ptr == nullptr) {
    return;
  }

  // 2. Return block to LIFO free list for immediate reuse.
  free_list_.push_back(ptr);
}

void ChunkPool::reserve(size_t count) {
  // 1. Reserve vector capacity to avoid reallocation during warm-up.
  free_list_.reserve(free_list_.size() + count);

  // 2. Preallocate aligned chunk pages and populate free list.
  for (size_t i = 0; i < count; ++i) {
    free_list_.push_back(allocate_block());
    total_allocated_++;
  }
}

void ChunkPool::purge() {
  // 1. Deallocate each cached block back to the underlying allocator.
  for (void *ptr : free_list_) {
    deallocate_block(ptr);
  }

  // 2. Clear free list and reset allocation tally.
  free_list_.clear();
  total_allocated_ = 0;
}

// =============================================================================
// Capacity & Telemetry Implementations
// =============================================================================

size_t ChunkPool::chunk_size() const noexcept { return chunk_size_; }
size_t ChunkPool::available() const noexcept { return free_list_.size(); }
size_t ChunkPool::allocated() const noexcept { return total_allocated_; }

size_t ChunkPool::in_use() const noexcept {
  return total_allocated_ - free_list_.size();
}

void ChunkPool::set_allocator(
    ChunkAllocFn alloc_fn,
    ChunkFreeFn free_fn
) noexcept {
  alloc_fn_ = alloc_fn;
  free_fn_ = free_fn;
}

// =============================================================================
// Low-Level Memory Helpers
// =============================================================================

void *ChunkPool::allocate_block() {
  // 1. Query hardware cache line alignment boundary (typically 64 bytes).
  const size_t alignment = Hardware::cache_line_size();

  // 2. Invoke custom allocator if registered; otherwise fallback to aligned heap.
  if (alloc_fn_) {
    return alloc_fn_(chunk_size_, alignment);
  }
  return Hardware::allocate_aligned(chunk_size_, alignment);
}

void ChunkPool::deallocate_block(void *ptr) {
  // 1. Guard against null pointer deallocation.
  if (ptr == nullptr) {
    return;
  }

  // 2. Invoke custom free if registered; otherwise fallback to hardware free.
  if (free_fn_) {
    free_fn_(ptr, chunk_size_);
    return;
  }
  Hardware::free_aligned(ptr);
}

} // namespace arche
