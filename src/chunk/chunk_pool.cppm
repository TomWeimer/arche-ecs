module;

#include <cstddef>
#include <vector>

export module arche:chunk_pool;

import :hardware;

export namespace arche {

// =============================================================================
// ChunkPool: Slab Memory Manager for 64KB Chunk Pages
// =============================================================================

/**
 * @class ChunkPool
 * @brief High-performance slab allocator managing reusable 64KB page-aligned
 * chunk memory.
 *
 * `ChunkPool` acts as a specialized L1 slab allocator for chunk memory blocks.
 * It maintains a pool of 64KB cache-line-aligned blocks (typically 65,536
 * bytes) to eliminate runtime dynamic allocation overhead and OS page-fault
 * latency during entity spawns and archetype transitions.
 *
 * ### Performance Characteristics:
 * - O(1) block acquisition and recycling via a LIFO free-list.
 * - Hardware cache line alignment (64 bytes) for SIMD vectorization.
 * - Support for pluggable custom allocators (e.g. huge pages, GPU shared
 * memory).
 *
 * ### Thread Safety:
 * `ChunkPool` instances are non-synchronized. Synchronization is maintained at
 * the World or frame barrier level during structural mutation phases.
 */
class ChunkPool {
public:
  /// Function pointer type for custom block allocation: void*(size_t bytes,
  /// size_t alignment).
  using ChunkAllocFn = void *(*)(size_t bytes, size_t alignment);

  /// Function pointer type for custom block deallocation: void(void *ptr,
  /// size_t bytes).
  using ChunkFreeFn = void (*)(void *ptr, size_t bytes);

private:
  size_t chunk_size_{65536};
  size_t total_allocated_{0};
  std::vector<void *> free_list_;
  ChunkAllocFn alloc_fn_{nullptr};
  ChunkFreeFn free_fn_{nullptr};

public:
  // --- Lifecycle & Construction ----------------------------------------------

  /**
   * @brief Constructs a ChunkPool with an optimal chunk page size and optional
   * reservation.
   * @param chunk_size Baseline chunk page size in bytes (default: 64 KB).
   * @param initial_reserve Preallocated block count reserved immediately on
   * construction.
   * @param custom_alloc Optional custom allocation delegate.
   * @param custom_free Optional custom deallocation delegate.
   */
  explicit ChunkPool(
      size_t chunk_size = 65536,
      size_t initial_reserve = 0,
      ChunkAllocFn custom_alloc = nullptr,
      ChunkFreeFn custom_free = nullptr
  );

  /**
   * @brief Destructs the ChunkPool, freeing all cached blocks back to the OS or
   * custom allocator.
   */
  ~ChunkPool();

  ChunkPool(const ChunkPool &) = delete;
  ChunkPool &operator=(const ChunkPool &) = delete;

  ChunkPool(ChunkPool &&other) noexcept;
  ChunkPool &operator=(ChunkPool &&other) noexcept;

  // --- Block Allocation & Recycling ------------------------------------------

  /**
   * @brief Acquires a 64KB aligned memory block from the pool or allocates a
   * new one.
   * @return Aligned memory pointer to the acquired chunk page.
   */
  [[nodiscard]] void *acquire();

  /**
   * @brief Returns a previously acquired chunk memory block to the pool for
   * reuse.
   * @param ptr Pointer to the chunk block (safe to call with nullptr).
   */
  void release(void *ptr);

  /**
   * @brief Preallocates a specified number of chunk blocks into the free list.
   * @param count Number of 64KB blocks to preallocate.
   */
  void reserve(size_t count);

  /**
   * @brief Releases all cached blocks in the free list back to the OS or custom
   * allocator.
   */
  void purge();

  // --- Capacity & Telemetry --------------------------------------------------

  /// Returns the configured chunk size in bytes (e.g. 65536).
  [[nodiscard]] size_t chunk_size() const noexcept;

  /// Returns the number of ready-to-use recycled blocks currently in the free
  /// list.
  [[nodiscard]] size_t available() const noexcept;

  /// Returns the cumulative number of chunk blocks allocated over the pool's
  /// lifetime.
  [[nodiscard]] size_t allocated() const noexcept;

  /// Returns the number of chunk blocks currently active and checked out of the
  /// pool.
  [[nodiscard]] size_t in_use() const noexcept;

  // --- Allocator Delegation --------------------------------------------------

  /**
   * @brief Replaces default OS heap allocation with custom memory handlers.
   * @param alloc_fn Custom memory allocation delegate.
   * @param free_fn Custom memory deallocation delegate.
   */
  void set_allocator(ChunkAllocFn alloc_fn, ChunkFreeFn free_fn) noexcept;

private:
  [[nodiscard]] void *allocate_block();
  void deallocate_block(void *ptr);
};

} // namespace arche
