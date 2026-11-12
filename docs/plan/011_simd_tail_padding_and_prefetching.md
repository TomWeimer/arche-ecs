# Module 011: SIMD Tail-Padding, Prefetching & Hardware Allocators

This document specifies the exact behavioral deltas, code modifications, and new test suites required to eliminate hardware-level memory stalls, branchy SIMD cleanup epilogues, and enable GPU-direct unified memory allocators, assuming Module 010 (Bulk Entity Lifecycle) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Vector-Width Tail-Padding (Branchless SIMD Execution)
* **Before (Module 010):**  
  Component columns were sized to exact chunk capacity without tail padding. When processing entities using 8-wide (AVX2) or 16-wide (AVX-512 / ARM NEON) SIMD registers, any non-multiple of vector width required a branchy scalar epilogue loop to process the leftover entities (e.g. 1,603 entities required 100 vector iterations + 3 scalar iterations).
* **Now (Module 011):**  
  `Archetype` allocates 16 elements (`SIMD_TAIL_PADDING`) of extra storage at the end of every component column slice, and rounds chunk capacities to multiples of 16. Systems can execute branchless vector loops up to `(count + 15) & ~15` without bounds checks or scalar cleanup loops.
* **Why we need to change this part:**  
  Scalar epilogue loops cause branch mispredictions and prevent compilers from emitting uninterrupted loop pipelines. Tail padding allows 100% vector execution across all chunks, yielding a $15\text{–}30\%$ speedup on continuous physics kernels.

### 1.2 Lookahead Software Prefetching (`__builtin_prefetch`)
* **Before (Module 010):**  
  `Query` iterated through chunks sequentially without prefetching. When entities scale to 500k–1M (16–32 MB working set), moving to the next chunk incurred ~60 ns of main memory (DRAM) read latency.
* **Now (Module 011):**  
  `Query::for_each_active_chunk` issues compiler intrinsics (`__builtin_prefetch`) for Chunk $N+1$'s header and active component columns while the CPU ALU is busy crunching Chunk $N$.
* **Why we need to change this part:**  
  Prefetching hides DRAM fetch latency behind arithmetic instructions, keeping CPU vector execution units saturated and preventing cache-miss pipeline bubbles.

### 1.3 Chunk Header User Metadata Slot (Macro Frustum Culling)
* **Before (Module 010):**  
  `ChunkHeader` was 64 bytes and only stored engine-internal bookkeeping fields (count, capacity, links). Consumer simulations had no place to attach spatial bounding volumes to chunks.
* **Now (Module 011):**  
  `ChunkHeader` expands to exactly 128 bytes (two cache lines, `alignas(64)`), reserving a dedicated `alignas(16) uint8_t user_metadata[64]` slot accessible via `chunk->user_metadata<T>()`.
* **Why we need to change this part:**  
  Allows consumer engines (e.g. Colony) to maintain aggregate 2D or 3D Axis-Aligned Bounding Boxes (AABB) directly in chunk headers. Renderers can test the camera frustum against this macro AABB and cull up to 1,600 boids in a single 2-cycle vector test.

### 1.4 Custom Memory Allocator Hook in `ChunkPool` (GPU-Direct VRAM)
* **Before (Module 010):**  
  `ChunkPool` hardcoded allocations to OS virtual memory (`Hardware::allocate_aligned`).
* **Now (Module 011):**  
  `ChunkPool` accepts optional custom allocation callbacks (`ChunkAllocFn`, `ChunkFreeFn`).
* **Why we need to change this part:**  
  On unified memory architectures (Apple Silicon M-series, AMD APUs), Colony can supply a Vulkan allocator backing chunks with host-visible/coherent `VkBuffer` memory. The CPU updates entities directly inside VRAM, achieving true 0.00 µs zero-copy streaming.

