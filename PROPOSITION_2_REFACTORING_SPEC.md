# Proposition 2 Refactoring Specification: Public API Contracts & Subsystems

This specification details the transition of **Arche ECS** to **Proposition 2**: a strict architectural separation between **Top-Level Core APIs**, **Active Subsystem Facades**, **Passive Public Types**, and **Internal Engine Implementations**.

---

## 1. Target Directory Hierarchy

```text
arche-ecs/
├── include/arche/
│   ├── arche.hpp                    # Master convenience umbrella header
│   ├── world.hpp                    # Central simulation coordinator (orchestrator only)
│   ├── query.hpp                    # Pure Query<Ts...> class declaration
│   ├── command.hpp                  # Pure CommandBuffer & MultiThreadCommandBuffer declarations
│   │
│   ├── facades/                     # Active Domain Subsystem Controllers (contracts)
│   │   ├── entity_facade.hpp        # EntityFacade (slot map / global entity table queries)
│   │   ├── archetype_facade.hpp     # ArchetypeFacade (graph metrics & chunk memory views)
│   │   ├── resource_facade.hpp      # ResourceFacade (global ambient singleton operations)
│   │   ├── system_facade.hpp        # SystemFacade (scheduler, pipeline, and stage execution)
│   │   ├── snapshot_facade.hpp      # SnapshotFacade (binary world persistence and disk I/O)
│   │   └── gpu_facade.hpp           # GpuFacade (contiguous chunk reflection & GPU streaming)
│   │
│   └── types/                       # Pure Passive Data Types, IDs, & Concepts (zero engine logic)
│       ├── entity.hpp               # Entity handle (64-bit uint wrapper), Batch descriptor
│       ├── entity_ref.hpp           # EntityRef (read-only) & EntityMut (mutable) accessors
│       ├── component_mask.hpp       # 128-bit SIMD bitset (uint64_t words[2]) & ResourceMask alias
│       ├── filters.hpp              # Query filter tags: With, Without, Mutated, Added, Or
│       ├── resource_id.hpp          # ResourceID typedef, INVALID_RESOURCE_ID constant
│       ├── access.hpp               # AccessDescriptor struct & AccessBuilder declaration
│       ├── plugin.hpp               # C++20 Plugin concept
│       ├── snapshot_header.hpp      # SnapshotHeader POD struct (magic bytes, version, counts)
│       └── gpu_span.hpp             # ComponentSpan<T> POD struct (ptr, count, byte stride)
│
└── src/                             # Internal Engine Subsystems & Implementations
    ├── archetype/                   # Archetype, ArchetypeGraph, ComponentMigration
    ├── chunk/                       # Chunk, ChunkDistributor, ChunkPool
    ├── command/                     # CommandPlayback, command_buffer.inl, command_buffer.cpp
    ├── component/                   # ColumnLayout, ComponentAccess, ComponentMask, TypeID
    ├── concurrency/                 # ChunkDispatcher, ThreadPool
    ├── entity/                      # SlotMap, EntityAllocator, EntityRecord, entity_view.inl
    ├── hardware/                    # Hardware cache descriptors, SIMD math utils
    ├── persistence/                 # Binary serialization layout, disk I/O helpers
    ├── query/                       # QueryIterator, query.inl (chunk SIMD visitor algorithms)
    ├── resource/                    # ResourceManager internal storage & slot registries
    ├── spatial/                     # Hilbert sort, Morton order utilities
    ├── system/                      # Scheduler, SystemContext, SystemStage implementation
    └── world/                       # WorldStorage, WorldAccess, world.cpp, world_impl.inl
```

---

## 2. File-by-File Refactoring Breakdown

---

### A. `include/arche/world.hpp` (Current: 221 lines)

#### What to KEEP in `include/arche/world.hpp`:
* `class World` top-level declaration.
* Facade member variables:
  ```cpp
  EntityFacade    entities{this};
  SnapshotFacade  snapshots{this};
  ResourceFacade  resources{this};
  ArchetypeFacade archetypes{this};
  SystemFacade    systems{this};
  GpuFacade       gpu{this};
  ```
* Public convenience lifecycle methods: `World(chunk_size)`, `~World()`, `clear()`.
* Public lifecycle template declarations: `spawn(...)`, `despawn(...)`.
* Public component template declarations: `get<T>()`, `get_mut<T>()`, `has<T>()`, `add<T>()`, `remove<T>()`.
* Handle factory declarations: `entity(id)` returning `EntityMut` / `EntityRef`.
* Query factory declaration: `query<Components...>(filters...)`.
* Public version accessor: `uint32_t version() const noexcept;`.

#### What to REMOVE from `include/arche/world.hpp`:
* ❌ `WorldStorage storage_;` (member held by value).
* ❌ `#include "world/world_storage.hpp"`.
* ❌ Inline implementation of `version()` reading `storage_.current_version`.
* ❌ All 6 trailing internal includes at the bottom:
  * `#include "chunk/chunk_distributor.inl"`
  * `#include "component/component_access.inl"`
  * `#include "entity/entity_allocator.inl"`
  * `#include "entity/entity_view.inl"`
  * `#include "world/world_access.hpp"`
  * `#include "world/world_facades_impl.hpp"`

