# Guide: Working with Entity Handles (EntityRef & EntityMut)

> **Status:** Documentation Skeleton (To be expanded)

## 1. When to Use Raw Entity vs. EntityRef / EntityMut
- **Raw `Entity`:** Ideal for storage inside other components (e.g. `Target { Entity enemy; }`), message queues, or event payloads. Extremely cheap (8 bytes).
- **`EntityRef`:** Read-only inspections on `const World&`.
- **`EntityMut`:** Multi-step chained operations on mutable `World&`.

## 2. Idiomatic Handle Chaining
```cpp
auto e = world.entity(id);
if (e.is_alive()) {
    e.get_mut<Position>().x += 5.0f;
    e.add(Velocity{1.0f, 0.0f});
}
```

## 3. Safe Conversions & Lifetimes
- Implicit conversion from `EntityMut` $\rightarrow$ `EntityRef`.
- Validating entity existence across frames with `is_alive()`.