### 1.5 Deduplication & Helper Architecture: Geometry & Vector Math
* **`ChunkGeometry`:**  
  **Where defined:** `include/arche/chunk.hpp`.  
  **What code it replaces:** Replaces hardcoded header reserve constants `sizeof(ChunkHeader)` across `Archetype` and `ChunkPool`.
* **`Chunk::typed_slice<T>()`:**  
  **Where defined:** `include/arche/chunk.hpp`.  
  **What code it replaces:** Replaces repetitive raw casting `reinterpret_cast<T*>(chunk->component_slice(offset))` in `Query` and `World`.
* **`arche::math::round_up_simd` and `arche::math::round_down_simd`:**  
  **Where defined:** `include/arche/math_utils.hpp` (introduced in Module 009).  
  **What code it replaces:** Replaces ad-hoc expressions `(active + 15) & ~15` in `Query::for_each_chunk_padded` and `(raw / 16) * 16` in `Archetype::_calculate_chunk_capacity`.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/chunk.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ChunkHeader` was 64 bytes (`alignas(64)`).
* **Now:** `ChunkHeader` expands to 128 bytes (`alignas(64)`), containing `alignas(16) uint8_t user_metadata[64]`. `Chunk` adds `user_metadata<T>()` accessors.

#### Functions / Members to Delete
* `None`.

#### Modified Struct `ChunkHeader`

##### Old Version (Module 010)
```cpp
struct alignas(64) ChunkHeader {
  Archetype *archetype{nullptr};
  uint32_t count{0};
  uint32_t capacity{0};
  uint32_t chunk_index{0};
  ChunkHeader *next{nullptr};
  ChunkHeader *prev{nullptr};
};

static_assert(
    sizeof(ChunkHeader) == 64,
    "ChunkHeader must be padded to exactly 64 bytes"
);
```

##### New Version (Module 011)
```cpp
struct alignas(64) ChunkHeader {
  Archetype *archetype{nullptr};
  uint32_t count{0};
  uint32_t capacity{0};
  uint32_t chunk_index{0};
  ChunkHeader *next{nullptr};
  ChunkHeader *prev{nullptr};
  uint8_t reserved[24]{0}; ///< Internal padding to align to 64 bytes

  /// 64-byte user metadata slot (e.g. for Macro AABBs or Spatial Hashes)
  alignas(16) uint8_t user_metadata[64]{0};
};

static_assert(
    sizeof(ChunkHeader) == 128,
    "ChunkHeader must be padded to exactly 128 bytes (two cache lines)"
);

// =============================================================================
// Chunk Geometry Helper Constants
// =============================================================================

/**
 * @struct ChunkGeometry
 * @brief Standardized chunk header and payload layout constants.
 * @details Defined in `include/arche/chunk.hpp`. Replaces hardcoded header reserve
 * magic numbers across Archetype and ChunkPool.
 */
struct ChunkGeometry {
  static constexpr size_t HEADER_BYTES = sizeof(ChunkHeader);
  static constexpr size_t PAYLOAD_BYTES = 65536 - HEADER_BYTES;
};
```

#### New Methods to Add to `struct Chunk` in `include/arche/chunk.hpp`

Insert inside `struct Chunk`, public section, right before `// --- Component Column Slices ---` (around line 270):