#### Where Removed Things Move:
1. **`storage_` pointer:** Change to an opaque forward-declared pointer in `World`:
   ```cpp
   namespace internal { struct WorldStorage; }
   // In class World private section:
   internal::WorldStorage *storage_{nullptr};
   ```
   Allocated in `src/world/world.cpp` constructor and deleted in its destructor.
2. **Template implementations:** Move template method bodies for `spawn`, `get`, `add`, etc., into `src/world/world_impl.inl`.
3. **Trailing `.inl` inclusions:** Included inside `src/world/world_impl.inl` or `src/world/world.cpp`, completely hidden from public consumers.

---

### B. `src/world/world_facades.hpp` (Current: 238 lines)
*Currently mistakenly placed in `src/world/`.*

#### What to KEEP and WHERE to Split:
Split this monolithic file into 6 dedicated facade contract headers inside `include/arche/facades/`:

1. **`include/arche/facades/entity_facade.hpp`**:
   * Contains `class EntityFacade`: `contains()`, `count()`, `capacity()`, `is_empty()`, `resolve()`.
   * Includes only: `#include "arche/types/entity.hpp"`.
2. **`include/arche/facades/archetype_facade.hpp`**:
   * Contains `class ArchetypeFacade`: `count()`, `chunks()`, `count_with<T>()`.
   * Forward declares: `class Archetype; struct Chunk;`.
3. **`include/arche/facades/resource_facade.hpp`**:
   * Contains `class ResourceFacade`: `insert<T>()`, `get<T>()`, `has<T>()`, `remove<T>()`.
   * Includes only: `#include "arche/types/resource_id.hpp"`.
4. **`include/arche/facades/system_facade.hpp`**:
   * Contains `class SystemFacade`: `run()`, `compile()`, `add_system()`, `add_plugin()`, `scheduler()`.
   * Includes: `#include "arche/types/access.hpp"` and `#include "arche/types/plugin.hpp"`.
   * Forward declares: `class ThreadPool; class Scheduler;`.
5. **`include/arche/facades/snapshot_facade.hpp`**:
   * Contains `class SnapshotFacade`: `save()`, `load()`, `save_to_file()`, `load_from_file()`, `size_bytes()`, `header()`.
   * Includes only: `#include "arche/types/snapshot_header.hpp"`.
6. **`include/arche/facades/gpu_facade.hpp`**:
   * Contains `class GpuFacade`: `view_chunks<T>()`, `stream_to<T>()`.
   * Includes only: `#include "arche/types/gpu_span.hpp"`.

#### What to REMOVE from the Facades:
* ❌ Remove direct includes of internal engine headers:
  * `#include "hardware/gpu_layout.hpp"`
  * `#include "persistence/snapshot.hpp"`
  * `#include "system/scheduler.hpp"`
* ❌ Move inline template implementations of `ResourceFacade::get`, `GpuFacade::view_chunks`, etc., into `src/world/world_facades_impl.hpp`.

---

### C. `include/arche/query.hpp` (Current: 764 lines)

#### What to KEEP in `include/arche/query.hpp`:
* `template <typename... Components> class Query` class declaration.
* Forward declaration: `template <typename... Components> class QueryIterator;`.
* Public API member function signatures:
  * `using Iterator = QueryIterator<Components...>;`
  * `begin()`, `end()`
  * `for_each_chunk(fn)`
  * `for_each_entity(fn)`
  * `for_each_chunk_parallel(pool, fn)`
  * `for_each_entity_parallel(pool, fn)`
  * `chunk_count()`, `entity_count()`
* Public inclusion of:
  * `#include "arche/types/entity.hpp"`
  * `#include "arche/types/filters.hpp"`

#### What to REMOVE from `include/arche/query.hpp`:
* ❌ Filter tag definitions (already extracted to `include/arche/types/filters.hpp`).
* ❌ All 600+ lines of internal template implementation bodies from the `Query` class:
  * Constructor `Query(World&, Filters...)` body
  * `for_each_chunk`, `for_each_entity` loops and lookahead prefetching
  * `for_each_chunk_parallel`, `for_each_entity_parallel` work-stealing/chunk dispatcher invocations
  * Private archetype matching and bitmask evaluation (`build_masks()`, `apply_filter()`, `matching_archetypes()`, `is_valid_archetype()`, `filter_custom_archetypes()`)
  * Column layout and pointer resolution (`get_chunks_for()`, `resolve_column_offset()`, `resolve_chunk_row_ptr()`)
  * Reactive tick checks (`chunk_matches_filters()`, `column_matches_component()`, `evaluate_mutated()`)
  * Trailing inline definitions of `begin()`, `end()`, and `World::query()`
