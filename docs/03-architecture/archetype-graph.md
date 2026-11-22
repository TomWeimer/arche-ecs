# Archetype Graph & Structural Mutations

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is an Archetype?
An archetype is a unique combination of component types. Every entity belonging to an archetype has the exact same data layout.

## 2. Transition Graph Mechanics
- Nodes represent unique archetypes.
- Directed edges represent structural mutations:
  - Adding component $T$: `traverse_add(current, T)`
  - Removing component $T$: `traverse_remove(current, T)`
- Graph edges are lazily generated and cached for $\mathcal{O}(1)$ transitions.

## 3. Entity Migration & Swap-and-Pop
- Relocating component data from source chunk to destination chunk.
- Compacting source chunk using Swap-and-Pop to maintain dense zero-hole memory.
- Updating `SlotMap` records for both the moved entity and the swapped neighbor.

## 4. Archetype Storage Introspection (ArchetypeFacade)
- `world.archetypes.count()`: Total number of active archetype schemas in the graph.
- `world.archetypes.chunks(arch)`: Direct access to allocated chunk buffer arrays.
- `world.archetypes.count_with<T>()`: Fast query-less frequency counts of entities with component $T$.
