# Arche-ECS Function Audit & Deprecation Report

This document cataloging every function and method across `arche-ecs`. It is formatted as an interactive review sheet for you to mark which functions you believe are unneeded, redundant, or should be removed.

Once you fill in your choices in the **Your Decision** column, I will pass through this document, verify the dependency graph for every marked function, and execute the removals/cleanups safely without breaking tests or library invariants.

---

## How to Use This Report

In the **Your Decision** column for each function, you can mark your choice by placing an `X` in the brackets:
- `[x] Keep`: Essential function, keep as-is.
- `[x] Remove`: Unneeded or redundant; schedule for deletion.
- `[x] Deprecate`: Keep temporarily with `[[deprecated]]` for backwards compatibility, then remove.
- `[x] Refactor / Internalize`: Keep the logic, but demote from Public API to `private` or move to a Plugin.

Feel free to also write comments directly next to the checkboxes (e.g. `[x] Remove - replaced by Scheduler`).

---

## 1. `arche::World` (`include/arche/world.hpp`)

`World` is the central simulation container managing archetypes, chunk paging, entity lifecycle, and scheduler execution.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Entity spawn(Comps&&... components)` | **Public** | Allocates an entity ID in `SlotMap`, finds/creates the matching archetype, and emplaces components in a chunk row. | **Core API**. Essential for single-entity creation. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `void spawn_batch(size_t count, Comps&&... components)` | **Public** | High-throughput batch spawner allocating contiguous entity blocks with identical archetype data. | **Stage 1 Fastpath**. Critical for high-scale benchmarks (100k–2M entities). | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `void spawn_batch(size_t count, ColumnWriterFn&& writer)` | **Public** | Populates columns directly via user callback per chunk block. | **Advanced Fastpath**. Used for procedural bulk generation. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `bool despawn(Entity entity)` | **Public** | Destroys an entity using swap-and-pop within its chunk, updates SlotMap, and bumps generation. | **Core API**. Essential for single-entity deletion. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `void despawn_batch(std::span<const Entity> entities)` | **Public** | Batched entity destruction with localized slot updates. | **Performance Fastpath**. Reduces SlotMap overhead during bulk despawn. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `bool has<T>(Entity entity) const noexcept` | **Public** | O(1) check if an alive entity contains component `T`. | **Core API**. Essential entity inspection. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `T& get<T>(Entity entity)` | **Public** | Returns mutable reference to component `T` for an alive entity. | **Core API**. Random access by entity handle. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `const T& get<T>(Entity entity) const` | **Public** | Returns const reference to component `T` for an alive entity. | **Core API**. Const random access. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `void add<T>(Entity entity, T&& component)` | **Public** | Moves entity to a new archetype with `+T`, copying old columns. | **Structural Mutation**. Essential dynamic archetype migration. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `void remove<T>(Entity entity)` | **Public** | Moves entity to a new archetype with `-T`. | **Structural Mutation**. Essential dynamic archetype migration. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query<Components...> query(FilterArgs&&... filters)` | **Public** | Creates a query iterator matching archetypes against component types and mutation filters (`With`, `Without`, `Mutated`). | **Core API**. Primary method for system/frame iteration. | `[ ] Keep`<br>`[ ] Remove`<br>`[?] Refactor` |
| `void run_systems(ThreadPool& pool)`<br>`void run_systems()` | **Public** | Advances the world frame sequence version and executes all registered systems across batches. | **Scheduler Driver**. The single authoritative driver of frame loops and versioning. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `uint32_t version() const noexcept` | **Public** | Returns current monotonic sequence version. | **Core API**. Read-only sequence version accessor. | `[x] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `uint32_t advance_version() noexcept` | **Internal** | Increments world sequence version counter `current_version_` (skipping 0). | **Internal Engine**. Private to World; invoked automatically by `Scheduler::run()` at the start of each frame. | `[x] Internalized` |
| ~~`void mark_dirty<T>(Chunk* chunk)`~~<br>~~`void mark_dirty<T>(Entity entity)`~~ | ~~**Public**~~ | ~~Manually stamps chunk column version with `current_version_`.~~ | **REMOVED**. Completely eliminated; declarative access descriptors (`Access().writes<T>()`) auto-stamp at batch barriers. | `[x] Removed` |
| ~~`void enable_double_buffer<T>()`~~ | ~~**Public**~~ | ~~Allocates second column storage for component `T` across all matching archetypes.~~ | **REMOVED**. Replaced by stage-based DAG scheduler concurrency (Pattern B), restoring 100% cache density. | `[x] Removed` |
| ~~`void flip<T>() noexcept`~~ | ~~**Public**~~ | ~~O(1) pointer swap between front and back buffers for component `T` across all chunks.~~ | **REMOVED**. Eliminated along with dual-slice chunks. Atomic batch barriers provide hazard-free synchronization. | `[x] Removed` |
| `void sort<Component, Filters...>(Compare&& comp)`<br>`void sort_by_key<Component, Filters...>(KeyExtractor&& key_fn)` | **Public** | In-place spatial sorting of entities across all matching archetypes (Morton curves). Supports general comparators and linear $O(N)$ Radix sort. | **Physics Optimization**. Cache-locality clustering for broadphase/narrowphase collision detection. | `[x] Keep` |
| `void permute(Archetype* arch, span<const uint32_t> target_indices)` | **Internal** | Reorders all chunk rows according to precomputed index permutation via cycle chasing. Located in `src/search/sort.cpp`. | **Internal Engine**. Private to World; invoked by `sort` and `sort_by_key`. | `[x] Internalized` |
| ~~`std::vector<ComponentSpan<T>> view_chunks<T>()`~~ | ~~**Public**~~ | ~~Returns non-owning contiguous spans of component `T` memory across all active chunks.~~ | **REFACTORED**. Moved to `world.gpu.view_chunks<T>()`. | `[x] Moved to world.gpu` |
| ~~`size_t stream_component_to<T>(void* destination_buffer)`~~ | ~~**Public**~~ | ~~Direct linear memcpy of all instances of `T` into contiguous destination buffer.~~ | **REFACTORED**. Moved to `world.gpu.stream_to<T>(buf)`. | `[x] Moved to world.gpu` |
| ~~`size_t save_snapshot(void* destination_buffer, size_t max_bytes)`~~ | ~~**Public**~~ | ~~Serializes entire world state (SlotMap, chunks, components) into contiguous 64-byte aligned binary blob.~~ | **REFACTORED**. Moved to `world.snapshots.save(buf, max_bytes)` and `world.snapshots.save_to_file(path)`. | `[x] Moved to world.snapshots` |
| ~~`void load_snapshot(const void* snapshot_buffer, size_t bytes)`~~ | ~~**Public**~~ | ~~Deserializes world state from snapshot blob, restoring entity generations and chunk data.~~ | **REFACTORED**. Moved to `world.snapshots.load(buf, bytes)` and `world.snapshots.load_from_file(path)`. | `[x] Moved to world.snapshots` |
| ~~`size_t snapshot_byte_size() const noexcept`~~ | ~~**Public**~~ | ~~Calculates exact required memory size for world snapshot.~~ | **REFACTORED**. Moved to `world.snapshots.size_bytes()`. | `[x] Moved to world.snapshots` |
| `void clear()` | **Public** | Recycles all chunks, resets SlotMap, clears query caches. | **Core API**. World reset. | `[x] Keep` |
| ~~`size_t count_components<T>() const noexcept`~~ | ~~**Public**~~ | ~~Computes total active living instances of component `T`.~~ | **REFACTORED**. Moved to `world.archetypes.count_with<T>()`. | `[x] Moved to world.archetypes` |
| `size_t entity_count() const noexcept` | **Public** | Returns total living entities across all archetypes. | **Core API**. Entity count. | `[x] Keep` |
| ~~`size_t archetype_count() const noexcept`~~ | ~~**Public**~~ | ~~Returns total instantiated archetypes in the graph.~~ | **REFACTORED**. Moved to `world.archetypes.count()`. | `[x] Moved to world.archetypes` |
| `size_t active_chunk_count() const noexcept` | **Public** | Returns total active 64 KB chunk pages in use. | **Diagnostics**. Memory footprint telemetry. | `[x] Keep` |
| ~~`T& emplace_resource<T>(Args&&... args)`~~<br>~~`T& insert_resource<T>(Args&&... args)`~~ | ~~**Public**~~ | ~~Constructs a singleton resource in-place.~~ | **REFACTORED**. Consolidated into `world.resources.insert<T>(args...)`. | `[x] Moved to world.resources` |
| ~~`T& resource<T>()`~~<br>~~`const T& resource<T>() const`~~ | ~~**Public**~~ | ~~Returns reference to singleton resource `T`.~~ | **REFACTORED**. Moved to `world.resources.get<T>()`. | `[x] Moved to world.resources` |
| ~~`bool has_resource<T>() const noexcept`~~<br>~~`bool has_resource(ResourceID id) const noexcept`~~ | ~~**Public**~~ | ~~Checks if resource `T` or resource ID is registered.~~ | **REFACTORED**. Moved to `world.resources.has<T>()` and `world.resources.has(id)`. | `[x] Moved to world.resources` |
| ~~`bool remove_resource<T>()`~~ | ~~**Public**~~ | ~~Removes singleton resource `T` if present.~~ | **REFACTORED**. Moved to `world.resources.remove<T>()`. | `[x] Moved to world.resources` |
| `World& add_plugin<P>()` | **Public** | Default-constructs plugin `P` and calls `plugin.build(*this, scheduler_)`. | **Plugin Architecture**. Essential declarative setup. | `[x] Keep` |
| `World& add_plugin<P>(P&& plugin)` | **Public** | Installs custom plugin instance into internal scheduler. | **Plugin Architecture**. Essential. | `[x] Keep` |
| `World& add_plugin<P>(Scheduler& ext_scheduler)` | **Public** | Installs plugin into an external scheduler instance. | **Plugin Architecture**. External scheduler support. | `[x] Keep` |
| `World& add_plugin<P>(P&& plugin, Scheduler& ext_scheduler)` | **Public** | Installs custom plugin instance into an external scheduler. | **Plugin Architecture**. External scheduler support. | `[x] Keep` |
| ~~`World& install_plugin(...)`~~ | ~~**Public**~~ | ~~Direct alias for `add_plugin`.~~ | **REMOVED**. Standardized on `add_plugin`. | `[x] Removed` |
| `void run_systems()` | **Public** | Runs internal scheduler systems synchronously or on default pool. | **Scheduler Facade**. Frame execution. | `[x] Keep` |
| `void run_systems(ThreadPool& pool)` | **Public** | Runs internal scheduler systems across worker threads. | **Scheduler Facade**. Primary multithreaded entry point. | `[x] Keep` |
| `void run_frame(ThreadPool& pool)`<br>`void run_frame()` | **Public** | Convenience alias for `run_systems`. | **Ergonomics**. Standard game-loop naming. | `[x] Keep` |
| `void commit(CommandBuffer& cmd_buffer)` | **Public** | Replays deferred mutations from a single command buffer into World. | **Command Buffer API**. Essential for deferred mutations. | `[x] Keep` |
| `void commit(ParallelCommandBuffer& cmd_buffers)` | **Public** | Replays and clears thread-isolated parallel command buffers. | **Command Buffer API**. Batch barrier playback. | `[x] Keep` | |
| `const EntityRecord& get_alive_record(Entity entity) const` | **Private** | Resolves entity record and asserts entity is alive. | **Internal Helper**. Essential engine primitive. | `[ ] Keep (Internal)` |
| `pair<Chunk*, uint32_t> allocate_slot(Archetype* arch, Entity entity)` | **Private** | Allocates row in active chunk or acquires a new chunk from pool. | **Internal Helper**. Essential engine primitive. | `[ ] Keep (Internal)` |
| `Chunk* acquire_chunk_for(Archetype* arch)` | **Private** | Allocates or acquires buffer from pool for archetype. | **Internal Helper**. Essential engine primitive. | `[ ] Keep (Internal)` |
| `void recycle_chunk(Archetype* arch, Chunk* chunk)` | **Private** | Returns empty chunk to chunk pool. | **Internal Helper**. Essential engine primitive. | `[ ] Keep (Internal)` |
| `void migrate_entity(Entity entity, Archetype* src, Archetype* dst, const void* add_data, TypeID add_tid)` | **Private** | Column-copy and row-swap logic for `add`/`remove` component. | **Internal Helper**. Core structural migration engine. | `[ ] Keep (Internal)` |
| `void dispatch_parallel_chunks(ThreadPool& pool, WorkerFn&& worker)` | **Private** | Atomic work-stealing chunk distribution loop. | **Internal Helper**. Core parallel chunk engine backing `ctx.query_parallel`. | `[ ] Keep (Internal)` |
| `void apply_command_create_entity(...)` | **Private** | Replays deferred spawn command from `CommandBuffer`. | **Internal Helper**. Friend playback primitive. | `[ ] Keep (Internal)` |
| `void apply_command_add_component(...)` | **Private** | Replays deferred component addition from `CommandBuffer`. | **Internal Helper**. Friend playback primitive. | `[ ] Keep (Internal)` |
| `void apply_command_remove_component(...)` | **Private** | Replays deferred component removal from `CommandBuffer`. | **Internal Helper**. Friend playback primitive. | `[ ] Keep (Internal)` |

---

### 1.1 `arche::World` Sub-Facades (`include/arche/world_facades.hpp`, `src/world_facades.cpp`)

To prevent `World` from becoming a monolithic "God Object", domain-specific capabilities are modularized into zero-indirection sub-facades accessible via property syntax (`world.<facade>.<method>`):

| Facade | Method Signature | Scope | Description & Role | Status |
| :--- | :--- | :--- | :--- | :--- |
| `world.snapshots` | `size_t save(void* buf, size_t max_bytes)` | **Public** | Serializes full world memory state (SlotMap + chunks) into memory buffer. | `[x] Active` |
| `world.snapshots` | `void load(const void* buf, size_t bytes)` | **Public** | Deserializes and restores world state from snapshot buffer in < 1 ms. | `[x] Active` |
| `world.snapshots` | `bool save_to_file(std::string_view path)` | **Public** | Serializes world directly into a binary snapshot file on disk. | `[x] Active` |
| `world.snapshots` | `bool load_from_file(std::string_view path)` | **Public** | Deserializes world directly from a binary snapshot file on disk. | `[x] Active` |
| `world.snapshots` | `size_t size_bytes() const noexcept` | **Public** | Calculates exact required memory size for world snapshot. | `[x] Active` |
| `world.snapshots` | `SnapshotHeader header() const noexcept` | **Public** | Generates the 64-byte snapshot header for current world state. | `[x] Active` |
| `world.resources` | `T& insert<T>(Args&&... args)` | **Public** | Constructs a singleton resource in-place. | `[x] Active` |
| `world.resources` | `T& get<T>()` / `const T& get<T>() const` | **Public** | Returns mutable or const reference to singleton resource `T`. | `[x] Active` |
| `world.resources` | `bool has<T>() const noexcept` | **Public** | Checks if resource `T` is registered. | `[x] Active` |
| `world.resources` | `bool has(ResourceID id) const noexcept` | **Public** | Runtime check used by Scheduler during system access validation. | `[x] Active` |
| `world.resources` | `bool remove<T>()` | **Public** | Removes singleton resource `T` if present. | `[x] Active` |
| `world.resources` | `ResourceContainer& container() noexcept` | **Public** | Accesses the underlying flat `ResourceContainer`. | `[x] Active` |
| `world.archetypes`| `size_t count() const noexcept` | **Public** | Returns total instantiated archetypes in the graph. | `[x] Active` |
| `world.archetypes`| `std::vector<Chunk*>& chunks(Archetype* arch)` | **Public** | Retrieves active chunk list for an archetype. | `[x] Active` |
| `world.archetypes`| `uint32_t count_with<T>() const noexcept` | **Public** | Computes total active living instances of component `T`. | `[x] Active` |
| `world.gpu` | `std::vector<ComponentSpan<T>> view_chunks<T>()` | **Public** | Returns non-owning contiguous spans of component `T` across active chunks. | `[x] Active` |
| `world.gpu` | `size_t stream_to<T>(void* destination_buffer)` | **Public** | Direct linear memcpy of all instances of `T` into contiguous destination buffer. | `[x] Active` |

---

## 2. `arche::Scheduler` & `arche::SystemContext` (`include/arche/scheduler.hpp`, `src/scheduler.cpp`)

The dependency analysis DAG engine that compiles access descriptors into conflict-free parallel batches.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `SystemContext::query_parallel<Comps...>(Func&& func)` | **Public** | Chunk-level work stealing across worker threads with isolated `CommandBuffer` per thread; single-threaded fallback when no pool exists. | **Unified Concurrency Bridge**. Primary way to parallelize heavy systems. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `SystemContext::parallel_for(Func&& func)` | **Public** | Dispatches arbitrary indexed loop across thread pool worker threads. | **Generic Concurrency**. Helpful for non-chunk parallel tasks. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::add_system(name, scale, access, fn)` | **Public** | Registers a system with explicit computational scale (`Wide` or `Narrow`) and access descriptor. | **Core Scheduler API**. Primary registration method. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::add_system(name, access, fn, scale = Narrow)` | **Public** | Overload with default `Narrow` scale. | **Ergonomics**. Allows concise registration of standard systems. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::add_system(name, fn)` | **Public** | Overload with empty access descriptor (assumes pure sequential or access-agnostic). | **Ergonomics**. Helpful for quick tests or debug systems. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::compile(World& world)` | **Public** | Compiles systems into execution batches, validates resource existence, partitions DAG. | **Core Scheduler API**. Essential before execution. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::run(World& world, ThreadPool& pool, float dt)` | **Public** | Executes compiled batches across worker threads with barriers between conflicting batches. | **Core Scheduler API**. Frame runner. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::validate_system_resources(World& world)` | **Internal** | Checks that all read/write resources declared by registered systems exist in `world`. | **Safety Validation**. Catches missing resources at init time. | `[ ] Keep (Internal)` |
| `Scheduler::partition_batches()` | **Internal** | Greedy level-partitioning algorithm grouping conflict-free systems into batches. | **Scheduler Core**. Batches non-conflicting systems. | `[ ] Keep (Internal)` |
| `Scheduler::execute_batch(...)` | **Internal** | Dispatches a batch (Wide system exclusive or Narrow systems parallel) and commits deferred mutations. | **Scheduler Core**. Thread execution and batch barrier. | `[ ] Keep (Internal)` |
| `Scheduler::batch_count() const` | **Public** | Returns number of compiled execution batches. | **Diagnostics**. Test validation and profiling. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::system_count() const` | **Public** | Returns total registered systems. | **Diagnostics**. Inspection. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::is_dirty() const` | **Public** | Returns true if systems were added since last compilation. | **State Inspection**. Dirty compilation tracker. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Scheduler::batches() const` | **Public** | Returns read-only reference to compiled batches vector. | **Diagnostics / Profiling**. Allows profiler inspection. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 3. `arche::CommandBuffer` & `arche::ParallelCommandBuffer` (`include/arche/command_buffer.hpp`, `src/command_buffer.cpp`)