* ❌ Direct internal engine includes:
  * `#include "archetype/archetype.hpp"`
  * `#include "chunk/chunk_distributor.hpp"`
  * `#include "component/component_mask.hpp"`
  * `#include "chunk/chunk.hpp"`
  * `#include "component/type_id.hpp"`
  * `#include "world/world_access.hpp"`
  * `#include "concurrency/chunk_dispatcher.inl"`
  * `#include "query/filter.hpp"`
  * `#include "query/query_iterator.hpp"`

#### Where Things Live & Move:
1. **Filter tags:** In `include/arche/types/filters.hpp` (public tags) and `src/query/filter.hpp` (internal unpackers).
2. **QueryIterator:** Defined in its own dedicated internal header **`src/query/query_iterator.hpp`** (155 lines; not defined in `query.hpp`).
3. **Query Implementation Bodies:** Move all internal template method bodies and trailing inlines (`begin()`, `end()`, `World::query()`) into **`src/query/query.inl`**.
4. At the bottom of `include/arche/query.hpp`, include only `#include "query/query.inl"` (which in turn includes `src/query/query_iterator.hpp` and internal subsystems).

---

### D. `include/arche/command.hpp` (Current: 320 lines)

#### What to KEEP in `include/arche/command.hpp`:
* `enum class CommandType : uint8_t` (command discriminant).
* `class CommandBuffer` public declaration:
  * `CommandBuffer(initial_capacity)`
  * `~CommandBuffer()`
  * `spawn(components...)`
  * `despawn(entity)` / `despawn(span<Entity>)`
  * `add<T>(entity, component)`
  * `remove<T>(entity)`
  * `commit(World &)`
  * `clear()`, `empty()`, `size_bytes()`
* `class MultiThreadCommandBuffer` declaration.
* Includes only:
  * `#include "arche/types/entity.hpp"`

#### What to REMOVE from `include/arche/command.hpp`:
* ❌ Private low-level byte-packing functions: `write_bytes`, `write_pod`, `write_entity_payload`, `write_component_payload`.
* ❌ Type-erased function pointers and helpers: `SpawnFn`, `RegisterTypeFn`, `spawn_entity_helper`, `register_type_helper`.
* ❌ Direct include of `#include "component/type_id.hpp"`.

#### Where Removed Things Move:
* Move all serialization helpers, byte-packing functions, and `MultiThreadCommandBuffer` inline implementations into `src/command/command_buffer.inl`.
* At the bottom of `include/arche/command.hpp`, include `#include "command/command_buffer.inl"`.

---

### E. `include/arche/entity.hpp` (Current: 366 lines)

#### What to KEEP and WHERE to Split:
Split into two distinct type headers in `include/arche/types/`:

1. **`include/arche/types/entity.hpp`**:
   * `struct Entity`: The pure 64-bit generational handle (bits 0-31: slot index, bits 32-63: generation).
   * Canonical sentinels: `null_entity`, `INDEX_NONE`.
   * Standard hash specialization: `template <> struct std::hash<arche::Entity>`.
   * `struct Batch { size_t count{0}; };`.
2. **`include/arche/types/entity_ref.hpp`**:
   * `class EntityRef`: Lightweight read-only accessor (`is_alive()`, `has<T>()`, `get<T>()`).
   * `class EntityMut`: Lightweight mutable accessor (`is_alive()`, `has<T>()`, `get<T>()`, `get_mut<T>()`, `add<T>()`, `remove<T>()`, `despawn()`).
   * Compatibility alias: `using EntityView = EntityMut;`.

#### What to REMOVE from `include/arche/entity.hpp`:
* ❌ `advance_nonzero(uint32_t)`: Move to internal `src/entity/slot_map.hpp` or `src/hardware/math_utils.hpp`.
* ❌ Inline method bodies of `EntityRef::get`, `EntityMut::add`, etc., stay in `src/entity/entity_view.inl`.

---

### F. `include/arche/resource.hpp` (Current: 324 lines)

#### What to KEEP in `include/arche/types/resource_id.hpp`:
* `using ResourceID = uint32_t;`
* `inline constexpr ResourceID INVALID_RESOURCE_ID = 0xFFFFFFFF;`

#### What to REMOVE from `include/arche/resource.hpp`:
* ❌ `Internal::allocate_resource_id_sequential()`
* ❌ `Internal::ResourceSlot` struct
* ❌ `Internal::ResourceRegistry` class
* ❌ Entire `class ResourceManager` (300+ lines of vector memory storage, type erasure, and slot lookups).

#### Where Removed Things Move:
* The entire `ResourceManager`, its slot structures, and sequential ID counters move to **`src/resource/resource_manager.hpp`**.
* Consumers only interact with resources through `world.resources` (`ResourceFacade`), keeping `ResourceManager` 100% private.

---

### G. `include/arche/system.hpp` (Current: 6 lines)
*Currently an empty forwarder with 4 `#include "system/..."` directives.*

