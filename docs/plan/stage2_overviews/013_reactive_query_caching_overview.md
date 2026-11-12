# Module 013: Reactive Flat Query Caching (Zero Hooks) — Overview

## 1. Architectural Mission
In Stage 1, constructing a `Query<Ts...>` iterates sequentially through all archetypes in `ArchetypeGraph` to evaluate matching bitmasks. In large worlds with frequent structural mutations, this linear scan creates avoidable overhead.

However, general-purpose ECS libraries (like Flecs or Unreal Mass) often implement dynamic observers, string entity names, and runtime `on_add` / `on_set` event hooks. In high-density simulations (like `colony`), **runtime event hooks are toxic to performance**:
- Every hook check introduces branch mispredictions in inner loops.
- Dynamic observer tables cause instruction-cache thrashing.

The mission of Module 013 is to implement **pure flat-data reactive query caching with ZERO virtual hooks and ZERO runtime callbacks**:
- Queries simply read from a pre-filtered, contiguous array of `Archetype*` pointers (`QueryCache`).
- When a new archetype is added to the graph, `ArchetypeGraph` tests it against registered `QueryCache` bitmasks and pushes the pointer.
- `colony` writes completely standard, hook-free code: `world.query<Position, Velocity>()`.

---

## 2. Key Capabilities & Interfaces

### 2.1 Pure Flat `QueryCache` Structure
```cpp
struct QueryCache {
    ComponentMask include_mask;
    ComponentMask exclude_mask;
    std::vector<Archetype*> matching_archetypes; // Contiguous pointer array
};
```
- No virtual methods, no abstract classes, no polymorphic interfaces.
- Stored and managed centrally in `World`.

### 2.2 Event-Driven Archetype Graph Population
```cpp
// Inside ArchetypeGraph::create_archetype (internal to arche-ecs):
for (QueryCache* cache : registered_caches_) {
    if (new_archetype->matches(cache->include_mask, cache->exclude_mask)) {
        cache->matching_archetypes.push_back(new_archetype);
    }
}
```
- When a new archetype is created in the DAG, it is tested once against active query caches.
- Subsequent query iterations simply loop over `cache->matching_archetypes` with zero graph polling.

### 2.3 Shared Query Cache Deduplication
- Multiple systems querying the same signature (`Query<Position, Velocity>`) share the exact same `QueryCache` pointer.
- Zero redundant bitmask evaluations across disparate systems.

---

## 3. Subsystem Modifications
- **`ArchetypeGraph` (`archetype_graph.hpp`):** Add `std::vector<QueryCache*> registered_caches_` and automatic pointer appending on new archetype creation.
- **`Query` (`query.hpp`):** Reference a cached `const QueryCache*` instead of holding a local `std::vector<Archetype*>`.
- **`World` (`world.hpp`):** Own a pool of `QueryCache` instances indexed by hash of `(include_mask, exclude_mask)`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero virtual functions; zero callbacks in consumer code; zero dynamic heap allocations during query iteration.
- **Performance Budget:** Query construction drops from $O(A)$ (where $A$ is total graph archetypes) to $O(1)$ table lookup. Query execution involves zero mask evaluations.

---

## 5. Verification & Testing Strategy
- **`query_caching_tests`:**
  - Verify dynamic query cache updates when new archetypes are instantiated *after* query construction.
  - Verify that `colony` code remains completely standard and declarative without any hook registration.
  - Test shared cache deduplication across multiple queries with identical component masks.

