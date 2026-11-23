# Storage Hierarchy & Architecture Refactoring Specification

## 1. Architectural Diagnosis: Why `ChunkDistributor` is an Antipattern

`ChunkDistributor` became a **"junk-drawer" god-struct**. It held completely disparate responsibilities:
1. **Physical slot allocation:** (`allocate_slot`) — writing into chunk rows.
2. **Chunk memory pooling:** (`acquire_chunk_for`, `recycle_chunk`) — borrowing/returning raw blocks to `ChunkPool`.
3. **Column memory broadcasting & streaming:** (`broadcast_component`, `stream_component`) — raw SoA memory copying.
4. **Storage ownership mapping:** (`get_archetype_chunks`) — reaching into `World` to index `std::vector<std::vector<Chunk*>>`.
5. **Universal storage traversal:** (`for_each_chunk`) — walking the whole world's chunks.

In a clean Data-Oriented Design (DOD):
- **Data storage owns its memory**,
- **Allocators own entity placement**, and
- **Queries own iteration**.

---

## 2. Layered Dependency Hierarchy (Strict Top-Down DAG)

To prevent circular dependencies and eliminate arbitrary cross-includes, the engine must follow a **6-Layer Strict Hierarchy**. A layer may **only** include files from lower layers. Circular includes within a layer or upwards are strictly forbidden.

```
Layer 5: Public Facades & Central World   (world.hpp, world_facades.hpp, system_context.hpp)
                          │
Layer 4: Scheduling & Query Execution     (query.hpp, query.inl, scheduler.hpp, sort.hpp)
                          │
Layer 3: Entity Lifecycle & Migration     (entity_allocator.hpp, component_migration.hpp, command_buffer.hpp)
                          │
Layer 2: Archetypal Table Storage         (archetype_storage.hpp, archetype_graph.hpp, archetype.hpp)
                          │
Layer 1: Chunk Block & Memory Primaries   (chunk.hpp, chunk_pool.hpp, slot_map.hpp, type_registry.hpp)
                          │
Layer 0: Core Types, Mask & Hardware      (types/, component_mask.hpp, hardware.hpp, math_utils.hpp)
```

---

## 3. Folder Hierarchy & File Responsibility Matrix