#### What to KEEP in `include/arche/system.hpp`:
Replace the entire file with the clean public contract declarations:
* `#include "arche/facades/system_facade.hpp"`
* `#include "arche/types/access.hpp"`
* `#include "arche/types/plugin.hpp"`

#### What to REMOVE from `include/arche/system.hpp`:
* ❌ `#include "system/system_context.hpp"` (internal).
* ❌ `#include "system/scheduler.hpp"` (internal).

#### Content of `include/arche/types/access.hpp`:
* `using ResourceMask = ComponentMask;` (or forward-declared bitmask type).
* `struct AccessDescriptor` (reads, writes bitmasks).
* `class AccessBuilder` (fluent builder for `.reads<T>()`, `.writes<T>()`).

#### Content of `include/arche/types/plugin.hpp`:
* C++20 `template <typename T> concept Plugin = requires(T plugin, World &world) { plugin.build(world); };`.

---

### H. `include/arche/arche.hpp` (Current: 51 lines)

#### What to KEEP in `include/arche/arche.hpp`:
The master umbrella header now cleanly exports only the public contracts and facades:
```cpp
#pragma once

// IWYU pragma: begin_exports

// Core Orchestrator
#include "arche/world.hpp"
#include "arche/query.hpp"
#include "arche/command.hpp"

// Subsystem Facades
#include "arche/facades/archetype_facade.hpp"
#include "arche/facades/entity_facade.hpp"
#include "arche/facades/gpu_facade.hpp"
#include "arche/facades/resource_facade.hpp"
#include "arche/facades/snapshot_facade.hpp"
#include "arche/facades/system_facade.hpp"

// Public Types & Concepts
#include "arche/types/access.hpp"
#include "arche/types/component_mask.hpp"
#include "arche/types/entity.hpp"
#include "arche/types/entity_ref.hpp"
#include "arche/types/filters.hpp"
#include "arche/types/gpu_span.hpp"
#include "arche/types/plugin.hpp"
#include "arche/types/resource_id.hpp"
#include "arche/types/snapshot_header.hpp"

// IWYU pragma: end_exports
```

#### What to REMOVE from `include/arche/arche.hpp`:
* ❌ All 20 direct inclusions of internal headers from `src/` (`hardware/hardware.hpp`, `component/component_mask.hpp`, `entity/slot_map.hpp`, `archetype/archetype.hpp`, etc.).

---

## 3. Specification for `include/arche/types/` Headers

The `include/arche/types/` directory hosts **pure passive types, descriptors, C++20 concepts, and handle wrappers**. These files contain zero runtime engine logic, zero chunk traversal algorithms, and zero memory pool interactions.

---

### 1. `include/arche/types/entity.hpp`
* **Purpose:** Opaque 64-bit generational entity handle and batch allocation configuration.
* **Exact Contents:**
  * `struct Batch`: Contains `size_t count{0};`.
  * `struct Entity`: 64-bit handle packed as:
    * Bits 0–31 (32 bits): Dense slot index in the SlotMap.
    * Bits 32–63 (32 bits): Monotonic generation counter.
    * Bitfield constants: `INDEX_BITS = 32`, `GENERATION_BITS = 32`, `INDEX_MASK`, `GENERATION_MASK`, `INDEX_NONE`, `INDEX_NONE_32`.
    * Constructors & Factory Methods: `constexpr Entity()`, `from_parts(index, gen)`, `from_raw(uint64_t)`.
    * Field Accessors: `id()`, `index()`, `generation()`, `raw()`.
    * Operators: `operator bool()`, `is_null()`, `operator<=> = default`, `operator== = default`.
  * Invariant Guarantees: `static_assert` for `sizeof == 8`, `is_standard_layout_v`, `is_trivially_copyable_v`, `is_trivially_destructible_v`.
  * Canonical Sentinel: `inline constexpr Entity null_entity = Entity::from_raw(Entity::INDEX_NONE);`.
  * Standard Hash Specialization: `template <> struct std::hash<arche::Entity>`.
* **Dependencies:** `<compare>`, `<cstddef>`, `<cstdint>`, `<functional>`, `<type_traits>`.

---

### 2. `include/arche/types/entity_ref.hpp`
* **Purpose:** Lightweight handles encapsulating an `Entity` and a pointer to `World` for ergonomic chained access.
* **Exact Contents:**
  * Forward declaration: `class World;`.
  * `class EntityRef` (Read-only view on `const World*`):
    * `constexpr EntityRef()`, `constexpr EntityRef(const World *world, Entity entity)`.
    * `id()`, `explicit operator Entity()`.
    * `is_alive() const noexcept`.
    * `template <typename T> bool has() const noexcept`.
    * `template <typename T> const T &get() const`.
  * `class EntityMut` (Mutable view on `World*`):
    * `constexpr EntityMut()`, `constexpr EntityMut(World *world, Entity entity)`.
    * `id()`, `explicit operator Entity()`.
    * `is_alive() const noexcept`.
    * `template <typename T> bool has() const noexcept`.
    * `template <typename T> const T &get() const`.
    * `template <typename T> T &get_mut()`.
    * `template <typename T> void add(T &&component)`.
    * `template <typename T> void remove()`.
    * `bool despawn()`.
    * `operator EntityRef() const noexcept`, `as_ref() const noexcept`.
