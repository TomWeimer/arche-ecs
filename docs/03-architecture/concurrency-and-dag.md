# Concurrency & DAG Scheduling

> **Status:** Documentation Skeleton (To be expanded)

## 1. Zero Runtime Locks
- Eliminates mutexes and reader-writer locks in simulation frames.
- Declarative access declarations: `AccessBuilder{}.reads<A>().writes<B>()`.

## 2. Directed Acyclic Graph (DAG) Compilation
- Systems with no data race conflicts are batched into parallel execution stages.
- Systems with write-write or read-write conflicts are strictly serialized across stages.

## 3. Work-Stealing Thread Pool
- Chunk-level parallel dispatching.
- Thread-safe command buffers for deferred structural mutations.