Thread-local command recording buffers for deferred structural mutations.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `CommandBuffer::spawn<Comps...>(Comps&&... components)` | **Public** | Encodes deferred entity creation command with type-erased unpacker lambda into local byte stream. | **Core Command API**. Thread-safe deferred spawn. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::despawn(Entity entity)` | **Public** | Records deferred entity destruction command. | **Core Command API**. Thread-safe deferred despawn. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::add_component<T>(Entity entity, T&& component)` | **Public** | Records deferred addition of component `T` to `entity`. | **Core Command API**. Thread-safe component addition. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::remove_component<T>(Entity entity)` | **Public** | Records deferred removal of component `T` from `entity`. | **Core Command API**. Thread-safe component removal. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::commit(World& world)` | **Public** | Iterates byte buffer and executes all deferred commands sequentially against `World`. | **Core Command API**. Playback engine. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::clear()` | **Public** | Resets buffer write head to 0 without reallocating underlying heap memory. | **Performance**. Zero-allocation reuse per frame. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::empty() const` | **Public** | Returns true if no commands are currently recorded. | **Inspection**. Fast check before playback. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `CommandBuffer::size() const` | **Public** | Returns total raw payload bytes recorded. | **Diagnostics**. Profiling and capacity tuning. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ParallelCommandBuffer::get(size_t thread_id)` | **Public** | Returns thread-isolated `CommandBuffer&` for worker `thread_id`. | **Lock-Free Concurrency**. Zero-contention command recording. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ParallelCommandBuffer::commit(World& world)` | **Public** | Sequentially replays thread 0..N buffers into `world`. | **Deterministic Barrier**. Guarantees reproducible playback. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ParallelCommandBuffer::clear()` | **Public** | Clears all thread-local command buffers. | **Frame Reset**. Prepares buffers for next batch/frame. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ParallelCommandBuffer::capacity() const` | **Public** | Returns number of thread buffers allocated. | **Inspection**. Matches worker count. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 4. `arche::Query` & `arche::QueryIterator` (`include/arche/query.hpp`, `include/arche/detail/query_iterator.hpp`)