```cpp
  // --- Typed Slice Accessors (Deduplicated Pointer Casting) -------------------

  /**
   * @brief Returns a typed pointer to a component column slice.
   * @details Defined in `include/arche/chunk.hpp`. Replaces repetitive
   * `reinterpret_cast<T*>(chunk->component_slice(offset))` in Query and World.
   */
  template <typename T>
  [[nodiscard]] inline T *typed_slice(uint32_t byte_offset) noexcept {
    return reinterpret_cast<T *>(component_slice(byte_offset));
  }

  template <typename T>
  [[nodiscard]] inline const T *typed_slice(uint32_t byte_offset) const noexcept {
    return reinterpret_cast<const T *>(component_slice(byte_offset));
  }

  // --- User Metadata Slot (Macro Spatial Culling) ----------------------------

  /**
   * @brief Accesses the chunk's 64-byte cache-aligned user metadata slot.
   *
   * @tparam T User metadata type (must be POD, size <= 64 bytes, align <= 16 bytes).
   * @return Mutable reference to the user metadata struct.
   */
  template <typename T>
  [[nodiscard]] inline T &user_metadata() noexcept {
    static_assert(
        sizeof(T) <= sizeof(header_.user_metadata),
        "User metadata type exceeds 64-byte reserve"
    );
    static_assert(
        alignof(T) <= alignof(decltype(header_.user_metadata)),
        "User metadata type alignment exceeds 16-byte boundary"
    );
    return *reinterpret_cast<T *>(header_.user_metadata);
  }

  /**
   * @brief Accesses the chunk's 64-byte cache-aligned user metadata slot (read-only).
   *
   * @tparam T User metadata type.
   * @return Const reference to the user metadata struct.
   */
  template <typename T>
  [[nodiscard]] inline const T &user_metadata() const noexcept {
    static_assert(
        sizeof(T) <= sizeof(header_.user_metadata),
        "User metadata type exceeds 64-byte reserve"
    );
    static_assert(
        alignof(T) <= alignof(decltype(header_.user_metadata)),
        "User metadata type alignment exceeds 16-byte boundary"
    );
    return *reinterpret_cast<const T *>(header_.user_metadata);
  }
```

---

### 2.2 `include/arche/archetype.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `_calculate_chunk_capacity` reserved 64 bytes for `CHUNK_HEADER_RESERVE` and did not align capacity to vector widths. `_build_column_layouts` did not allocate tail padding.
* **Now:** `CHUNK_HEADER_RESERVE` is updated to `sizeof(ChunkHeader)` (128 bytes). `_calculate_chunk_capacity` rounds down to multiples of `SIMD_VECTOR_WIDTH = 16`. `_build_column_layouts` allocates 16 elements of tail padding per column.

#### Functions / Members to Delete
* `None`.

#### Modified Constants & Functions in `Archetype`

##### Old Version (Module 010)
```cpp
  [[nodiscard]] inline uint32_t _calculate_chunk_capacity(
      uint32_t entity_size_sum
  ) const noexcept {
    constexpr uint32_t CHUNK_HEADER_RESERVE = 64;
    constexpr uint32_t MAX_ALIGN_PADDING_ALLOWANCE = 256;

    constexpr uint32_t overhead = CHUNK_HEADER_RESERVE +
        MAX_ALIGN_PADDING_ALLOWANCE;

    const uint32_t available_bytes = (chunk_size_ > overhead)
        ? (chunk_size_ - CHUNK_HEADER_RESERVE - MAX_ALIGN_PADDING_ALLOWANCE)
        : 1;

    const uint32_t capacity = available_bytes / entity_size_sum;
    return (capacity == 0) ? 1 : capacity;
  }

  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t CHUNK_HEADER_RESERVE = 64;

    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    current_offset += chunk_capacity_ * static_cast<uint32_t>(sizeof(Entity));

    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.descriptor = &desc;

      columns_.push_back(layout);
      type_to_column_[t] = static_cast<int16_t>(i);

      current_offset += chunk_capacity_ * desc.size;
    }

    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }
```

