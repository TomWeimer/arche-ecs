# Architecture Violations & Remediation Specification

This document catalogues all 20 architectural violations detected across the library's layered subsystem architecture, explains why each violates architectural boundaries, and provides concrete, step-by-step code remediations.

---

## 1. Architectural Layers Reference

| Layer | Subsystems | Permitted Outward Dependencies |
| :---: | :--- | :--- |
| **0** | `types`, `hardware`, `core_component` | Only Layer 0 primitives |
| **1** | `chunk`, `sparse_entity`, `public_entities`, `persistence` | Layer 0 only |
| **2** | `archetype`, `query_cache` | Layers 0 and 1 |
| **3** | `entity_lifecycle`, `command`, `component_access` | Layers 0, 1, and 2 |
| **4** | `query`, `concurrency`, `system`, `spatial` | Layers 0, 1, 2, and 3 (+ sibling concurrency) |
| **5** | `world`, `contracts` | All lower layers (assembly coordinator) |
| **6** | `arche_master` (`include/arche/arche.hpp`) | Umbrella include |

---

## 2. Inventory of Violations

```
├── Upward Inclusions of World (Layer 4 -> Layer 5)
│   ├── [1] include/arche/query.hpp:4
│   ├── [2] include/arche/query.hpp:11
│   ├── [7] src/spatial/sort.hpp:8
│   └── [9] src/system/system_context.hpp:6
│
├── Bidirectional Include Cycles (.hpp <-> .inl)
│   ├── [10] include/arche/types/access.hpp <-> src/system/access.inl
│   ├── [11] include/arche/world.hpp <-> src/world/world_access.hpp <-> world_access.inl
│   ├── [12] src/world/world_access.hpp <-> src/world/world_access.inl
│   ├── [13] src/component/component_access.inl <-> src/component/component_access.hpp
│   ├── [14] include/arche/world.hpp <-> src/component/component_access.inl
│   ├── [15] include/arche/world.hpp <-> src/entity/entity_allocator.inl
│   ├── [16] include/arche/world.hpp <-> src/entity/entity_view.inl
│   ├── [17] include/arche/world.hpp <-> src/world/world_facades_impl.hpp
│   ├── [18] include/arche/query.hpp <-> src/query/query.inl
│   ├── [19] include/arche/command.hpp <-> src/command/command_buffer.inl
│   └── [20] include/arche/query.hpp <-> src/concurrency/chunk_dispatcher.inl
│
├── Misclassified Subsystem Location
│   └── [4] include/arche/types/access.hpp:107
│
└── Sibling & Granular Dependency Boundaries
    ├── [3] include/arche/query.hpp:411
    ├── [5] src/chunk/chunk.hpp:3
    ├── [6] src/entity/slot_map.hpp:3
    └── [8] src/system/system_context.hpp:4
```

---

## 3. Detailed Remediation Guide

---

### Category A: Upward Inclusions of `World` (Violations [1], [2], [7], [9])

#### Issue
Layer 4 headers (`query.hpp`, `sort.hpp`, `system_context.hpp`) directly `#include "arche/world.hpp"` or `"world/world_access.hpp"`. 
`World` is the top-level assembly coordinator (Layer 5). Lower-level execution engines must never depend on the coordinator.

---

#### Violation [1] & [2]: `include/arche/query.hpp`
* **File:** `include/arche/query.hpp:4, 11`
* **Current Code:**
  ```cpp
  #include "arche/world.hpp"
  #include "world/world_access.hpp"
  ```
* **Why it's invalid:** `Query` only needs an archetype container to find matching tables and a registry to look up component IDs. It does not need `World`.
* **Fix (Dependency Inversion):**
  1. Remove both includes from `include/arche/query.hpp`:
     ```diff
     -#include "arche/world.hpp"
     -#include "world/world_access.hpp"
     ```
  2. Change `Query`'s constructor to take `ArchetypeGraph&` and `ComponentRegistry&` (or `std::span<Archetype*> matching_archetypes`):
     ```cpp
     // include/arche/query.hpp
     explicit Query(ArchetypeGraph &graph, ComponentRegistry &registry);
     ```
  3. Let `World` in `include/arche/world.hpp` provide the factory:
     ```cpp
     // include/arche/world.hpp
     template <typename... Ts>
     auto World::query() {
         return Query<Ts...>(this->graph_, this->registry_);
     }
     ```

---

#### Violation [7]: `src/spatial/sort.hpp`
* **File:** `src/spatial/sort.hpp:8`
* **Current Code:**
  ```cpp
  #include "arche/world.hpp"
  ...
  void permute(World &world, Archetype *arch, std::span<const uint32_t> target_indices);
  ```
* **Why it's invalid:** Spatial sorting only rearranges chunks within an archetype and updates the entity slot map.
* **Fix:**
  1. Remove `#include "arche/world.hpp"`.
  2. Change signature to operate directly on the archetype and slot map:
     ```cpp
     // src/spatial/sort.hpp
     void permute(Archetype *arch, Internal::SlotMap &slot_map, std::span<const uint32_t> target_indices);
     ```
  3. `World` delegates to `permute(arch, this->slot_map_, target_indices)`.

---

#### Violation [9]: `src/system/system_context.hpp`
* **File:** `src/system/system_context.hpp:6`
* **Current Code:**
  ```cpp
  #include "arche/world.hpp"
  ```
* **Why it's invalid:** `SystemContext` is delivered to systems during execution.
* **Fix:**
  1. Replace `#include "arche/world.hpp"` with a forward declaration:
     ```cpp
     namespace arche { class World; }
     ```
  2. In `system_context.hpp`, `World&` is only held as a reference `World &world;`. A forward declaration is standard and clean here because `system_context` is an execution context delivered at runtime by the `World` runner.

