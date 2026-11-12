#pragma once

#include "arche/hardware.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace arche {

// =============================================================================
// Chunk Slab Allocator Pool
// =============================================================================

/**
 * @class ChunkPool
 * @brief High-performance Slab Allocator pool for page-aligned,
 * hardware-adaptive chunks.
 *
 * @details
 * ### Architectural Rationale:
 * Operating system virtual memory allocations (`posix_memalign`,
 * `VirtualAlloc`) incur expensive kernel mode transitions, page table mapping
 * locks, and TLB shootdowns. In dynamic simulations where entities frequently
 * spawn and despawn:
 * 1. **Zero-Allocation Steady State:** When an archetype empties a chunk, the
 * memory is returned to `ChunkPool`'s LIFO free-list rather than freed to the
 * OS.
 * 2. **L1/L2 Cache Warmth:** Re-acquiring a recently recycled chunk reuses
 * cache-warm physical memory, eliminating CPU page faults.
 * 3. **Page-Aligned Geometry:** Every chunk buffer in the pool is strictly
 * aligned to `Hardware::cache_line_size()` and sized to exact integer multiples
 * of OS pages.
 */
class ChunkPool {

private:
  // --- Member Data -----------------------------------------------------------

  /// Page-aligned chunk buffer size in bytes.
  size_t chunk_size_{65536};

  /// Total number of chunks allocated from host OS.
  size_t total_allocated_{0};

  /// LIFO stack of available recycled chunk pointers.
  std::vector<void *> free_list_;

public:
  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Initializes the pool and pre-allocates an initial reserve of chunks.
   *
   * @param chunk_size Desired chunk buffer capacity in bytes (automatically
   * rounded to page multiple).
   * @param initial_preallocate Number of chunk buffers to allocate eagerly.
   */
  explicit ChunkPool(size_t chunk_size = 65536, size_t initial_preallocate = 16)
      : chunk_size_(Hardware::optimal_chunk_size(chunk_size)) {
    preallocate(initial_preallocate);
  }

  /**
   * @brief Releases all pooled chunk memory back to the host operating system.
   */
  ~ChunkPool() { clear(); }

  // Non-copyable, non-movable: owns physical memory slab allocations
  ChunkPool(const ChunkPool &) = delete;
  ChunkPool &operator=(const ChunkPool &) = delete;
  ChunkPool(ChunkPool &&) = delete;
  ChunkPool &operator=(ChunkPool &&) = delete;

  // --- Pool Capacity & State Queries -----------------------------------------

  /**
   * @brief Returns the uniform byte size of each chunk buffer in the pool.
   */
  [[nodiscard]] inline size_t chunk_size() const noexcept {
    return chunk_size_;
  }

  /**
   * @brief Returns the number of recycled chunks currently available for
   * acquisition.
   */
  [[nodiscard]] inline size_t free_count() const noexcept {
    return free_list_.size();
  }

  /**
   * @brief Returns the cumulative number of chunks ever allocated by this pool.
   */
  [[nodiscard]] inline size_t total_allocated() const noexcept {
    return total_allocated_;
  }

  // --- Buffer Acquisition & Recycling ----------------------------------------

  /**
   * @brief Acquires a page-aligned chunk buffer from the pool in O(1) time.
   *
   * @details
   * - Fast Path: Pops a recycled, cache-warm buffer from the free list.
   * - Slow Path: Allocates a new aligned buffer from the host OS if the pool is
   * empty.
   *
   * @return Raw pointer to an aligned chunk buffer.
   */
  [[nodiscard]] inline void *acquire_buffer() {
    if (free_list_.empty()) {
      allocate_new_chunk();
    }

    void *ptr = free_list_.back();
    free_list_.pop_back();
    return ptr;
  }

  /**
   * @brief Recycles a chunk buffer back into the pool for future reuse.
   *
   * @param ptr Pointer to aligned chunk buffer previously allocated by this
   * pool.
   */
  inline void release_buffer(void *ptr) noexcept {
    if (!ptr) {
      return;
    }
    free_list_.push_back(ptr);
  }

  /**
   * @brief Preallocates a batch of chunk buffers to prevent OS allocations
   * during runtime.
   *
   * @param count Number of chunk buffers to allocate and add to the free list.
   */
  inline void preallocate(size_t count) {
    free_list_.reserve(free_list_.size() + count);
    for (size_t i = 0; i < count; ++i) {
      allocate_new_chunk();
    }
  }

  /**
   * @brief Frees all pooled chunk buffers directly to the OS and resets
   * allocation counters.
   */
  inline void clear() noexcept {
    for (void *ptr : free_list_) {
      Hardware::free_aligned(ptr);
    }
    free_list_.clear();
    total_allocated_ = 0;
  }

private:
  // --- Internal Allocation Helpers -------------------------------------------

  /**
   * @brief Allocates an aligned buffer from the operating system virtual
   * memory.
   */
  [[nodiscard]] inline void *allocate_raw_buffer() const {
    const size_t alignment = std::max(
        Hardware::cache_line_size(), static_cast<size_t>(64)
    );
    void *ptr = Hardware::allocate_aligned(chunk_size_, alignment);
    assert(
        ptr != nullptr && "OS failed to allocate aligned memory for ChunkPool"
    );
    return ptr;
  }

  /**
   * @brief Allocates a new aligned chunk and registers it in the free list.
   */
  inline void allocate_new_chunk() {
    void *ptr = allocate_raw_buffer();
    free_list_.push_back(ptr);
    ++total_allocated_;
  }
};

} // namespace arche
