# Queries & Filters

> **Status:** Documentation Skeleton (To be expanded)

## 1. Streaming Query Semantics
- `world.query<Components...>()` traverses matching archetype chunks.
- Supports reference (`T&`), const reference (`const T&`), and optional pointer (`const T*`, `T*`) component arguments.

## 2. Compile-Time Query Filters
- `With<T...>`: Inclusion requirements without unpacking data into loop bindings.
- `Without<T...>`: Instant rejection of archetypes containing excluded components.
- `Mutated<T>`: Reactive iteration matching only chunks whose component column was modified since the target epoch.
- `Added<T>`: Iteration matching entities created or given component `T` in the current frame.
- `Or<A, B>`: Disjunctive filtering across multiple component choices.

## 3. Parallel Visitation
- `for_each_chunk_parallel()`
- `for_each_entity_parallel()`
