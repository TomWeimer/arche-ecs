# Colony ECS Scheduled Sorting Architecture & Strategy Specification

**Status:** Approved Architecture  
**Domain:** Layer 5 (Sort Engine Core) & Layer 8 (Scheduler Execution Plan)  
**Target:** `arche-ecs` Integration & `colony` Swarm Simulation

---

## 1. Architectural Philosophy & Core Invariants

### 1.1 Zero Consumer Context Invariant
`arche-ecs` enforces **Zero Consumer Context**:
- The ECS library has **zero knowledge of wall-clock time, `delta_time`, frame rates, or presentation swapchains**.
- World `version` is a strictly monotonic mutation sequence counter (incremented when structural mutations occur or when a mutable entity/component view is requested), **not a clock**.
- Any timing model (simulation clock, accumulator, physics ticks) belongs strictly to the consuming application and is registered as a user-defined struct within `ResourceContainer`.

### 1.2 Placement of the Sorting Pipeline
Sorting does **not** live in an isolated secondary scheduler (which would introduce data-race hazards and synchronization lock-contention across memory chunks). Instead, it spans two decoupled layers:

1. **Sort Engine Core (Layer 5 — `src/runtime/sort/`)**:
   - Pure, stateless algorithms.
   - Operates on a low-level boundary: `Archetype*`, its contiguous 64 KB memory chunks, and the `SlotMap` entity routing table.
   - Decomposes sorting into two phases:
     - **Phase 1: Index Permutation Generation** ($O(N)$ LSD Radix Sort for integer keys; $O(N \log N)$ stable sort fallback).
     - **Phase 2: In-Place Disjoint Cyclic Orbit Permutation** (shifts component columns in-place across chunks using $O(1)$ scratch space per column, avoiding 64 KB page reallocations).
2. **Execution Plan (Layer 8 — `src/runtime/scheduler/`)**:
   - Compiles into the DAG as a dedicated `SortStage` / `SortBatch` between conflicting system batches.
   - Scheduled at pipeline barriers: preceding worker batches complete, the sort executes with exclusive write access over the target archetype columns and `SlotMap`, and succeeding batches read contiguous, cache-aligned chunk memory with near 100% L1/L2 prefetch hits.

---

## 2. Triggering & Scheduling Mechanisms

Because the scheduler does not assume a clock or fixed `dt`, the sort stage is triggered conditionally via one of three decoupled mechanisms:

```
                            SORT TRIGGER MECHANISMS
                                       │
         ┌─────────────────────────────┼─────────────────────────────┐
         ▼                             ▼                             ▼
1. RESOURCE PREDICATE        2. MUTATION EPOCH DISTANCE    3. REACTIVE DIRTY FLAG
   `ctx.resources`              `Δversion ≥ Threshold`        `needs_sort = true`
   User lambda evaluates        Autonomous cache hygiene;     Triggered by spatial grid
   custom `SimulationClock`     skips if world is stationary  or bulk spawn events
```

### Strategy 1: Resource-Inspecting Predicate (`ctx.resources`)
The sort stage receives a condition predicate `std::function<bool(const SystemContext&)>`. The user's application inspects its own custom clock resource:
```cpp
scheduler.add_sort_stage<Position3D, With<BoidTag>>(
    "SpatialMortonSort",
    [](const SystemContext& ctx) -> bool {
        if (!ctx.resources.has<SimulationClock>()) return false;
        const auto& clock = ctx.resources.get<SimulationClock>();
        return (clock.tick_index % 30 == 0);
    },
    morton_3d_key_extractor
);
```

### Strategy 2: Mutation Epoch Distance ($\Delta \text{version} \ge K$)
Autonomous data-driven maintenance. Tracks the world version of the last sort. If $\Delta v = (\text{current\_version} - \text{last\_sorted\_version}) \ge K$, sorting executes. If the world was idle or stationary, no cycles are wasted.

### Strategy 3: Reactive Dirty Flag
An upstream system (e.g. `BoidSpawningSystem` or `SpatialGridBroadphase`) detects high spatial drift or structural churn and sets a flag on a resource:
```cpp
if (drift_metric > threshold) {
    ctx.resources.get<SpatialGridSettings>().needs_sort = true;
}
```

---

## 3. Target Archetype Declaration & Resolution

Users do not track raw `ArchetypeID` numbers. Target archetypes are declared via **Primary Component** and **Query Filters**:

```cpp
scheduler.add_sort_stage<Position3D, With<BoidTag>, Without<DeadTag>>(
    "BoidSpatialSort",
    trigger_predicate,
    key_extractor
);
```