* **Out-of-line Implementation:** All method bodies calling `world_->...` reside in `src/entity/entity_view.inl`.
* **Dependencies:** `"arche/types/entity.hpp"`, `<cassert>`, `<cstddef>`, `<cstdint>`.

---

### 3. `include/arche/types/filters.hpp`
* **Purpose:** Compile-time tag types and structural query filter descriptors.
* **Exact Contents:**
  * Tag Types:
    * `template <typename... Ts> struct With {};` (Inclusion filter without loop unpacking).
    * `template <typename... Ts> struct Without {};` (Exclusion filter).
    * `template <typename T> struct Has { using component_type = T; };` (Optional boolean presence check).
    * `template <typename... Filters> struct Or {};` (Disjunctive filter).
  * Compile-Time Filter Traits:
    * `is_with_filter<T>`, `is_with_filter_v<T>`
    * `is_without_filter<T>`, `is_without_filter_v<T>`
    * `is_has<T>`, `is_has_v<T>`
    * `is_or_filter<T>`, `is_or_filter_v<T>`
  * Query Element Traits:
    * `query_element<T>` (maps `T` to `T&`, pointers to `T`, and `Has<T>` to `bool`).
    * `column_pointer_type<T>` (maps `T` to `T*`, pointers to `T`, and `Has<T>` to `bool`).
* **Dependencies:** `<cstdint>`, `<type_traits>`.

---

### 4. `include/arche/types/resource_id.hpp`
* **Purpose:** Strong integer identifier and constants for world-level ambient singleton resources.
* **Exact Contents:**
  * `using ResourceID = uint32_t;`
  * `inline constexpr ResourceID INVALID_RESOURCE_ID = 0xFFFFFFFF;`
* **Dependencies:** `<cstdint>`.

---

### 5. `include/arche/types/component_mask.hpp`
* **Purpose:** 128-bit fixed-width component signature bitmask and `ResourceMask` alias for instant bitwise operations and archetype matching.
* **Exact Contents:**
  * `struct ComponentMask`:
    * Constants: `MAX_COMPONENTS = 128`, `WORDS = 2`.
    * Member Data: `uint64_t words[2]{0, 0};` (16 bytes total, fits in single SIMD register `%xmm`/NEON).
    * Constructors: `constexpr ComponentMask() noexcept = default;`, `constexpr explicit ComponentMask(uint64_t w0, uint64_t w1) noexcept;`.
    * Bit Manipulation Methods: `set(bit)`, `reset(bit)`, `flip(bit)`, `test(bit) const`, `any() const`, `all() const`, `empty() const`, `count() const`.
    * Set Relationship Methods: `contains_all(const ComponentMask &other) const`, `contains_any(const ComponentMask &other) const`, `contains_none(const ComponentMask &other) const`.
    * Bitwise Operator Overloads: `&`, `|`, `^`, `~`, `&=`, `|=`, `^=`, `==`, `<=>`.
  * `static_assert(sizeof(ComponentMask) == 16, "ComponentMask must be exactly 16 bytes");`
  * Standard layout and trivially copyable `static_assert` invariant checks.
  * Type Alias: `using ResourceMask = ComponentMask;` (Reuses the 128-bit fixed-width mask layout for World resources).
* **Dependencies:** `<bit>`, `<cstddef>`, `<cstdint>`, `<type_traits>`. Zero dependencies on engine classes.

---

### 6. `include/arche/types/access.hpp`
* **Purpose:** Declarative data-access descriptor contracts and fluent builder interface for compile-time parallel DAG scheduling.
* **Pure Public Contract (What stays in `types/access.hpp`):**
  * Includes: `#include "arche/types/component_mask.hpp"`, `#include "arche/types/resource_id.hpp"`.
  * `struct AccessDescriptor`:
    * Public declarations: `bool conflicts_with(const AccessDescriptor &other) const noexcept;`
    * Public declarations: `bool is_empty() const noexcept;`
    * Bitmask member data:
      ```cpp
      ComponentMask read_components{};
      ComponentMask write_components{};
      ResourceMask  read_resources{};
      ResourceMask  write_resources{};
      ```
  * `class AccessBuilder`:
    * Pure template method declarations:
      ```cpp
      template <typename... Comps> AccessBuilder &reads() noexcept;
      template <typename... Comps> AccessBuilder &writes() noexcept;
      template <typename... Res>   AccessBuilder &reads_resource() noexcept;
      template <typename... Res>   AccessBuilder &writes_resource() noexcept;
      [[nodiscard]] AccessDescriptor build() const noexcept;
      ```
  * `using Access = AccessBuilder;`
  * Trailing include at bottom: `#include "system/access.inl"`
