# Arche ECS

<p align="center">
  <img src="docs/images/arche_ecs_banner.jpg" alt="Arche ECS Banner" width="600">
</p>

<p align="center">
  A standalone, ultra-high-performance C++20 Archetype Entity Component System (ECS) library engineered for extreme-density simulations (<b>50,000 to 2,000,000+ entities @ 120+ Hz</b>).
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/Tests-19%2F19%20Passing-brightgreen.svg" alt="Build Status">
  <img src="https://img.shields.io/badge/Dependencies-Zero-orange.svg" alt="Zero Dependencies">
  <img src="https://img.shields.io/badge/Scale-2%2C000%2C000%2B%20Entities-blueviolet.svg" alt="Scale">
  <a href="LLMS.md"><img src="https://img.shields.io/badge/AI%20Context-LLMS.md-informational.svg" alt="LLMS.md"></a>
</p>

> 🤖 **Working with AI Coding Assistants (Antigravity, Cursor, Claude Code, Copilot)?**  
> Feed [LLMS.md](LLMS.md) directly to your AI assistant for compact, canonical API rules, anti-hallucination constraints, and drop-in prompt templates.

---

## Table of Contents

- [Architectural Principles & Hard Invariants](#architectural-principles--hard-invariants)
- [Comparative Benchmarks (arche-ecs vs. Flecs vs. EnTT)](#comparative-benchmarks-arche-ecs-vs-flecs-vs-entt)
  - [1. Extreme Scale: 2,000,000 Entities](#1-extreme-scale-2000000-entities)
  - [2. Multi-Threaded Chunk Parallelism (8 Cores)](#2-multi-threaded-chunk-parallelism-8-cores)
  - [3. Entity Lifecycle & Structural Mutations](#3-entity-lifecycle--structural-mutations)
  - [4. Random Entity Access](#4-random-entity-access)
  - [5. In-Place Entity Sorting (500k Entities)](#5-in-place-entity-sorting-500k-entities)
- [Zero-Loss Integration Guide](#zero-loss-integration-guide)
  - [Single Umbrella Header](#single-umbrella-header)
  - [CMake Subproject Setup](#cmake-subproject-setup)
  - [Crucial Compiler Flags for Maximum Performance](#crucial-compiler-flags-for-maximum-performance)
- [Modern API Usage Guide](#modern-api-usage-guide)
  - [1. Defining Components](#1-defining-components)
  - [2. Initializing the World & Entity Lifecycle](#2-initializing-the-world--entity-lifecycle)
  - [3. High-Throughput Batch Spawning (Broadcasting & SoA Streaming)](#3-high-throughput-batch-spawning-broadcasting--soa-streaming)
  - [4. Component Access & Structural Mutation](#4-component-access--structural-mutation)
  - [5. Querying & Iteration](#5-querying--iteration)
    - [Range-Based For-Loops & Structured Bindings](#range-based-for-loops--structured-bindings)
    - [Querying Entity Handles](#querying-entity-handles)
    - [Compile-Time Tag Filters (`With` & `Without`)](#compile-time-tag-filters-with--without)
    - [Hot-Path SIMD Chunk Traversal (`for_each_chunk`)](#hot-path-simd-chunk-traversal-for_each_chunk)
  - [6. Multi-Threaded Work-Stealing Simulation (`query_parallel`)](#6-multi-threaded-work-stealing-simulation-query_parallel)
  - [7. Double-Buffered Physics Sub-Steps & O(1) Pointer Flipping](#7-double-buffered-physics-sub-steps--o1-pointer-flipping)
  - [8. Dirty Version Ticking & Change Detection (`Mutated` & `Added`)](#8-dirty-version-ticking--change-detection-mutated--added)
  - [9. In-Place Spatial Sorting & Cache Locality](#9-in-place-spatial-sorting--cache-locality)
  - [10. Direct GPU Ring Streaming & Draw Indirect Commands](#10-direct-gpu-ring-streaming--draw-indirect-commands)
  - [11. Instant World Snapshots & Deterministic Rollback](#11-instant-world-snapshots--deterministic-rollback)
- [Full Simulation Frame Walkthrough (Colony Flocking Engine)](#full-simulation-frame-walkthrough-colony-flocking-engine)
- [Architectural Roadmap & The Generality Tax](#architectural-roadmap--the-generality-tax)
- [Building & Testing](#building--testing)
- [Project Layout](#project-layout)
- [License](#license)

---

## Architectural Principles & Hard Invariants

1. **Flat Data over Abstraction:**
   - **Zero virtual methods, zero inheritance, zero dynamic dispatch.**
   - Components are strictly Plain Old Data (POD) structs arranged linearly in 64 KB cache-aligned chunk pages in pure Structure-of-Arrays (SoA) layout.
2. **Zero External Dependencies:**
   - Standard library (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`, `<span>`, `<concepts>`) and hardware CPU intrinsics only.
   - Absolutely no dependency on game engines, graphics APIs (no Vulkan/Metal headers), or third-party frameworks.
3. **Hardware-Aligned Chunk Storage:**
   - Adapts automatically to host cache line sizes (64B on x86, 128B on Apple Silicon) and virtual memory page boundaries (4 KB / 16 KB) to prevent TLB thrashing and false sharing.
4. **Deterministic O(1) Operations:**
   - Entity routing via a 32-byte generational `SlotMap` with automatic stale-handle invalidation.
   - Chunk compaction via $O(1)$ Swap-and-Pop with zero memory holes.
5. **Lock-Free Concurrency on the Hot Path:**
   - Chunks are independent 64 KB memory pages; worker threads process discrete chunks without mutex locks or atomic contention.

---

## Comparative Benchmarks (arche-ecs vs. Flecs vs. EnTT)

Empirically verified against **Flecs** (v4.0.4, archetype table engine) and **EnTT** (v3.13.2, sparse-set engine) under identical hardware conditions (Apple Silicon M-Series, 3.2 GHz reference, Clang 17, `-O3 -march=native -ffast-math`).

### 1. Extreme Scale: 2,000,000 Entities

| Benchmark Scenario | arche-ecs (Chunked SoA) | Flecs (Archetype Table) | EnTT (Sparse Set) | Speedup vs. Competitors |
| :--- | :---: | :---: | :---: | :---: |
| **Realistic Swarm Physics**<br>*(Euler Integration + Bounding Box Bounds)* | **1.70 ms** | 2.63 ms | 5.26 ms | **1.55× faster than Flecs**<br>**3.1× faster than EnTT** |
| **Simple Streaming Iteration**<br>*(pos += vel \* dt)* | 0.93 ms | **0.51 ms** | 2.85 ms *(Owned)*<br>4.27 ms *(View)* | **3.1×–4.6× faster than EnTT** |
| **Wide Entity Simulation**<br>*(500,000 Entities: 8 Components)* | 1.04 ms | **0.78 ms** | 7.03 ms | **6.8× faster than EnTT** |

> **Why arche-ecs beats Flecs in Swarm Physics:**  
> When compute loops contain realistic arithmetic density (e.g. Reynolds steering, boundary bouncing, vector clamps), `arche-ecs` chunks fit perfectly into CPU L1/L2 caches (128 KB on Apple Silicon), allowing Clang/GCC auto-vectorizers (NEON / AVX2) to saturate vector execution units without cache thrashing.

### 2. Multi-Threaded Chunk Parallelism (8 Cores)

Traversing **2,000,000 entities** across 8 CPU worker threads using chunk-level work-stealing:

```
|               ns/op |                op/s |    err% | Execution Strategy
|--------------------:|--------------------:|--------:|:---------------------------------------------
|          426,770.90 |            2,343.18 |   12.1% | arche-ecs (8-Thread query_parallel): 0.42 ms (2,343 FPS!)
|          924,260.42 |            1,081.95 |    0.7% | arche-ecs (Single-Thread baseline) : 0.92 ms
```

### 3. Entity Lifecycle & Structural Mutations

| Operation (20,000 Entities) | arche-ecs | EnTT | Flecs | Advantage |
| :--- | :---: | :---: | :---: | :---: |
| **Bulk Entity Spawning** | **68.4 µs** | 511.8 µs | 5,423.3 µs | **79× faster than Flecs**<br>**7.5× faster than EnTT** |
| **Single Spawn Loop** | 728.2 µs | **511.8 µs** | 5,423.3 µs | **7.4× faster than Flecs** |
| **Add Component (Archetype Migration)** | 1,037.7 µs | **469.4 µs** *(Sparse pool append)* | 5,540.8 µs | **5.3× faster than Flecs** |
| **Entity Destruction (Despawn)** | **858.1 µs** | 950.1 µs | 5,299.0 µs | **6.2× faster than Flecs** |

### 4. Random Entity Access

100,000 component lookups across shuffled, random generational handles:

```
|               ns/op |                op/s |    err% | Engine & Architecture
|--------------------:|--------------------:|--------:|:---------------------------------------------
|          405,653.85 |            2,465.16 |    1.5% | arche-ecs (SlotMap Indirection) : 405.6 µs
|          636,254.81 |            1,571.70 |    0.5% | EnTT (Direct Sparse Set Lookup) : 636.2 µs (1.5x slower)
|        1,355,353.41 |              737.81 |    5.5% | Flecs (Entity Index Table)      : 1,355.3 µs (3.3x slower)
```

### 5. In-Place Entity Sorting (500k Entities)

Sorting 500,000 entities in-place by spatial key to enforce hardware cache locality:

```
|               ns/op |                op/s |    err% | Sorting Implementation
|--------------------:|--------------------:|--------:|:---------------------------------------------
|       51,264,550.00 |               19.51 |    2.2% | arche-ecs (In-place cycle chasing) : 51.2 ms
|       90,105,680.67 |               11.10 |    3.7% | EnTT (registry.sort dense pool)    : 90.1 ms (1.75x slower)
```

---

## Zero-Loss Integration Guide

### Single Umbrella Header

Consume the entire library with zero header hunting:

```cpp
#include <arche/arche.hpp>
```

### CMake Subproject Setup

Add `arche-ecs` via `FetchContent` or as a Git submodule:

```cmake
include(FetchContent)

FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/smilies009/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

add_executable(my_simulation src/main.cpp)
target_link_libraries(my_simulation PRIVATE arche::arche)
```

### Crucial Compiler Flags for Maximum Performance

Configure consumer targets with the following compiler flags:

```cmake
# 1. Enable Link-Time Optimization (LTO / IPO)
set_property(TARGET my_simulation PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set_property(TARGET arche PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# 2. Enable Native SIMD Vectorization
if (MSVC)
    target_compile_options(my_simulation PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(my_simulation PRIVATE -O3 -march=native -ffast-math)
endif()

# 3. Disable RTTI (arche-ecs uses zero dynamic_cast or runtime RTTI)
if (NOT MSVC)
    target_compile_options(my_simulation PRIVATE -fno-rtti)
endif()
```

---

## Modern API Usage Guide

### 1. Defining Components

Components are plain, unadorned C++ structs (Plain Old Data):

```cpp
struct Position { float x{0.0f}, y{0.0f}, z{0.0f}; };
struct Velocity { float vx{0.0f}, vy{0.0f}, vz{0.0f}; };
struct BoidState { float hunger{0.0f}; uint32_t flock_id{0}; };
struct MortonKey { uint32_t code{0}; };

// Zero-sized tag components (occupy 0 bytes in chunk columns)
struct TagBoid {};
struct TagPredator {};
struct TagDead {};
```

---

### 2. Initializing the World & Entity Lifecycle

The `World` is the central coordinator managing archetypes, chunk allocation pools, and entity slot routing:

```cpp
#include <arche/arche.hpp>

arche::World world;

// 1. Scalar spawn: Infers archetype and constructs components in-place
arche::Entity boid = world.spawn(
    Position{10.0f, 20.0f, 0.0f},
    Velocity{1.0f, -0.5f, 0.0f},
    TagBoid{}
);

// 2. Checking handle validity in O(1)
if (world.is_alive(boid)) {
    // 3. Direct component inspection
    Position& pos = world.get<Position>(boid);
    pos.x += 1.0f;
}

// 4. Despawn entity (reclaims slot and executes O(1) chunk swap-and-pop)
world.despawn(boid);
assert(!world.is_alive(boid));
```

---

### 3. High-Throughput Batch Spawning (Broadcasting & SoA Streaming)

#### Approach A: Prototype Broadcasting
Spawns $N$ entities sharing identical initial component values:

```cpp
// Spawns 100,000 boids in ~0.7 ms (> 140 Million entities/sec)
std::vector<arche::Entity> entities = world.spawn_batch(
    100'000,
    Position{0.0f, 0.0f, 0.0f},
    Velocity{1.0f, 0.0f, 0.0f},
    TagBoid{}
);
```

#### Approach B: Direct Structure-of-Arrays (SoA) Streaming
Streams pre-computed unique column arrays directly into chunk columns via `std::memcpy` with **zero heap allocations**:

```cpp
std::vector<Position> positions(50'000, Position{...});
std::vector<Velocity> velocities(50'000, Velocity{...});
std::vector<arche::Entity> out_handles(50'000);

// Ingests external arrays directly into cache-aligned chunks (> 250M entities/sec)
world.spawn_batch(
    50'000,
    std::span<arche::Entity>(out_handles),
    positions.data(),
    velocities.data()
);

// Batch despawn: destroys 50,000 entities in a single cache-efficient pass
world.despawn_batch(out_handles);
```

---

### 4. Component Access & Structural Mutation

Entities migrate dynamically between archetypes via pre-cached DAG edges in $O(1)$ time:

```cpp
arche::Entity e = world.spawn(Position{0.0f, 0.0f, 0.0f});

// Test component presence in O(1) bitmask check
assert(world.has<Position>(e));
assert(!world.has<Velocity>(e));

// Add component: transitions entity to [Position, Velocity]
world.add(e, Velocity{1.5f, 2.5f, 0.0f});

// Remove component: transitions entity back to [Position]
world.remove<Velocity>(e);
```

---

### 5. Querying & Iteration

#### Range-Based For-Loops & Structured Bindings

```cpp
// Superset matching: iterates any entity possessing AT LEAST [Position, Velocity]
for (auto [pos, vel] : world.query<Position, Velocity>()) {
    pos.x += vel.vx * 0.016f;
    pos.y += vel.vy * 0.016f;
}
```

#### Querying Entity Handles

Include `arche::Entity` as a template parameter to access the entity handle during iteration:

```cpp
for (auto [entity, pos, vel] : world.query<arche::Entity, Position, Velocity>()) {
    if (pos.y < -100.0f) {
        // Queue deferred destruction
    }
}
```

#### Compile-Time Tag Filters (`With` & `Without`)

Filter archetypes in 1–2 CPU clock cycles using 128-bit bitwise AND operations:

```cpp
using arche::With;
using arche::Without;

// Iterates entities with Position & Velocity, tagged TagBoid, but NOT TagDead
auto q = world.query<Position, Velocity>(
    With<TagBoid>{},
    Without<TagDead>{}
);

for (auto [pos, vel] : q) {
    // Process active boids only
}
```

#### Hot-Path SIMD Chunk Traversal (`for_each_chunk`)

Bypasses iterator dereferencing overhead to process raw 64 KB component columns:

```cpp
auto q = world.query<Position, const Velocity>();
constexpr float dt = 0.00833f;

// Executes in ~0.85 CPU cycles per entity!
q.for_each_chunk([dt](uint32_t count, Position* pos, const Velocity* vel) {
    #pragma clang loop vectorize(enable) interleave(enable)
    for (uint32_t i = 0; i < count; ++i) {
        pos[i].x += vel[i].vx * dt;
        pos[i].y += vel[i].vy * dt;
        pos[i].z += vel[i].vz * dt;
    }
});
```

---

### 6. Multi-Threaded Work-Stealing Simulation (`query_parallel`)

Because 64 KB memory chunks are completely independent, `arche-ecs` treats chunks as discrete parallel tasks:

```cpp
arche::ThreadPool pool(8);                      // 8 worker threads
arche::ParallelCommandBuffer cmd_buffers(8);    // Thread-isolated mutation buffers

world.query_parallel<Position, const Velocity>(
    pool,
    cmd_buffers,
    [](uint32_t count, arche::CommandBuffer& cmd, Position* pos, const Velocity* vel) {
        for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.00833f;
            pos[i].y += vel[i].vy * 0.00833f;
            
            // Thread-safe deferred mutation recorded without mutex locks
            if (pos[i].y > 1000.0f) {
                cmd.add(ents[i], TagDead{});
            }
        }
    }
);

// Replay deferred mutations deterministically on the main thread
world.commit(cmd_buffers);
```

---

### 7. Double-Buffered Physics Sub-Steps & O(1) Pointer Flipping

In dense swarm simulations, reading and writing positions in the same buffer causes race conditions and order-dependent steering errors. `arche-ecs` provides native double-buffered component columns:

```cpp
// 1. Enable dual read/write slices for Position
world.enable_double_buffer<Position>();

// 2. Query reading Step t (const Position*) and writing Step t+1 (Write<Position>)
world.query_parallel<const Position, const Velocity, arche::Write<Position>>(
    pool,
    cmd_buffers,
    [](uint32_t count, arche::CommandBuffer&, const Position* pos, const Velocity* vel, arche::Write<Position> next_pos) {
        for (uint32_t i = 0; i < count; ++i) {
            next_pos[i].x = pos[i].x + vel[i].vx * 0.00833f;
            next_pos[i].y = pos[i].y + vel[i].vy * 0.00833f;
            next_pos[i].z = pos[i].z + vel[i].vz * 0.00833f;
        }
    }
);

// 3. O(1) Pointer Flip: swaps chunk read/write slice offsets in constant time
world.flip<Position>();
```

---

### 8. Dirty Version Ticking & Change Detection (`Mutated` & `Added`)

Every chunk header maintains monotonic structure and column modification ticks:

```cpp
world.tick(); // Advances global simulation tick

// Iterates only chunks where Position was modified since tick 42
for (auto [pos, vel] : world.query<Position, Velocity>(arche::Mutated<Position>{42})) {
    // Skips hundreds of unmutated chunks in 1 CPU cycle at the chunk boundary!
}

// Iterates only chunks with newly spawned or migrated entities
for (auto [pos] : world.query<Position>(arche::Added{42})) {
    // Process new arrivals
}
```

---

### 9. In-Place Spatial Sorting & Cache Locality

Improves CPU cache hit rates during neighbor searches by re-ordering entities into Morton Z-order curve alignment via **cycle chasing**:

```cpp
arche::Archetype* arch = world.get_record(boid).archetype;

// In-place sort by Morton Key
world.sort<MortonKey>(arch, [](const MortonKey& a, const MortonKey& b) {
    return a.code < b.code;
});

// Or apply arbitrary permutations directly
std::vector<uint32_t> permutation = { ... };
world.permute(arch, permutation);
```

---

### 10. Direct GPU Ring Streaming & Draw Indirect Commands

```cpp
// Streams component column directly to mapped GPU VRAM ring buffer (17.5+ GB/s)
size_t bytes_written = world.stream_gpu<Position>(
    ring_slot_index,
    mapped_vram_base,
    slot_capacity_bytes
);

// Generate GPU DrawIndirectCommand buffers with frustum culling
size_t draw_count = world.indirect_draw(
    arch,
    indirect_command_buffer,
    max_commands,
    [](const Position& pos) { return pos.z >= 0.0f; } // Culling predicate
);
```

---

### 11. Instant World Snapshots & Deterministic Rollback

Serializes and restores the complete ECS state (entity slot map, chunk memory, and dirty versions) in raw binary with zero translation overhead:

```cpp
std::vector<uint8_t> snapshot_buffer(16 * 1024 * 1024);

// Save snapshot in ~0.5 ms
size_t bytes = world.save_snapshot(snapshot_buffer.data(), snapshot_buffer.size());

// Instant rollback
world.load_snapshot(snapshot_buffer.data(), bytes);
```

---

## Full Simulation Frame Walkthrough (Colony Flocking Engine)

From [examples/colony_simulation_loop.cpp](examples/colony_simulation_loop.cpp): a complete, production-grade 120 Hz lockstep frame simulating **100,000 boid entities**:

```cpp
#include <arche/arche.hpp>

int main() {
    arche::World world;
    arche::ThreadPool pool(8);
    arche::ParallelCommandBuffer cmd_buffers(8);

    // 1. Configure Double-Buffered Physics
    world.enable_double_buffer<Position>();

    // 2. High-Throughput Bulk Spawn (100,000 Boids)
    world.spawn_batch(100'000, Position{}, Velocity{1.0f, 0.5f, 0.0f}, BoidState{});

    // 3. 120 Hz Simulation Loop (120 frames executed in 18.9 ms -> 158 µs/frame!)
    for (uint32_t frame = 0; frame < 120; ++frame) {
        // Step A: Advance simulation tick
        world.tick();

        // Step B: Concurrent Flocking (reading Step t, writing Step t+1)
        world.query_parallel<const Position, const Velocity, BoidState, arche::Write<Position>>(
            pool,
            cmd_buffers,
            [](uint32_t count, arche::CommandBuffer&, const Position* pos, const Velocity* vel, BoidState* state, arche::Write<Position> next_pos) {
                for (uint32_t i = 0; i < count; ++i) {
                    next_pos[i].x = pos[i].x + vel[i].vx * 0.00833f;
                    next_pos[i].y = pos[i].y + vel[i].vy * 0.00833f;
                    next_pos[i].z = pos[i].z + vel[i].vz * 0.00833f;
                    state[i].hunger += 0.01f;
                }
            }
        );

        // Step C: Replay deferred mutations on main thread
        world.commit(cmd_buffers);

        // Step D: O(1) Double-buffer flip
        world.flip<Position>();
    }

    return 0;
}
```

---

## Architectural Roadmap & The Generality Tax

```mermaid
flowchart LR
    S1["Stage 1: Core Storage<br>(100% COMPLETE)"] --> S2["Stage 2: Concurrency & Scale<br>(100% COMPLETE)"]
    S2 -.-> S3["Stage 3: Relational Graphs<br>(DELIBERATELY EXCLUDED)"]
    S3 -.-> S4["Stage 4: Distributed Compute<br>(OUT OF SCOPE)"]

    style S1 fill:#1b4731,stroke:#3fb950,stroke-width:2px,color:#fff
    style S2 fill:#1b4731,stroke:#3fb950,stroke-width:2px,color:#fff
    style S3 fill:#4d2d18,stroke:#d29922,stroke-width:1px,stroke-dasharray: 5 5,color:#fff
    style S4 fill:#3b1e1e,stroke:#f85149,stroke-width:1px,stroke-dasharray: 5 5,color:#fff
```

### Why Stage 1 & Stage 2 are Complete
- **Stage 1 (Storage):** Cache-aligned 64 KB SoA chunk pages, generational `SlotMap` routing, and transition DAG commutativity.
- **Stage 2 (Concurrency & Scale):** Multi-threaded work-stealing parallelism (`query_parallel`), double-buffered columns (`Write<T>`, `flip`), dirty version ticking (`Mutated<T>`), in-place sorting (`sort`), and bulk SoA streaming. Tested to **2,000,000 entities**.

### Why Stage 3 & Stage 4 are Deliberately Excluded ("The Generality Tax")
More features in an ECS do not equate to better performance:
1. **The Archetype Fragmentation Penalty:** Dynamic entity relationships (`ChildOf`, `TargetOf`) fragment dense chunks into hundreds of tiny, partially filled tables. CPU queries spend more cycles pointer chasing across fragmented archetypes than executing vector arithmetic.
2. **GPU Synchronization Stalls:** Heterogeneous compute graphs require CPU-GPU synchronization barriers, destroying throughput compared to our linear DMA uploads (> 17.5 GB/s).

---

## Building & Testing

### Requirements
- **C++ Compiler:** Modern C++20 compiler (`AppleClang 13+`, `Clang 13+`, `GCC 11+`, or `MSVC 2019+`)
- **Build System:** `CMake 3.20+`

### Build & Run Test Suites
```bash
# 1. Configure build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build all targets
cmake --build build -j8

# 3. Run all 19 unit test suites via CTest
ctest --test-dir build --output-on-failure

# 4. Run the Colony 100k Boid Simulation example
./build/colony_simulation_loop

# 5. Run the Stage 2 Hardware Benchmark profiler
./build/arche_benchmarks

# 6. Run the Comparative Benchmark against Flecs and EnTT
./build/comparative_ecs_benchmarks
```

---

## Project Layout

```text
arche-ecs/
├── CMakeLists.txt                      # CMake build definitions
├── include/
│   └── arche/
│       ├── arche.hpp                   # Single umbrella public header
│       ├── world.hpp                   # Central World facade (spawn, get, tick, commit)
│       ├── entity.hpp                  # Generational entity handle
│       ├── query.hpp                   # Queries, iterators, and filter tags (With/Without/Write)
│       ├── command_buffer.hpp          # CommandBuffer & ParallelCommandBuffer
│       ├── type_registry.hpp           # TypeID & TypeRegistry component reflection
│       ├── component_mask.hpp          # 128-bit component presence bitset
│       ├── archetype.hpp               # Archetype definition & column layouts
│       ├── chunk.hpp                   # 64 KB SoA chunk memory geometry
│       ├── gpu_reflection.hpp          # VRAM ring buffer & draw indirect commands
│       ├── hardware.hpp                # CPU cache line & page size topology probe
│       ├── thread_pool.hpp             # Work-stealing concurrency pool
│       └── detail/                     # Engine internals (isolated plumbing)
│           ├── archetype_graph.hpp     # Transition DAG edges
│           ├── chunk_pool.hpp          # 64 KB slab allocation pool
│           ├── math_utils.hpp          # SIMD bounds & alignment math
│           ├── query_cache.hpp         # Cached archetype queries
│           ├── query_iterator.hpp      # Range-for structured binding iterator
│           └── slot_map.hpp            # Entity-to-chunk index routing table
├── src/
│   ├── arche.cpp                       # Library symbol root
│   ├── chunk.cpp                       # Chunk memory slice logic
│   ├── command_buffer.cpp              # Deferred mutation playback
│   └── world.cpp                       # World lifecycle management
├── tests/                              # 19 Dedicated unit test suites
│   ├── entity_tests.cpp                # Generational handle integrity
│   ├── type_tests.cpp                  # TypeRegistry & reflection
│   ├── archetype_tests.cpp             # DAG commutativity & edge caching
│   ├── chunk_storage_tests.cpp         # 64 KB chunk SoA alignment
│   ├── structural_mutation_tests.cpp   # Add/remove & swap-and-pop
│   ├── query_tests.cpp                 # Superset queries & SIMD iteration
│   ├── gpu_reflection_tests.cpp        # std430 traits & VRAM streaming
│   ├── arche_tests.cpp                 # Integration regression suite
│   ├── handles_and_snapshots_tests.cpp # SlotMap & snapshot serialization
│   ├── batch_lifecycle_tests.cpp       # Bulk spawn & SoA streaming
│   ├── simd_and_hardware_tests.cpp     # SIMD bounds & topology
│   ├── dirty_ticks_tests.cpp           # Monotonic version tracking
│   ├── query_caching_tests.cpp         # Event-driven query caching
│   ├── archetype_permutation_tests.cpp # In-place cycle chasing permutation
│   ├── double_buffered_tests.cpp       # Dual-slice double buffering
│   ├── multithreaded_query_tests.cpp   # Multi-threaded query dispatch
│   ├── stage2_validation_tests.cpp     # Stage 2 validation integration
│   └── public_api_tests.cpp            # Modern clean API test suite
├── examples/
│   └── colony_simulation_loop.cpp      # 100k boid 120 Hz lockstep frame example
└── benchmarks/
    ├── arche_benchmarks.cpp            # Cycle-accurate TSC hardware profiler
    └── comparative_benchmarks.cpp      # 2M scale head-to-head vs Flecs & EnTT
```

---

## License

This project is licensed under the [MIT License](LICENSE).
