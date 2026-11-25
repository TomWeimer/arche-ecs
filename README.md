# Arche ECS

<p align="center">
  <img src="docs/images/arche_ecs_banner.jpg" alt="Arche ECS Banner" width="600">
</p>

<p align="center">
  A standalone, ultra-high-performance C++20 Archetype Entity Component System (ECS) library engineered for extreme-density simulations (<b>50,000 to 2,000,000+ entities @ 120+ Hz</b>).
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20%20Modules-blue.svg" alt="C++20 Modules"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/Tests-25%2F25%20Passing-brightgreen.svg" alt="Build Status">
  <img src="https://img.shields.io/badge/Architecture-AaC%20Verified-blueviolet.svg" alt="Architecture as Code">
  <img src="https://img.shields.io/badge/Scale-2%2C000%2C000%2B%20Entities-blue.svg" alt="Scale">
  <a href="LLMS.md"><img src="https://img.shields.io/badge/AI%20Context-LLMS.md-informational.svg" alt="LLMS.md"></a>
</p>

> 🤖 **Working with AI Coding Assistants (Antigravity, Cursor, Claude Code, Copilot)?**  
> Feed [LLMS.md](LLMS.md) directly to your AI assistant for compact, canonical API rules, anti-hallucination constraints, and drop-in prompt templates.

---

## Table of Contents