* **What MUST be moved out to `src/` (No implementation in header):**
  * ❌ `AccessDescriptor::conflicts_with()` logic (30 lines of SIMD bitmask operations) $\rightarrow$ Moved to **`src/system/access.cpp`** (compiled once into `libarche.a`).
  * ❌ `AccessDescriptor::is_empty()` logic $\rightarrow$ Moved to **`src/system/access.cpp`**.
  * ❌ `AccessBuilder` template bodies (`reads()`, `writes()`, `reads_resource()`, `writes_resource()`, `build()`) $\rightarrow$ Moved to **`src/system/access.inl`**.
  * ❌ Direct includes of `#include "component/type_id.hpp"` $\rightarrow$ Moved to **`src/system/access.inl`**.
* **Dependencies:** `"arche/types/component_mask.hpp"`, `"arche/types/resource_id.hpp"`, `<cstdint>`, `<type_traits>`.

---

### 7. `include/arche/types/plugin.hpp`
* **Purpose:** Compile-time C++20 concept enforcing the modular plugin lifecycle without virtual methods or RTTI.
* **Exact Contents:**
  * Forward declarations: `class World; class Scheduler;`.
  * `template <typename P> concept Plugin`:
    * Requires `plugin.build(world, scheduler) -> std::same_as<void>`: Mandatory initialization hook.
    * Requires `P::name() -> std::convertible_to<std::string_view>`: Static unique plugin name for logging and duplicate prevention.
* **Dependencies:** `<concepts>`, `<string_view>`.

---

### 8. `include/arche/types/snapshot_header.hpp`
* **Purpose:** Fixed-width 64-byte binary serialization header for full-world checkpoints.
* **Exact Contents:**
  * `struct alignas(16) SnapshotHeader`:
    * `uint32_t magic{0x48435241};` ('ARCH' in Little-Endian)
    * `uint32_t version{1};`
    * `uint64_t current_version{0};`
    * `uint32_t slot_count{0};`
    * `uint32_t free_list_head{0};`
    * `uint32_t active_entities{0};`
    * `uint32_t chunk_count{0};`
    * `size_t total_payload_bytes{0};`
    * `uint32_t archetype_count{0};`
    * `uint8_t reserved[20]{0};` (Padding to exactly 64 bytes)
  * `static_assert(sizeof(SnapshotHeader) == 64, "SnapshotHeader must be exactly 64 bytes");`
* **Dependencies:** `<cstddef>`, `<cstdint>`.

---

### 9. `include/arche/types/gpu_span.hpp`
* **Purpose:** Lightweight contiguous slice descriptors for zero-copy GPU buffer uploads.
* **Exact Contents:**
  * `template <typename T> class ComponentSpan`:
    * Constructor: `ComponentSpan(const T *data, uint32_t count) noexcept`.
    * `const T *data() const noexcept`.
    * `uint32_t count() const noexcept`.
    * `size_t size_bytes() const noexcept`.
    * `size_t stride() const noexcept`.
    * `bool empty() const noexcept`.
    * `const T &operator[](size_t index) const noexcept`.
    * `std::span<const T> as_span() const noexcept`.
  * `template <typename T> struct Std430Traits`:
    * Compile-time traits validating alignment and padding against GPU std430 specifications (Vulkan SSBOs, Metal, DirectX).
* **Dependencies:** `<cassert>`, `<cstddef>`, `<cstdint>`, `<span>`, `<type_traits>`.

---

## 4. Implementation Separation Rules (.inl vs .cpp)

To guarantee that **zero implementation bodies** remain in the public contract headers (`include/arche/`), the following strict rules apply across the codebase:

### The Separation Rules:
1. **Public Headers (`include/arche/`) Contain Pure Declarations Only:**
   - No function bodies with engine logic.
   - No internal helper includes.
   - The *only* exceptions are `constexpr` arithmetic on value objects (`Entity::index()`, `Entity::generation()`) which C++ requires in the header for compile-time evaluation.
2. **Non-Template Functions $\rightarrow$ Dedicated `.cpp` Files in `src/`:**
   - Any function that does not depend on a template parameter must be compiled into `libarche.a`.
   - Examples: `World::World()`, `World::despawn()`, `EntityFacade::contains()`, `SnapshotFacade::save()`, `SystemFacade::run()`.
3. **Template & Inline Functions $\rightarrow$ Dedicated `.inl` Files in `src/`:**
   - Any template method that cannot be compiled into an object file has its implementation body moved out of `include/arche/` into a dedicated `.inl` file located inside the corresponding `src/<subsystem>/` directory.
   - The public header (or internal compilation unit) includes the `.inl` file at the bottom, keeping the header clean and readable as a contract.

---

### Detailed Implementation Location Mapping

