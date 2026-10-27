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