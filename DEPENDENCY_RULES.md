# Arche ECS — Architecture Dependency Specification

This document is the authoritative reference for all inter-layer, intra-layer, and include dependency rules across Arche ECS. Every file in the repository must strictly conform to these rules.

---

## 1. Architectural Layers & Top-Down Flow (Inter-Layer Rules)

Dependencies flow strictly **top-down (higher layers $\rightarrow$ lower layers)**. A layer may only depend on layers below it.

```
Layer 6: arche_master (arche/arche.hpp)
   │
   ▼
Layer 5: world, contracts
   │
   ▼
Layer 4: query, concurrency, system, spatial
   │
   ▼
Layer 3: command, entity_lifecycle, component_access
   │
   ▼
Layer 2: archetype, query_cache
   │
   ▼
Layer 1: chunk, sparse_entity, public_entities, persistence
   │
   ▼
Layer 0: types, hardware, core_component
```

### Layer Summary Table

| Layer | Subsystem | Scope / Directory | Permitted Downward Targets |
| :---: | :--- | :--- | :--- |
| **0** | `types`<br>`hardware`<br>`core_component` | `include/arche/types/`<br>`src/hardware/`<br>`src/component/` (`type_id`, `component_mask`, `column_layout`) | **Zero external dependencies** (Self-contained primitives, CPU intrinsics, and bitmasks only) |
| **1** | `chunk`<br>`sparse_entity`<br>`public_entities`<br>`persistence` | `src/chunk/`<br>`src/entity/` (`slot_map`, `entity_record`)<br>`include/arche/entity.hpp`, `resource.hpp`<br>`src/persistence/` | **Layer 0 only** |
| **2** | `archetype`<br>`query_cache` | `src/archetype/`<br>`src/query/query_cache.hpp` | **Layers 0 and 1** |
| **3** | `entity_lifecycle`<br>`command`<br>`component_access` | `src/entity/` (`entity_allocator`, `entity_view`)<br>`src/command/`, `include/arche/command.hpp`<br>`src/component/component_access.hpp` | **Layers 0, 1, and 2** |
| **4** | `query`<br>`concurrency`<br>`system`<br>`spatial` | `include/arche/query.hpp`, `src/query/`<br>`src/concurrency/`<br>`src/system/`<br>`src/spatial/` | **Layers 0, 1, 2, and 3** (+ authorized same-layer siblings) |
| **5** | `world`<br>`contracts` | `include/arche/world.hpp`, `src/world/`<br>`include/arche/contracts/` | **All lower layers (0 through 4)** (Assembly and coordination facade) |
| **6** | `arche_master` | `include/arche/arche.hpp` | **All layers** (Umbrella convenience header) |

---

## 2. Subsystem Matrix & Intra-Layer Rules (Same-Layer Sibling Rules)

Subsystems located on the **same layer** are **disjoint and isolated by default**. Cross-dependencies between siblings in the same layer are strictly **forbidden** unless explicitly whitelisted below.

### Complete Subsystem Allowed Targets Matrix

| Subsystem | Layer | Whitelisted Allowed Targets (`allowed_targets`) | Explicitly Forbidden Siblings |
| :--- | :---: | :--- | :--- |
| **`types`** | 0 | `types` | `hardware`, `core_component` |
| **`hardware`** | 0 | `hardware`, `types` | `core_component` |
| **`core_component`** | 0 | `core_component`, `hardware`, `types` | *None* |
| **`chunk`** | 1 | `core_component`, `hardware`, `types`, `chunk` | **`sparse_entity` (SlotMap)**, `public_entities` |
| **`sparse_entity`** | 1 | `core_component`, `hardware`, `types`, `sparse_entity` | **`chunk`**, `public_entities` |
| **`public_entities`** | 1 | `types`, `public_entities` | `chunk`, `sparse_entity` |
| **`persistence`** | 1 | `types`, `sparse_entity`, `hardware`, `persistence` | `chunk`, `public_entities` |
| **`query_cache`** | 2 | `core_component`, `types`, `query_cache` | `archetype` |
| **`archetype`** | 2 | `chunk`, `sparse_entity`, `core_component`, `hardware`, `types`, `query_cache`, `archetype` | *None* |
| **`entity_lifecycle`**| 3 | `archetype`, `chunk`, `sparse_entity`, `core_component`, `hardware`, `types`, `entity_lifecycle` | `command`, `component_access` |
| **`command`** | 3 | `archetype`, `chunk`, `sparse_entity`, `entity_lifecycle`, `core_component`, `hardware`, `types`, `command` | `component_access` |
| **`component_access`**| 3 | `archetype`, `chunk`, `sparse_entity`, `core_component`, `hardware`, `types`, `component_access` | `command`, `entity_lifecycle` |
| **`query`** | 4 | `archetype`, `chunk`, `sparse_entity`, `query_cache`, `core_component`, `hardware`, `types`, `concurrency`, `query` | `system`, `spatial` |
| **`concurrency`** | 4 | `query`, `command`, `archetype`, `chunk`, `hardware`, `types`, `concurrency` | `system`, `spatial` |
| **`system`** | 4 | `command`, `query`, `concurrency`, `archetype`, `chunk`, `sparse_entity`, `core_component`, `hardware`, `types`, `system` | `spatial` |
| **`spatial`** | 4 | `query`, `archetype`, `chunk`, `core_component`, `hardware`, `types`, `spatial` | `system`, `concurrency` |
| **`contracts`** | 5 | `types`, `public_entities`, `contracts` | *None* |
| **`world`** | 5 | `*` (All lower subsystems) | *None* |
| **`arche_master`** | 6 | `*` (All subsystems) | *None* |

