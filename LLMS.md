# Arche ECS — AI Assistant Reference (LLMS.md)

> **Context for AI Models (Cursor, Claude Code, Copilot, Gemini):**  
> `arche-ecs` is a standalone, ultra-high-performance **C++20 Archetype Entity Component System**.  
> Always follow the modern API established in Stage 2. Do not hallucinate EnTT, Flecs, or legacy Stage 1 syntax.

---

## 1. Golden Invariants & Negative Constraints

1. **Include Only One Header:**
   ```cpp
   #include <arche/arche.hpp> // Includes the entire public library
   ```
2. **Components MUST Be Plain Old Data (POD):**
   - Pure structs with standard layout and trivial copyability only.
   - **ZERO** virtual methods, **ZERO** polymorphic inheritance, **ZERO** base classes.
   - Zero-sized empty structs are valid tag components (occupy 0 bytes in chunk columns).
3. **NEVER Hallucinate Foreign ECS Syntax:**
   - **NO EnTT:** Do NOT use `registry.view<...>()`, `registry.emplace<...>()`, or `registry.get<...>()`.
   - **NO Flecs:** Do NOT use `ecs.entity().set<...>()`, `ecs.filter<...>()`, or `ecs.system<...>()`.
4. **NEVER Use Deprecated Stage 1 Method Names:**
   - ❌ `world.create_entity(...)` $\to$ ✅ `world.spawn(...)`
   - ❌ `world.create_entities(...)` $\to$ ✅ `world.spawn_batch(...)`
   - ❌ `world.create_entities_soa(...)` $\to$ ✅ `world.spawn_batch(...)`
   - ❌ `world.destroy_entity(...)` $\to$ ✅ `world.despawn(...)`
   - ❌ `world.destroy_entities(...)` $\to$ ✅ `world.despawn_batch(...)`
   - ❌ `world.get_component<T>(...)` $\to$ ✅ `world.get<T>(...)`
   - ❌ `world.has_component<T>(...)` $\to$ ✅ `world.has<T>(...)`
   - ❌ `world.add_component<T>(...)` $\to$ ✅ `world.add<T>(...)`
   - ❌ `world.remove_component<T>(...)` $\to$ ✅ `world.remove<T>(...)`
   - ❌ `world.advance_tick()` $\to$ ✅ `world.tick()`
   - ❌ `cmd.playback(world)` $\to$ ✅ `world.commit(cmd)`
   - ❌ `world.register_double_buffered_component<T>()` $\to$ ✅ `world.enable_double_buffer<T>()`
   - ❌ `world.flip_double_buffer<T>()` $\to$ ✅ `world.flip<T>()`
   - ❌ `world.sort_archetype(...)` $\to$ ✅ `world.sort(...)`
   - ❌ `world.apply_permutation(...)` $\to$ ✅ `world.permute(...)`
   - ❌ `world.parallel_query(...)` $\to$ ✅ `world.query_parallel(...)`
   - ❌ `world.stream_component_to_ring(...)` $\to$ ✅ `world.stream_gpu(...)`
   - ❌ `world.write_draw_indirect_commands(...)` $\to$ ✅ `world.indirect_draw(...)`
   - ❌ `world.create_snapshot(...)` $\to$ ✅ `world.save_snapshot(...)`
   - ❌ `world.restore_snapshot(...)` $\to$ ✅ `world.load_snapshot(...)`

---

## 2. Complete Canonical API Reference

### Entity & World Lifecycle
```cpp
arche::World world;

// Spawn scalar entity (in-place component construction)
arche::Entity e = world.spawn(Position{1.0f, 2.0f}, Velocity{0.5f, 0.0f});

// Check if handle is alive (O(1) generational lookup)
bool alive = world.is_alive(e);

// Component access (returns mutable or const reference)
Position& pos = world.get<Position>(e);
const Position& cpos = std::as_const(world).get<Position>(e);

// Component presence check (O(1) 128-bit bitmask check)
bool has_vel = world.has<Velocity>(e);

// Dynamic component addition (migrates entity along cached DAG edge)
world.add(e, Mass{10.0f});

// Dynamic component removal
world.remove<Mass>(e);

// Despawn scalar entity (O(1) Swap-and-Pop compaction)
world.despawn(e);
```

