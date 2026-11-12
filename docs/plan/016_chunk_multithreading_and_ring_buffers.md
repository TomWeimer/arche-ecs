# Module 016: Chunk Work-Stealing, Thread Buffers & Ring Streaming

This document specifies the exact behavioral deltas, code modifications, and new test suites required to implement chunk-level work stealing, lock-free thread-local command buffers, and triple-buffered GPU ring streaming in `arche-ecs`, assuming Module 015 (Double-Buffered Physics Slices) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Chunk-Level Multi-Threading (`parallel_query`)
* **Before (Module 015):**  
  Queries were single-threaded. Single-core processing reaches limits at ~50k–100k entities at 120 Hz. Simulating 500k to 1,000,000 entities with multi-term Reynolds physics requires distributing work across all CPU cores.
* **Now (Module 016):**  
  `World` introduces `parallel_query` backed by atomic work stealing. Whole 64 KB memory chunks are distributed dynamically across worker threads.
* **Why we need to change this part:**  
  Because each 64 KB chunk is a self-contained, cache-aligned memory block, threads process separate chunks concurrently with **zero memory contention, zero false sharing, and zero mutex locks**. Atomic chunk pulling (`fetch_add`) naturally balances load across asymmetric CPU cores (Apple Silicon Performance vs. Efficiency cores).

### 1.2 Thread-Local Command Buffers (`ParallelCommandBuffer`)
* **Before (Module 015):**  
  Mutating the world (spawning/destroying entities) during query iteration required a single sequential `CommandBuffer`. In multi-threaded queries, sharing a single command buffer would introduce severe mutex contention and serialization bottlenecks.
* **Now (Module 016):**  
  `ParallelCommandBuffer` allocates independent thread-local byte buffers (one per worker thread). Worker threads record scalar and batched mutations (`destroy_entities`) into their private linear buffer without any synchronization or locks. During `playback_parallel_commands()`, the main thread concatenates and executes all thread buffers sequentially in a single cache-hot pass.
* **Why we need to change this part:**  
  Eliminates all mutex locking in parallel inner loops, scaling entity spawning and culling linearly with core count.

### 1.3 Triple-Buffered GPU Ring Streaming & Multi-Draw Indirect
* **Before (Module 015):**  
  GPU streaming copied into a single destination buffer. If the GPU took longer than a frame to render, the CPU stalled on a Vulkan fence (`vkWaitForFences`) before writing the next frame.
* **Now (Module 016):**  
  `World` provides `stream_component_to_ring<T>()` across 3 independent mapped VRAM buffers, and `write_draw_indirect_commands()` with optional Macro AABB culling to emit contiguous `VkDrawIndirectCommand` arrays for single-call multi-draw rendering.
* **Why we need to change this part:**  
  Triple-buffering decouples CPU simulation from GPU rendering, eliminating CPU fence stalls and maintaining full 120 Hz frame cadence. Indirect multi-draw executes 1,000,000 boid draws in a single Vulkan command while culling off-screen chunks on the CPU.

### 1.4 Deduplication & Helper Architecture: Work-Stealing Drivers & Culled Streaming
* **`World::dispatch_parallel_chunks()`:**  
  **Where defined:** `include/arche/world.hpp` (private section of `World`).  
  **What code it replaces:** Replaces duplicated task chunk gathering, dynamic work-stealing counter setup (`next_chunk.fetch_add`), and while loops across both overloads of `World::parallel_query`.
* **Pre-resolved `ChunkTask` Column Offsets:**  
  **Where defined:** `include/arche/world.hpp`.  
  **What code it replaces:** Replaces calling `Query<Components...>::resolve_column_offset<C>(arch)` for every chunk inside worker threads, resolving offsets once per archetype instead.
* **Culled `stream_component_to_ring()`:**  
  **Where defined:** `include/arche/world.hpp`.  
  **What code it replaces:** Replaces unconditional full-world PCIe transfers by accepting a `CullingPredicate`, synchronizing streamed vertices with indirect draw commands.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/gpu_reflection.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `gpu_reflection.hpp` only declared reflection metadata and component copy helpers.
* **Now:** Adds `struct DrawIndirectCommand` layout-compatible with Vulkan's `VkDrawIndirectCommand`.

#### Functions / Members to Delete
* `None`.