```
include/arche/                      <-- Clean Public API only (zero internal headers)
├── arche.hpp                       (Single master include)
├── world.hpp                       (World coordinator declaration)
├── query.hpp                       (Query<Components...> declaration)
├── command.hpp                     (CommandBuffer & ParallelCommandBuffer)
├── entity.hpp                      (Entity, EntityRef, Batch handles)
├── resource.hpp                    (ResourceID, registration tags)
├── contracts/                      (Domain sub-facade contract declarations)
│   ├── entity.hpp
│   ├── archetype.hpp
│   ├── resource.hpp
│   ├── snapshot.hpp
│   ├── system.hpp
│   └── gpu.hpp
└── types/                          (Zero-dependency POD types & C++20 traits)
    ├── entity.hpp
    ├── filters.hpp                 (With, Without, Or, Mutated, Added, is_with_v, etc.)
    ├── resource_id.hpp
    └── system.hpp

src/                                <-- Private Implementation Subsystems
├── core/                           <-- Layer 0: Primitives & Hardware
│   ├── hardware.hpp                (Cache line size, L1/L2 prefetch ICE intrinsics, SIMD alignment)
│   ├── math_utils.hpp              (round_up_simd, alignment helpers)
│   ├── type_id.hpp                 (Compile-time monotonic TypeID generation)
│   └── component_mask.hpp          (64/128/256-bit bitmask operations)
│
├── storage/                        <-- Layer 1 & 2: Physical Memory & Archetypes (REPLACES chunk/ & archetype/)
│   ├── chunk_header.hpp            (Extracted POD ChunkHeader: 128 bytes, dirty versions, counts)
│   ├── chunk.hpp                   (64KB physical page slice operations, column_slice, swap_and_pop)
│   ├── chunk_pool.hpp              (Page virtual allocator, thread-safe free list of 64KB slabs)
│   ├── column_layout.hpp           (Column offsets, size, alignment, element sizing)
│   ├── archetype.hpp               (Archetype definition, column layouts, ComponentMask, entity counts)
│   ├── archetype_graph.hpp         (DAG nodes, add_component/remove_component edges, TypeRegistry)
│   └── archetype_storage.hpp       (NEW: Replaces ChunkDistributor storage responsibilities!)
│
├── entity/                         <-- Layer 1 & 3: Sparse Identification & Lifecycle
│   ├── entity_record.hpp           (Entity -> Archetype*, Chunk*, row index)
│   ├── slot_map.hpp                (Dense/sparse generational ID allocator, free list, snapshots)
│   ├── entity_allocator.hpp / .inl (Fast entity spawn, batch spawn, swap-and-pop entity placement)
│   └── entity_view.hpp / .inl      (EntityRef handle methods: add, remove, get, despawn)
│
├── query/                          <-- Layer 4: Matchers, Filters & Iteration
│   ├── filter.hpp                  (Runtime change filters: Added, Mutated<T>, apply_filter_to_mask)
│   ├── query_cache.hpp             (Reactive matching Archetype* lists invalidated by ArchetypeGraph)
│   ├── query_iterator.hpp          (C++17 structured bindings iterator [pos, vel])
│   ├── query_rules.hpp             (Compile-time grammar verification rules)
│   └── query.inl                   (Query template definitions: each_chunk, each_parallel)
│
├── command/                        <-- Layer 3: Deferred Recording & Playback
│   ├── command_buffer.hpp / .inl   (Lock-free thread-local command stream)
│   └── command_playback.hpp / .cpp (Serial flush of structural commands into World)
│
├── system/                         <-- Layer 4: Execution DAG & Concurrency
│   ├── access.hpp / .inl           (AccessDescriptor, AccessBuilder bitmasks)
│   ├── thread_pool.hpp             (Work-stealing or static partitioned worker threads)
│   ├── system_context.hpp          (Context passed to systems: query, resources, commands)
│   ├── scheduler.hpp / .cpp        (Phase DAG topological sorter, hazard detection)
│   └── plugin.hpp                  (Module installer contract)
│
├── spatial/                        <-- Layer 4: Cache Locality & Sorting
│   ├── sort.hpp                    (Permutation index builder based on Hilbert/Morton/Axis)
│   └── sort.cpp                    (In-place row permutation across archetype columns)
│
└── world/                          <-- Layer 5: Coordination Hub & Storage Assembly
    ├── world_storage.hpp           (Full definition of internal WorldStorage containing all systems)
    ├── world_access.hpp / .inl     (Friend-bridge exposing WorldStorage to private subsystems)
    ├── world_facades.hpp / .cpp    (Domain facade method implementations)
    └── world.cpp                   (World constructor, destructor, clear, non-template API)
```

---

## 4. Where Does Everything From `ChunkDistributor` Move?

Instead of `ChunkDistributor`, responsibilities are partitioned cleanly into their **natural domain owners**:

| Old Function (`ChunkDistributor::...`) | New Location | New Name & Exact Scope | Does it need `World`? | New Minimal Dependencies |
| :--- | :--- | :--- | :---: | :--- |
| `get_archetype_chunks(world, arch)` | `src/archetype/archetype.hpp` | `arch->chunks()` | ❌ **NO** | **None** (`arch` owns `chunks_`) |
| `acquire_chunk_for(world, arch)` | `src/archetype/archetype.hpp` | `arch->ensure_writable_chunk(pool)` | ❌ **NO** | `ChunkPool&` only |
| `recycle_chunk(world, arch, chunk)` | `src/archetype/archetype.hpp` | `arch->recycle_chunk(pool, chunk)` | ❌ **NO** | `ChunkPool&` only |
| `for_each_chunk(world, visitor)` | `src/archetype/archetype_graph.hpp` | `graph.for_each_chunk(visitor)` | ❌ **NO** | **None** (Graph iterates its own archetypes' chunks) |
| `allocate_slot(world, arch, entity)` | `src/archetype/archetype.hpp` | `arch->allocate_slot(pool, entity)` | ❌ **NO** | `ChunkPool&` only |
| `update_entity_locations(...)` | `src/entity/entity_allocator.hpp` | `EntityAllocator::update_entity_locations(...)` | ❌ **NO** | `SlotMap&` only |
| `populate_chunks_batch(...)` | `src/entity/entity_allocator.hpp` | `EntityAllocator::populate_chunks_batch(...)` | ❌ **NO** | `ChunkPool&`, `SlotMap&` (No `World`) |
| `broadcast_component(...)` | `src/chunk/chunk.hpp` | `chunk->fill_column<Comp>(start_row, count, value)` | ❌ **NO** | **None** (Operates purely on chunk byte memory) |
| `stream_component(...)` | `src/chunk/chunk.hpp` | `chunk->copy_column<Comp>(start_row, src_data, count)` | ❌ **NO** | **None** (Operates purely on chunk byte memory) |

---

## 5. Concrete Design of the Two New Storage & Query Helpers

### 1. Storage-Level Helper: `ArchetypeStorage::for_each_chunk`
- **File:** `src/storage/archetype_storage.hpp`
- **Scope:** Raw, unconditional traversal over 100% of memory chunks in the world. Zero component filtering, zero bitmasks, zero caching.
- **Contract:**
```cpp
namespace arche::internal {
struct ArchetypeStorage {
    // Replaces ChunkDistributor storage functions
    static Chunk* acquire_chunk(World &world, Archetype *arch);
    static void recycle_chunk(World &world, Archetype *arch, Chunk *chunk);
    static std::span<Chunk *const> get_chunks(const World &world, const Archetype *arch) noexcept;

    // Helper 1: Storage-Level Walker (Used by SnapshotFacade, World::clear, VersionStamping)
    template <typename Visitor>
    static inline void for_each_chunk(World &world, Visitor &&visitor) {
        auto &graph = WorldAccess::graph(world);
        for (const auto &arch_ptr : graph.archetypes()) {
            Archetype *arch = arch_ptr.get();
            for (Chunk *chunk : get_chunks(world, arch)) {
                visitor(arch, chunk);
            }
        }
    }
};
}
```

### 2. Query-Level Helper: `Query::for_each_active_chunk`
- **File:** `src/query/query.inl` (private member of `Query<Components...>`)
- **Scope:** Filtered, precomputed traversal over matched archetypes. Handles:
  - Skipping empty chunks (`chunk->is_empty()`).
  - Checking temporal change versions (`chunk_matches_filters(chunk, arch)`).
  - Resolving column byte offsets once per archetype (`resolve_column_offset`).
  - Issuing lookahead L1/L2 hardware prefetch hints (`prefetch_next_chunk_with_offsets`).
- **Contract:**
```cpp
template <typename... Components>
template <typename Visitor>
inline void Query<Components...>::for_each_active_chunk(Visitor &&visitor) {
    for (Archetype *arch : matching_archetypes()) {
        const auto chunks = internal::ArchetypeStorage::get_chunks(world_, arch);
        const size_t num_chunks = chunks.size();
        if (num_chunks == 0) continue;

        const auto offsets = std::make_tuple(
            resolve_column_offset<Components>(arch)...
        );

        for (size_t i = 0; i < num_chunks; ++i) {
            Chunk *chunk = chunks[i];
            if (chunk->is_empty() || !chunk_matches_filters(chunk, arch)) {
                continue;
            }

            // Lookahead prefetch for next chunk into L1/L2 cache
            if (i + 1 < num_chunks) {
                prefetch_next_chunk_with_offsets(chunks[i + 1], offsets);
            }

            visitor(chunk, arch, offsets);
        }
    }
}
```
- **Who uses it:**
  - `Query::each_chunk` / `for_each_chunk`
  - `Query::each_chunk_padded` / `for_each_chunk_padded`
  - `Query::each_parallel`
  - `QueryIterator::advance_to_valid_chunk`

---

## 6. Step-by-Step Dependency Order for Implementation

When performing the refactoring, follow this exact order from bottom to top so the project compiles at each step:

1. **Step 1 (Layer 1): Enrich `Chunk` with memory ops**
   - Move `broadcast_component` and `stream_component` into `src/chunk/chunk.hpp` as `chunk->fill_column(...)` and `chunk->copy_column(...)`.
2. **Step 2 (Layer 2): Create `src/storage/archetype_storage.hpp`**
   - Move chunk vector indexing, chunk acquisition, recycling, and `for_each_chunk` into `ArchetypeStorage`.
3. **Step 3 (Layer 3): Move placement methods to `src/entity/entity_allocator.hpp`**
   - Move `allocate_slot`, `update_entity_locations`, and `populate_chunks_batch` into `EntityAllocator`.
4. **Step 4 (Delete): Retire `ChunkDistributor`**
   - Delete `src/chunk/chunk_distributor.hpp` and `src/chunk/chunk_distributor.inl`.
5. **Step 5 (Layer 4): Unify `Query::for_each_active_chunk`**
   - Fuse empty checking, dirty checking, prefetching, and offset computation into `Query::for_each_active_chunk` in `src/query/query.inl`.
6. **Step 6 (Layer 5): Route consumers to their proper helper**
   - `SnapshotFacade` and `World::clear` call `ArchetypeStorage::for_each_chunk`.
   - `GpuFacade`, `SystemContext`, and iteration callbacks call `Query::each_chunk` / `Query::each_parallel`.
