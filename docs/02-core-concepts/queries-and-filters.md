# Queries & Filters

> **Status:** Documentation Skeleton (To be expanded)

## 1. Separation of Data Terms vs. Filter Tags
- Strict role separation enforced at compile time via C++20 concepts:
  - **Data Terms (`QueryTerm...`):** Types yielded to user callbacks or structured bindings (`T&`, `const T&`, `T*`, `const T*` / `Opt<T>`, `Entity`, `Has<T>`).
  - **Filter Tags (`QueryFilter...`):** Archetype and chunk pruning directives yielding zero data (`With`, `Without`, `Or`, `Mutated`, `Added`).

## 2. Compile-Time Archetype & Chunk Filters
- **Structural Filters (Archetype-level pruning via `ComponentMask`):**
  - `With<Ts...>`: Demands presence of components without yielding data into callback parameters.
  - `Without<Ts...>`: Instant rejection of archetypes containing any excluded component.
  - `Or<With<...>, Without<...>>`: Disjunctive filtering matching archetypes satisfying at least one branch (branches strictly constrained by `ValidOrBranch`).
- **Temporal Change-Detection Filters (Chunk-level pruning):**
  - `Mutated<T>(since_version)`: Evaluates chunk component column version against `since_version`.
  - `Added(since_version)`: Evaluates chunk `structure_version` against `since_version` for newly created or moved entities.
  - `SINCE_LAST_FRAME`: Automatic default sentinel tracking frame-over-frame mutations.
  - RFC 1982 modular distance arithmetic ensuring mathematically robust 32-bit tick wrap-around protection.

## 3. Query Execution & Traversal Paradigms
- **Range-Based For Traversal (`begin()` / `end()`):**
  - Standard C++20 forward iterator (`QueryIterator`) yielding structured bindings tuples with `std::apply` row unpacking.
- **Contiguous Chunk Batching (`each_chunk`):**
  - High-throughput contiguous chunk array traversal with CPU lookahead prefetching (`prefetch_next_chunk_with_offsets`).
- **SIMD Tail-Padded Traversal (`each_chunk_padded`):**
  - Passes active count and 16-element rounded-up SIMD count for branchless vectorized execution.
- **Multi-Threaded Parallel Traversal (`each_parallel`):**
  - Dynamic chunk distribution across `ThreadPool` workers.
  - Inferred callback signatures: Mode A (pure compute: `count, Comp*...`) and Mode B (deferred mutations: `count, CommandBuffer&, Comp*...`).