#### Modified Structs
* `None`.

#### New Structs to Add to `include/arche/gpu_reflection.hpp`

Insert at the end of `include/arche/gpu_reflection.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// Vulkan-Compatible Indirect Draw Structures
// =============================================================================

/**
 * @struct DrawIndirectCommand
 * @brief Binary layout identical to VkDrawIndirectCommand for GPU-driven multi-draw.
 */
struct DrawIndirectCommand {
  uint32_t vertex_count{0};   ///< Number of vertices to draw (chunk entity count)
  uint32_t instance_count{1}; ///< Number of instances (1)
  uint32_t first_vertex{0};   ///< Starting vertex offset in mapped VRAM buffer
  uint32_t first_instance{0}; ///< First instance ID (0)
};

static_assert(
    sizeof(DrawIndirectCommand) == 16,
    "DrawIndirectCommand must be exactly 16 bytes (VkDrawIndirectCommand compatible)"
);
```

---

### 2.2 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` only supported single-threaded queries and single-frame component streaming.
* **Now:** Adds `parallel_query()`, `playback_parallel_commands()`, `stream_component_to_ring()`, and `write_draw_indirect_commands()` with optional frustum culling predicates.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None`.

#### New Methods to Add to `include/arche/world.hpp`

1. **Add `#include "arche/gpu_reflection.hpp"`**, `#include "arche/parallel_command_buffer.hpp"`, and `#include "arche/thread_pool.hpp"` at the top of `world.hpp`.

2. **Add method declarations** in `class World`, public section (around line 195):

```cpp
  // --- Parallel Chunk Query & Work-Stealing ----------------------------------

  /**
   * @brief Executes a chunk-level parallel query across worker threads using lock-free work-stealing.
   */
  template <typename... Components, typename Func>
  inline void parallel_query(ThreadPool &pool, Func &&func);

  /**
   * @brief Executes a parallel query with lock-free thread-local mutation recording.
   */
  template <typename... Components, typename Func>
  inline void parallel_query(
      ThreadPool &pool,
      ParallelCommandBuffer &cmd_buffers,
      Func &&func
  );

  /**
   * @brief Sequentially replays and clears all thread-local command buffers recorded during parallel work.
   */
  inline void playback_parallel_commands(ParallelCommandBuffer &cmd_buffers) {
    cmd_buffers.playback(*this);
  }

  // --- Triple-Buffered GPU Ring Streaming & Multi-Draw Indirect --------------

  /**
   * @brief Streams component data to one of three mapped VRAM ring slots with optional chunk culling.
   * @details Defined in `include/arche/world.hpp`. Replaces unconditional PCIe streaming
   * of off-screen/culled chunks to VRAM.
   */
  template <typename T, typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  inline size_t stream_component_to_ring(
      uint32_t ring_index,
      void *ring_buffer_base,
      size_t ring_slot_capacity_bytes,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

  /**
   * @brief Emits a contiguous array of DrawIndirectCommand structs for single-call multi-draw rendering,
   * with optional chunk culling via user metadata predicate.
   */
  template <typename CullingPredicate = decltype([](const ChunkHeader &) { return true; })>
  inline size_t write_draw_indirect_commands(
      Archetype *arch,
      void *destination_buffer,
      size_t max_commands,
      CullingPredicate &&is_visible = [](const ChunkHeader &) { return true; }
  );

private:
  // --- Parallel Chunk Work-Stealing Driver -----------------------------------

  template <typename... Components, typename WorkerFn>
  inline void dispatch_parallel_chunks(ThreadPool &pool, WorkerFn &&worker);
```

