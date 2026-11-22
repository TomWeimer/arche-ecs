# Concurrency & DAG Scheduling

> **Status:** Documentation Skeleton (To be expanded)

## 1. Zero Runtime Locks
- Eliminates mutexes, spinlocks, and reader-writer locks in simulation loops.
- All parallelism is planned and validated before execution begins.

## 2. Declarative Access Descriptors
- Systems declare exact read/write dependencies via `AccessBuilder`:
  - `reads<Comps...>()` / `writes<Comps...>()`
  - `reads_resource<Res...>()` / `writes_resource<Res...>()`
- `AccessDescriptor`: Four 128-bit bitmasks (`read_components`, `write_components`, `read_resources`, `write_resources`).
- `conflicts_with()`: Bitwise race detection verifying read-write and write-write independence in $\mathcal{O}(1)$.

## 3. Directed Acyclic Graph (DAG) Compilation
- Systems with zero data conflicts are batched into parallel execution stages.
- Systems with read-write or write-write conflicts are sequenced into consecutive stages.
- Topological stage sorting guarantees deterministic, race-free simulation frames.

## 4. SystemScale: Computational Weight Classes
- `SystemScale::Narrow`: Inter-system parallelism (multiple independent systems run concurrently across threads).
- `SystemScale::Wide`: Intra-system parallelism (heavy systems get exclusive ThreadPool access for chunk-level `parallel_for`).
- `SystemScale::Sequential`: Synchronous execution pinned to caller/main thread.

## 5. SystemFacade & Pipeline Execution
- `world.systems.add_system(...)`: Registers systems with name, access, callback, and scale.
- `world.systems.compile()`: Compiles the conflict-free parallel DAG stages.
- `world.systems.run(pool)`: Dispatches compiled stages across worker threads.
- Thread-safe command buffers (`CommandBuffer`, `ParallelCommandBuffer`) for deferred structural mutations.