Direct archetype cache matching and contiguous column pointer resolution.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Query::begin()` / `Query::end()` | **Public** | Standard C++20 input iterator range enabling `for (auto [pos, vel] : world.query<...>())`. | **Core API**. Idiomatic modern C++ query syntax. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::with<Components...>()` | **Public** | Adds filter requiring matching archetypes to have specified components without binding pointers. | **Query Filter**. Useful for tag filtering. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::without<Components...>()` | **Public** | Adds filter excluding archetypes possessing specified components. | **Query Filter**. Negative matching (e.g. `without<Disabled>`). | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::changed<Components...>(uint32_t since_tick)` | **Public** | Filters chunks whose column modification tick exceeds `since_tick`. | **Dirty Tracking**. Bypasses unchanged chunks in O(1). | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::for_each_chunk(Func&& func)` | **Public** | Traverses chunks, unpacking raw column pointers `(uint32_t count, Comps*...)` directly to user callback. | **SIMD Fastpath**. Unlocks compiler auto-vectorization across chunks. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::for_each_chunk_padded(Func&& func)` | **Public** | Passes both active count and 16-element SIMD rounded-up count. | **Explicit SIMD**. Padded buffer iteration for AVX/NEON vectorization. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Query::for_each_active_chunk(Visitor&& visitor)` | **Internal** | Traverses non-empty chunks with prefetching; used by engine chunk dispatchers. | **Internal Primitive**. Backing chunk traversal for `dispatch_parallel_chunks`. | `[ ] Keep (Internal)` |
| `Query::resolve_column_offset<T>(Archetype* arch)` | **Internal** | Pre-resolves byte offset of column `T` within archetype chunk layout. | **Internal Optimization**. Avoids redundant column lookups inside loops. | `[ ] Keep (Internal)` |
| `Query::get_column_ptr<T>(Chunk* chunk, uint32_t offset)` | **Internal** | Resolves raw pointer to typed column within chunk page. | **Internal Optimization**. Direct pointer arithmetic. | `[ ] Keep (Internal)` |
| `QueryIterator::operator*()` | **Public** | Unpacks structured tuple of component references for current row. | **Iterator Primitive**. Dereference. | `[ ] Keep (Internal)` |
| `QueryIterator::operator++()` | **Public** | Advances row index; jumps to next chunk or archetype when chunk ends. | **Iterator Primitive**. Step advance. | `[ ] Keep (Internal)` |

---

## 5. `arche::AccessBuilder` & `arche::Access` (`include/arche/access.hpp`)

128-bit SIMD bitmask algebra for compile-time and runtime declarative system conflict detection.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Access::reads<Components...>()` | **Public** | Sets read bitmask for specified component types. | **Declarative Scheduler API**. RAR conflict analysis. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Access::writes<Components...>()` | **Public** | Sets write bitmask for specified component types. | **Declarative Scheduler API**. RAW/WAR/WAW conflict analysis. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Access::reads_resource<Resources...>()` | **Public** | Sets read bitmask for specified resource types. | **Declarative Scheduler API**. Resource sharing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Access::writes_resource<Resources...>()` | **Public** | Sets write bitmask for specified resource types. | **Declarative Scheduler API**. Resource exclusion. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Access::build() const noexcept` | **Public** | Finalizes and returns immutable `AccessDescriptor` bitmask. | **Declarative Scheduler API**. Build step. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `AccessDescriptor::conflicts_with(const AccessDescriptor& other)` | **Public** | Evaluates RAW, WAR, and WAW conflicts across components and resources in parallel via bitwise operations. | **Core Concurrency Math**. Determines batch compatibility in O(1). | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 6. `arche::Plugin` (`include/arche/plugin.hpp`)

