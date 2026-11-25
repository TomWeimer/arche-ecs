# Arche ECS — AI Assistant Reference (LLMS.md)

> **Context for AI Models (Cursor, Claude Code, Copilot, Gemini):**  
> `arche-ecs` is a standalone, ultra-high-performance **C++20 Archetype Entity Component System**.  
> Always follow the current canonical API. Do not hallucinate EnTT, Flecs, or legacy deprecated methods.

---

## 1. Golden Invariants & Negative Constraints

1. **Import Native C++20 Module or Include Umbrella Header:**
   ```cpp
   import arche;                 // Primary C++20 module import
   // or #include <arche/arche.hpp> if using legacy header builds
   ```
2. **Components & Resources MUST Be Plain Old Data (POD):**
   - Standard layout and trivially copyable structs only.
   - **ZERO** virtual methods, **ZERO** polymorphic inheritance, **ZERO** base classes.
   - Zero-sized empty structs are valid tag components (occupy 0 bytes in chunk columns).
3. **NEVER Hallucinate Foreign ECS Syntax:**
   - **NO EnTT:** Do NOT use `registry.view<...>()`, `registry.emplace<...>()`, or `registry.get<...>()`.
   - **NO Flecs:** Do NOT use `ecs.entity().set<...>()`, `ecs.filter<...>()`, or `ecs.system<...>()`.
4. **Current Canonical Method Names (Do Not Use Deprecated Variants):**
   - Use `world.spawn(...)` (scalar or batch via `{.count = N}`). Do NOT use `spawn_batch(...)` or `create_entity(...)`.
   - Use `world.despawn(e)` to destroy entities.
   - Use `world.get<T>(e)` (returns reference) and `world.has<T>(e)` (returns bool).
   - Use `world.get_mut<T>(e)` to mutate and automatically stamp column version ticks.
   - Use `world.add(e, val)` and `world.remove<T>(e)` for archetype transitions. Do NOT use `add_component` or `remove_component`.
   - Use `world.query<Comps...>()` and `.each_chunk(...)`. Do NOT use `for_each_chunk(...)`.
   - Use `world.add_resource<T>(val)` and `world.resource<T>()` for singleton resources.
   - Use `world.add_plugin<PluginType>()` and `world.run()` for scheduled DAG execution.
   - Use `world.version()` or `ctx.tick()` to read simulation ticks.

---

## 2. Complete Canonical API Reference

### Component & Resource Definitions
```cpp
import arche;

// Components: POD structs
struct Position { float x{0.0f}, y{0.0f}; };
struct Velocity { float vx{0.0f}, vy{0.0f}; };
struct Health { float hp{100.0f}; };

// Tag components (occupy 0 bytes in chunk storage)
struct TagBoid {};
struct TagDead {};

// Singleton resources (world-level global state)
struct TimeStepResource { float dt{0.016f}; };
struct AudioQueue { int pending_sounds{0}; };
```

### Entity & World Lifecycle
```cpp
arche::World world;

// 1. Scalar spawn: In-place construction in 64KB chunk page
arche::Entity e = world.spawn(Position{1.0f, 2.0f}, Velocity{0.5f, 0.0f});

// 2. Check handle validity in O(1) (generational slot_map lookup)
bool alive = world.entities.contains(e);

// 3. Component inspection & mutation
Position& pos = world.get<Position>(e);
world.get_mut<Position>(e).x += 10.0f; // Stamps column modification tick

// 4. Component presence check (O(1) 128-bit bitmask check)
bool has_vel = world.has<Velocity>(e);

// 5. Dynamic component addition & removal (archetype migration)
world.add(e, Health{100.0f}); // Migrates to [Position, Velocity, Health]
world.remove<Health>(e);      // Migrates back to [Position, Velocity]

// 6. Despawn entity (O(1) chunk swap-remove compaction)
world.despawn(e);
```

### High-Throughput Bulk Lifecycle
```cpp
// 1. Prototype Broadcasting: spawns N entities sharing identical components
std::vector<arche::Entity> batch = world.spawn(
    {.count = 100'000},
    Position{0.0f, 0.0f},
    Velocity{1.0f, 0.0f}
);

// 2. Direct SoA Streaming: ingest pre-computed arrays with zero heap allocations
std::vector<Position> positions(50'000);
std::vector<Velocity> velocities(50'000);
std::vector<arche::Entity> out_handles(50'000);

world.spawn(
    {.count = 50'000},
    std::span<arche::Entity>(out_handles),
    positions.data(),
    velocities.data()
);
```

### Queries & Iteration
```cpp
// 1. Range-based for loop with structured bindings
for (auto [pos, vel] : world.query<Position, const Velocity>()) {
    pos.x += vel.vx * 0.016f;
    pos.y += vel.vy * 0.016f;
}

// 2. Querying Entity handles
for (auto [entity, pos] : world.query<arche::Entity, Position>()) {
    if (pos.y < 0.0f) {
        // Handle logic
    }
}

// 3. Compile-time tag exclusion and matching
using arche::With;
using arche::Without;

auto q = world.query<Position, Velocity>(
    With<TagBoid>{},
    Without<TagDead>{}
);

// 4. Hot-path 64KB SoA chunk iteration (auto-vectorizable)
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

### Singleton Resources
```cpp
// Register resources
world.add_resource<TimeStepResource>(TimeStepResource{0.016f});