---

### Category B: Circular Include Cycles (.hpp <-> .inl) (Violations [10] through [20])

#### Issue
An `.inl` file is an *inline implementation fragment* designed to be included at the bottom of its `.hpp`.
Having `#include "foo.hpp"` inside `foo.inl` creates an immediate bidirectional cycle:
`foo.hpp -> foo.inl -> foo.hpp`.

#### Fix Rule
**`.inl` files must never include their parent `.hpp`.** The parent `.hpp` is already responsible for including the `.inl` file.

---

#### Violations [18], [19], [20]: Core Query & Command Cycles
* **`src/query/query.inl` (Violation [18]):**
  Remove `#include "arche/query.hpp"` at the top of `src/query/query.inl`.
* **`src/command/command_buffer.inl` (Violation [19]):**
  Remove `#include "arche/command.hpp"` at the top of `src/command/command_buffer.inl`.
* **`src/concurrency/chunk_dispatcher.inl` (Violation [20]):**
  Remove `#include "arche/query.hpp"` at the top of `src/concurrency/chunk_dispatcher.inl`.

---

#### Violations [11], [12], [13], [14], [15], [16], [17]: World & Component Access Cycles
* **`src/world/world_access.inl` (Violations [11], [12]):**
  Remove `#include "world/world_access.hpp"` and `#include "arche/world.hpp"` from `src/world/world_access.inl`.
* **`src/component/component_access.inl` (Violations [13], [14]):**
  Remove `#include "component/component_access.hpp"` and `#include "arche/world.hpp"` from `src/component/component_access.inl`.
* **`src/entity/entity_allocator.inl` (Violation [15]):**
  Remove `#include "arche/world.hpp"` from `src/entity/entity_allocator.inl`.
* **`src/entity/entity_view.inl` (Violation [16]):**
  Remove `#include "arche/world.hpp"` from `src/entity/entity_view.inl`.
* **`src/world/world_facades_impl.hpp` (Violation [17]):**
  Remove `#include "arche/world.hpp"` from `src/world/world_facades_impl.hpp`.

---

### Category C: Misclassified Header Location (Violation [4])

#### Violation [4]: `include/arche/types/access.hpp`
* **File:** `include/arche/types/access.hpp:107`
* **Current Code:**
  ```cpp
  #include "system/access.inl"
  ```
* **Why it's invalid:** `include/arche/types/` is Layer 0 (pure primitive types). However, `access.hpp` defines `AccessBuilder`, which configures system scheduling masks and includes `system/access.inl` (Layer 4). A Layer 0 header cannot reach up into Layer 4.
* **Fix Options:**
  * **Option A (Move Header):** Move `include/arche/types/access.hpp` to `include/arche/system/access.hpp` (Layer 4), where it belongs conceptually.
  * **Option B (Separate Types from Builder):** Keep the pure bitmask POD types (`AccessDescriptor`) in `types/access.hpp`, and move the builder method implementation (`AccessBuilder::build()`) into `src/system/access.cpp` or `system/scheduler.cpp`.

---

### Category D: Sibling & Granular Dependencies (Violations [3], [5], [6], [8])

---

#### Violations [5] & [6]: Entity Primitive vs. Public Facade
* **Files:**
  * `src/chunk/chunk.hpp:3`
  * `src/entity/slot_map.hpp:3`
* **Current Code:**
  ```cpp
  #include "arche/entity.hpp"
  ```
* **Why it's invalid:** `arche/entity.hpp` is a Layer 1 public facade header. Low-level memory structures (`chunk.hpp` and `slot_map.hpp`) only require the raw 64-bit integer handle type.
* **Fix:**
  In both files, change the include to the Layer 0 primitive:
  ```diff
  -#include "arche/entity.hpp"
  +#include "arche/types/entity.hpp"
  ```

---

#### Violations [3] & [8]: Sibling Concurrency Dependency
* **Files:**
  * `include/arche/query.hpp:411` (`#include "concurrency/chunk_dispatcher.inl"`)
  * `src/system/system_context.hpp:4` (`#include "concurrency/thread_pool.hpp"`)
* **Why it's invalid:** `concurrency` was not explicitly listed in the allowed dependencies for `query` and `system` in `scripts/check_architecture.py`.
* **Fix:**
  In our ECS architecture, parallel query execution (`par_for_each`) and multi-threaded system dispatching legitimately require the thread pool. 
  Update `SUBSYSTEM_RULES` in `scripts/check_architecture.py` to add `"concurrency"` to `allowed_targets` for `"query"` and `"system"`:
  ```python
  "query": {
      "layer": 4,
      "allowed_targets": [..., "concurrency"],
  },
  "system": {
      "layer": 4,
      "allowed_targets": [..., "concurrency"],
  },
  ```

---

## 4. Summary Action Plan

| Step | Action | Files Affected |
| :---: | :--- | :--- |
| **1** | Remove reciprocal `#include` statements from `.inl` files. | `query.inl`, `command_buffer.inl`, `chunk_dispatcher.inl`, `world_access.inl`, `component_access.inl`, `entity_allocator.inl`, `entity_view.inl` |
| **2** | Switch to primitive entity type in Layer 1 headers. | `src/chunk/chunk.hpp`, `src/entity/slot_map.hpp` |
| **3** | Decouple `Query` and `permute` from `World`. | `include/arche/query.hpp`, `src/spatial/sort.hpp` |
| **4** | Replace `#include "arche/world.hpp"` with `class World;` forward declaration in `system_context`. | `src/system/system_context.hpp` |
| **5** | Move `access.hpp` or decouple `AccessBuilder` from `types/`. | `include/arche/types/access.hpp` |
| **6** | Authorize `concurrency` dependency for `query` and `system`. | `scripts/check_architecture.py` |
