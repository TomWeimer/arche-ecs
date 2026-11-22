# Encapsulation & Capability Audit Report

## Mission & Purpose
This document provides a comprehensive structural audit of data encapsulation across the library, identifying every internal container, physical layout detail, and storage pointer currently exposed to users. It classifies their exposure mode, evaluates safety and invariant hazards, and defines the target encapsulation boundaries.

The concluding section identifies architectural capabilities, state introspection mechanisms, and mutation patterns present in modern archetype ecosystems (such as Bevy ECS) that are not currently available in Arche-ECS.

---

## 1. Inventory of Current Encapsulation Leaks

The following table catalogs every internal type, pointer, or mutable reference accessible from the public surface of the library:

| Member / Function | Hosting Type | Exposure Mode | Return / Parameter Type | Encapsulation Hazard |
| :--- | :--- | :--- | :--- | :--- |
| `get_record(Entity)` | `World` | Const Reference to Internal State | `const EntityRecord &` | **Critical:** Exposes raw pointers `Archetype*` and `Chunk*`, along with internal chunk row index. Breaks if the entity moves or migrates. |
| `registry()` | `World` | **Mutable Reference** | `TypeRegistry &` | **Critical:** Allows arbitrary modification, registration, and deregistration of component type descriptors while the simulation is running. |
| `graph()` | `World` | **Mutable Reference** | `ArchetypeGraph &` | **Critical:** Allows external callers to traverse, mutate, or alter the archetype directed acyclic graph (DAG) outside engine controls. |
| `get_archetype_chunks(arch)` | `World` | **Mutable Reference** | `std::vector<Chunk *> &` | **Critical:** Exposes raw array of chunk pointers. Callers can clear, reorder, or corrupt chunk allocations directly. |
| `get_archetype_chunks(arch) const` | `World` | Span of Raw Pointers | `std::span<Chunk *const>` | **High:** Exposes raw `Chunk*` pointers, allowing manual pointer dereference into 64 KB memory pages. |
| `chunks(arch)` | `ArchetypeFacade` | **Mutable Reference** | `std::vector<Chunk *> &` | **Critical:** Duplicate of the chunk leak under the facade namespace. |
| `chunks(arch) const` | `ArchetypeFacade` | Span of Raw Pointers | `std::span<Chunk *const>` | **High:** Exposes raw `Chunk*` memory blocks directly to users. |
| `allocate_slot(arch, entity)` | `World` | Raw Pointer Return | `std::pair<Chunk *, uint32_t>` | **High:** Direct low-level chunk memory allocation exposed on the coordinator. |
| `acquire_chunk_for(arch)` | `World` | Raw Pointer Return | `Chunk *` | **High:** Internal distributor function exposed directly to users. |
| `recycle_chunk(arch, chunk)` | `World` | Raw Pointer Parameter | `Archetype *, Chunk *` | **High:** Internal memory deallocation exposed directly to users. |
| `mark_dirty<T>(chunk)` | `World` | Raw Pointer Parameter | `Chunk *` | **Medium:** Requires caller to have obtained a raw `Chunk*` pointer to stamp column version. |
| `permute(arch, indices)` | `World` | Raw Pointer Parameter | `Archetype *` | **Medium:** Requires caller to have obtained an internal `Archetype*` pointer to trigger spatial sorting. |
| `get_or_create_query_cache(...)` | `World` | Raw Pointer Return | `QueryCache *` | **Medium:** Exposes raw internal cache pointer and requires internal component masks. |
| `container()` | `ResourceFacade` | **Mutable Reference** | `ResourceContainer &` | **High:** Allows direct untracked mutation of the underlying type-erased resource storage. |
| `scheduler()` | `SystemFacade` | **Mutable Reference** | `Scheduler &` | **High:** Exposes internal system dependency stages, barriers, and graph nodes for direct mutation. |
| `column_layouts()` | `Archetype` | Const Reference to Internals | `const std::vector<ColumnLayout> &` | **Medium:** Exposes internal byte offsets, sizes, alignments, and type IDs of chunk columns. |
| `component_element(...)` | `Chunk` | Untyped Raw Byte Pointer | `uint8_t *` | **Critical:** Unchecked manual pointer arithmetic into 64 KB physical pages without version ticking. |