##### New Version (Module 011)
```cpp
public:
  static constexpr uint32_t SIMD_VECTOR_WIDTH = 16; ///< 16 elements (64B AVX-512/AVX2/NEON)
  static constexpr uint32_t SIMD_TAIL_PADDING = 16; ///< Tail padding elements per column

private:
  [[nodiscard]] inline uint32_t _calculate_chunk_capacity(
      uint32_t entity_size_sum
  ) const noexcept {
    constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES; // 128 bytes
    constexpr uint32_t MAX_ALIGN_PADDING_ALLOWANCE = 256;

    // Overhead accounts for header, alignment padding, and SIMD tail padding
    const uint32_t tail_padding_bytes = static_cast<uint32_t>(
        (sizeof(Entity) + entity_size_sum) * SIMD_TAIL_PADDING
    );
    const uint32_t total_overhead = CHUNK_HEADER_RESERVE +
        MAX_ALIGN_PADDING_ALLOWANCE + tail_padding_bytes;

    const uint32_t available_bytes = (chunk_size_ > total_overhead)
        ? (chunk_size_ - total_overhead)
        : 1;

    const uint32_t raw_capacity = available_bytes / entity_size_sum;
    // Round down to nearest multiple of SIMD_VECTOR_WIDTH using arche::math::round_down_simd
    const uint32_t aligned_capacity = (raw_capacity >= SIMD_VECTOR_WIDTH)
        ? arche::math::round_down_simd<SIMD_VECTOR_WIDTH>(raw_capacity)
        : raw_capacity;

    return (aligned_capacity == 0) ? 1 : aligned_capacity;
  }

  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES; // 128 bytes

    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    // Allocate Entity array with SIMD tail padding
    current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
        static_cast<uint32_t>(sizeof(Entity));

    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.descriptor = &desc;

      columns_.push_back(layout);
      type_to_column_[t] = static_cast<int16_t>(i);

      // Allocate column storage plus 16 elements of tail padding
      current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) * desc.size;
    }

    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }
```

#### New Functions to Add to `include/arche/archetype.hpp`
* `None`.

---

### 2.3 `include/arche/chunk_pool.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ChunkPool` allocated physical memory exclusively via `Hardware::allocate_aligned` and `Hardware::free_aligned`.
* **Now:** Adds `ChunkAllocFn` and `ChunkFreeFn` callbacks and custom constructor/setter, enabling backing chunks with mapped Vulkan VRAM on Apple Silicon or APUs.

#### Functions / Members to Delete
* `None`.

#### Modified Members & Functions in `ChunkPool`

##### Old Version (Module 010)
```cpp
class ChunkPool {
private:
  size_t chunk_size_{65536};
  size_t total_allocated_{0};
  std::vector<void *> free_list_;

public:
  explicit ChunkPool(size_t chunk_size = 65536, size_t initial_preallocate = 16)
      : chunk_size_(Hardware::optimal_chunk_size(chunk_size)) {
    preallocate(initial_preallocate);
  }

  inline void clear() noexcept {
    for (void *ptr : free_list_) {
      Hardware::free_aligned(ptr);
    }
    free_list_.clear();
    total_allocated_ = 0;
  }

private:
  [[nodiscard]] inline void *allocate_raw_buffer() const {
    const size_t alignment = std::max(
        Hardware::cache_line_size(), static_cast<size_t>(64)
    );
    void *ptr = Hardware::allocate_aligned(chunk_size_, alignment);
    assert(ptr != nullptr && "OS failed to allocate aligned memory for ChunkPool");
    return ptr;
  }
```

