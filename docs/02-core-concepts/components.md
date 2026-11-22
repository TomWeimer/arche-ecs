# Components & Type System

> **Status:** Documentation Skeleton (To be expanded)

## 1. Component Rules & Invariants
- Plain Old Data (POD) structs only.
- Trivially copyable (`std::is_trivially_copyable_v<T> == true`).
- Zero virtual methods, zero inheritance.

## 2. Compile-Time Type Registration & TypeIDs
- Sequential TypeID generation.
- 128-bit SIMD `ComponentMask` for instantaneous bitwise presence testing.

## 3. Dirty Ticks & Change Detection
- How writes (`world.get_mut<T>()`) increment chunk column dirty ticks.
- Enabling reactive systems using the `Mutated<T>` query filter.