| Contract Header (`include/arche/`) | Methods / Signatures | Type | Implementation File | Location |
| :--- | :--- | :---: | :--- | :--- |
| **`world.hpp`** | `World(chunk_size)`, `~World()`, `clear()`, `despawn(Entity)`, `despawn(span)`, `version()` | Non-Template | `.cpp` | `src/world/world.cpp` |
| | `spawn(components...)`, `spawn(Batch, ...)` | Template | `.inl` | `src/entity/entity_allocator.inl` (via `src/world/world_impl.inl`) |
| | `get<T>()`, `get_mut<T>()`, `has<T>()`, `add<T>()`, `remove<T>()` | Template | `.inl` | `src/component/component_access.inl` (via `src/world/world_impl.inl`) |
| | `entity(id)` (const & mutable) | Inline | `.inl` | `src/entity/entity_view.inl` |
| | `query<Comps...>(filters...)` | Template | `.inl` | `src/query/query.inl` |
| **`query.hpp`** | `begin()`, `end()`, `chunk_count()`, `entity_count()` | Template | `.inl` | `src/query/query.inl` |
| | `for_each_chunk()`, `for_each_entity()` | Template | `.inl` | `src/query/query.inl` |
| | `for_each_chunk_parallel()`, `for_each_entity_parallel()` | Template | `.inl` | `src/concurrency/chunk_dispatcher.inl` & `src/query/query.inl` |
| | `QueryIterator` (operator `++`, `*`, `!=`) | Template | Header | `src/query/query_iterator.hpp` (included by `src/query/query.inl`) |
| **`command.hpp`** | `commit(World &)`, `clear()` | Non-Template | `.cpp` | `src/command/command_buffer.cpp` |
| | `spawn(comps...)`, `add<T>()`, `remove<T>()`, `despawn()` | Template | `.inl` | `src/command/command_buffer.inl` |
| | Byte-packing helpers (`write_pod`, `write_bytes`, `spawn_entity_helper`) | Internal | `.inl` | `src/command/command_buffer.inl` |
| | `MultiThreadCommandBuffer` methods | Inline | `.inl` | `src/command/command_buffer.inl` |
| **`facades/entity_facade.hpp`**<br>*(or `contracts/entity.hpp`)* | `contains()`, `count()`, `capacity()`, `is_empty()`, `resolve()` | Non-Template | `.cpp` | `src/world/world_facades.cpp` (or `src/entity/entity_facade.cpp`) |
| **`facades/archetype_facade.hpp`**<br>*(or `contracts/archetype.hpp`)* | `count()`, `chunks(arch)`, `chunks(const arch)` | Non-Template | `.cpp` | `src/world/world_facades.cpp` |
| | `count_with<T>()` | Template | `.inl` | `src/archetype/archetype_facade.inl` (or `src/world/world_facades_impl.hpp`) |
| **`facades/resource_facade.hpp`**<br>*(or `contracts/resource.hpp`)* | `has(ResourceID id)` | Non-Template | `.cpp` | `src/world/world_facades.cpp` |
| | `insert<T>()`, `get<T>()`, `has<T>()`, `remove<T>()` | Template | `.inl` | `src/resource/resource_facade.inl` (or `src/world/world_facades_impl.hpp`) |
| **`facades/system_facade.hpp`**<br>*(or `contracts/system.hpp`)* | `run()`, `compile()`, `add_system()`, `scheduler()` | Non-Template | `.cpp` | `src/world/world_facades.cpp` |
| | `add_plugin<P>()` | Template | `.inl` | `src/system/system_facade.inl` (or `src/world/world_facades_impl.hpp`) |
| **`facades/snapshot_facade.hpp`**<br>*(or `contracts/snapshot.hpp`)* | `save()`, `load()`, `save_to_file()`, `load_from_file()`, `size_bytes()`, `header()` | Non-Template | `.cpp` | `src/world/world_facades.cpp` *(100% non-template, zero `.inl`!)* |
| **`facades/gpu_facade.hpp`**<br>*(or `contracts/gpu.hpp`)* | `view_chunks<T>()`, `stream_to<T>()` | Template | `.inl` | `src/hardware/gpu_facade.inl` (or `src/world/world_facades_impl.hpp`) |
| **`types/entity.hpp`** | `index()`, `generation()`, `from_parts()`, `from_raw()`, `operator<=>` | `constexpr` | In-Header | `include/arche/types/entity.hpp` *(Required for compile-time evaluation)* |
| **`types/entity_ref.hpp`** | `is_alive()`, `has<T>()`, `get<T>()` | Template/Inline | `.inl` | `src/entity/entity_view.inl` |
| | `get_mut<T>()`, `add<T>()`, `remove<T>()`, `despawn()` | Template/Inline | `.inl` | `src/entity/entity_view.inl` |
| **`types/component_mask.hpp`** | `words[2]`, `set()`, `test()`, `contains_all()`, bitwise ops | `constexpr`/Inline | In-Header | `include/arche/types/component_mask.hpp` *(Pure 128-bit POD bitset)* |
| **`types/access.hpp`** | `conflicts_with()`, `is_empty()` | Non-Template | `.cpp` | `src/system/access.cpp` |
| | `AccessBuilder::reads<T>()`, `.writes<T>()`, `.reads_resource<T>()`, `.writes_resource<T>()`, `.build()` | Template | `.inl` | `src/system/access.inl` |
| **`types/gpu_span.hpp`** | `ComponentSpan<T>::operator[]`, `as_span()`, `size_bytes()` | Template | `.inl` | `src/hardware/gpu_span.inl` (or direct inline getters in struct) |
| **`types/filters.hpp`** | Tag structs (`With`, `Without`, `Has`, `Or`) & traits | Pure Types | None | Pure declaration tags (no runtime functions) |
| **`types/plugin.hpp`** | `concept Plugin` | Pure Concept | None | Pure compile-time concept (no runtime functions) |
| **`types/resource_id.hpp`** | `ResourceID` typedef & `INVALID_RESOURCE_ID` | Constants | None | Pure typedef (no runtime functions) |
| **`types/snapshot_header.hpp`** | `struct alignas(16) SnapshotHeader` | POD Struct | None | Pure data layout struct (no runtime functions) |