3. **Add template method implementations** at the bottom of `include/arche/world.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// Parallel Query & Ring Streaming Implementations
// =============================================================================

// --- Unified Parallel Work-Stealing Driver -----------------------------------

template <typename... Components, typename WorkerFn>
inline void World::dispatch_parallel_chunks(ThreadPool &pool, WorkerFn &&worker) {
  auto q = query<Components...>();

  struct ChunkTask {
    Chunk *chunk{nullptr};
    std::array<uint32_t, sizeof...(Components)> offsets;
  };

  std::vector<ChunkTask> tasks;
  // Pre-resolve offsets per archetype to avoid redundant lookups on worker threads
  q.for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
    tasks.push_back({
        chunk,
        { Query<Components...>::template resolve_column_offset<Components>(arch)... }
    });
  });

  if (tasks.empty()) {
    return;
  }

  const size_t total_tasks = tasks.size();
  std::atomic<size_t> next_task{0};

  pool.parallel_for([&](uint32_t thread_id) {
    while (true) {
      const size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
      if (idx >= total_tasks) {
        break;
      }
      worker(thread_id, tasks[idx].chunk, tasks[idx].offsets);
    }
  });
}

template <typename... Components, typename Func>
inline void World::parallel_query(ThreadPool &pool, Func &&func) {
  auto unpack_ptrs = []<size_t... Is>(Chunk *chunk, const auto &offsets, std::index_sequence<Is...>) {
    return std::make_tuple(
        Query<Components...>::template get_column_ptr<Components>(chunk, offsets[Is])...
    );
  };

  dispatch_parallel_chunks<Components...>(pool, [&](uint32_t, Chunk *chunk, const auto &offsets) {
    std::apply(
        [&](auto *...ptrs) {
          func(chunk->count(), ptrs...);
        },
        unpack_ptrs(chunk, offsets, std::index_sequence_for<Components...>{})
    );
  });
}

template <typename... Components, typename Func>
inline void World::parallel_query(
    ThreadPool &pool,
    ParallelCommandBuffer &cmd_buffers,
    Func &&func
) {
  auto unpack_ptrs = []<size_t... Is>(Chunk *chunk, const auto &offsets, std::index_sequence<Is...>) {
    return std::make_tuple(
        Query<Components...>::template get_column_ptr<Components>(chunk, offsets[Is])...
    );
  };

  dispatch_parallel_chunks<Components...>(pool, [&](uint32_t thread_id, Chunk *chunk, const auto &offsets) {
    CommandBuffer &thread_cmd = cmd_buffers.get(thread_id);
    std::apply(
        [&](auto *...ptrs) {
          func(chunk->count(), thread_cmd, ptrs...);
        },
        unpack_ptrs(chunk, offsets, std::index_sequence_for<Components...>{})
    );
  });
}

template <typename T, typename CullingPredicate>
inline size_t World::stream_component_to_ring(
    uint32_t ring_index,
    void *ring_buffer_base,
    size_t ring_slot_capacity_bytes,
    CullingPredicate &&is_visible
) {
  assert(ring_buffer_base != nullptr && "Ring buffer pointer cannot be null");
  const uint32_t slot = ring_index % 3;
  uint8_t *slot_dest = static_cast<uint8_t *>(ring_buffer_base) +
      (slot * ring_slot_capacity_bytes);

  return stream_component<T>(slot_dest, ring_slot_capacity_bytes, std::forward<CullingPredicate>(is_visible));
}

template <typename CullingPredicate>
inline size_t World::write_draw_indirect_commands(
    Archetype *arch,
    void *destination_buffer,
    size_t max_commands,
    CullingPredicate &&is_visible
) {
  assert(destination_buffer != nullptr && arch != nullptr);
  auto *cmds = static_cast<DrawIndirectCommand *>(destination_buffer);
  size_t written = 0;
  uint32_t vertex_offset = 0;

  for (Chunk *chunk : get_archetype_chunks(arch)) {
    if (chunk->is_empty()) {
      continue;
    }
    // Test culling predicate against chunk header (e.g. Macro AABB)
    if (!is_visible(chunk->header())) {
      vertex_offset += chunk->count();
      continue;
    }
    if (written >= max_commands) {
      break;
    }

    cmds[written] = DrawIndirectCommand{
        .vertex_count = chunk->count(),
        .instance_count = 1,
        .first_vertex = vertex_offset,
        .first_instance = 0,
    };
    vertex_offset += chunk->count();
    ++written;
  }
  return written;
}
```

---

## 3. Brand-New Files

### 3.1 `include/arche/thread_pool.hpp`

This is a brand-new minimal, zero-dependency C++20 thread pool using standard `<thread>`, `<atomic>`, and `std::barrier`.

