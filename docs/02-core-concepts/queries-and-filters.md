# Queries & Filters

> **Status:** Documentation Skeleton (To be expanded)

## 1. Streaming Query Semantics
- `world.query<Components...>()` traverses matching archetype chunks with zero heap allocations.
- Supports reference (`T&`), const reference (`const T&`), and optional pointer (`const T*`, `T*`) component bindings.

## 2. Compile-Time Query Filters
- `With<Ts...>`: Inclusion requirements without unpacking data into loop bindings.
- `Without<Ts...>`: Instant rejection of archetypes containing excluded components.
- `Has<T>`: Query argument yielding a boolean presence flag without excluding entities that lack it.
- `Or<Filters...>`: Disjunctive filtering matching archetypes satisfying at least one branch.
- `Mutated<T>`: Reactive iteration matching only chunks whose component column was modified since target tick.
- `Added<T>`: Reactive iteration matching entities created or given component `T` in the current frame.

## 3. Parallel Query Visitation
- `for_each_chunk_parallel()`: Chunk-level parallel dispatching across thread pool workers.
- `for_each_entity_parallel()`: Entity-level parallel iteration with chunk affinity.
