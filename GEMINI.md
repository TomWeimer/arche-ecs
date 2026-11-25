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
| **1** | `storage` (`chunk`, `slot_map`) | 64KB chunk page memory, SlotMap routing table | Layer 0 only |
| **2** | `structure` (`archetype`, `query_cache`) | Archetype tables, transition graph, reactive cache | Layers 0 and 1 |
| **3** | `lifecycle` (`mutation`, `command`, `snapshot`, `resource`, `entity_view`) | Archetype transitions, command buffer, snapshot I/O, singleton resources, EntityRef | Layers 0, 1, and 2 |
| **4** | `runtime` (`query`, `concurrency`, `system`, `sort`) | Query iterators, ThreadPool, Scheduler DAG, sort / permute | Layers 0, 1, 2, and 3 (+ sibling concurrency) |
| **5** | `domain` | Domain facades (archetype, command, entity, resource, snapshot, system) | Layers 0, 1, 2, 3, and 4 |
| **6** | `world` | Central World assembly, domain facades composition, storage | All lower layers (assembly coordinator) |
| **7** | `arche_master` | Umbrella header & module (`src/arche.cppm`, `include/arche/arche.hpp`) | All layers |

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
