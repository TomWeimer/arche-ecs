# Arche ECS

A standalone, ultra-high-performance C++20 Archetype Entity Component System (ECS) library designed for extreme-density simulations (50,000+ entities @ 120 Hz).

---

## Table of Contents

- [Architectural Principles & Hard Invariants](#architectural-principles--hard-invariants)
- [Zero-Loss Integration Guide](#zero-loss-integration-guide)
  - [CMake Subproject Setup](#cmake-subproject-setup)
  - [Crucial Compiler Flags for Maximum Performance](#crucial-compiler-flags-for-maximum-performance)
- [Core Concepts & Usage Guide](#core-concepts--usage-guide)
  - [1. Defining Components](#1-defining-components)
  - [2. Initializing the World & Entity Lifecycle](#2-initializing-the-world--entity-lifecycle)
  - [3. Structural Mutations (Add / Remove Components)](#3-structural-mutations-add--remove-components)
  - [4. Querying & Iteration](#4-querying--iteration)
    - [Range-Based For-Loops & Structured Bindings](#range-based-for-loops--structured-bindings)
    - [Querying Entity Handles](#querying-entity-handles)
    - [Query Filters (`With` & `Without`)](#query-filters-with--without)
    - [Hot-Path SIMD Chunk Iteration (`for_each_chunk`)](#hot-path-simd-chunk-iteration-for_each_chunk)
  - [5. Deferred Mutations with `CommandBuffer`](#5-deferred-mutations-with-commandbuffer)
  - [6. GPU std430 Reflection & Zero-Repack Streaming](#6-gpu-std430-reflection--zero-repack-streaming)
    - [Compile-Time std430 Safety Traits](#compile-time-std430-safety-traits)
    - [Approach A: Batched Linear Upload (Universal DMA)](#approach-a-batched-linear-upload-universal-dma)
    - [Approach B: Zero-Copy Chunk Spans](#approach-b-zero-copy-chunk-spans)
- [Performance Benchmarks](#performance-benchmarks)
- [Architectural Roadmap & The 4 ECS Stages](#architectural-roadmap--the-4-ecs-stages)
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

---

## Zero-Loss Integration Guide

To consume `arche-ecs` in an external consumer project (e.g. game engine, flocking simulation) **without losing a single clock cycle of performance**, follow these guidelines:

### CMake Subproject Setup

Add `arche-ecs` via `FetchContent` or as a Git submodule inside your `extern/` or `vendor/` directory:

```cmake
# In your project's CMakeLists.txt:
include(FetchContent)

FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/<your-username>/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

# Or, if using a git submodule / vendored copy:
# add_subdirectory(extern/arche-ecs)

add_executable(my_simulation src/main.cpp)
target_link_libraries(my_simulation PRIVATE arche::arche)
```

### Crucial Compiler Flags for Maximum Performance

Because `arche-ecs` is compiled as a static library, configure your consumer target with the following optimizations:

```cmake
# 1. Enable Link-Time Optimization (LTO / IPO)
# Allows the compiler to inline cross-translation-unit calls between your code and libarche.a
set_property(TARGET my_simulation PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set_property(TARGET arche PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# 2. Enable Native SIMD Vectorization
if (MSVC)
    target_compile_options(my_simulation PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(my_simulation PRIVATE -O3 -march=native -ffast-math)
endif()

# 3. Disable RTTI (arche-ecs does not use dynamic_cast or typeid lookups)
if (NOT MSVC)
    target_compile_options(my_simulation PRIVATE -fno-rtti)
endif()
```

---

## Core Concepts & Usage Guide

### 1. Defining Components

Components in `arche-ecs` are plain C++ structs. They require no base classes, registration macros, or interface inheritance:

```cpp
#include <cstdint>

struct Position {
    float x{0.0f};
    float y{0.0f};
};

struct Velocity {
    float vx{0.0f};
    float vy{0.0f};
};

struct Mass {
    float kilograms{1.0f};
};

// Tag components (0 bytes in chunk columns)
struct TagBoid {};
struct TagPredator {};
struct TagDead {};
```

---

### 2. Initializing the World & Entity Lifecycle

The `World` is the central coordinator managing archetypes, chunk allocation pools, and entity slot routing:

```cpp
#include "arche/world.hpp"

arche::World world;

// 1. Spawning entities with arbitrary components (archetype inferred automatically)
arche::Entity boid = world.create_entity(
    Position{10.0f, 20.0f},
    Velocity{1.0f, -0.5f},
    TagBoid{}
);

// 2. Checking handle validity (O(1) generational lookup)
if (world.is_valid(boid)) {
    // 3. Inspecting or mutating components directly
    Position& pos = world.get_component<Position>(boid);
    pos.x += 1.0f;
}

// 4. Destroying entities (O(1) Swap-and-Pop compaction)
world.destroy_entity(boid);

// Stale handles immediately fail validation
assert(!world.is_valid(boid));
```

---

### 3. Structural Mutations (Add / Remove Components)

Entities can transition between archetypes dynamically at runtime. The transition graph preserves and relocates existing components automatically:

```cpp
arche::Entity e = world.create_entity(Position{0.0f, 0.0f});

// Dynamically add a Velocity component: transitions entity to [Position, Velocity]
world.add_component(e, Velocity{1.5f, 2.5f});

// Dynamically tag entity as dead: transitions to [Position, Velocity, TagDead]
world.add_component(e, TagDead{});

// Remove a component: transitions entity back to [Position, Velocity]
world.remove_component<TagDead>(e);
```

---

### 4. Querying & Iteration

#### Range-Based For-Loops & Structured Bindings

The most intuitive way to iterate matching entities is via structured bindings:

```cpp
#include "arche/query.hpp"

// Matches any entity containing AT LEAST [Position, Velocity] (superset matching)
for (auto [pos, vel] : world.query<Position, Velocity>()) {
    pos.x += vel.vx * 0.016f;
    pos.y += vel.vy * 0.016f;
}
```

#### Querying Entity Handles

Include `arche::Entity` as a query parameter to retrieve entity IDs alongside component references:

```cpp
for (auto [entity, pos, vel] : world.query<arche::Entity, Position, Velocity>()) {
    if (pos.y < -100.0f) {
        // Mark or queue for destruction
    }
}
```

#### Query Filters (`With` & `Without`)

Refine queries with compile-time tag and component filters:

```cpp
using arche::With;
using arche::Without;

// Query entities with Position and Velocity, tagged as TagBoid, but NOT TagDead
auto boid_query = world.query<Position, Velocity>(
    With<TagBoid>{},
    Without<TagDead>{}
);

for (auto [pos, vel] : boid_query) {
    // Process active boids only
}
```

#### Hot-Path SIMD Chunk Iteration (`for_each_chunk`)

For maximum performance in simulation hot loops, bypass iterator dereference overhead and process entire contiguous 64 KB component columns at once:

```cpp
arche::Query<Position, Velocity> query(world);
constexpr float dt = 0.01666f;

// Executes in ~0.97 cycles per entity on Apple Silicon / x86-64!
query.for_each_chunk([dt](uint32_t count, Position* p, Velocity* v) {
    #pragma clang loop vectorize(enable)
    for (uint32_t i = 0; i < count; ++i) {
        p[i].x += v[i].vx * dt;
        p[i].y += v[i].vy * dt;
    }
});
```

---

### 5. Deferred Mutations with `CommandBuffer`

Directly adding components, removing components, or destroying entities while actively iterating through a query is unsafe because structural mutations alter chunk layouts.

`CommandBuffer` records mutations in an unformatted byte buffer and executes them safely outside the query loop:

```cpp
#include "arche/command_buffer.hpp"

arche::CommandBuffer cmd;

// Safe iteration: record mutations into CommandBuffer
for (auto [entity, pos] : world.query<arche::Entity, Position>()) {
    if (pos.y < 0.0f) {
        cmd.destroy_entity(entity);
    } else if (pos.x > 1000.0f) {
        cmd.add_component(entity, TagDead{});
    }
}

// Deferred creation:
cmd.create_entity(Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

// Safe sync point: flush and playback all recorded commands in FIFO order
cmd.playback(world);
```

---

### 6. GPU std430 Reflection & Zero-Repack Streaming

In hybrid CPU-GPU engines, repacking scattered entity data into vertex buffers wastes 2–4 ms per frame. Because `arche-ecs` stores identical components in contiguous Structure-of-Arrays (SoA) columns, data can be streamed directly into GPU VRAM with **zero repacking**.

#### Compile-Time std430 Safety Traits

GLSL `layout(std430)` rules dictate that arrays of 3D vectors (`vec3`) require **16-byte alignment and 16-byte strides**. Standard 12-byte C++ structs (`float[3]`) cause off-by-4-byte shader read corruption.

`arche-ecs` validates this at compile time:

```cpp
#include "arche/gpu_reflection.hpp"

// SAFE: 8 bytes (vec2), power-of-two aligned
struct GpuPosition { float x, y; };
static_assert(arche::GpuStd430Compatible<GpuPosition>);

// SAFE: explicitly padded to 16 bytes
struct alignas(16) SafeVec3 { float x, y, z, pad; };
static_assert(arche::GpuStd430Compatible<SafeVec3>);

// REJECTED AT COMPILE TIME: unpadded 12-byte struct
struct HazardVec3 { float x, y, z; };
// static_assert(arche::GpuStd430Compatible<HazardVec3>); // <-- Compile error!
```

#### Approach A: Batched Linear Upload (Universal DMA)

Upload all instances of a component across all chunks into a single GPU VRAM buffer with high-speed sequential memory copies:

```cpp
void* mapped_vram = vulkan_staging_buffer.map();

// Streams 50,000 particle positions (400 KB) in < 1 microsecond!
size_t bytes_written = world.stream_component_to<GpuPosition>(mapped_vram);

vulkan_staging_buffer.unmap();
// Render with a single draw call: vkCmdDraw(50000)
```

#### Approach B: Zero-Copy Chunk Spans

On platforms with Unified Memory (Apple Silicon) or Vulkan Buffer Device Address (BDA), bind chunk column pointers directly without copying any memory:

```cpp
// Returns non-owning spans over contiguous chunk memory
std::vector<arche::ComponentSpan<GpuPosition>> spans = 
    world.view_component_chunks<GpuPosition>();

for (const auto& span : spans) {
    const GpuPosition* chunk_data = span.data();
    uint32_t count = span.count();
    size_t bytes = span.size_bytes();

    // Bind chunk_data directly to vertex or storage buffer descriptor (0.00 µs copy cost!)
}
```

---

## Performance Benchmarks

Measured on Apple Silicon (M-Series, 3.2 GHz reference equivalent) via hardware cycle timing (`mach_absolute_time` / `cntvct_el0` / `__rdtsc`):

| Benchmark Workload | Scale | Metric | Target Budget | Result |
| :--- | :--- | :--- | :--- | :--- |
| **Dense Movement (`pos += vel * dt`)** | 50,000 Entities | **0.0151 ms / frame** | $\le 533\text{ cycles/entity}$ | **0.97 cycles / entity** |
| **Dense Movement (`pos += vel * dt`)** | 100,000 Entities | **0.0337 ms / frame** | $\le 533\text{ cycles/entity}$ | **1.08 cycles / entity** |
| **Dense Movement (`pos += vel * dt`)** | 500,000 Entities | **0.1715 ms / frame** | $\le 533\text{ cycles/entity}$ | **1.10 cycles / entity** |
| **Structural Mutation (Spawn/Despawn)** | 100,000 Entities | **5.11 ms** | $> 10\text{M ops/sec}$ | **39.12M ops / sec** |
| **Direct VRAM Streaming (Approach A)** | 50,000 Entities (400 KB) | **0.19 µs / frame** | $\le 50\ \mu\text{s}$ | **1,921 GB/s (Cache DMA)** |

---

## Architectural Roadmap & The 4 ECS Stages

`arche-ecs` is engineered for a specific class of computational problems: **extreme-density simulations** (50,000 to 500,000+ entities @ 120 Hz, such as flocking swarms, particle physics, and granular spatial systems).

In Data-Oriented Design, Entity Component Systems evolve across 4 recognized architectural tiers. Understanding these tiers explains our deliberate design boundaries:

```mermaid
flowchart LR
    S1["Stage 1: Core Storage<br>(COMPLETED)"] --> S2["Stage 2: Multi-Threading<br>(IN PROGRESS)"]
    S2 -.-> S3["Stage 3: Relational Graphs<br>(DELIBERATELY EXCLUDED)"]
    S3 -.-> S4["Stage 4: Distributed Compute<br>(OUT OF SCOPE)"]

    style S1 fill:#1f3a60,stroke:#58a6ff,stroke-width:2px,color:#fff
    style S2 fill:#1b4731,stroke:#3fb950,stroke-width:2px,color:#fff
    style S3 fill:#4d2d18,stroke:#d29922,stroke-width:1px,stroke-dasharray: 5 5,color:#fff
    style S4 fill:#3b1e1e,stroke:#f85149,stroke-width:1px,stroke-dasharray: 5 5,color:#fff
```

### Stage 1: Memory Invariants & Single-Core Saturation (Current Milestone — 100% Complete)
* **Goal:** Maximize single-core memory bandwidth and guarantee deterministic $O(1)$ operations with zero dependencies.
* **Delivered Capabilities:**
  - Contiguous, hardware-aligned 64 KB SoA chunk pages.
  - Generational 32-byte `SlotMap` routing (zero ABA hazards).
  - Archetype transition DAG with cached commutativity.
  - Hot-path SIMD chunk traversal (~0.97 cycles/entity).
  - Direct zero-repack GPU `std430` streaming (54+ GB/s memory bandwidth).

---

### Stage 2: Concurrency & Query Optimization (Active Roadmap)
* **Goal:** Scale the 0.97 cycle/entity baseline across all CPU cores and eliminate redundant frame calculations without breaking the flat-memory invariant.
* **Target Features Planned for Stage 2:**
  1. **Multi-Threaded Chunk Job Dispatch:**
     - Because 64 KB chunks within an archetype are completely independent in memory, chunks act as discrete, parallel jobs.
     - A lock-free work-stealing job pool will distribute chunks across CPU worker threads, scaling throughput linearly with core count ($4\times\text{ to }12\times$ frame speedup).
  2. **Dirty / Version Ticking per Chunk:**
     - Monotonic tick counters stamped on chunk headers when components are mutated.
     - Allows downstream systems (e.g., spatial hashing, AABB recalculation) to skip entire unmutated chunks in $O(1)$ time.
  3. **Event-Driven Query Cache Invalidation:**
     - Replacing linear archetype graph scans with sparse registration lists that notify active queries only when relevant archetypes are created.
  4. **Bulk Creation & Deletion APIs:**
     - Batch creation of $N$ entities into pre-allocated chunk slices with single-pass SlotMap index increments.

---

### Why Stage 3 & Stage 4 are Deliberately Excluded ("The Generality Tax")

More features in an ECS do not equate to better performance. In massive-scale simulations, advanced features introduce a **"Generality Tax"** that destroys hardware cache locality:

#### 1. The Archetype Fragmentation Penalty (Stage 3: Entity Relationships & Hierarchies)
* In systems supporting dynamic entity relationships (e.g. `(ChildOf, Parent)` or `(TargetOf, Entity)`), each unique pair creates a distinct archetype.
* In a 50,000-boid flock, instead of **1 dense archetype occupying 32 contiguous 64 KB chunks**, relationships fragment the world into **hundreds of tiny archetypes with partially empty chunks**.
* Queries spend more CPU cycles jumping across fragmented archetype pointers than executing SIMD math, causing iteration cost to degrade from **~1 cycle to 15–30 cycles per entity**.

#### 2. Synchronization Barriers & Pipeline Stalls (Stage 4: Heterogeneous GPU Compute ECS)
* Sharing ECS chunk lifecycles directly between CPU threads and GPU compute shaders requires hardware fences, memory barriers, and bidirectional synchronization locks.
* For local simulations, this synchronization overhead dwarfs the cost of a high-speed linear DMA upload (our Module 007 streams 50,000 entities in $< 0.2\ \mu\text{s}$ with zero pipeline stalls).

> **Architectural Conclusion:**  
> **Stage 1 + Stage 2 is the architectural "sweet spot" for `arche-ecs`.** It delivers 100% of the raw hardware throughput required for 120 Hz high-density simulations without paying the memory fragmentation penalty of general-purpose game engine ECS frameworks.

---

## Building & Testing

### Requirements
- **C++ Compiler:** Modern C++20 compiler (`AppleClang 13+`, `Clang 13+`, `GCC 11+`, or `MSVC 2019+`)
- **Build System:** `CMake 3.20+`

### Build & Run All Tests
```bash
# 1. Configure release build
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 2. Build static library and test targets
cmake --build build

# 3. Run all unit test suites via CTest
ctest --test-dir build --output-on-failure

# 4. Run cycle-accurate performance benchmarks directly
./build/arche_benchmarks
```

### Full Clean Rule
```bash
# Removes all intermediate object files, static library, and test binaries
cmake --build build --target fclean
```

---

## Project Layout

```text
arche-ecs/
├── CMakeLists.txt                      # Build configuration and test runner definitions
├── include/
│   └── arche/
│       ├── arche.hpp                   # Master public umbrella header
│       ├── entity.hpp                  # Generational Entity handle (index + generation)
│       ├── slot_map.hpp                # O(1) routing table mapping Entity -> Chunk row
│       ├── type_registry.hpp           # Component reflection, TypeID, and bitmasks
│       ├── archetype.hpp               # Canonical archetype definition & column layouts
│       ├── archetype_graph.hpp         # Transition DAG with cached edge traversal
│       ├── chunk.hpp                   # 64 KB SoA chunk memory page layout
│       ├── chunk_pool.hpp              # Recycling pool for cache-aligned chunk pages
│       ├── hardware.hpp                # Host cache line and page size hardware probe
│       ├── world.hpp                   # Central ECS coordinator and entity storage
│       ├── query.hpp                   # Superset archetype query matching and iteration
│       ├── query_iterator.hpp          # Range-based for-loop structured binding iterator
│       ├── query_filter.hpp            # Compile-time With<...> and Without<...> filters
│       ├── command_buffer.hpp          # Deferred structural mutation recording buffer
│       └── gpu_reflection.hpp          # std430 layout validation and GPU buffer views
├── src/
│   ├── arche.cpp                       # Master library symbol registration
│   ├── chunk.cpp                       # Chunk memory slice logic
│   ├── world.cpp                       # World lifecycle management
│   └── command_buffer.cpp              # Deferred mutation playback
├── tests/                              # Standalone unit and invariant test suites
│   ├── entity_tests.cpp                # Module 001: SlotMap generational handles
│   ├── type_tests.cpp                  # Module 002: TypeRegistry & ComponentMask
│   ├── archetype_tests.cpp             # Module 003: DAG commutativity & edge caching
│   ├── chunk_storage_tests.cpp         # Module 004: 64 KB chunk SoA alignment
│   ├── structural_mutation_tests.cpp   # Module 005: Add/remove component & Swap-and-Pop
│   ├── query_tests.cpp                 # Module 006: Superset queries & SIMD iteration
│   ├── gpu_reflection_tests.cpp        # Module 007: std430 traits & VRAM streaming
│   └── arche_tests.cpp                 # Module 008: All-in-one integration regression suite
└── benchmarks/
    └── arche_benchmarks.cpp            # Headless cycle-accurate profiler (read_tsc)
```

---

## License

This project is licensed under the [MIT License](LICENSE).
