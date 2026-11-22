# Why Archetypes? Memory Layout & Cache Mechanics

> **Status:** Documentation Skeleton (To be expanded)

## 1. The Cache Problem in Traditional OOP & Sparse Sets
- **OOP (Array of Pointers):** Pointer chasing across the heap, guaranteed cache misses on every entity.
- **Sparse Sets:** Great for fast addition/removal, but fragmented memory when multiple components are iterated together.
- **Archetype ECS (Structure of Arrays in Pages):** Stores all entities with the exact same component set contiguously in memory.

## 2. Chunk-Based Archetype Layout
- Fixed 64 KB memory chunks aligned to L1/L2 cache prefetchers.
- Components partitioned into dense parallel arrays (columns).
- SIMD vectorization out-of-the-box (zero padding between elements).

## 3. Trade-offs & Structural Mutations
- Adding or removing a component relocates an entity between archetypes.
- How Swap-and-Pop compacts chunks in $\mathcal{O}(1)$.