---

## 5. Summary Mapping Table

| Item / Type | Current Location | New Location (Proposition 2) | Nature |
| :--- | :--- | :--- | :--- |
| `World` | `include/arche/world.hpp` | `include/arche/world.hpp` | Top-Level Orchestrator |
| `WorldStorage` | `include/arche/world.hpp` | `src/world/world_storage.hpp` | Internal Storage (Opaque Ptr in World) |
| `Query<...>` | `include/arche/query.hpp` | `include/arche/query.hpp` | Public Query Interface |
| `Query<...> template bodies` | `include/arche/query.hpp` | `src/query/query.inl` | Internal Implementation |
| `QueryIterator` | `src/query/query_iterator.hpp` | `src/query/query_iterator.hpp` | Internal Implementation Header |
| `With`, `Without`, `Mutated` | `include/arche/query.hpp` | `include/arche/types/filters.hpp` | Public Value Tag |
| `CommandBuffer` | `include/arche/command.hpp` | `include/arche/command.hpp` | Public Command Interface |
| `spawn_entity_helper`, `write_pod` | `include/arche/command.hpp` | `src/command/command_buffer.inl` | Internal Implementation |
| `Entity` | `include/arche/entity.hpp` | `include/arche/types/entity.hpp` | Public 64-bit Handle |
| `EntityRef`, `EntityMut` | `include/arche/entity.hpp` | `include/arche/types/entity_ref.hpp` | Public Handle Accessors |
| `Batch` | `include/arche/entity.hpp` | `include/arche/types/entity.hpp` | Public Descriptor |
| `advance_nonzero` | `include/arche/entity.hpp` | `src/entity/slot_map.hpp` | Internal Math Helper |
| `EntityFacade` | `src/world/world_facades.hpp` | `include/arche/facades/entity_facade.hpp` | Public Subsystem Facade |
| `ArchetypeFacade` | `src/world/world_facades.hpp` | `include/arche/facades/archetype_facade.hpp` | Public Subsystem Facade |
| `ResourceFacade` | `src/world/world_facades.hpp` | `include/arche/facades/resource_facade.hpp` | Public Subsystem Facade |
| `SystemFacade` | `src/world/world_facades.hpp` | `include/arche/facades/system_facade.hpp` | Public Subsystem Facade |
| `SnapshotFacade` | `src/world/world_facades.hpp` | `include/arche/facades/snapshot_facade.hpp` | Public Subsystem Facade |
| `GpuFacade` | `src/world/world_facades.hpp` | `include/arche/facades/gpu_facade.hpp` | Public Subsystem Facade |
| `ResourceManager` | `include/arche/resource.hpp` | `src/resource/resource_manager.hpp` | Internal Engine Storage |
| `ResourceID`, `INVALID_...` | `include/arche/resource.hpp` | `include/arche/types/resource_id.hpp` | Public Type / Constant |
| `ComponentMask` | `src/component/component_mask.hpp` | `include/arche/types/component_mask.hpp` | Public 128-bit SIMD Bitset / Value Type |
| `ResourceMask` | `src/system/access.hpp` | `include/arche/types/component_mask.hpp` | Public Type Alias (`using ResourceMask = ComponentMask;`) |
| `Plugin` concept | `src/system/plugin.hpp` | `include/arche/types/plugin.hpp` | Public C++20 Concept |
| `AccessDescriptor`, `AccessBuilder` | `src/system/access.hpp` | `include/arche/types/access.hpp` | Public Declaration Struct/Builder |
| `SnapshotHeader` | `src/persistence/snapshot.hpp` | `include/arche/types/snapshot_header.hpp` | Public POD Struct |
| `ComponentSpan<T>` | `src/hardware/gpu_layout.hpp` | `include/arche/types/gpu_span.hpp` | Public POD Span Struct |
