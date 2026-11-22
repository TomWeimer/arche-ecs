# Entities & Generational Handles

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is an Entity?
An `Entity` is not a container, class, or pointer. It is an opaque 64-bit integer handle:
- **Bits 0–31 (32 bits):** Dense slot index into the SlotMap.
- **Bits 32–63 (32 bits):** Generation counter.

## 2. Eliminating ABA Hazards with Generational Indexing
- When entity $A$ is deleted, its slot index is recycled into a free list.
- The generation counter increments on recycling ($gen \rightarrow gen + 1$).
- Stale handles referencing dead entities instantly fail validation in $\mathcal{O}(1)$.

## 3. SlotMap Architecture
- Indirection table decoupling opaque entity handles from dynamic chunk memory locations.
- How `SlotMap` updates row mappings during Swap-and-Pop without invalidating existing handles.

## 4. Entity Table Introspection (EntityFacade)
- `world.entities.contains(e)`: Direct alive & generation validation in $\mathcal{O}(1)$.
- `world.entities.count()`: Total living entities across all archetypes.
- `world.entities.capacity()`: Total allocated slot map capacity.
- `world.entities.is_empty()`: True if zero entities currently exist.
- `world.entities.resolve(slot_index)`: Reconstructs a valid handle from a slot index if alive.