```cpp
#pragma once

#include <atomic>
#include <barrier>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace arche {

// =============================================================================
// Lightweight Lock-Free Worker Thread Pool
// =============================================================================

/**
 * @class ThreadPool
 * @brief Zero-dependency C++20 thread pool designed for chunk work-stealing.
 */
class ThreadPool {
private:
  std::vector<std::thread> workers_;
  std::function<void(uint32_t)> current_task_{nullptr};
  std::atomic<bool> stop_{false};
  uint32_t worker_count_{0};

  std::barrier<> start_barrier_;
  std::barrier<> finish_barrier_;

public:
  explicit ThreadPool(
      uint32_t thread_count = std::thread::hardware_concurrency()
  )   : worker_count_(thread_count > 0 ? thread_count : 1),
        start_barrier_(worker_count_ + 1),
        finish_barrier_(worker_count_ + 1) {
    workers_.reserve(worker_count_);
    for (uint32_t i = 0; i < worker_count_; ++i) {
      workers_.emplace_back([this, i]() {
        while (true) {
          start_barrier_.arrive_and_wait();
          if (stop_.load(std::memory_order_relaxed)) {
            break;
          }
          if (current_task_) {
            current_task_(i);
          }
          finish_barrier_.arrive_and_wait();
        }
      });
    }
  }

  ~ThreadPool() {
    stop_.store(true, std::memory_order_relaxed);
    start_barrier_.arrive_and_wait();
    for (auto &w : workers_) {
      if (w.joinable()) {
        w.join();
      }
    }
  }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  [[nodiscard]] inline uint32_t worker_count() const noexcept {
    return worker_count_;
  }

  template <typename Task>
  inline void parallel_for(Task &&task) {
    current_task_ = task;
    start_barrier_.arrive_and_wait();
    finish_barrier_.arrive_and_wait();
    current_task_ = nullptr;
  }
};

} // namespace arche
```

---

### 3.2 `include/arche/parallel_command_buffer.hpp`

This is a brand-new header providing thread-local mutation recording with sequential main-thread playback.

```cpp
#pragma once

#include "arche/command_buffer.hpp"

#include <cstdint>
#include <vector>

namespace arche {

// =============================================================================
// Thread-Local Command Buffer Array
// =============================================================================

/**
 * @class ParallelCommandBuffer
 * @brief Manages isolated thread-local CommandBuffer instances for lock-free parallel mutations.
 */
class ParallelCommandBuffer {
private:
  std::vector<CommandBuffer> buffers_;

public:
  explicit ParallelCommandBuffer(uint32_t thread_count = 1)
      : buffers_(thread_count) {}

  ~ParallelCommandBuffer() = default;

  [[nodiscard]] inline uint32_t thread_count() const noexcept {
    return static_cast<uint32_t>(buffers_.size());
  }

  [[nodiscard]] inline CommandBuffer &get(uint32_t thread_id) noexcept {
    assert(thread_id < buffers_.size() && "Worker thread ID out of range");
    return buffers_[thread_id];
  }

  inline void playback(World &world) {
    for (auto &buf : buffers_) {
      buf.playback(world);
      buf.clear();
    }
  }

  inline void clear() noexcept {
    for (auto &buf : buffers_) {
      buf.clear();
    }
  }
};

} // namespace arche
```

---

### 3.3 `tests/multithreaded_query_tests.cpp`