### High-Throughput Bulk Lifecycle
```cpp
// 1. Prototype Broadcasting: spawns N entities sharing identical components
std::vector<arche::Entity> batch = world.spawn_batch(
    100'000, 
    Position{0.0f, 0.0f}, 
    Velocity{1.0f, 0.0f}
);

// 2. Direct Structure-of-Arrays (SoA) Streaming: zero-allocation external array streaming
std::vector<Position> positions(50'000);
std::vector<Velocity> velocities(50'000);
std::vector<arche::Entity> out_handles(50'000);

world.spawn_batch(
    50'000,
    std::span<arche::Entity>(out_handles),
    positions.data(),
    velocities.data()
);

// Batch despawn in a single cache-efficient pass
world.despawn_batch(out_handles);
```

### Queries & Iteration
```cpp
// Range-based for loop with C++17 structured bindings
for (auto [pos, vel] : world.query<Position, Velocity>()) {
    pos.x += vel.vx * dt;
}

// Accessing the Entity handle in queries
for (auto [entity, pos] : world.query<arche::Entity, Position>()) {
    if (pos.y < 0.0f) cmd.despawn(entity);
}

// Compile-time tag filters
using arche::With;
using arche::Without;

auto q = world.query<Position, Velocity>(
    With<TagBoid>{},
    Without<TagDead>{}
);

// Hot-path raw 64 KB SIMD chunk iteration (~0.85 cycles/entity)
world.query<Position, const Velocity>().for_each_chunk(
    [](uint32_t count, Position* pos, const Velocity* vel) {
        #pragma clang loop vectorize(enable) interleave(enable)
        for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.016f;
            pos[i].y += vel[i].vy * 0.016f;
        }
    }
);
```

### Multi-Threading & Deferred Mutations
```cpp
arche::ThreadPool pool(8);                      // 8 worker threads
arche::ParallelCommandBuffer cmd_buffers(8);    // Thread-isolated mutation queues

// Lock-free chunk work-stealing across all CPU cores
world.query_parallel<Position, const Velocity>(
    pool,
    cmd_buffers,
    [](uint32_t count, arche::CommandBuffer& cmd, Position* pos, const Velocity* vel) {
        for (uint32_t i = 0; i < count; ++i) {
            pos[i].x += vel[i].vx * 0.016f;
            // Record deferred structural changes safely without mutex locks
            if (pos[i].y > 1000.0f) {
                cmd.despawn(ents[i]);
            }
        }
    }
);

// Replay deferred mutations on main thread deterministically
world.commit(cmd_buffers);
```

### Double-Buffered Sub-Steps & Pointer Flipping
```cpp
// 1. Enable dual read/write slices for Position
world.enable_double_buffer<Position>();

// 2. Query reading Step t (const Position*) and writing Step t+1 (Write<Position>)
world.query_parallel<const Position, const Velocity, arche::Write<Position>>(
    pool,
    cmd_buffers,
    [](uint32_t count, arche::CommandBuffer&, const Position* pos, const Velocity* vel, arche::Write<Position> next_pos) {
        for (uint32_t i = 0; i < count; ++i) {
            next_pos[i].x = pos[i].x + vel[i].vx * dt;
            next_pos[i].y = pos[i].y + vel[i].vy * dt;
        }
    }
);

// 3. O(1) Pointer Flip: swaps chunk slice byte offsets and stamps dirty versions
world.flip<Position>();
```

### Change Detection & Monotonic Ticks
```cpp
uint32_t current_tick = world.tick(); // Advance simulation tick counter

// Skip entire unmutated chunks in 1 CPU cycle
for (auto [pos] : world.query<Position>(arche::Mutated<Position>{since_tick})) {
    // Executes only for chunks where Position changed after since_tick
}

for (auto [pos] : world.query<Position>(arche::Added{since_tick})) {
    // Executes only for chunks containing newly spawned or migrated entities
}
```

### In-Place Sorting & Cache Locality
```cpp
arche::Archetype* arch = world.get_record(entity).archetype;

// Sort entities in-place via cycle chasing (e.g. by Morton Z-order key)
world.sort<MortonKey>(arch, [](const MortonKey& a, const MortonKey& b) {
    return a.code < b.code;
});

// Or apply arbitrary permutations directly
world.permute(arch, permutation_indices);
```