Compile-time concept for modular engine extensions.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Plugin<T>` (C++20 Concept) | **Public** | Requires `plugin.name() -> std::string_view` and `plugin.build(World&, Scheduler&) -> void`. | **Zero-Virtual Plugin Interface**. Guaranteed zero vtable overhead. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `World::has_plugin<P>()` | **Public** | Checks if plugin type `P` has been registered. | **Plugin Introspection**. Prevents duplicate installation. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `World::installed_plugins()` | **Public** | Returns list of installed plugin string names. | **Diagnostics / Profiling**. Extension tracking. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 7. `arche::ResourceContainer` (`include/arche/resource.hpp`)

Flat type-erased singleton storage for global simulation state.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `emplace<T>(Args&&... args)` | **Public** | Constructs singleton resource `T` in-place, assigning unique sequential `ResourceID`. | **Core Resource API**. Thread-safe setup. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `get<T>()` | **Public** | Returns mutable reference to resource `T`. | **Core Resource API**. Unchecked fastpath. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `has<T>() const` | **Public** | Checks if resource `T` is registered. | **Core Resource API**. Presence query. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `has(ResourceID id) const` | **Public** | Fast runtime ID presence check used by scheduler validation. | **Scheduler Runtime Validator**. Essential. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `remove<T>()` | **Public** | Destroys resource `T` and frees its storage. | **Core Resource API**. Cleanup. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `clear()` | **Public** | Clears all registered resources. | **Cleanup API**. World reset. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 8. `arche::Archetype` (`include/arche/archetype.hpp`)

Describes unique component combinations and precomputes chunk column layout offsets.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `has_component(TypeID tid) const` | **Public** | Tests component existence in archetype in O(1). | **Core Archetype Primitive**. Mask check. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `column_index(TypeID tid) const` | **Public** | Returns dense column index for component `tid` or `-1`. | **Core Archetype Primitive**. Column lookup. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `column_layout(size_t col) const` | **Public** | Returns layout metadata (size, alignment, byte offset) for column. | **Core Archetype Primitive**. Memory layout. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `column_layout_for(TypeID tid) const` | **Public** | Returns layout metadata pointer for component `tid`. | **Core Archetype Primitive**. Memory layout. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `component_mask() const` | **Public** | Returns 128-bit `ComponentMask` bitset for archetype. | **Core Archetype Primitive**. Fast bitset filtering. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `chunk_capacity() const` | **Public** | Precomputed max entities that fit inside one 64 KB chunk for this layout. | **Paging Math**. Critical for chunk allocation. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `entity_count() const` | **Public** | Current total living entities belonging to this archetype. | **Inspection**. Archetype population. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 9. `arche::Chunk` & `ChunkHeader` (`include/arche/chunk.hpp`, `src/chunk.cpp`)

64 KB aligned contiguous memory pages storing entity arrays and columnar data.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Chunk::create(...)` | **Internal** | Placement-new constructs a chunk header inside raw 64 KB buffer page. | **Paging Core**. Chunk allocation. | `[ ] Keep (Internal)` |
| `Chunk::header()` | **Public** | Access to 64-byte aligned chunk header metadata. | **Chunk Inspection**. Metadata access. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::count() const` | **Public** | Current number of active rows/entities in this chunk page. | **Core Paging Primitive**. Iteration bound. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::capacity() const` | **Public** | Max entities this chunk can hold. | **Core Paging Primitive**. Capacity bound. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::is_full() const` | **Public** | Returns `count() == capacity()`. | **Core Paging Primitive**. Allocation trigger. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::is_empty() const` | **Public** | Returns `count() == 0`. | **Core Paging Primitive**. Recycling trigger. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::entity(uint32_t row) const` | **Public** | Returns `Entity` handle stored at chunk row. | **Core Paging Primitive**. Entity identity. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::typed_slice<T>(uint32_t offset)` | **Public** | Resolves direct contiguous typed pointer to column `T`. | **Zero-Copy Access**. Essential for SIMD and memory views. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::column_version(uint16_t col) const` | **Public** | Returns last modified simulation tick for column. | **Dirty Tracking**. Bypasses unchanged chunks in O(1). | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::set_column_version(uint16_t col, uint32_t tick)` | **Public** | Updates column modification tick stamp. | **Dirty Tracking**. Update tick. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Chunk::user_metadata<T>()` | **Public** | Provides 16 bytes of custom chunk-level metadata (e.g. Macro AABB for spatial culling). | **Spatial / Culling Optimization**. Chunk-level hierarchical pruning. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 10. `arche::detail::SlotMap` (`include/arche/detail/slot_map.hpp`)

Generational sparse-to-dense entity lookup table.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `allocate()` | **Internal** | Pops index from free-list or grows slot array; returns new `Entity`. | **Entity Core**. Entity ID dispenser. | `[ ] Keep (Internal)` |
| `deallocate(Entity entity)` | **Internal** | Bumps generation, inserts index into intrusive free list. | **Entity Core**. Prevents ABA problems. | `[ ] Keep (Internal)` |
| `is_alive(Entity entity) const` | **Internal** | Validates generation match in O(1). | **Entity Core**. Liveness test. | `[ ] Keep (Internal)` |
| `find_record(Entity entity)` | **Internal** | Returns pointer to `EntityRecord` containing archetype, chunk, and row. | **Entity Core**. Primary O(1) entity locator. | `[ ] Keep (Internal)` |
| `update_location(Entity entity, Archetype*, Chunk*, uint32_t row)` | **Internal** | Updates location after chunk swap-and-pop or sort. | **Entity Core**. Location maintenance. | `[ ] Keep (Internal)` |
| `snapshot_byte_size() const` | **Internal** | Calculates size of dense slot array for serialization. | **Module 017 Core**. Snapshotting. | `[ ] Keep (Internal)` |
| `serialize(void* dst, size_t max)` | **Internal** | Serializes slot array directly to buffer. | **Module 017 Core**. Snapshotting. | `[ ] Keep (Internal)` |
| `deserialize(const void* src, size_t bytes)` | **Internal** | Restores slot array from snapshot buffer. | **Module 017 Core**. Rollback. | `[ ] Keep (Internal)` |

---

## 11. `arche::detail::ArchetypeGraph` (`include/arche/detail/archetype_graph.hpp`)

Graph structure caching archetypes and component add/remove transition edges.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `find_or_create(const TypeIDSet& types)` | **Internal** | Finds existing archetype matching type set or instantiates new one. | **Archetype Core**. Deduplication. | `[ ] Keep (Internal)` |
| `transition_add(Archetype* src, TypeID tid)` | **Internal** | Traverses or caches graph edge for `+tid`. | **Archetype Core**. O(1) structural mutation. | `[ ] Keep (Internal)` |
| `transition_remove(Archetype* src, TypeID tid)` | **Internal** | Traverses or caches graph edge for `-tid`. | **Archetype Core**. O(1) structural mutation. | `[ ] Keep (Internal)` |
| `archetypes() const` | **Public** | Returns read-only range over all instantiated archetypes. | **Query / View Primitive**. Archetype traversal. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `clear()` | **Internal** | Clears all archetypes in graph. | **Cleanup API**. World reset. | `[ ] Keep (Internal)` |

---

## 12. `arche::detail::ChunkPool` (`include/arche/detail/chunk_pool.hpp`)

Recycle pool for 64 KB memory pages to eliminate page faults and OS `mmap`/`malloc` calls.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `acquire_buffer()` | **Internal** | Pops 64 KB buffer from free stack or allocates new page. | **Memory Core**. Page reuse. | `[ ] Keep (Internal)` |
| `release_buffer(void* buffer)` | **Internal** | Returns 64 KB buffer to free stack for future reuse. | **Memory Core**. Zero heap churn. | `[ ] Keep (Internal)` |
| `allocated_count() const` | **Internal** | Total buffers currently allocated by pool. | **Telemetry**. Memory tracking. | `[ ] Keep (Internal)` |
| `free_count() const` | **Internal** | Total idle recycled buffers ready for reuse. | **Telemetry**. Cache pooling tracking. | `[ ] Keep (Internal)` |
| `clear()` | **Internal** | Deallocates all pooled buffers back to OS. | **Cleanup API**. Shutdown. | `[ ] Keep (Internal)` |

---

## 13. `arche::detail::QueryCache` (`include/arche/detail/query_cache.hpp`)

Caches matching archetypes per unique query component signature.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `update(Archetype* arch)` | **Internal** | Tests newly instantiated archetype against cached query mask. | **Query Core**. Incremental cache maintenance. | `[ ] Keep (Internal)` |
| `clear()` | **Internal** | Clears cached archetype pointers. | **Cleanup API**. Query reset. | `[ ] Keep (Internal)` |

---

## 14. `arche::TypeRegistry` & `arche::TypeID` (`include/arche/type_registry.hpp`)

Fast compile-time sequential type ID generation without RTTI.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `type_id<T>()` | **Public** | Assigns and returns unique sequential `TypeID` (0..127) for component `T` in O(1). | **Hard Invariant Compliant**. Zero RTTI, `-fno-rtti` compliant. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `name_of<T>()` | **Public** | Returns demangled or pretty type name string view. | **Diagnostics / Profiling**. Logging and errors. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 15. `arche::Entity` (`include/arche/entity.hpp`)

Generational 64-bit entity handle (`32-bit ID + 32-bit Generation`).

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `Entity::id() const` | **Public** | Returns 32-bit slot index. | **Core Entity Primitive**. Indexing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Entity::generation() const` | **Public** | Returns 32-bit generational counter. | **Core Entity Primitive**. Stale handle detection. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Entity::is_null() const` | **Public** | Checks if entity equals `null_entity()`. | **Core Entity Primitive**. Sentinel test. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `null_entity()` | **Public** | Returns global invalid entity constant. | **Core Entity Primitive**. Sentinel constant. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `make_entity(id, generation)` | **Internal** | Packs raw ID and generation into 64-bit handle. | **Core Entity Primitive**. Handle packing. | `[ ] Keep (Internal)` |

---

## 16. `arche::ComponentMask` (`include/arche/component_mask.hpp`)

128-bit bitset backed by SIMD intrinsics for component presence and conflict tests.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `set(size_t bit)` | **Public** | Sets bit in 128-bit mask. | **Core Bitmask Primitive**. Component registration. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `reset(size_t bit)` | **Public** | Clears bit in 128-bit mask. | **Core Bitmask Primitive**. Component clearing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `test(size_t bit) const` | **Public** | Tests if bit is set. | **Core Bitmask Primitive**. Presence test. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `contains(ComponentMask other) const` | **Public** | Checks if `(mask & other) == other` via SIMD. | **Query Matching**. Archetype filter matching. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `intersects(ComponentMask other) const` | **Public** | Checks if `(mask & other) != 0` via SIMD. | **Conflict Detection**. Scheduler RAW/WAR/WAW. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| Bitwise operators `&`, `\|`, `^`, `~` | **Public** | SIMD bitwise algebra over 128-bit masks. | **Bitmask Math**. SIMD hardware accelerated. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 17. `arche::ThreadPool` (`include/arche/thread_pool.hpp`)

Lightweight worker thread pool for intra-system chunk stealing and parallel system dispatch.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `ThreadPool(size_t worker_count)` | **Public** | Spawns worker threads pinned to task queue. | **Core Concurrency**. Thread life. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `parallel_for(Func&& func)` | **Public** | Dispatches work across all worker threads and blocks until all complete. | **Core Concurrency**. Fork-join parallel barrier. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `enqueue(Func&& func)` | **Public** | Enqueues single fire-and-forget task. | **Concurrency Primitive**. Asynchronous dispatch. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `worker_count() const` | **Public** | Returns number of worker threads. | **Inspection**. Worker count query. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 18. `arche::Hardware` (`include/arche/hardware.hpp`)

CPU hardware topology and runtime cache line detection.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `cache_line_size()` | **Public** | Queries hardware L1 cache line size (typically 64 or 128 bytes on Apple Silicon). | **Hardware Alignment**. Prevents false sharing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `cpu_concurrency()` | **Public** | Returns detected physical CPU core count. | **Hardware Topology**. Default thread pool sizing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `has_avx2()` / `has_neon()` | **Public** | Detects runtime SIMD instruction support. | **Hardware Acceleration**. Vectorization dispatch. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |

---

## 19. `arche::gpu_reflection` / `ComponentSpan` (`include/arche/gpu_reflection.hpp`)

Non-owning contiguous memory span wrappers and layout verification traits.

| Function / Method Signature | Scope | Description & Role | Architectural Assessment | Your Decision |
| :--- | :--- | :--- | :--- | :--- |
| `ComponentSpan::data()` | **Public** | Returns raw pointer to contiguous component array. | **Zero-Copy View**. Memory span. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ComponentSpan::count()` | **Public** | Returns number of elements in span. | **Zero-Copy View**. Element count. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ComponentSpan::size_bytes()` | **Public** | Returns byte size of span (`count * sizeof(T)`). | **Zero-Copy View**. Memory size. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ComponentSpan::operator[]` | **Public** | Subscript element access within span. | **Ergonomics**. Array indexing. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `ComponentSpan::as_span()` | **Public** | Converts to standard C++20 `std::span<const T>`. | **Standard Interop**. C++20 compatibility. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `Std430Traits<T>` | **Public** | Validates struct packing against GPU std430 rules (rejects unpadded 12-byte `vec3` hazards). | **Safety Traits**. Essential if doing GPU staging. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Refactor` |
| `DrawIndirectCommand` | **Public** | 16-byte POD struct matching `VkDrawIndirectCommand`. | **Consumer Staging Struct**. Archived in `RENDER_PLUGIN_SPECIFICATION.md`. Candidate for moving out to renderer plugin. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Move to Plugin` |
| `GpuBufferView` | **Public** | Type-erased pointer + count descriptor for graphics staging. | **Consumer Staging Struct**. Candidate for moving out to renderer plugin. | `[ ] Keep`<br>`[ ] Remove`<br>`[ ] Move to Plugin` |

