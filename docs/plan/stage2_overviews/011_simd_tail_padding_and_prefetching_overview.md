# Module 011: SIMD Tail-Padding, Prefetching & Hardware Allocators — Overview

## 1. Architectural Mission
At 100,000 to 1,000,000 entities, two hardware-level inefficiencies degrade loop throughput:
1. **Branchy Scalar Cleanup Loops (The Epilogue Penalty):** When a chunk has 1,603 active entities, an AVX2/AVX-512/NEON loop processes 1,600 entities in 8-wide or 16-wide vectors, leaving 3 entities for a branchy scalar cleanup loop.
2. **DRAM Fetch Latency Stalls:** A 1M entity dataset (16–32 MB) exceeds L2/L3 caches. Moving from one chunk to the next introduces ~60 ns of DRAM latency.
3. **Frustum Culling Overhead:** In Colony, testing camera frustums entity-by-entity wastes CPU milliseconds.

The mission of Module 011 is to eliminate these hardware bottlenecks:
- **Tail-Padded Slices:** Round column capacities up to multiples of vector width (16 floats), allowing branchless SIMD execution with zero scalar epilogues.
- **Lookahead Software Prefetching:** Issue `__builtin_prefetch` instructions for Chunk $N+1$ while computing Chunk $N$, hiding DRAM latency behind ALU cycles.
- **Chunk User Metadata Slot:** Reserve 64 bytes in `ChunkHeader` so `colony` can store Macro AABBs and cull 1,600 boids in a single 2-cycle vector test.
- **Custom Memory Allocator Hook in `ChunkPool`:** Allow backing chunks with mapped `VkBuffer` host-coherent VRAM for true 0.00 µs zero-copy compute on Apple Silicon / APUs.

---

## 2. Key Capabilities & Interfaces

### 2.1 Vector-Width Tail-Padding (Branchless SIMD)
- In `Archetype::compute_chunk_layout`, component column capacities are rounded down to a multiple of 16 (for 64-byte AVX-512 / AVX2 / NEON alignment) with tail-padding allocated at the end of each column:
  ```cpp
  constexpr size_t SIMD_VECTOR_WIDTH = 16; // 16 floats (64 bytes)
  uint32_t aligned_capacity = (raw_capacity / SIMD_VECTOR_WIDTH) * SIMD_VECTOR_WIDTH;
  ```
- The query loop can process $\lceil count / \text{vector\_width} \rceil$ unconditionally without branching.

### 2.2 Lookahead Software Prefetching (`__builtin_prefetch`)
- When iterating through an archetype's chunk list, prefetch instructions are issued for the next chunk's primary component columns:
  ```cpp
  for (size_t c = 0; c < chunks.size(); ++c) {
      if (c + 1 < chunks.size()) {
          const Chunk* next_c = chunks[c + 1];
          #if defined(__GNUC__) || defined(__clang__)
          __builtin_prefetch(next_c->component_slice(pos_offset), 0 /* read */, 3 /* high temporal locality */);
          __builtin_prefetch(next_c->component_slice(vel_offset), 0 /* read */, 3 /* high temporal locality */);
          #endif
      }
      process_chunk(chunks[c]);
  }
  ```

### 2.3 Chunk Header User Metadata Slot (Macro Frustum Culling Support)
- The 64-byte `Chunk` header reserves a dedicated, cache-aligned user metadata field:
  ```cpp
  struct Chunk {
      // ... existing header fields ...
      alignas(16) uint8_t user_metadata[64]; // e.g. 2D/3D AABB: glm::vec3 aabb_min, aabb_max
  };
  ```
- Exposes accessors:
  ```cpp
  template <typename T>
  T& user_metadata() noexcept;
  ```
- Colony calculates the chunk's aggregate 2D or 3D bounding box and tests camera frustums against it, discarding off-screen chunks in a single 2-cycle vector test.

### 2.4 Custom Memory Allocator Hook in `ChunkPool` (GPU-Direct Unified Memory)
- `ChunkPool` accepts an optional custom allocation and deallocation function:
  ```cpp
  using ChunkAllocFn = void* (*)(size_t bytes, size_t alignment);
  using ChunkFreeFn = void (*)(void* ptr, size_t bytes);

  ChunkPool(ChunkAllocFn alloc_fn, ChunkFreeFn free_fn);
  ```
- Allows Colony to back chunk pages with mapped `VkBuffer` host-coherent VRAM for 0.00 µs zero-copy integration on Apple Silicon.

---

## 3. Subsystem Modifications
- **`Archetype` (`archetype.hpp`):** Compute tail-padded column capacities aligned to 16-element boundaries.
- **`Chunk` (`chunk.hpp`):** Add `user_metadata[64]` field and accessors.
- **`ChunkPool` (`chunk_pool.hpp`):** Accept custom allocation callbacks.
- **`Query` (`query.hpp`):** Implement lookahead prefetching inside `for_each_chunk`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero dynamic heap allocations; POD data preserved; zero epilogue branches.
- **Performance Budget:** $1.15\times - 1.3\times$ speedup on continuous SIMD physics kernels via tail padding and prefetching.

---

## 5. Verification & Testing Strategy
- **`simd_and_hardware_tests`:**
  - Verify tail-padded memory bounds and absence of buffer overruns on SIMD vector operations.
  - Verify prefetch instruction compilation across x86-64 and ARM64.
  - Verify `user_metadata` byte persistence and custom allocator backing.