### GPU Ring Streaming & Draw Indirect
```cpp
// Direct memory streaming to mapped GPU VRAM (> 17.5 GB/s)
size_t bytes = world.stream_gpu<Position>(ring_index, vram_ptr, capacity_bytes);

// Generate GPU DrawIndirectCommand buffers with frustum culling
size_t draw_count = world.indirect_draw(
    arch,
    indirect_cmd_buffer,
    max_commands,
    [](const Position& pos) { return pos.z >= 0.0f; } // Culling predicate
);
```

### Snapshots & Deterministic Rollback
```cpp
std::vector<uint8_t> buffer(16 * 1024 * 1024);

// Save full binary world snapshot
size_t bytes = world.save_snapshot(buffer.data(), buffer.size());

// Instant rollback
world.load_snapshot(buffer.data(), bytes);
```

---

## 3. Standard Lockstep Simulation Loop Pattern

This is the standard, canonical 120 Hz simulation frame pattern for consumer engines (like Colony):

```cpp
#include <arche/arche.hpp>

void run_simulation_frame(
    arche::World& world,
    arche::ThreadPool& pool,
    arche::ParallelCommandBuffer& cmd_buffers,
    float dt
) {
    // 1. Advance simulation tick counter
    world.tick();

    // 2. Parallel Physics / Steering (Reading step t, writing step t+1)
    world.query_parallel<const Position, const Velocity, BoidState, arche::Write<Position>>(
        pool,
        cmd_buffers,
        [dt](uint32_t count,
             arche::CommandBuffer& cmd,
             const Position* pos,
             const Velocity* vel,
             BoidState* state,
             arche::Write<Position> next_pos) {
            for (uint32_t i = 0; i < count; ++i) {
                next_pos[i].x = pos[i].x + vel[i].vx * dt;
                next_pos[i].y = pos[i].y + vel[i].vy * dt;
                next_pos[i].z = pos[i].z + vel[i].vz * dt;
                state[i].hunger += 0.01f;
            }
        }
    );

    // 3. Commit deferred mutations sequentially on main thread
    world.commit(cmd_buffers);

    // 4. O(1) Double-buffer swap
    world.flip<Position>();
}
```

---

## 4. Drop-In Prompt Rules for Consumer Projects

When consuming `arche-ecs` inside an external project (such as *Colony*), add the directive block below to your AI assistant's configuration file based on the tool you use:

| AI Coding Assistant | Configuration File Location |
| :--- | :--- |
| **Google Antigravity** | `GEMINI.md` (project root) or `.gemini/rules/arche-ecs.md` |
| **Claude Code** | `CLAUDE.md` (project root) |
| **Cursor** | `.cursor/rules/arche-ecs.mdc` or `.cursorrules` (project root) |
| **GitHub Copilot** | `.github/copilot-instructions.md` |
| **Windsurf** | `.windsurfrules` (project root) |

```markdown
### Arche-ECS Library Directives for AI Assistants:
- Always `#include <arche/arche.hpp>` (single umbrella header).
- Components must be Plain Old Data (POD) structs only: standard layout, trivially copyable, zero virtual methods, no inheritance.
- Use `world.spawn(...)` to create entities; `world.spawn_batch(...)` for bulk spawning (prototype broadcast or SoA streaming).
- Use `world.despawn(e)` to destroy entities; `world.despawn_batch(...)` for batches.
- Use `world.get<T>(e)` (mutable/const), `world.has<T>(e)`, `world.add<T>(e, val)`, `world.remove<T>(e)`.
- Use `world.query_parallel<Comps...>(pool, cmd_buffers, ...)` for chunk-level multi-threading; replay with `world.commit(cmd_buffers)`.
- Use `world.enable_double_buffer<T>()`, `arche::Write<T>`, and `world.flip<T>()` for sub-step physics isolation.
- Use `world.tick()` to advance simulation versions; use `arche::Mutated<T>{tick}` and `arche::Added{tick}` for O(1) chunk change detection.
- Use `world.sort<Key>(arch, comp)` and `world.permute(arch, indices)` for in-place cache locality sorting.
- Use `world.stream_gpu<T>(ring_idx, vram, cap)` and `world.indirect_draw(...)` for direct GPU rendering.
- Strictly forbidden: EnTT syntax (`registry.view`, `emplace`), Flecs syntax (`entity().set`), and deprecated Stage 1 methods (`create_entity`, `get_component`, `playback`).
```