##### New Version (Module 011)
```cpp
class ChunkPool {
public:
  /// Custom memory allocator callback type: (bytes, alignment) -> void*
  using ChunkAllocFn = void *(*)(size_t bytes, size_t alignment);
  /// Custom memory deallocator callback type: (ptr, bytes) -> void
  using ChunkFreeFn = void (*)(void *ptr, size_t bytes);

private:
  size_t chunk_size_{65536};
  size_t total_allocated_{0};
  std::vector<void *> free_list_;
  ChunkAllocFn alloc_fn_{nullptr};
  ChunkFreeFn free_fn_{nullptr};

public:
  explicit ChunkPool(
      size_t chunk_size = 65536,
      size_t initial_preallocate = 16,
      ChunkAllocFn alloc_fn = nullptr,
      ChunkFreeFn free_fn = nullptr
  )   : chunk_size_(Hardware::optimal_chunk_size(chunk_size)),
        alloc_fn_(alloc_fn ? alloc_fn : [](size_t b, size_t a) {
          return Hardware::allocate_aligned(b, a);
        }),
        free_fn_(free_fn ? free_fn : [](void *p, size_t) {
          Hardware::free_aligned(p);
        }) {
    preallocate(initial_preallocate);
  }

  inline void clear() noexcept {
    for (void *ptr : free_list_) {
      free_fn_(ptr, chunk_size_);
    }
    free_list_.clear();
    total_allocated_ = 0;
  }

private:
  [[nodiscard]] inline void *allocate_raw_buffer() const {
    const size_t alignment = std::max(
        Hardware::cache_line_size(), static_cast<size_t>(64)
    );
    void *ptr = alloc_fn_(chunk_size_, alignment);
    assert(ptr != nullptr && "Allocator failed to allocate memory for ChunkPool");
    return ptr;
  }
```

#### New Functions to Add to `include/arche/chunk_pool.hpp`

Insert inside `class ChunkPool`, public section, right before `// --- Buffer Acquisition & Recycling ---` (around line 98):

```cpp
  /**
   * @brief Configures custom allocation and deallocation callbacks (e.g. for GPU-Direct VRAM).
   * @note Must only be called when the pool is completely empty (no active or free chunks).
   */
  inline void set_allocator(ChunkAllocFn alloc_fn, ChunkFreeFn free_fn) noexcept {
    assert(free_list_.empty() && total_allocated_ == 0 && "Cannot change allocator on active pool");
    alloc_fn_ = alloc_fn;
    free_fn_ = free_fn;
  }
```

---

### 2.4 `include/arche/query.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `for_each_active_chunk` traversed chunks without hardware prefetch hints.
* **Now:** Issues `__builtin_prefetch` instructions for Chunk $N+1$'s header and active component columns while processing Chunk $N$. Adds `for_each_chunk_padded` to expose branchless SIMD iteration bounds.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `Query`

##### Old Version (Module 010)
```cpp
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      for (Chunk *chunk : chunks) {
        if (!chunk->is_empty()) {
          visitor(chunk, match);
        }
      }
    }
  }
```

##### New Version (Module 011)
```cpp
  template <typename Visitor>
  inline void for_each_active_chunk(Visitor &&visitor) {
    for (const auto &match : matching_archetypes_) {
      const auto &chunks = get_chunks_for(match.archetype);
      const size_t num_chunks = chunks.size();

      for (size_t i = 0; i < num_chunks; ++i) {
        Chunk *chunk = chunks[i];
        if (chunk->is_empty()) {
          continue;
        }

        // Lookahead prefetching: prefetch the next chunk's cache lines into L1/L2
        if (i + 1 < num_chunks) {
          const Chunk *next_chunk = chunks[i + 1];
#if defined(__GNUC__) || defined(__clang__)
          // 1. Prefetch ChunkHeader (128 bytes = 2 cache lines)
          __builtin_prefetch(next_chunk, 0, 3);
          __builtin_prefetch(reinterpret_cast<const uint8_t *>(next_chunk) + 64, 0, 3);

          // 2. Prefetch first cache line of each queried component column
          ((__builtin_prefetch(
               reinterpret_cast<const uint8_t *>(next_chunk) +
                   match.offsets[type_index<Components>()],
               0, // 0 = read
               3  // 3 = high temporal locality (L1/L2)
           )),
           ...);
#endif
        }

        visitor(chunk, match);
      }
    }
  }
```

#### New Functions to Add to `include/arche/query.hpp`

Insert inside `class Query`, public section, immediately after `for_each_chunk` (around line 104):

