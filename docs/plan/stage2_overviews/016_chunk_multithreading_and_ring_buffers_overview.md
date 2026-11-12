# Module 016: Chunk Work-Stealing, Thread Buffers & Ring Streaming — Overview

## 1. Architectural Mission
In simulations with 100,000 to 1,000,000+ entities:
1. **Single-Core Scaling Ceiling:** Single-threaded execution saturates at ~50k–100k entities at 120 Hz. To simulate 500k–1M entities with multi-term Reynolds physics, execution must scale across 8 to 16+ CPU cores.
2. **Lock Contention During Parallel Mutation:** If worker threads need to spawn, destroy, or tag entities (e.g. culling dead boids or spawning splashes), locking a central command buffer causes catastrophic mutex contention.
3. **GPU Render Fence Stalls:** If the CPU simulation finishes early (e.g. in 3 ms), double-buffering forces the CPU to wait on a Vulkan fence (`vkWaitForFences`) until the GPU finishes reading the previous frame.

The mission of Module 016 is to solve all three concurrency bottlenecks:
- **Lock-Free Chunk Parallel Query (`world.parallel_query`):** Distribute independent 64 KB chunks across worker threads via lock-free atomic work stealing with zero mutexes and zero false sharing.
- **Thread-Local Command Buffers (`ThreadLocalCommandBuffer`):** Worker threads write mutations into thread-isolated linear byte buffers, concatenated and executed sequentially during main-thread playback.
- **Triple-Buffered GPU Ring Streaming:** Allocate 3 independent physical mapped VRAM buffers, allowing the CPU to compute the next frame without stalling on GPU render fences.
- **Multi-Draw Indirect Command Helper:** Emit contiguous `VkDrawIndirectCommand` buffers for single-call GPU rendering.

---

## 2. Key Capabilities & Interfaces

### 2.1 Chunk-Level Parallel Traversal (`parallel_query`)
```cpp
// Distributes 64 KB chunks across worker threads in parallel:
world.parallel_query<Position, Velocity>(pool, [dt](uint32_t count, Position* p, Velocity* v) {
    #pragma clang loop vectorize(enable)
    for (uint32_t i = 0; i < count; ++i) {
        p[i].x += v[i].vx * dt;
        p[i].y += v[i].vy * dt;
    }
});
```
- Each 64 KB chunk represents an isolated memory slice. Since no two threads write to the same chunk, there is **zero memory contention and zero locking**.
- Worker threads pull chunks via an atomic `fetch_add` counter, ensuring optimal load balancing across asymmetric CPU cores (Apple Silicon P-cores vs. E-cores).

### 2.2 Thread-Local Command Buffers (`ThreadLocalCommandBuffer`)
```cpp
world.parallel_query<Entity, Position>(pool, [](Entity e, Position& p, ThreadLocalCommandBuffer& cmd) {
    if (p.y < 0.0f) {
        cmd.destroy_entity(e); // Lock-free write into thread-local buffer!
    }
});

// Main thread safe sync point:
world.playback_parallel_commands();
```
- Each worker thread receives a dedicated unformatted linear byte buffer.
- During `playback_parallel_commands()`, the main thread replays all thread buffers sequentially in a single cache-hot pass.

### 2.3 Triple-Buffered GPU Ring Streaming
```cpp
// Rotates across 3 pre-allocated mapped VRAM frames:
// Ring 0: Currently being rendered by GPU
// Ring 1: Submitted to Vulkan queue
// Ring 2: Actively written by CPU ECS systems
size_t bytes_written = world.stream_component_to_ring<GpuPosition>(ring_index);
```
- Completely eliminates CPU stalls on `vkWaitForFences`.
- Maintains full 120 Hz CPU throughput even when GPU render times fluctuate.

### 2.4 Multi-Draw Indirect Command Helper (`vkCmdDrawIndirect`)
```cpp
struct DrawIndirectCommand {
    uint32_t vertex_count;    // chunk->count()
    uint32_t instance_count;  // 1
    uint32_t first_vertex;    // chunk_vram_offset
    uint32_t first_instance;  // 0
};

// Generates contiguous indirect commands for all active chunks in an archetype:
size_t write_draw_indirect_commands(void* destination_buffer, size_t max_commands);
```
- The Vulkan renderer issues a single command:
  ```cpp
  vkCmdDrawIndirect(commandBuffer, indirectBuffer, 0, activeChunkCount, sizeof(DrawIndirectCommand));
  ```
- Drops CPU draw submission overhead to practically zero, whether rendering 10,000 or 1,000,000 boids.

---

## 3. Subsystem Modifications
- **`ThreadPool` / `JobPool`:** Minimal, zero-dependency C++20 thread pool using standard `<thread>`, `<atomic>`, and `std::barrier`.
- **`ThreadLocalCommandBuffer`:** Array of per-thread byte buffers with unified sequential playback.
- **`World`:** Add `parallel_query`, `playback_parallel_commands`, `stream_component_to_ring`, and `write_draw_indirect_commands`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero data races on chunk columns; zero mutex contention in the parallel hot loop; zero external threading dependencies (no TBB/OpenMP).
- **Performance Budget:** 
  - Linear core scaling ($6\times \text{ to } 7.5\times$ speedup on 8-core CPUs).
  - 1,000,000 entity integration frametimes under $< 0.1\text{ ms}$ on 8 cores.
  - Zero GPU fence stall time on the CPU thread.

---

## 5. Verification & Testing Strategy
- **`multithreaded_query_tests`:**
  - Verify mathematical parity between single-threaded and parallel chunk execution.
  - Stress-test thread-local mutations with TSAN (ThreadSanitizer).
  - Benchmark work-stealing load balancing across heterogeneous CPU cores.
  - Verify 3-ring VRAM pointer rotation and memory isolation.

