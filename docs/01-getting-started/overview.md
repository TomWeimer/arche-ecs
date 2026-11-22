# Arche ECS — Overview

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is Arche ECS?
Arche ECS is a standalone, zero-dependency, cache-aligned archetype Entity Component System written in modern C++20.

## 2. Core Philosophy & Invariants
- **Flat Data over Abstraction:** No virtual methods, no abstract base classes, zero polymorphic overhead, `-fno-rtti` compliant.
- **Cache-Locality First:** Entities with identical component sets are packed contiguously into fixed-size 64 KB memory chunks.
- **Zero Consumer Context:** Pure standard library and CPU intrinsics only (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`). Zero game logic in the core library.

## 3. High-Level Architecture & Domain Sub-Facades
The `World` coordinates simulation state through focused domain sub-facades:
- **`world.entities`:** Generational handle validation, population counts, and slot resolution.
- **`world.archetypes`:** Graph schema metrics, chunk buffer arrays, and component frequency counts.
- **`world.resources`:** Ambient world singleton management with zero-RTTI flat monotonic storage.
- **`world.systems`:** Declarative access descriptors, parallel DAG compilation, and stage execution.
- **`world.gpu`:** Contiguous component chunk views and zero-copy streaming for graphics buffers.
- **`world.snapshots`:** Full-world binary persistence and checkpoint serialization.
