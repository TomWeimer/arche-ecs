# Components & Type System

> **Status:** Documentation Skeleton (To be expanded)

## 1. Component Rules & Invariants
- Plain Old Data (POD) structs only.
- Trivially copyable (`std::is_trivially_copyable_v<T> == true`).
- Zero virtual methods, zero inheritance, zero heap allocations inside components.

## 2. Compile-Time Type Registration & TypeIDs
- Dense sequential 32-bit `TypeID` (`0, 1, 2...`).
- Monotonic type counter guaranteeing zero alignment gaps in chunk column layouts.

## 3. 128-Bit SIMD ComponentMask
- Flat bitset backed by two 64-bit integer words (`uint64_t words[2]`, 16 bytes).
- Hardware population counting (`std::popcount`).
- Bitwise presence operations: `set()`, `reset()`, `test()`, `contains_all()`, `contains_none()`, `contains_any()`.
- Golden-ratio hash functor (`ComponentMaskHash`) for constant-time hash map lookups.

## 4. Dirty Ticks & Change Detection
- Monotonic version tick per archetype chunk column.
- Mutable component access (`world.get_mut<T>()`) stamps the current simulation tick.
- Enables reactive queries via the `Mutated<T>` filter without scanning unchanged memory.