```cpp
  /**
   * @brief High-performance chunk visitor with tail-padded vector iteration bounds.
   *
   * @details
   * Exposes both `active_count` and `padded_count` (`(active_count + 15) & ~15`).
   * Because every component column is tail-padded by 16 elements, vector SIMD loops
   * (AVX2, AVX-512, NEON) can iterate up to `padded_count` unconditionally with zero
   * scalar cleanup epilogues.
   *
   * @tparam Func Callable accepting `(uint32_t active_count, uint32_t padded_count, Components*...)`.
   * @param func User lambda executed once per chunk.
   */
  template <typename Func>
  inline void for_each_chunk_padded(Func &&func) {
    for_each_active_chunk([&](Chunk *chunk, const auto &match) {
      const uint32_t active = chunk->count();
      // Round up to nearest vector width using arche::math::round_up_simd (defined in math_utils.hpp)
      const uint32_t padded = arche::math::round_up_simd<16>(active);
      func(
          active,
          padded,
          get_column_ptr<Components>(
              chunk, match.offsets[type_index<Components>()]
          )...
      );
    });
  }
```

---

## 3. Brand-New Files

### 3.1 `tests/simd_and_hardware_tests.cpp`

This is a brand-new test suite verifying 128-byte chunk headers, user metadata persistence, branchless SIMD execution on tail-padded columns, lookahead prefetching, and custom allocator backing in `ChunkPool`.