---

## 3. Canonical Include Usage Rules

Every `#include` directive must follow these exact structural conventions:

### Rule 1: Include Path Formats
* **Public Library Headers:**
  Always use `#include "arche/..."`:
  ```cpp
  #include "arche/types/entity.hpp"
  #include "arche/types/filters.hpp"
  #include "arche/query.hpp"
  ```
* **Internal Subsystem Headers:**
  Always use `#include "<subsystem>/<file>.hpp"`:
  ```cpp
  #include "chunk/chunk.hpp"
  #include "archetype/archetype.hpp"
  #include "component/column_layout.hpp"
  ```
* **Sibling Files in the Identical Directory:**
  May use bare quotes:
  ```cpp
  #include "chunk_pool.hpp" // Inside src/chunk/
  ```
* **Strictly Forbidden:**
  * **Relative directory traversal:** Never write `../` (e.g., `#include "../component/type_id.hpp"` is forbidden).
  * **Global catch-all includes:** Never include `arche/world.hpp` or `arche/arche.hpp` in any lower layer.

---

### Rule 2: Unidirectional `.inl` Implementation Fragments
`.inl` files are inline implementation fragments, **not** standalone headers or translation units.
1. The parent `.hpp` header includes its `.inl` file at the **very bottom**:
   ```cpp
   // At the bottom of include/arche/query.hpp:
   #include "query/query.inl"
   ```
2. **The `.inl` file must NEVER `#include` its parent `.hpp` header.**
   * `query.inl` must **never** write `#include "arche/query.hpp"`.
   * `command_buffer.inl` must **never** write `#include "arche/command.hpp"`.
   * `world_access.inl` must **never** write `#include "world/world_access.hpp"`.
3. Inclusion flows strictly **one way**: `.hpp` $\rightarrow$ `.inl`.

---

### Rule 3: No Forward-Declaration Bypasses
Forward declarations must **never** be used to call methods or access members of a higher layer or forbidden sibling:
* **Forbidden Pattern:** Forward-declaring `class World;` in Layer 4 (`query.hpp`) and then calling `world.foo()` inside an inline method.
* **Required Pattern (Dependency Inversion):** Remove the parameter. Pass only the exact data structure needed (`const QueryCache&`, `ArchetypeGraph&`, `SlotMap&`).

---

### Rule 4: Public Wall (Layer 0 Purity)
Public headers in `include/arche/types/` and `include/arche/contracts/` represent pure POD contracts and interfaces:
* They must **never** `#include` files from `src/`.
* They must depend **only** on standard library headers and sibling files in `include/arche/types/`.

---

## 4. Architectural Case Studies

### Case Study A: `Chunk` vs. `SlotMap` (Layer 1 Horizontal Isolation)
* **Status:** `chunk` and `sparse_entity` are both in Layer 1. They are horizontal siblings.
* **Rule:** `chunk` must **never** include or call `SlotMap`.
* **Design:** 
  * `Chunk` is a pure 64KB physical memory slab. It only moves bytes in memory during `swap_and_pop`.
  * `Chunk::swap_and_pop` returns the relocated `Entity` (or calls an uncoupled lambda callback).
  * The caller in Layer 2 (`Archetype`) or Layer 5 (`World`) updates `SlotMap`.

### Case Study B: `Query` vs. `World` (Layer 4 $\rightarrow$ Layer 5 Upward Prohibition)
* **Status:** `Query` is Layer 4. `World` is Layer 5.
* **Rule:** `Query` must **never** include `arche/world.hpp` or `world/world_access.hpp`.
* **Design:**
  * `Query` takes `(const QueryCache &cache, Filters... filters)`.
  * `World::query()` in Layer 5 acquires the cache and constructs the `Query`.
  * `Query` has zero reference to `World`.