### Engine Resolution Pipeline:
1. **Bitmask Compilation**: Compiles compile-time type IDs into 128-bit `include_mask` and `exclude_mask`.
2. **QueryCache Resolution**: Matches archetypes in `ArchetypeGraph`. Dynamically formed archetypes at runtime are automatically registered.
3. **Conflict Graph Footprint**: Declares exclusive write locks across all component columns in matching archetypes, allowing disjoint systems (e.g. Audio, UI) to run concurrently on worker threads.

---

## 4. Execution Scope: Full Archetype Atomic

- **Intra-chunk / Chunk-sliced sorting is strictly prohibited**:
  - Sorting within individual 64 KB chunks destroys global spatial ordering and causes macro AABB bounding boxes of every chunk to overlap, ruining chunk-level frustum and broadphase culling.
- **Global Order Guarantee**:
  - All chunks belonging to the target archetype are gathered into a single virtual span $[0 \dots N)$ and sorted globally.
  - Entities cross chunk boundaries so that Chunk 0 holds Morton range $[0, K_0]$, Chunk 1 holds $(K_0, K_1]$, etc.
- **Extreme Scale Budgeting**:
  - Rather than fragmenting chunks, high-scale budgets interleave *across archetypes* (Archetype A on tick $N$, Archetype B on tick $N+1$).

---

## 5. Colony Project Sorting Strategies (Key Extractors)

`colony` requires three primary sorting strategies implemented in the simulation layer:

### 5.1 3D Morton Code (Z-Order Curve) — Primary Spatial Cache Coherency
Maps 3D floating-point positions to 64-bit interleaved Morton integers:
```cpp
struct Morton3DConfig {
    glm::vec3 world_min{-100.0f, -100.0f, -100.0f};
    float     inv_cell_size{1.0f / 2.0f};
};

// Key Extractor (O(N) Radix Path)
inline uint64_t extract_morton_3d(const Components::Position3D& p, const Morton3DConfig& cfg) noexcept {
    const uint32_t ux = static_cast<uint32_t>((p.x - cfg.world_min.x) * cfg.inv_cell_size);
    const uint32_t uy = static_cast<uint32_t>((p.y - cfg.world_min.y) * cfg.inv_cell_size);
    const uint32_t uz = static_cast<uint32_t>((p.z - cfg.world_min.z) * cfg.inv_cell_size);
    return Detail::morton_encode_3d(ux, uy, uz);
}
```
- **Use Case**: 50,000 boid flocking and spatial grid neighbor lookups.
- **Algorithm**: 4-pass 8-bit LSD Radix Sort ($< 0.6\text{ ms}$ for 50k entities).

### 5.2 Camera Distance (Depth) — Transparency & Early-Z
Computes distance along the active camera ray:
```cpp
struct CameraDepthConfig {
    glm::vec3 camera_pos{0.0f};
    glm::vec3 camera_forward{0.0f, 0.0f, 1.0f};
    bool      back_to_front{true}; // true for transparency, false for early-Z
};

inline float extract_camera_depth(const Components::Position3D& p, const CameraDepthConfig& cfg) noexcept {
    const float dist = glm::dot(glm::vec3(p.x, p.y, p.z) - cfg.camera_pos, cfg.camera_forward);
    return cfg.back_to_front ? -dist : dist;
}
```
- **Use Case**: Transparent water droplets, DFSPH fluid sprays, and particle plumes.
- **Algorithm**: $O(N \log N)$ Stable Index Sort.

### 5.3 Component Scalar Field — Species & Identity Ordering
Sorts by contiguous component scalar properties:
```cpp
inline uint32_t extract_species_id(const Components::SpeciesData& s) noexcept {
    return s.species_id;
}
```
- **Use Case**: Grouping Starlings, Falcons, and Predators into separate memory blocks to eliminate branch mispredictions in flocking loops and instanced VAT draw calls.
- **Algorithm**: $O(N)$ 1-pass Radix Sort.

---

## 6. The Low-Level Execution Contract Boundary

```cpp
namespace arche::sort {

struct SortTarget {
    Archetype*               arch;           // Target archetype table
    std::span<Chunk* const>  chunks;         // Contiguous 64 KB memory pages
    SlotMap&                 slot_map;       // Global entity handle routing table
    uint32_t                 new_version;    // Mutation sequence stamp
};

template <typename KeyType>
void execute_radix_sort_orbit(SortTarget& target, 
                              std::span<const KeyType> keys,
                              std::span<uint32_t> perm_scratch);

} // namespace arche::sort
```

This guarantees Layer 5 remains a stateless, high-performance mathematical kernel with zero dependencies on scheduling, presentation, or user timing constructs.
