# Arche ECS

<p align="center">
  <img src="docs/images/arche_ecs_banner.jpg" alt="Arche ECS Banner" width="600">
</p>

<p align="center">
  A C++20 archetype entity component system (ECS) library designed for dense data-oriented simulations.
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20%20Modules-blue.svg" alt="C++20 Modules"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/Tests-25%2F25%20Passing-brightgreen.svg" alt="Build Status">
  <img src="https://img.shields.io/badge/Architecture-AaC%20Verified-blueviolet.svg" alt="Architecture as Code">
  <a href="LLMS.md"><img src="https://img.shields.io/badge/AI%20Context-LLMS.md-informational.svg" alt="LLMS.md"></a>
</p>

---

## Table of Contents

- [Architectural Rules & Invariants](#architectural-rules--invariants)
- [Layered Architecture & Strict Dependency Flow](#layered-architecture--strict-dependency-flow)
- [Comparative Benchmark Results](#comparative-benchmark-results)
  - [1. Memory Consumption (1,000,000 Entities)](#1-memory-consumption-1000000-entities)
  - [2. Change Detection (500,000 Entities, 5% Mutated)](#2-change-detection-500000-entities-5-mutated)
  - [3. Streaming & Swarm Iteration (2,000,000 Entities)](#3-streaming--swarm-iteration-2000000-entities)
  - [4. Multi-Threaded Query Scaling (1, 2, 4, 8 Threads)](#4-multi-threaded-query-scaling-1-2-4-8-threads)
  - [5. Full-Frame 10-System Game Loop (100k Entities)](#5-full-frame-10-system-game-loop-100k-entities)
  - [6. Entity Lifecycle & Structural Mutation (20k Entities)](#6-entity-lifecycle--structural-mutation-20k-entities)
  - [7. Dynamic Archetype Churn (50k Entities Buff Cycling)](#7-dynamic-archetype-churn-50k-entities-buff-cycling)
- [Performance Analysis & Areas of Failure](#performance-analysis--areas-of-failure)
  - [Direct Comparison Table](#direct-comparison-table)
  - [Root Causes of Slowdowns](#root-causes-of-slowdowns)
  - [Planned Fixes (Next Steps)](#planned-fixes-next-steps)
- [Integration](#integration)
- [API Reference & Usage](#api-reference--usage)
  - [1. Defining Components & Resources](#1-defining-components--resources)
  - [2. Initializing World & Entity Lifecycle](#2-initializing-world--entity-lifecycle)
  - [3. Bulk Spawning](#3-bulk-spawning)
  - [4. Structural Mutation](#4-structural-mutation)
  - [5. Iteration & Queries](#5-iteration--queries)
  - [6. Systems, Access Footprints & Scheduler](#6-systems-access-footprints--scheduler)
  - [7. Change Detection via Dirty Ticks](#7-change-detection-via-dirty-ticks)
  - [8. In-Place Sorting](#8-in-place-sorting)
- [Building & Running Benchmarks](#building--running-benchmarks)
- [Project Layout](#project-layout)
- [License](#license)

---

## Architectural Rules & Invariants

1. **Flat Data over Abstraction:**
   - No virtual methods, no abstract classes, no polymorphic interfaces, no runtime RTTI.
   - Plain Old Data (POD) structs only. Components are laid out in 64 KB cache-aligned chunk pages in Structure-of-Arrays (SoA) order.
2. **Zero Consumer Context:**
   - Standard library (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`, `<span>`, `<concepts>`) and CPU intrinsics only.
   - No game logic, math libraries, or external engine dependencies in this repository.
3. **Deterministic Memory Operations:**
   - Generational `SlotMap` handle routing.
   - Constant-time chunk compaction via swap-remove.
4. **Declarative Concurrency:**
   - Explicit component read/write access declarations (`Access{}.reads<T>().writes<U>()`).
   - Dependency graph compilation with batch dispatch across a worker thread pool.

---

## Layered Architecture & Strict Dependency Flow

Dependencies flow strictly top-down. Lower layers never depend on or include higher layers. Validated by `make lint` (`scripts/check_architecture.py`).

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

## Comparative Benchmark Results

Tested against **Flecs** (v4.0.4, archetype table engine) and **EnTT** (v3.13.2, sparse-set engine) under identical conditions (Apple Silicon M-Series, Clang C++20, `-O3 -DNDEBUG`). 

Raw telemetry is serialized to [build/benchmark_results.json](build/benchmark_results.json). Run `make charts` to update the SVG dashboard at [docs/benchmarks/benchmark_dashboard.html](docs/benchmarks/benchmark_dashboard.html).

### 1. Memory Consumption (1,000,000 Entities)

1,000,000 entities possessing `Position` (8 B) + `Velocity` (8 B) = 16.00 MB component payload:

| Engine | Heap Allocated | Bytes / Entity | Overhead vs Payload (16 B) | Packing Efficiency | Allocations in Steady-State |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`arche-ecs`** | **38.73 MB** | **40.6 B** | **+24.6 B** | **39.4%** | **0 allocs (0 B)** |
| **`EnTT`** | 50.79 MB | 53.3 B | +37.3 B | 30.0% | **0 allocs (0 B)** |
| **`Flecs`** | 90.17 MB | 94.5 B | +78.5 B | 16.9% | **0 allocs (0 B)** |

### 2. Change Detection (500,000 Entities, 5% Mutated)

Querying 500,000 entities where 25,000 (5%) have changed since the previous tick:

| Candidate | Latency / op | Throughput | Notes |
| :--- | :---: | :---: | :--- |
| **`arche-ecs (Mutated<Position> chunk skip)`** | **1.0 µs** | 964,834 op/s | Evaluates modification tick in chunk header |
| `EnTT (ActiveFlag Sparse View 5%)` | 69.9 µs | 14,303 op/s | Sparse view iterating only active entities |
| `Flecs (Full Scan baseline)` | 120.0 µs | 8,334 op/s | Standard full table query |
| `arche-ecs (Full Scan baseline)` | 139.7 µs | 7,158 op/s | Querying without dirty tick filter |
| `Flecs (Filter iter.changed() check)` | 779.1 µs | 1,284 op/s | Table-level changed check overhead |
| `EnTT (Full Scan baseline 500k)` | 901.9 µs | 1,109 op/s | Full sparse view iteration |

### 3. Streaming & Swarm Iteration (2,000,000 Entities)

| Scenario | arche-ecs (Chunked SoA) | Flecs (Archetype Table) | EnTT (Sparse Set) | Notes |
| :--- | :---: | :---: | :---: | :--- |
| **Realistic Swarm Physics** (Euler + bounds) | **2.68 ms** | 3.12 ms | 5.21 ms | Tightly packed cache lines benefit branchy math |
| **Simple Streaming** (`pos += vel * dt`) | 962 µs | **515 µs** | 2,979 µs | Flecs table iterators are faster on trivial loops |
| **Wide Simulation** (500k entities, 8 comps) | 978 µs | **741 µs** | 6,297 µs | Dense tables avoid EnTT sparse pool pointer hopping |

### 4. Multi-Threaded Query Scaling (1, 2, 4, 8 Threads)

Iterating **2,000,000 entities** across worker threads:

| Threads | `arche-ecs (query_parallel)` | `Flecs (multi_threaded)` |
| :---: | :---: | :---: |
| **1 Thread** | 965.8 µs | 530.2 µs |
| **2 Threads** | 369.4 µs | 538.5 µs |
| **4 Threads** | **295.1 µs** | 998.7 µs |
| **8 Threads** | 373.7 µs | 462.8 µs |

### 5. Full-Frame 10-System Game Loop (100k Entities)

10 read/write systems with resources, mutation, death cleanup, and spawn:

| Framework | Latency / Frame | Frame Rate (FPS) |
| :--- | :---: | :---: |
| **`Flecs (Pipeline 1 Thread)`** | **315.8 µs** | **3,167 FPS** |
| **`Flecs (Pipeline 8 Threads)`** | 349.6 µs | 2,861 FPS |
| **`arche-ecs (Scheduler 1 Thread)`** | 755.5 µs | 1,324 FPS |
| **`arche-ecs (Scheduler 8 Threads)`** | 767.2 µs | 1,303 FPS |
| **`EnTT (Sequential 1 Thread)`** | 842.5 µs | 1,187 FPS |

### 6. Entity Lifecycle & Structural Mutation (20k Entities)

| Operation | arche-ecs | EnTT | Flecs | Notes |
| :--- | :---: | :---: | :---: | :--- |
| **Bulk Spawning** | 807 µs | **359 µs** | 5,195 µs | Chunk block spawn vs individual emplace |
| **Single Spawn Loop** | 5,507 µs | **359 µs** | 5,195 µs | Sparse set push is faster than archetype allocation |
| **Add Component (Mutation)** | 8,697 µs | **342 µs** | 5,203 µs | Archetype migration copies columns; EnTT appends pool |
| **Entity Despawn** | 7,446 µs | **689 µs** | 5,165 µs | Swap-remove row compaction overhead |

### 7. Dynamic Archetype Churn (50k Entities Buff Cycling)

Adding and removing a component on 50,000 entities every frame:

| Benchmark | `arche-ecs` | `Flecs` | `EnTT` |
| :--- | :---: | :---: | :---: |
| **Add & Remove SpeedBuff (50k entities)** | 22.97 ms | 8.27 ms | **0.83 ms** |
| **Multi-Archetype 4-Way Alternating Cycle** | 23.04 ms | 8.04 ms | **0.87 ms** |

---

## Performance Analysis & Areas of Failure

The benchmarks highlight several areas where `arche-ecs` is significantly slower than existing solutions.

### Direct Comparison Table

| Benchmark | `arche-ecs` | `Flecs` | `EnTT` | Observation |
| :--- | :---: | :---: | :---: | :--- |
| **Simple Streaming (2M)** | 962 µs | **515 µs** | 2,979 µs | Flecs is ~1.9× faster |
| **Idiomatic Range-For (2M)** | 4,418 µs | **536 µs** | 2,979 µs | **Flecs is ~8× faster; our iterator is poorly optimized** |
| **Game Loop (100k, 10 sys)** | 755 µs | **315 µs** | 842 µs | Flecs is ~2.4× faster |
| **Random Component Lookup** | 2,444 µs | 986 µs | **492 µs** | EnTT is 5× faster, Flecs is 2.5× faster |
| **Entity Creation (20k single)** | 5,515 µs | 5,270 µs | **359 µs** | EnTT is 15× faster |
| **Add Component (20k)** | 8,737 µs | 5,321 µs | **342 µs** | EnTT is 25× faster, Flecs is 1.6× faster |
| **Entity Destruction (20k)** | 7,452 µs | 5,182 µs | **691 µs** | EnTT is 10× faster, Flecs is 1.4× faster |
| **Dynamic Churn (50k Add/Remove)**| 22.9 ms | 8.2 ms | **0.83 ms** | EnTT is 27× faster, Flecs is ~3× faster |

### Root Causes of Slowdowns

1. **Range-for Iterator Overhead (4.4 ms vs. Flecs 0.5 ms):**
   - While `each_chunk` runs in 962 µs, `for (auto [pos, vel] : query)` takes 4,418 µs.
   - The iterator steps entity-by-entity in abstraction, recalculating chunk pointers and row boundaries on every `++`. The compiler cannot auto-vectorize the loop, causing instruction stalls on each entity.
2. **Random Component Lookup Indirection (2.4 ms vs. EnTT 0.5 ms):**
   - In EnTT, getting a component is an index into a dense array (`pool[entity_id]`).
   - In `arche-ecs`, `world.get<T>(e)` traverses multiple hops: SlotMap $\to$ Archetype $\to$ linear search through `column_ids` to locate `T` $\to$ chunk address computation.
3. **Uncached Archetype Transitions (8.7 ms vs. Flecs 5.3 ms vs. EnTT 0.3 ms):**
   - On `world.add<T>(e)`, `arche-ecs` modifies the bitmask and searches the global `ArchetypeIndex` table list dynamically.
   - Flecs uses a pre-linked graph (`archetype->edges[X].add`) for an $O(1)$ pointer hop.
   - EnTT avoids migrations entirely by appending to an independent dense pool.
4. **Scheduler Dispatch Overhead (755 µs vs. Flecs 315 µs):**
   - Flecs evaluates tables and executes flat function arrays.
   - `arche::Scheduler` dynamically checks DAG stages, barrier synchronization points, and deferred command queues each frame, adding ~400 µs of runtime overhead.

### Planned Fixes (Next Steps)

1. **Query Iterator Redesign (P0):**
   - Re-implement `QueryIterator` to hold a contiguous pointer slice to the active chunk. Advance to the next chunk only when the slice boundary is reached, allowing compilers to auto-vectorize range-for loops.
2. **Archetype Transition Graph (P0):**
   - Add cached transition edge pointers (`add_edges`, `remove_edges`) to `Archetype` nodes to make transitions a direct pointer lookup rather than a table search.
3. **Component Column Index Caching (P1):**
   - Eliminate linear searches over `column_ids` in `get<T>()` by caching column indices in query/access paths or using small lookup tables.
4. **Pre-Baked Scheduler Dispatch Batches:**
   - Pre-sort systems and matching tables at registration time so that `world.run()` executes a fixed sequence of function calls without runtime graph evaluation.

---

## Integration

### C++20 Module Import

```cpp
import arche;
```

### CMake Setup

```cmake
include(FetchContent)

FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/TomWeimer/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE arche)
```

---

## API Reference & Usage

### 1. Defining Components & Resources

```cpp
import arche;

struct Position { float x{0.0f}, y{0.0f}; };
struct Velocity { float vx{0.0f}, vy{0.0f}; };
struct Health { float hp{100.0f}; };

struct GameConfig {
    float gravity{-9.81f};
    float delta_time{0.016f};
};
```

### 2. Initializing World & Entity Lifecycle

```cpp
arche::World world;

// Singleton resource
world.add_resource<GameConfig>(GameConfig{-9.81f, 0.016f});

// Spawn entity
arche::Entity e = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, Health{100.0f});

// Component access
if (world.has<Position>(e)) {
    world.get<Position>(e).x += 5.0f;
}

// Despawn
world.despawn(e);
```

### 3. Bulk Spawning

```cpp
// Broadcast spawn
std::vector<arche::Entity> handles = world.spawn(
    {.count = 100'000},
    Position{0.0f, 0.0f},
    Velocity{1.0f, 1.0f}
);

// SoA array streaming
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

### 4. Structural Mutation

```cpp
arche::Entity e = world.spawn(Position{0.0f, 0.0f});

// Add component
world.add(e, Velocity{1.0f, 2.0f});

// Remove component
world.remove<Velocity>(e);
```

### 5. Iteration & Queries

```cpp
// Range-for loop
for (auto [pos, vel] : world.query<Position, const Velocity>()) {
    pos.x += vel.vx * 0.016f;
    pos.y += vel.vy * 0.016f;
}

// Chunk iteration
world.query<Position, const Velocity>().each_chunk(
    [](uint32_t count, Position* pos, const Velocity* vel) {
        for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.016f;
            pos[i].y += vel[i].vy * 0.016f;
        }
    }
);
```

### 6. Systems, Access Footprints & Scheduler

```cpp
struct MovementPlugin {
    static std::string_view name() { return "MovementPlugin"; }

    void build(arche::World&, arche::PluginContext& ctx) {
        ctx.add_system(
            "MovementSystem",
            arche::Access{}.writes<Position>().reads<Velocity>().build(),
            [](arche::SystemContext& ctx) {
                // System runs safely in parallel batch based on access footprints
            }
        );
    }
};

world.add_plugin<MovementPlugin>();
world.set_threads(8);
world.run();
```

### 7. Change Detection via Dirty Ticks

```cpp
world.run();
const uint32_t baseline = world.version();

world.get_mut<Position>(e).x += 10.0f; // Stamps column modification version

// Skips unmutated chunks in O(1)
world.query<Position>(arche::Mutated<Position>{baseline})
    .each_chunk([](uint32_t count, Position* p) {
        // Only runs on chunks mutated after baseline
    });
```

### 8. In-Place Sorting

```cpp
world.sort<Position>(
    archetype_ptr,
    [](const Position& a, const Position& b) { return a.x < b.x; }
);
```

---

## Building & Running Benchmarks

```bash
# Build
make build

# Architecture validation & test suite
make lint
make test

# Benchmarks
make benchmark

# Re-generate charts & dashboard
make charts
```

Interactive charts are generated to [docs/benchmarks/benchmark_dashboard.html](docs/benchmarks/benchmark_dashboard.html).

---

## Project Layout

```text
arche-ecs/
├── CMakeLists.txt                      # CMake build definitions (C++20 modules)
├── Makefile                            # Build, lint, test, benchmark & charts targets
├── src/                                # Layered C++20 Module Partitions (AaC Verified)
│   ├── arche.cppm                      # Root umbrella module interface
│   ├── foundation/                     # Layer 0: Hardware, types, entity, component ID & mask
│   ├── storage/                        # Layer 1: 64KB chunk page memory, slot_map, resource, command
│   ├── archetype/                      # Layer 2: Archetype tables, index & graph
│   ├── mutation/                       # Layer 3: Immediate transitions & command buffer playback
│   ├── runtime/
│   │   ├── concurrency/                # Layer 4: Barrier work-stealing thread pool
│   │   ├── query/                      # Layer 5: Queries, iterators, filters
│   │   ├── sort/                       # Layer 5: In-place cycle-chasing permutation
│   │   ├── access/                     # Layer 6: Declarative access footprints
│   │   ├── system/                     # Layer 7: SystemContext & signatures
│   │   ├── scheduler/                  # Layer 8: Conflict DAG compilation & batch dispatch
│   │   └── plugin/                     # Layer 9: Modular plugin lifecycle
│   ├── views/                          # Layer 10: Domain facades (entity, archetype, resource, snapshot)
│   └── world/                          # Layer 11: World storage assembly & facade composition
├── tests/                              # 25 Unit Test Suites
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