---

## 20. Subsystem-Driven Architecture (12 First-Class Subsystems)

Resulting from the architectural reorganization of `arche-ecs` into 12 self-contained subsystems with dedicated `.inl` inlined hot-paths:

| Subsystem | Scope & Role | Headers & Inlined Templates | Compiled Implementation |
| :--- | :--- | :--- | :--- |
| **`entity/`** | Generational handles, coordinate records, slot mapping, single/bulk allocation. | `include/arche/entity/entity.hpp`<br>`include/arche/entity/entity_record.hpp`<br>`include/arche/entity/slot_map.hpp`<br>`include/arche/entity/entity_allocator.inl` | Handled via inline template fastpaths |
| **`component/`** | Zero-RTTI TypeIDs, 128-bit SIMD bitsets, column layouts, and typed component pointer resolution. | `include/arche/component/type_id.hpp`<br>`include/arche/component/component_mask.hpp`<br>`include/arche/component/column_layout.hpp`<br>`include/arche/component/component_access.inl` | Inlined pointer math |
| **`archetype/`** | Canonical component combinations, transition DAG (+/- component), row swap-and-pop. | `include/arche/archetype/archetype.hpp`<br>`include/arche/archetype/archetype_graph.hpp` | Transition graph edge traversal |
| **`chunk/`** | 64 KB cache-line aligned memory pages, buffer recycling pool, batch capacity filling & SoA streaming. | `include/arche/chunk/chunk.hpp`<br>`include/arche/chunk/chunk_pool.hpp`<br>`include/arche/chunk/chunk_distributor.inl` | `src/chunk/chunk.cpp` |
| **`query/`** | Multi-component views, SIMD chunk visitors, compile-time/runtime filters (`With`, `Without`, `Mutated`), and query caching. | `include/arche/query/query.hpp`<br>`include/arche/query/filter.hpp`<br>`include/arche/query/query_cache.hpp`<br>`include/arche/query/query_iterator.hpp` | QueryCache reactive updates |
| **`system/`** | Declarative read/write access descriptors, DAG compilation, system execution context, and plugin system. | `include/arche/system/access.hpp`<br>`include/arche/system/scheduler.hpp`<br>`include/arche/system/system_context.hpp`<br>`include/arche/system/plugin.hpp` | `src/system/scheduler.cpp` |
| **`concurrency/`** | Worker thread pool, atomic work-stealing parallel chunk dispatch loop. | `include/arche/concurrency/thread_pool.hpp`<br>`include/arche/concurrency/chunk_dispatcher.inl` | Fork-join barrier management |
| **`command/`** | Lock-free thread-local mutation recording buffer, thread-isolated buffers, deferred playback engine. | `include/arche/command/command_buffer.hpp` | `src/command/command_buffer.cpp` |
| **`resource/`** | Flat type-erased container for global singleton simulation state. | `include/arche/resource/resource.hpp` | Inlined flat container storage |
| **`persistence/`** | 16-byte aligned binary world state serialization, memory sizing, disk file persistence & rollbacks. | `include/arche/persistence/snapshot.hpp` | `src/world_facades.cpp` |
| **`spatial/`** | In-place spatial sorting, 2D/3D Morton curves, linear $O(N)$ radix sorting, and cycle chasing. | `include/arche/spatial/sort.hpp` | `src/spatial/sort.cpp` |
| **`hardware/`** | CPU topology, runtime cache line detection, SIMD intrinsics, std430 GPU alignment validation. | `include/arche/hardware/hardware.hpp`<br>`include/arche/hardware/gpu_layout.hpp`<br>`include/arche/hardware/math_utils.hpp` | CPU/SIMD feature detection |


