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
 * hardware-adaptive chunks with pluggable memory allocator support.
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
 * aligned to `Hardware::cache_line_size()` (or at least 64 bytes) and sized to
 * exact integer multiples of OS pages.
 * 4. **Pluggable Hardware Allocators (GPU-Direct VRAM / Unified Memory):**
 * `ChunkPool` accepts optional custom allocation and deallocation callbacks
 * (`ChunkAllocFn`, `ChunkFreeFn`). On unified memory architectures (Apple
 * Silicon, AMD APUs) or discrete GPU setups, consumers can back chunk buffers
 * with host-visible Vulkan/Metal `VkBuffer` or unified VRAM memory. This
 * enables 0.00 µs zero-copy compute and render passes without host-to-device
 * PCI-e transfers. When omitted, allocations cleanly default to
 * `Hardware::allocate_aligned`.
 */
class ChunkPool {

public:
  /**
   * @brief Custom memory allocator callback signature.
   *
   * @details
   * Must allocate and return a pointer to at least `bytes` of contiguous
   * physical memory, aligned to `alignment` bytes (which is guaranteed to be at
   * least 64 bytes).
   *
   * @param bytes Total buffer allocation size in bytes.
   * @param alignment Required memory alignment in bytes.
   * @return Raw pointer to the allocated memory buffer, or nullptr on failure.
   */
  using ChunkAllocFn = void *(*)(size_t bytes, size_t alignment);

  /**
   * @brief Custom memory deallocator callback signature.
   *
   * @details
   * Must reclaim memory previously allocated via the corresponding
   * `ChunkAllocFn`.
   *
   * @param ptr Pointer to the memory buffer to release.
   * @param bytes Exact byte size of the buffer originally requested.
   */
  using ChunkFreeFn = void (*)(void *ptr, size_t bytes);

private:
  // --- Member Data -----------------------------------------------------------

  /// Page-aligned chunk buffer size in bytes.
  size_t chunk_size_{65536};

  /// Total number of chunks allocated from host OS or custom allocator.
  size_t total_allocated_{0};

  /// LIFO stack of available recycled chunk pointers.
  std::vector<void *> free_list_;

  /// Active memory allocation callback (custom hook or
  /// Hardware::allocate_aligned).
  ChunkAllocFn alloc_fn_{nullptr};

  /// Active memory deallocation callback (custom hook or
  /// Hardware::free_aligned).
  ChunkFreeFn free_fn_{nullptr};

public:
  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Initializes the pool, configures optional memory allocators, and
   * pre-allocates an initial reserve of chunks.
   *
   * @param chunk_size Desired chunk buffer capacity in bytes (automatically
   * rounded to page multiple).
   * @param initial_preallocate Number of chunk buffers to allocate eagerly.
   * @param alloc_fn Optional custom allocator callback. If nullptr, defaults to
   * `Hardware::allocate_aligned`.
   * @param free_fn Optional custom deallocator callback. If nullptr, defaults
   * to `Hardware::free_aligned`.
   */
  explicit ChunkPool(
      size_t chunk_size = 65536,
      size_t initial_preallocate = 16,
      ChunkAllocFn alloc_fn = nullptr,
      ChunkFreeFn free_fn = nullptr
  )
      : chunk_size_(Hardware::optimal_chunk_size(chunk_size)),
        alloc_fn_(
            alloc_fn ? alloc_fn
                     : [](size_t b,
                          size_t a) { return Hardware::allocate_aligned(b, a); }
        ),
        free_fn_(free_fn ? free_fn : [](void *p, size_t) {
          Hardware::free_aligned(p);
        }) {
    preallocate(initial_preallocate);
  }

  /**
   * @brief Releases all pooled chunk memory via the active deallocator
   * callback.
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

  /**
   * @brief Configures custom allocation and deallocation callbacks (e.g. for
   * GPU-Direct VRAM).
   *
   * @details
   * Allows redirecting chunk allocations to host-visible Vulkan/Metal unified
   * memory or custom slab arenas.
   *
   * @param alloc_fn Custom allocator callback conforming to `ChunkAllocFn`.
   * @param free_fn Custom deallocator callback conforming to `ChunkFreeFn`.
   *
   * @note Must only be called when the pool is completely empty (no active or
   * free chunks).
   */
  inline void set_allocator(
      ChunkAllocFn alloc_fn,
      ChunkFreeFn free_fn
  ) noexcept {
    assert(
        free_list_.empty() && total_allocated_ == 0 &&
        "Cannot change allocator on active pool"
    );
    alloc_fn_ = alloc_fn;
    free_fn_ = free_fn;
  }

  // --- Buffer Acquisition & Recycling ----------------------------------------

  /**
   * @brief Acquires a page-aligned chunk buffer from the pool in O(1) time.
   *
   * @details
   * - Fast Path: Pops a recycled, cache-warm buffer from the free list.
   * - Slow Path: Allocates a new aligned buffer via the active allocator if the
   * pool is empty.
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
   * @brief Frees all pooled chunk buffers via the active deallocator callback
   * and resets allocation counters.
   */
  inline void clear() noexcept {
    for (void *ptr : free_list_) {
      free_fn_(ptr, chunk_size_);
    }
    free_list_.clear();
    total_allocated_ = 0;
  }

private:
  // --- Internal Allocation Helpers -------------------------------------------

  /**
   * @brief Allocates an aligned buffer via the active allocator callback.
   */
  [[nodiscard]] inline void *allocate_raw_buffer() const {
    const size_t alignment = std::max(
        Hardware::cache_line_size(), static_cast<size_t>(64)
    );
    void *ptr = alloc_fn_(chunk_size_, alignment);
    assert(
        ptr != nullptr && "Allocator failed to allocate memory for ChunkPool"
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
