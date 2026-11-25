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

| Layer | Subsystem / Tier | Contents | Permitted Dependencies |
| :---: | :--- | :--- | :--- |
| **0** | `foundation` (`entity`, `hardware`, `component`) | Entity handle, POD types, bitmasks, CPU intrinsics, ColumnLayout, ComponentID | Only Layer 0 |
| **1** | `storage` (`chunk`, `slot_map`) | 64KB chunk page memory, SlotMap routing table (independent peers) | Layer 0 only |
| **2** | `structure` (`archetype`, `archetype_index`) | Archetype tables, transition graph, reactive table index | Layers 0 and 1 |
| **3** | `lifecycle` (`mutation`, `snapshot`, `resource`) | Archetype transitions & command buffers, snapshot I/O, singleton resources (independent peers) | Layers 0, 1, and 2 |
| **4** | `concurrency` (`thread_pool`) | Barrier-synchronized worker thread pool | Layer 0 only |
| **5** | `query` (`query`, `sort`) | Query iterators, SIMD tail padding, each_parallel, sort / permute | Layers 0..4 (storage, structure, mutation, concurrency) |
| **6** | `access` | Declarative component and resource access footprints | Layers 0 and 3 (`resource`) |
| **7** | `system` (`system_context`, `system_types`) | System execution scope and function signatures | Layers 0..6 (mutation, query, concurrency, access) |
| **8** | `scheduler` | Conflict graph compilation, topological sort, batch dispatch | Layers 0..7 (access, system, concurrency, mutation) |
| **9** | `plugin` | Autonomous plugin installation and system registration | Layers 6, 7, 8 (`access`, `system`, `scheduler`) |
| **10** | `views` | Domain views (`archetype_view`, `entity_view`, `resource_view`, `snapshot_view`, `system_view`) | Layers 0..9 |
| **11** | `world` | Central World assembly, domain facades composition, storage | All lower layers (assembly coordinator) |
| **12** | `arche_master` | Umbrella header & module (`src/arche.cppm`, `include/arche/arche.hpp`) | All layers |

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

## Execution & Workflow Directives
1. **Error Detection & Linting:**
   - To check for syntax, type, and compiler errors as well as architectural rules across the codebase, run `make lint` (which executes `python3 scripts/check_architecture.py` followed by `ninja -C build`).
   - Do NOT run `scripts/lint.py`.
2. **Formatter & Benchmarking:**
   - Do NOT run the code formatter (`clang-format`) or benchmarks (`arche_benchmarks`, `comparative_benchmarks`, etc.) unless explicitly instructed by the user.