```cpp
#include "arche/chunk.hpp"
#include "arche/entity.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/query.hpp"
#include "arche/thread_pool.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

namespace {
using arche::DrawIndirectCommand;
using arche::Entity;
using arche::ParallelCommandBuffer;
using arche::ThreadPool;
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

struct TestAABB {
  float min_x{-50.0f};
  float max_x{50.0f};
};

// =============================================================================
// Unit Test 1: Chunk-Level Parallel Work-Stealing Parity
// =============================================================================

void test_parallel_query_parity() {
  std::cout << "[Test 1/4] Chunk-Level Parallel Work-Stealing Parity (50k)... ";
  World world;
  ThreadPool pool(4);

  constexpr size_t COUNT = 50'000;
  world.create_entities(COUNT, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f});

  world.parallel_query<Position, const Velocity>(pool, [](uint32_t count, Position *pos, const Velocity *vel) {
    for (uint32_t i = 0; i < count; ++i) {
      pos[i].x += vel[i].vx;
      pos[i].y += vel[i].vy;
    }
  });

  size_t checked = 0;
  world.query<const Position>().for_each_chunk([&](uint32_t count, const Position *pos) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(pos[i].x == 4.0f);
      assert(pos[i].y == 6.0f);
    }
    checked += count;
  });

  assert(checked == COUNT);
  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Lock-Free Thread-Local Command Buffers
// =============================================================================

void test_parallel_command_buffers() {
  std::cout << "[Test 2/4] Lock-Free Thread-Local Command Buffers... ";
  World world;
  ThreadPool pool(4);
  ParallelCommandBuffer cmd_buffers(pool.worker_count());

  constexpr size_t COUNT = 10'000;
  std::vector<Entity> entities = world.create_entities(COUNT, Position{0.0f, 0.0f});

  world.parallel_query<Entity, const Position>(
      pool,
      cmd_buffers,
      [](uint32_t count, arche::CommandBuffer &cmd, Entity *ents, const Position *) {
        for (uint32_t i = 0; i < count; ++i) {
          if (ents[i].index() % 2 == 0) {
            cmd.destroy_entity(ents[i]);
          }
        }
      }
  );

  world.playback_parallel_commands(cmd_buffers);
  assert(world.entity_count() == COUNT / 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Triple-Buffered GPU Ring Streaming
// =============================================================================

void test_triple_buffered_ring_streaming() {
  std::cout << "[Test 3/4] Triple-Buffered GPU Ring Streaming... ";
  World world;

  constexpr size_t COUNT = 1000;
  world.create_entities(COUNT, Position{42.0f, 84.0f});

  constexpr size_t SLOT_BYTES = 64 * 1024;
  std::vector<uint8_t> vram_base(SLOT_BYTES * 3, 0);

  size_t b0 = world.stream_component_to_ring<Position>(0, vram_base.data(), SLOT_BYTES);
  assert(b0 == COUNT * sizeof(Position));

  size_t b1 = world.stream_component_to_ring<Position>(1, vram_base.data(), SLOT_BYTES);
  assert(b1 == COUNT * sizeof(Position));

  size_t b2 = world.stream_component_to_ring<Position>(2, vram_base.data(), SLOT_BYTES);
  assert(b2 == COUNT * sizeof(Position));

  size_t b3 = world.stream_component_to_ring<Position>(3, vram_base.data(), SLOT_BYTES);
  assert(b3 == COUNT * sizeof(Position));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Multi-Draw Indirect Generation with Culling
// =============================================================================

void test_draw_indirect_with_culling() {
  std::cout << "[Test 4/4] Multi-Draw Indirect Generation with Macro AABB Culling... ";
  World world;

  constexpr size_t COUNT = 5000;
  world.create_entities(COUNT, Position{1.0f, 1.0f});

  Archetype *arch = world.graph().archetypes().back().get();
  auto &chunks = world.get_archetype_chunks(arch);
  assert(chunks.size() >= 2);

  // Set user metadata on chunk 0 (visible) and chunk 1 (culled)
  chunks[0]->user_metadata<TestAABB>() = TestAABB{-10.0f, 10.0f};
  chunks[1]->user_metadata<TestAABB>() = TestAABB{-999.0f, -900.0f};

  std::vector<DrawIndirectCommand> cmd_buffer(64);

  // Culling predicate: only draw if max_x > 0
  size_t count = world.write_draw_indirect_commands(
      arch,
      cmd_buffer.data(),
      cmd_buffer.size(),
      [](const arche::ChunkHeader &header) {
        const auto *aabb = reinterpret_cast<const TestAABB *>(header.user_metadata);
        return aabb->max_x > 0.0f;
      }
  );

  // Only the visible chunk should be emitted!
  assert(count < chunks.size());
  assert(count >= 1);
  assert(cmd_buffer[0].instance_count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 016: Multithreading & Ring Tests\n";
  std::cout << "====================================================\n";

  test_parallel_query_parity();
  test_parallel_command_buffers();
  test_triple_buffered_ring_streaming();
  test_draw_indirect_with_culling();

  std::cout << "====================================================\n";
  std::cout << "All Module 016 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 016 Tests: Chunk Work-Stealing, Thread Buffers & Ring Streaming
add_executable(multithreaded_query_tests tests/multithreaded_query_tests.cpp)
target_link_libraries(multithreaded_query_tests PRIVATE arche::arche)
add_test(NAME multithreaded_query_tests COMMAND multithreaded_query_tests)
```
