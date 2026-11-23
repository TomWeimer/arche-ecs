# Arche ECS — Library Directives

## Mission
Standalone, high-performance C++20 archetype ECS library.

## Hard Invariants
1. **Flat Data over Abstraction:**
   - No virtual methods, no abstract classes, no polymorphic interfaces.
   - Plain Old Data (POD) structs only.
2. **Current Milestone:**
   - Stage 1 (Naive pointer / flat array storage).
   - Forbidden: Archetype graphs, chunked tables, sparse sets until Stage 1 has recorded telemetry.
3. **Zero Consumer Context:**
   - Standard library and CPU intrinsics only (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`).
   - ZERO game logic, boid steering math, or simulation code in this repository.

## Architectural Layers & Dependency Flow
Dependencies flow strictly **top-down**. Lower layers must NEVER depend on or include higher layers.

| Layer | Subsystem | Contents | Permitted Dependencies |
| :---: | :--- | :--- | :--- |
| **0** | `types`, `hardware`, `core_component` | POD types, bitmasks, CPU intrinsics, ColumnLayout | Only Layer 0 |
| **1** | `chunk`, `sparse_entity`, `public_entities` | 64KB chunk page memory, SlotMap, Entity handles | Layer 0 only |
| **2** | `archetype`, `query_cache` | Archetype tables, transition graph, reactive cache | Layers 0 and 1 |
| **3** | `command`, `entity_lifecycle`, `component_access` | Command buffer, EntityAllocator, EntityRef view | Layers 0, 1, and 2 |
| **4** | `query`, `concurrency`, `system`, `spatial` | Query iterators, ThreadPool, Scheduler DAG, sort | Layers 0, 1, 2, and 3 (+ sibling concurrency) |
| **5** | `world`, `contracts` | Central World assembly, sub-facades, storage | All lower layers (assembly coordinator) |
| **6** | `arche_master` | Umbrella header (`include/arche/arche.hpp`) | All layers |

## Architecture Rules & Code Conventions
1. **No Upward Dependencies:**
   - Lower layers must never depend on higher layers (e.g., `Query` in Layer 4 must NEVER include or reference `World` in Layer 5).
   - Do NOT use forward declarations as a syntactic workaround to call into higher layers. Invert the dependency so lower layers accept only what they actually need (`QueryCache&`, `SlotMap&`, etc.).
2. **No Circular Includes in Inline Fragments:**
   - `.inl` implementation files are fragments included at the bottom of their parent `.hpp`.
   - `.inl` files must NEVER `#include` their parent `.hpp` header. The dependency is strictly unidirectional: `.hpp` $\rightarrow$ `.inl`.
3. **Canonical Include Path Conventions:**
   - Public types and API headers: `#include "arche/..."` (e.g., `#include "arche/types/entity.hpp"`).
   - Internal subsystem headers: `#include "<subsystem>/<file>.hpp"` (e.g., `#include "chunk/chunk.hpp"`, `#include "archetype/archetype.hpp"`).
   - Sibling files within the exact same directory may use `#include "<file>.hpp"`.
   - Never use relative directory traversing (`../`).
4. **Stable Build Configuration:**
   - Do NOT modify `CMakeLists.txt` or tweak search paths to paper over architectural violations. Fix the code to conform to the architecture rules.