- [Architectural Principles & Hard Invariants](#architectural-principles--hard-invariants)
- [Layered Architecture & Strict Dependency Flow](#layered-architecture--strict-dependency-flow)
- [Comparative Benchmarks (arche-ecs vs. Flecs vs. EnTT)](#comparative-benchmarks-arche-ecs-vs-flecs-vs-entt)
  - [1. Memory Footprint & Steady-State Zero Allocations (1,000,000 Entities)](#1-memory-footprint--steady-state-zero-allocations-1000000-entities)
  - [2. Change Detection & Sparse Activity (500,000 Entities, 5% Active)](#2-change-detection--sparse-activity-500000-entities-5-active)
  - [3. Extreme Scale: 2,000,000 Entities Simulation](#3-extreme-scale-2000000-entities-simulation)
  - [4. Multi-Threaded Query Scaling (1, 2, 4, 8 Threads)](#4-multi-threaded-query-scaling-1-2-4-8-threads)
  - [5. Full-Frame 10-System Game Loop Simulation (100k Entities)](#5-full-frame-10-system-game-loop-simulation-100k-entities)
  - [6. Entity Lifecycle & Structural Mutations (20k Entities)](#6-entity-lifecycle--structural-mutations-20k-entities)
  - [7. Dynamic Archetype Churn (50k Entities Buff/Tag Cycling)](#7-dynamic-archetype-churn-50k-entities-bufftag-cycling)
- [Comparative Performance Analysis & Honest Diagnostic](#comparative-performance-analysis--honest-diagnostic)
  - [The Harsh Numbers: Where arche-ecs Lost](#the-harsh-numbers-where-arche-ecs-lost)
  - [Why We Are Losing](#why-we-are-losing)
  - [What Specifically Needs to Be Fixed (Optimization Roadmap)](#what-specifically-needs-to-be-fixed-optimization-roadmap)
- [Zero-Loss Integration Guide](#zero-loss-integration-guide)
  - [C++20 Module Import](#c20-module-import)
  - [CMake Subproject Setup](#cmake-subproject-setup)
- [Modern API Usage Guide](#modern-api-usage-guide)
  - [1. Defining Components & Resources](#1-defining-components--resources)
  - [2. Initializing the World & Entity Lifecycle](#2-initializing-the-world--entity-lifecycle)
  - [3. Bulk Spawning (Broadcasting & SoA Ingestion)](#3-bulk-spawning-broadcasting--soa-ingestion)
  - [4. Component Access & Structural Mutation](#4-component-access--structural-mutation)
  - [5. Querying & Iteration](#5-querying--iteration)
  - [6. Declarative Plugins & Conflict-Resolving DAG Scheduler](#6-declarative-plugins--conflict-resolving-dag-scheduler)
  - [7. Change Detection via Dirty Ticks (`Mutated` & `Added`)](#7-change-detection-via-dirty-ticks-mutated--added)
  - [8. In-Place Spatial Sorting & Cache Locality](#8-in-place-spatial-sorting--cache-locality)
- [Building, Testing & Visualizing](#building-testing--visualizing)
- [Project Layout](#project-layout)
- [License](#license)

---

## Architectural Principles & Hard Invariants

1. **Flat Data over Abstraction:**
   - **Zero virtual methods, zero inheritance, zero runtime RTTI.**
   - Components are strictly Plain Old Data (POD) structs arranged linearly in 64 KB cache-aligned chunk pages in pure Structure-of-Arrays (SoA) layout.
2. **Zero External Dependencies:**
   - Pure C++20 standard library (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`, `<span>`, `<concepts>`) and hardware CPU intrinsics only.
   - Absolutely no dependency on game engines, graphics APIs, or third-party containers.
3. **Hardware-Aligned Chunk Storage:**
   - Adapts to host cache line sizes (64B on x86, 128B on Apple Silicon) and virtual memory page boundaries (4 KB / 16 KB) to prevent TLB thrashing and false sharing.
4. **Deterministic O(1) Operations:**
   - Entity routing via a 32-byte generational `SlotMap` with automatic stale-handle invalidation.
   - Chunk compaction via $O(1)$ Swap-and-Pop with zero memory holes.
5. **Declarative Concurrency & Scheduling:**
   - Compile-time and runtime component access footprints (`Access{}.reads<T>().writes<U>()`).
   - Automated DAG stage sorting, conflict-free batch parallelism, and barrier synchronization.

---

## Layered Architecture & Strict Dependency Flow

`arche-ecs` is implemented with a strictly enforced **Architecture-as-Code (AaC)** DAG. Lower layers must NEVER depend on or include higher layers. Verified via `make lint` (`scripts/check_architecture.py`).

| Layer | Subsystem / Tier | Contents | Permitted Dependencies |
| :---: | :--- | :--- | :--- |
| **0** | `foundation` (`entity`, `hardware`, `component`) | Entity handle, POD types, bitmasks, CPU intrinsics, ColumnLayout, ComponentID, SnapshotHeader | Only Layer 0 |
| **1** | `storage` (`chunk`, `slot_map`, `resource`, `command`) | 64KB chunk page memory, SlotMap routing table, singleton resources, CommandBuffer byte queue | Layer 0 only |
| **2** | `archetype` (`archetype`, `archetype_index`) | Archetype tables, transition graph, reactive table index | Layers 0 and 1 |
| **3** | `mutation` | Immediate archetype transitions & command buffer playback | Layers 0, 1, and 2 |
| **4** | `concurrency` (`thread_pool`) | Barrier-synchronized work-stealing worker thread pool | Layer 0 only |
| **5** | `query` (`query`, `sort`) | Query iterators, SIMD tail padding, each_parallel, sort / permute | Layers 0..4 |
| **6** | `access` | Declarative component and resource access footprints | Layers 0 and 1 (`resource`) |
| **7** | `system` (`system_context`, `system_types`) | System execution scope and function signatures | Layers 0..6 |
| **8** | `scheduler` | Conflict graph compilation, topological sort, batch dispatch | Layers 0..7 |
| **9** | `plugin` | Autonomous plugin installation and system registration | Layers 6, 7, 8 |
| **10** | `views` | Domain facades (`archetype_view`, `entity_view`, `resource_view`, `snapshot_view`, `system_view`) | Layers 0..9 |
| **11** | `world` | Central World assembly, storage, facade coordinator | All lower layers |
| **12** | `arche` | Umbrella C++20 module (`src/arche.cppm`) | All layers |

---

## Comparative Benchmarks (arche-ecs vs. Flecs vs. EnTT)

Empirically verified against **Flecs** (v4.0.4, archetype table engine) and **EnTT** (v3.13.2, sparse-set engine) under identical hardware conditions (Apple Silicon M-Series, Clang C++20, `-O3 -DNDEBUG`). 

All benchmark telemetry is serialized directly to [build/benchmark_results.json](build/benchmark_results.json). Run `make charts` to view the interactive dashboard in [docs/benchmarks/benchmark_dashboard.html](docs/benchmarks/benchmark_dashboard.html).

### 1. Memory Footprint & Steady-State Zero Allocations (1,000,000 Entities)

Measuring exact heap bytes allocated for 1,000,000 entities possessing `Position` (8 B) + `Velocity` (8 B) = 16.00 MB data payload:

| Engine & Architecture | Total Heap Allocated | Bytes / Entity | Overhead vs Payload (16 B) | Packing Efficiency | Steady-State Heap Allocs / Frame |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`arche-ecs (64KB Chunk Pages)`** | **38.73 MB** | **40.6 B** | **+24.6 B** | **39.4%** | **0 allocs (0 B)** ✅ |
| **`EnTT (Dense Sparse Pools)`** | 50.79 MB | 53.3 B | +37.3 B | 30.0% | **0 allocs (0 B)** ✅ |
| **`Flecs (Table Blocks)`** | 90.17 MB | 94.5 B | +78.5 B | 16.9% | **0 allocs (0 B)** ✅ |

> **Key Finding:** `arche-ecs` achieves the tightest memory packing of any tested engine, consuming **57% less memory than Flecs** and **24% less memory than EnTT**. All three engines guarantee zero steady-state heap allocations during gameplay.

### 2. Change Detection & Sparse Activity (500,000 Entities, 5% Active)

Querying 500,000 entities when only 25,000 (5%) have mutated since the previous tick:

| Implementation Candidate | Latency / op | Throughput | Relative Speedup vs Full Scan |
| :--- | :---: | :---: | :---: |
| **`arche-ecs (Mutated<Position> chunk skip)`** | **1.0 µs** | **964,834 op/s** | **~135× faster** than full scan |
| `arche-ecs (Full Scan baseline without filter)` | 139.7 µs | 7,158 op/s | 1.0× baseline |
| `EnTT (ActiveFlag Sparse View 5%)` | 69.9 µs | 14,303 op/s | ~13× faster than full scan |
| `Flecs (Filter iter.changed() table skipping)` | 779.1 µs | 1,284 op/s | Overhead of per-entity check |
| `Flecs (Full Scan baseline without filter)` | 120.0 µs | 8,334 op/s | 1.0× baseline |
| `EnTT (Full Scan baseline 500k)` | 901.9 µs | 1,109 op/s | 1.0× baseline |

> **Key Finding:** `arche-ecs` skips dormant chunks in $O(1)$ at the 64KB chunk boundary by checking the column modification tick header before touching entity data, executing in **1.0 µs**.

### 3. Extreme Scale: 2,000,000 Entities Simulation

| Benchmark Scenario | arche-ecs (Chunked SoA) | Flecs (Archetype Table) | EnTT (Sparse Set) | Analysis |
| :--- | :---: | :---: | :---: | :--- |
| **Realistic Swarm Physics**<br>*(Euler + Boundary Clamps)* | **2.68 ms** | 3.12 ms | 5.21 ms | **arche-ecs is 1.16× faster than Flecs, 1.94× faster than EnTT** |
| **Simple Streaming Iteration**<br>*(pos += vel \* dt)* | 962 µs | **515 µs** | 2,979 µs | Flecs leads pure trivial loops; both beat EnTT by 3×–5× |
| **Wide Entity Simulation**<br>*(500,000 Entities: 8 Components)* | 978 µs | **741 µs** | 6,297 µs | **Archetypes beat Sparse Sets by 6.4× to 8.5×** |

### 4. Multi-Threaded Query Scaling (1, 2, 4, 8 Threads)

Iterating **2,000,000 entities** across 1, 2, 4, and 8 worker threads:

| Threads | `arche-ecs (query_parallel)` | `Flecs (multi_threaded)` | arche-ecs Scaling Factor |
| :---: | :---: | :---: | :---: |
| **1 Thread** | 965.8 µs | 530.2 µs | 1.0× (Baseline) |
| **2 Threads** | 369.4 µs | 538.5 µs | **2.61× speedup** |
| **4 Threads** | **295.1 µs** | 998.7 µs | **3.27× speedup** |
| **8 Threads** | 373.7 µs | 462.8 µs | Memory bandwidth saturation |

### 5. Full-Frame 10-System Game Loop Simulation (100k Entities)

Full frame execution: 10 interleaved read/write systems with resource access (`TimeStep`, `AudioQueue`), component mutations, death cleanup, and spawn:

| Framework & Dispatch Model | Frame Latency | Frame Rate (FPS) |
| :--- | :---: | :---: |
| **`Flecs (Pipeline 1 Thread)`** | **315.8 µs** | **3,167 FPS** |
| **`Flecs (Pipeline 8 Threads)`** | 349.6 µs | 2,861 FPS |
| **`arche-ecs (Scheduler 1 Thread)`** | 755.5 µs | 1,324 FPS |
| **`arche-ecs (Scheduler 8 Threads)`** | 767.2 µs | 1,303 FPS |
| **`EnTT (Sequential 1 Thread)`** | 842.5 µs | 1,187 FPS |

> **Key Finding:** `arche-ecs`'s automatic conflict-resolving DAG scheduler runs **10% faster than EnTT's manual sequential loop** while providing stage sorting, safety isolation, and deferred command playback.

### 6. Entity Lifecycle & Structural Mutations (20k Entities)

| Operation (20,000 Entities) | arche-ecs | EnTT | Flecs | Real-World Advantage |
| :--- | :---: | :---: | :---: | :--- |
| **Bulk Entity Spawning** | 807 µs | **359 µs** | 5,195 µs | arche-ecs is **6.4× faster than Flecs** |
| **Single Spawn Loop** | 5,507 µs | **359 µs** | 5,195 µs | EnTT sparse sets excel at single-item creation |
| **Add Component (Mutation)** | 8,697 µs | **342 µs** | 5,203 µs | Sparse pool appends beat table migrations |
| **Entity Destruction (Despawn)** | 7,446 µs | **689 µs** | 5,165 µs | Swap-remove chunk compaction overhead |

### 7. Dynamic Archetype Churn (50k Entities Buff/Tag Cycling)

Continuously adding and removing temporary buffs/tags on 50,000 entities per frame:

| Dynamic Churn Pattern | `arche-ecs` | `Flecs` | `EnTT` |
| :--- | :---: | :---: | :---: |
| **Add & Remove SpeedBuff (50k Entities)** | 22.97 ms | 8.27 ms | **0.83 ms** |
| **Multi-Archetype 4-Way Alternating Cycle** | 23.04 ms | 8.04 ms | **0.87 ms** |

---

## Comparative Performance Analysis & Honest Diagnostic

To keep `arche-ecs` accountable, we explicitly record where and why competitors currently win, and what is required to close the gap.

### The Harsh Numbers: Where arche-ecs Lost

| Benchmark Scenario | `arche-ecs` | `Flecs` | `EnTT` | Reality |
| :--- | :---: | :---: | :---: | :--- |
| **Simple Streaming (2M)** | 962 µs | **515 µs** | 2,979 µs | **Flecs is ~1.9× faster** |
| **Idiomatic Range-For (2M)** | 4,418 µs | **536 µs** | 2,979 µs | **Flecs is ~8× faster; our iterator is horribly slow** |
| **Game Loop (100k, 10 sys)** | 755 µs | **315 µs** | 842 µs | **Flecs is ~2.4× faster** |
| **Random Component Lookup** | 2,444 µs | 986 µs | **492 µs** | **EnTT is 5× faster, Flecs is 2.5× faster** |
| **Entity Creation (20k single)** | 5,515 µs | 5,270 µs | **359 µs** | **EnTT crushes both by 15×** |
| **Add Component (20k)** | 8,737 µs | 5,321 µs | **342 µs** | **EnTT is 25× faster, Flecs is 1.6× faster** |
| **Entity Destruction (20k)** | 7,452 µs | 5,182 µs | **691 µs** | **EnTT is 10× faster, Flecs is 1.4× faster** |
| **Dynamic Churn (50k Add/Remove)**| 22.9 ms | 8.2 ms | **0.83 ms** | **EnTT is 27× faster, Flecs is ~3× faster** |

### Why We Are Losing

1. **Range-for Iterator is Broken Performance-Wise (4.4 ms vs Flecs 0.5 ms):**
   - While `each_chunk` is fast, `for (auto [pos, vel] : query)` is 4.5× slower than our own chunk loop and 8× slower than Flecs. The iterator recalculates chunk pointers, row offsets, and boundaries on every `++` increment, preventing compiler auto-vectorization and causing pipeline stalls.
2. **Random Access / `get<T>()` Indirection (2.4 ms vs EnTT 0.5 ms):**
   - In EnTT, getting a component is a single direct array lookup (`pool[entity_id]`). In `arche-ecs`, `world.get<T>(e)` performs 4–5 memory hops (SlotMap $\to$ Archetype $\to$ linear search through `column_ids` $\to$ chunk offset).
3. **Uncached Archetype Transitions (8.7 ms vs Flecs 5.3 ms vs EnTT 0.3 ms):**
   - When calling `world.add<T>(e)`, `arche-ecs` takes the current bitmask, sets the bit, and searches the `ArchetypeIndex` hash map / list dynamically. Flecs uses a pre-linked graph (`archetype->edges[X].add`) for an $O(1)$ single pointer dereference.
4. **Scheduler Dynamic Dispatch Overhead (755 µs vs Flecs 315 µs):**
   - Flecs compiles systems down to flat arrays of matching tables. Our scheduler re-evaluates dependency rules, stage barriers, and command queues dynamically each frame, adding ~400 µs of dispatch overhead.

### What Specifically Needs to Be Fixed (Optimization Roadmap)

1. **Fix the Iterator (P0):** Redesign `QueryIterator` to hold a direct pointer span to the current chunk's slice. Advance to the next chunk only when the span ends. This allows compilers to auto-vectorize idiomatic range-for loops to match `each_chunk` speeds.
2. **Add an Archetype Transition Graph (P0):** Add `std::vector<Archetype*> add_edges` and `remove_edges` inside each `Archetype` node to turn migrations into $O(1)$ pointer hops.
3. **Cache Column Indices (P1):** Eliminate runtime column index searches in `get<T>()` by caching column indices in component accessors or indexing by compact ComponentIDs.
4. **Pre-Bake Scheduler Dispatch Batches:** Pre-sort and pre-match system-table batches at registration time so `world.run()` executes a branchless loop of function pointers.

---

## Zero-Loss Integration Guide

### C++20 Module Import

Consume `arche-ecs` directly as a native C++20 module:

```cpp
import arche;
```

### CMake Subproject Setup

Add `arche-ecs` via `FetchContent` or as a Git submodule:

```cmake
include(FetchContent)

FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/TomWeimer/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

add_executable(my_simulation src/main.cpp)
target_link_libraries(my_simulation PRIVATE arche)
```

---

## Modern API Usage Guide

### 1. Defining Components & Resources

Components and resources are strictly POD structs:

```cpp
import arche;

struct Position { float x{0.0f}, y{0.0f}; };
struct Velocity { float vx{0.0f}, vy{0.0f}; };
struct Health { float hp{100.0f}; };

// Singleton resource (global state)
struct GameConfig {
    float gravity{-9.81f};
    float delta_time{0.016f};
};
```

### 2. Initializing the World & Entity Lifecycle

```cpp
arche::World world;

// Register global resource
world.add_resource<GameConfig>(GameConfig{-9.81f, 0.016f});

// Spawn entity (returns generational handle)
arche::Entity e = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, Health{100.0f});

// Inspect component
assert(world.has<Position>(e));
world.get<Position>(e).x += 5.0f;

// Despawn (O(1) chunk swap-and-pop)
world.despawn(e);
assert(!world.entities.contains(e));
```

### 3. Bulk Spawning (Broadcasting & SoA Ingestion)

```cpp
// A. Prototype broadcasting: spawns 100,000 identical entities
std::vector<arche::Entity> handles = world.spawn(
    {.count = 100'000},
    Position{0.0f, 0.0f},
    Velocity{1.0f, 1.0f}
);

// B. Direct SoA streaming: ingest pre-computed arrays with zero heap allocations
std::vector<Position> positions(50'000);
std::vector<Velocity> velocities(50'000);
std::vector<arche::Entity> out_ents(50'000);

world.spawn(
    {.count = 50'000},
    std::span<arche::Entity>(out_ents),
    positions.data(),
    velocities.data()
);
```

### 4. Component Access & Structural Mutation

```cpp
arche::Entity e = world.spawn(Position{0.0f, 0.0f});

// Add component (moves entity to [Position, Velocity] archetype)
world.add(e, Velocity{1.0f, 2.0f});

// Remove component (moves entity back to [Position] archetype)
world.remove<Velocity>(e);
```

### 5. Querying & Iteration

```cpp
// Idiomatic range-for iteration
for (auto [pos, vel] : world.query<Position, const Velocity>()) {
    pos.x += vel.vx * 0.016f;
    pos.y += vel.vy * 0.016f;
}

// Hot-path cache-aligned SoA chunk iteration
world.query<Position, const Velocity>().each_chunk(
    [](uint32_t count, Position* pos, const Velocity* vel) {
        #pragma clang loop vectorize(enable)
        for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.016f;
            pos[i].y += vel[i].vy * 0.016f;
        }
    }
);
```

### 6. Declarative Plugins & Conflict-Resolving DAG Scheduler

```cpp
struct MovementPlugin {
    static std::string_view name() { return "MovementPlugin"; }

    void build(arche::World&, arche::PluginContext& ctx) {
        ctx.add_system(
            "MovementSystem",
            arche::Access{}.writes<Position>().reads<Velocity>().build(),
            [](arche::SystemContext& ctx) {
                // System runs safely in parallel batch based on access footprint
            }
        );
    }
};

world.add_plugin<MovementPlugin>();
world.set_threads(8); // Multi-threaded worker pool
world.run();          // Advances tick, dispatches parallel batches, commits commands
```

### 7. Change Detection via Dirty Ticks (`Mutated` & `Added`)

```cpp
world.run(); // Advances world version
const uint32_t baseline_tick = world.version();

// Modify one entity
world.get_mut<Position>(e).x += 10.0f; // Stamps column modification tick

// Skips dormant chunks in O(1) at chunk boundary (1.0 µs across 500k entities)
world.query<Position>(arche::Mutated<Position>{baseline_tick})
    .each_chunk([](uint32_t count, Position* p) {
        // Only executes for chunks mutated since baseline_tick
    });
```

### 8. In-Place Spatial Sorting & Cache Locality

```cpp
// Sort entities in-place via cycle chasing permutation to maximize cache hits
world.sort<Position>(
    archetype_ptr,
    [](const Position& a, const Position& b) { return a.x < b.x; }
);
```

---

## Building, Testing & Visualizing

```bash
# 1. Build the library and test suite
make build

# 2. Run Architecture-as-Code linter and test suite (25/25 tests)
make lint
make test

# 3. Run hardware & comparative benchmarks
make benchmark

# 4. Generate interactive HTML dashboard and markdown charts
make charts
```

Interactive charts are generated to [docs/benchmarks/benchmark_dashboard.html](docs/benchmarks/benchmark_dashboard.html).

---

## Project Layout

```text
arche-ecs/
├── CMakeLists.txt                      # CMake build definitions (C++20 modules)
├── Makefile                            # Ergonomic build, lint, benchmark & charts targets
├── src/                                # Layered C++20 Module Partitions (AaC Verified)
│   ├── arche.cppm                      # Root umbrella module interface
│   ├── foundation/                     # Layer 0: Hardware, types, entity, component ID & mask
│   ├── storage/                        # Layer 1: 64KB chunk page memory, slot_map, resource, command
│   ├── archetype/                      # Layer 2: Archetype tables, index & graph
│   ├── mutation/                       # Layer 3: Immediate transitions & command buffer playback
│   ├── runtime/
│   │   ├── concurrency/                # Layer 4: Barrier work-stealing thread pool
│   │   ├── query/                      # Layer 5: Queries, iterators, filters (With/Without)
│   │   ├── sort/                       # Layer 5: In-place cycle-chasing permutation
│   │   ├── access/                     # Layer 6: Declarative read/write access footprints
│   │   ├── system/                     # Layer 7: SystemContext & system signatures
│   │   ├── scheduler/                  # Layer 8: Conflict DAG compilation & batch dispatch
│   │   └── plugin/                     # Layer 9: Modular plugin lifecycle
│   ├── views/                          # Layer 10: Domain facades (entity, archetype, resource, snapshot)
│   └── world/                          # Layer 11: World storage assembly & facade composition
├── tests/                              # 25 Dedicated Unit Test Suites (100% passing)
│   ├── entity_tests.cpp                # Generational handle integrity
│   ├── component_id_tests.cpp          # ComponentID assignment & reflection
│   ├── archetype_tests.cpp             # Archetype creation & commutativity
│   ├── chunk_storage_tests.cpp         # 64KB chunk memory layout & alignment
│   ├── structural_mutation_tests.cpp   # Add/remove & swap-and-pop compaction
│   ├── query_tests.cpp                 # Superset queries & SIMD iteration
│   ├── query_caching_tests.cpp         # Event-driven query caching
│   ├── batch_lifecycle_tests.cpp       # Bulk spawn & SoA streaming
│   ├── dirty_ticks_tests.cpp           # Monotonic version tracking & chunk skipping
│   ├── archetype_permutation_tests.cpp # In-place cycle-chasing sort
│   ├── double_buffered_tests.cpp       # Dual-slice double buffering
│   ├── multithreaded_query_tests.cpp   # Multi-threaded query dispatch
│   ├── work_stealing_tests.cpp         # Barrier thread pool & task stealing
│   ├── scheduler_tests.cpp             # DAG conflict resolution & dispatch
│   ├── plugin_tests.cpp                # Plugin installation & system wiring
│   ├── world_facade_tests.cpp          # Facade views & encapsulation
│   ├── resource_tests.cpp              # Singleton resources
│   ├── access_tests.cpp                # Access footprint declarations
│   ├── simd_and_hardware_tests.cpp     # SIMD bounds & CPU topology
│   ├── stage2_validation_tests.cpp     # Stage 2 validation integration
│   ├── handles_and_snapshots_tests.cpp # SlotMap & binary snapshots
│   └── public_api_tests.cpp            # Public API regression suite
├── docs/benchmarks/                    # Visualizations & Telemetry
│   ├── benchmark_dashboard.html        # Interactive responsive SVG dashboard
│   └── BENCHMARK_RESULTS.md            # Generated Markdown telemetry tables
└── scripts/
    ├── check_architecture.py           # Architecture-as-Code boundary validator
    └── benchmarks/
        ├── arche_benchmarks.cpp        # Cycle-accurate TSC hardware profiler
        ├── comparative_benchmarks.cpp  # Head-to-head vs Flecs & EnTT (2M scale)
        └── benchmark_charts.py         # Reusable chart & dashboard generator
```

---

## License

This project is licensed under the [MIT License](LICENSE).