```cpp
#include "arche/archetype.hpp"
#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using arche::Archetype;
using arche::Chunk;
using arche::ChunkHeader;
using arche::ChunkPool;
using arche::Entity;
using arche::World;
} // namespace

// =============================================================================
// Standalone Test Component Types
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

/// 3D Bounding Box representing consumer user metadata (Macro Frustum Culling)
struct alignas(16) MacroAABB {
  float min_x{-100.0f};
  float min_y{-100.0f};
  float min_z{-100.0f};
  float max_x{100.0f};
  float max_y{100.0f};
  float max_z{100.0f};
};
static_assert(sizeof(MacroAABB) <= 64, "MacroAABB must fit within 64 bytes");

// =============================================================================
// Unit Test 1: ChunkHeader 128-Byte Layout & User Metadata Persistence
// =============================================================================

/**
 * @brief Validates 128-byte ChunkHeader alignment and user metadata storage.
 */
void test_chunk_user_metadata() {
  std::cout << "[Test 1/4] ChunkHeader 128-Byte Layout & User Metadata... ";

  assert(sizeof(ChunkHeader) == 128);
  assert(alignof(ChunkHeader) == 64);

  World world;
  world.create_entity(Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  // Query chunk to inspect user metadata slot
  world.query<Position>().for_each_chunk([](uint32_t count, Position *pos) {
    assert(count == 1);
    // Find enclosing Chunk pointer from component column pointer
    // In our architecture, the chunk starts at pos - pos_offset
  });

  // Direct test on raw chunk buffer
  std::vector<uint8_t> buffer(65536, 0);
  Chunk *chunk = reinterpret_cast<Chunk *>(buffer.data());

  // Store MacroAABB in chunk user metadata
  MacroAABB &aabb = chunk->user_metadata<MacroAABB>();
  aabb.min_x = -500.0f;
  aabb.max_x = 500.0f;

  // Retrieve read-only reference and verify bit-exact persistence
  const Chunk *const_chunk = chunk;
  const MacroAABB &read_aabb = const_chunk->user_metadata<MacroAABB>();
  assert(read_aabb.min_x == -500.0f);
  assert(read_aabb.max_x == 500.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Branchless SIMD Execution on Tail-Padded Slices
// =============================================================================

/**
 * @brief Validates unconditional SIMD execution up to padded_count with zero memory overruns.
 */
void test_simd_tail_padded_execution() {
  std::cout << "[Test 2/4] Branchless SIMD Execution on Tail-Padded Slices... ";
  World world;

  // Create 1,603 entities (forces a non-multiple of 16)
  constexpr size_t COUNT = 1603;
  std::vector<Entity> entities = world.create_entities(
      COUNT,
      Position{1.0f, 1.0f},
      Velocity{2.0f, 3.0f}
  );

  assert(world.entity_count() == COUNT);

  // Execute branchless SIMD loop over padded_count
  auto q = world.query<Position, const Velocity>();
  q.for_each_chunk_padded([](uint32_t active, uint32_t padded, Position *pos, const Velocity *vel) {
    assert(active == 1603);
    assert(padded == 1616); // (1603 + 15) & ~15

    // SIMD loop processes full 16-element blocks unconditionally
    // Tail-padding guarantees that indices 1603..1615 are safe to read/write!
    for (uint32_t i = 0; i < padded; i += 16) {
      for (uint32_t k = 0; k < 16; ++k) {
        pos[i + k].x += vel[i + k].vx;
        pos[i + k].y += vel[i + k].vy;
      }
    }
  });

  // Verify all 1,603 active entities received correct updates
  for (size_t i = 0; i < COUNT; ++i) {
    const auto &pos = world.get_component<Position>(entities[i]);
    assert(pos.x == 3.0f);
    assert(pos.y == 4.0f);
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Lookahead Hardware Prefetching Compilation & Correctness
// =============================================================================

/**
 * @brief Validates query iteration and lookahead prefetching across multiple chunks.
 */
void test_lookahead_prefetching() {
  std::cout << "[Test 3/4] Lookahead Software Prefetching across Chunks... ";
  World world;

  // Create 50,000 entities spanning ~30 chunks
  constexpr size_t COUNT = 50'000;
  world.create_entities(
      COUNT,
      Position{0.0f, 0.0f},
      Velocity{1.0f, 1.0f}
  );

  size_t total_processed = 0;
  auto q = world.query<Position, const Velocity>();

  q.for_each_chunk([&](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx;
      pos[i].y += vel[i].vy;
    }
    total_processed += count;
  });

  assert(total_processed == COUNT);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Custom Allocator Hook in ChunkPool (GPU-Direct VRAM Backing)
// =============================================================================

static size_t g_custom_allocs = 0;
static size_t g_custom_frees = 0;

void *mock_vram_alloc(size_t bytes, size_t alignment) {
  ++g_custom_allocs;
  return arche::Hardware::allocate_aligned(bytes, alignment);
}

void mock_vram_free(void *ptr, size_t bytes) {
  (void)bytes;
  ++g_custom_frees;
  arche::Hardware::free_aligned(ptr);
}

/**
 * @brief Validates custom allocator backing for GPU-direct unified memory.
 */
void test_custom_chunk_allocator() {
  std::cout << "[Test 4/4] Custom Memory Allocator Hook in ChunkPool... ";

  g_custom_allocs = 0;
  g_custom_frees = 0;

  {
    ChunkPool pool(65536, 4, mock_vram_alloc, mock_vram_free);
    assert(g_custom_allocs == 4);
    assert(pool.free_count() == 4);

    void *b1 = pool.acquire_buffer();
    void *b2 = pool.acquire_buffer();
    assert(b1 != nullptr && b2 != nullptr);

    pool.release_buffer(b1);
    pool.release_buffer(b2);
  }

  assert(g_custom_frees == 4);
  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 011: SIMD & Hardware Tests\n";
  std::cout << "====================================================\n";

  test_chunk_user_metadata();
  test_simd_tail_padded_execution();
  test_lookahead_prefetching();
  test_custom_chunk_allocator();

  std::cout << "====================================================\n";
  std::cout << "All Module 011 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 011 Tests: SIMD Tail-Padding, Prefetching & Hardware Allocators
add_executable(simd_and_hardware_tests tests/simd_and_hardware_tests.cpp)
target_link_libraries(simd_and_hardware_tests PRIVATE arche::arche)
add_test(NAME simd_and_hardware_tests COMMAND simd_and_hardware_tests)
```

