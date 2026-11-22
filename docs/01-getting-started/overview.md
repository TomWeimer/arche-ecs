# Arche ECS — Overview

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is Arche ECS?
Arche ECS is a standalone, zero-dependency, cache-aligned archetype Entity Component System written in modern C++20.

## 2. Core Philosophy & Invariants
- **Flat Data over Abstraction:** No virtual methods, no abstract base classes, zero polymorphic overhead, `-fno-rtti` compliant.
- **Cache-Locality First:** Entities with identical component sets are packed contiguously into fixed-size 64 KB memory chunks.
- **Zero Consumer Context:** Pure standard library and CPU intrinsics only (`<cstdint>`, `<vector>`, `<cstring>`, `<chrono>`). Zero game logic in the core library.

## 3. High-Level Architecture
- **World:** Central coordinator and sub-facade provider.
- **Entities:** Opaque 64-bit generational handles (`SlotMap`).
- **Archetypes:** Contiguous columns of POD components stored in 64 KB memory pages.
- **Queries:** Type-safe, SIMD-aligned streaming traversal across matching archetype chunks.
- **Scheduler:** Declarative compile-time conflict detection and DAG-based parallel execution.