// Access resource
float dt = world.resource<TimeStepResource>().dt;
```

### Plugins, Access Footprints & DAG Scheduler
```cpp
struct SimulationPlugin {
    static std::string_view name() { return "SimulationPlugin"; }

    void build(arche::World&, arche::PluginContext& ctx) {
        // System A: Reads TimeStepResource, Writes Position, Reads Velocity
        ctx.add_system(
            "MovementSystem",
            arche::Access{}.writes<Position>().reads<Velocity>().reads_resource<TimeStepResource>().build(),
            [](arche::SystemContext& ctx) {
                float dt = ctx.resource<TimeStepResource>().dt;
                ctx.query<Position, const Velocity>().each_chunk(
                    [dt](uint32_t count, Position* p, const Velocity* v) {
                        for (uint32_t i = 0; i < count; ++i) {
                            p[i].x += v[i].vx * dt;
                            p[i].y += v[i].vy * dt;
                        }
                    }
                );
            }
        );
    }
};

world.add_plugin<SimulationPlugin>();
world.set_threads(8); // Multi-threaded worker thread pool
world.run();          // Advances tick, compiles conflict DAG, executes batches, commits commands
```

### Change Detection via Dirty Ticks (`Mutated` & `Added`)
```cpp
world.run(); // Advances world version
const uint32_t baseline_tick = world.version();

// Modify one entity
world.get_mut<Position>(e).x += 10.0f; // Stamps column modification tick

// Skips unmutated chunks in O(1) at the 64KB chunk boundary
world.query<Position>(arche::Mutated<Position>{baseline_tick})
    .each_chunk([](uint32_t count, Position* p) {
        // Only executes for chunks mutated after baseline_tick
    });
```

### In-Place Sorting & Cache Locality
```cpp
// Sort entities in-place via cycle chasing permutation on an archetype
world.sort<Position>(
    archetype_ptr,
    [](const Position& a, const Position& b) { return a.x < b.x; }
);
```

---

## 3. Drop-In Prompt Rules for Consumer Projects

When consuming `arche-ecs` inside an external project, add the directive block below to your AI assistant's configuration file based on the tool you use:

| AI Coding Assistant | Configuration File Location |
| :--- | :--- |
| **Google Antigravity** | `GEMINI.md` (project root) or `.gemini/rules/arche-ecs.md` |
| **Claude Code** | `CLAUDE.md` (project root) |
| **Cursor** | `.cursor/rules/arche-ecs.mdc` or `.cursorrules` (project root) |
| **GitHub Copilot** | `.github/copilot-instructions.md` |
| **Windsurf** | `.windsurfrules` (project root) |

```markdown
### Arche-ECS Library Directives for AI Assistants:
- Always use native C++20 module `import arche;` (or `#include <arche/arche.hpp>` for legacy header builds).
- Components and resources MUST be Plain Old Data (POD) structs only: standard layout, trivially copyable, zero virtual methods, no inheritance.
- Use `world.spawn(...)` (scalar or batch with `{.count = N}`).
- Use `world.despawn(e)` to destroy entities.
- Use `world.get<T>(e)` (read), `world.get_mut<T>(e)` (mutate with dirty tick stamping), `world.has<T>(e)`.
- Use `world.add(e, val)` and `world.remove<T>(e)` for structural archetype migrations.
- Use `world.query<...>().each_chunk(...)` for high-throughput auto-vectorizable loops.
- Use `world.add_resource<T>(...)` and `world.resource<T>()` for world-level singletons.
- Use `ctx.add_system(name, Access{}.reads<...>().writes<...>().build(), lambda)` for declarative scheduling.
- Use `world.run()` to execute frames.
- Strictly forbidden: EnTT syntax (`registry.view`, `emplace`), Flecs syntax (`entity().set`, `system()`), and obsolete methods (`spawn_batch`, `create_entity`, `for_each_chunk`).
```

---

## 4. CMake & Compiler Optimization Template

When generating or modifying a `CMakeLists.txt` for a project using `arche-ecs`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Fetch arche-ecs
include(FetchContent)
FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/TomWeimer/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

# 2. Define Consumer Target
add_executable(my_simulation src/main.cpp)
target_link_libraries(my_simulation PRIVATE arche)

# 3. Interprocedural Optimization (LTO)
set_property(TARGET my_simulation PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# 4. Native SIMD Vectorization & Optimization
if (MSVC)
    target_compile_options(my_simulation PRIVATE /O2 /arch:AVX2 /fp:fast)
else()
    target_compile_options(my_simulation PRIVATE 
        -O3 
        -march=native 
        -ffast-math 
        -fno-rtti
    )
endif()
```