---

## 2. In-Depth Encapsulation Analysis by Subsystem

### 2.1 Entity Physical Location (`EntityRecord`)
- **Current State:** `World::get_record(Entity)` returns `const EntityRecord &`, exposing:
  ```cpp
  struct EntityRecord {
    Archetype *archetype{nullptr};
    Chunk *chunk{nullptr};
    uint32_t row{0};
  };
  ```
- **Hazard:** 
  1. If an entity has a component added or removed, it migrates to a different archetype and chunk. The previously returned `EntityRecord` references dead or relocated data.
  2. If an entity is despawned, the last entity in the chunk is swapped into its row (`swap_and_pop`), invalidating the record's row offset.
- **Remediation:** 
  - Restrict `EntityRecord` to `internal::WorldAccess::slot_map(world)` only.
  - The public API should only provide boolean verification (`world.is_alive(e)`) and component access (`world.get<T>(e)`).

### 2.2 Global Registries & Graphs (`TypeRegistry`, `ArchetypeGraph`)
- **Current State:** `World::registry()` and `World::graph()` return mutable references (`TypeRegistry &`, `ArchetypeGraph &`).
- **Hazard:** 
  - External code can add types, invalidate component masks, or mutate DAG edges while queries and command buffers hold cached pointers or indices.
- **Remediation:**
  - `registry` and `graph` must be completely private within `WorldStorage`.
  - Registration must happen automatically and immutably via template instantiation (`type_id<T>()`).

### 2.3 Chunk Memory Management (`ChunkPool`, `Chunk*`, `allocate_slot`)
- **Current State:** Low-level chunk allocation and deallocation (`allocate_slot`, `acquire_chunk_for`, `recycle_chunk`, `mark_dirty`) are declared on `World`.
- **Hazard:**
  - Callers can free chunks that contain living entities, cause double-frees in the pool, or corrupt chunk capacity metadata.
- **Remediation:**
  - Relocate all chunk management methods exclusively into `internal::ChunkDistributor`.
  - User code should never encounter a `Chunk*` type under any circumstance.

### 2.4 Domain Facade Leakage (`ResourceFacade`, `SystemFacade`)
- **Current State:** `world.resources.container()` and `world.systems.scheduler()` return mutable references to their backing containers.
- **Hazard:**
  - Bypasses facade safety invariants (e.g., resource initialization tracking, system dependency sorting).
- **Remediation:**
  - Remove `.container()` from `ResourceFacade`. Keep only typed methods: `insert<T>()`, `get<T>()`, `has<T>()`, `remove<T>()`.
  - Remove `.scheduler()` from `SystemFacade`. Keep only workflow methods: `add_plugin<P>()`, `run()`.

---

## 3. Recommended Encapsulation Boundary Matrix

| Layer | Accessible To Users? | Permitted Types & Return Shapes | Prohibited Types & Returns |
| :--- | :--- | :--- | :--- |
| **Public User Interface** | **Yes** | Value types (`Entity`, `TypeID`), typed references (`T&`, `const T&`), domain queries (`Query<T...>`), status codes. | Raw pointers (`Chunk*`, `Archetype*`), location records (`EntityRecord`), internal storage containers (`SlotMap&`, `ChunkPool&`). |
| **Inspection Facades** | **Yes (Read-Only)** | Numeric metrics (`size_t count()`), type lists (`ComponentMask`, `span<const TypeID>`), immutable views. | Mutable references to containers, raw chunk arrays, writable internal descriptors. |
| **Subsystem Plumbing** | **No** (Engine Only) | Routed via `internal::WorldAccess`: direct access to `WorldStorage`, `SlotMap`, `ArchetypeGraph`, `ChunkPool`. | Never exposed in public headers or documentation. |

---

## 4. Capabilities & Features Present in Modern Archetype Architectures (e.g. Bevy ECS) Not Yet Available in Arche-ECS

The following section identifies advanced architectural capabilities, state introspection mechanisms, and mutation patterns found in mature archetype engines that are currently absent from Arche-ECS:

### 4.1 Component Lifecycle Hooks & Observers
- **Description:** Automatic invocation of user-defined callbacks when specific component lifecycle transitions occur on an entity:
  - `OnAdd`: Triggered when an entity first gains a component.
  - `OnInsert`: Triggered when a component value is overwritten or updated.
  - `OnRemove`: Triggered immediately before a component is stripped from an entity.
  - `OnReplace`: Triggered when an existing component is swapped.
- **Current State in Arche-ECS:** Completely absent. Component addition and removal are raw memory migrations (`ComponentMigration::migrate`) without notification channels or observer registrations.

### 4.2 Dynamic / Untyped Entity & Component Manipulation
- **Description:** Ability to create entities, attach components, and inspect values at runtime using dynamic type descriptors (such as runtime component names, string IDs, or reflection tables) without requiring compile-time C++ template types.
- **Current State in Arche-ECS:** Strictly compile-time static types only (`type_id<T>()`). There is no mechanism to spawn an entity or add components dynamically from a script, data file, or network packet without pre-compiled C++ types.

### 4.3 Hybrid Storage Strategy (Archetypes + Sparse Sets)
- **Description:** Ability to designate storage strategies on a per-component basis:
  - **Table / Chunk Storage:** For densely packed, frequently iterated components (current Arche-ECS model).
  - **Sparse-Set Storage:** For rarely present, highly volatile, or frequently toggled components (e.g., status effects, flags like `Selected`, `Stunned`). In table storage, toggling these flags triggers continuous archetype migration and chunk fragmentation ("archetype explosion"). Sparse-set storage keeps the entity in its base archetype and tracks the flag out-of-band.
- **Current State in Arche-ECS:** 100% monolithic archetype chunk storage. Toggling any component forces an immediate physical memory copy across archetypes.

### 4.4 First-Class Entity Relationships & Hierarchies
- **Description:** Built-in semantic relations between entities, such as `ChildOf(parent)`, `Target(entity)`, or `Owner(entity)`:
  - Recursive cascading despawns (despawning a parent automatically clears all children).
  - Relationship-aware queries (e.g., query all `ChildOf(P)` where `P` matches a filter).
  - Structural propagation (transform trees).
- **Current State in Arche-ECS:** Zero relational context. Entities are completely flat and isolated. Relationships must be manually maintained through user-level ID components without engine cleanup guarantees.

### 4.5 Fine-Grained Query Change Detection (`Added<T>`, `Changed<T>`)
- **Description:** In queries, differentiating precisely between entities that were freshly spawned (`Added<T>`), entities whose component data was modified during the current or previous tick (`Changed<T>`), and entities whose structure changed.
- **Current State in Arche-ECS:** Coarse tick versioning only. Version numbers are tracked per chunk column (`chunk->set_column_version(...)`). If one entity in a 64 KB chunk is mutated, the entire chunk column is marked dirty, causing queries to evaluate all entities in that chunk. Individual per-entity mutation tracking is not supported.

### 4.6 Automatic Concurrency Ambiguity Detection
- **Description:** Static or startup-time validation of all registered systems in a schedule to detect conflicting memory accesses (e.g., System A writes `Position` while System B reads `Position` in the same parallel stage without an explicit execution order barrier).
- **Current State in Arche-ECS:** Stage-based execution requires manual placement of systems into stages or explicit dependency graphs. There is no automated analysis of system access masks to warn or throw on un-synchronized concurrent data races.

### 4.7 Partial World Splitting / Sub-World Borrows
- **Description:** Ability to split `World` access into safe, disjoint subsets (e.g. `WorldCell` or parameter fetchers), allowing multiple concurrent threads or systems to safely obtain disjoint mutable views of different components simultaneously.
- **Current State in Arche-ECS:** World access during deferred mutation requires exclusive locks or command buffer recording. Concurrent direct mutation across threads is prohibited without a global command buffer playback barrier.

### 4.8 Param-Based Dependency Injection for Systems
- **Description:** Systems defined as arbitrary callable functions where parameters declare their own resource or query needs (e.g., `void movement(Query<Position&, const Velocity&>, Res<Time>)`), automatically resolved and bound by the scheduler.
- **Current State in Arche-ECS:** Systems must conform to a fixed signature (`SystemContext` or explicit loops) and manually extract resources and queries from the world.
