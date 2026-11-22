# World Resources

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is a Resource?
- World-level global singletons (e.g. `Time`, `Input`, `PhysicsConfig`).
- Decoupled from entities: exactly zero or one instance of type `T` per `World`.
- Zero-RTTI compliant: supports `-fno-rtti` builds without `typeid` or polymorphic vtables.

## 2. Dense Flat Monotonic Storage
- Dense sequential 32-bit `ResourceID` (`0, 1, 2...`) assigned on first access.
- Contiguous array of pointers/slots in L1 cache (`ResourceContainer`).
- `ResourceMask` (128-bit bitset) tracking active resources with bitwise speed.

## 3. ResourceFacade API
```cpp
// Insert singleton resource
world.resources.insert(Time{0.0f, 0.016f});

// Read and mutate resources
if (world.resources.has<Time>()) {
    const Time &time = world.resources.get<Time>();
    world.resources.get_mut<Time>().delta_time = 0.033f;
}

// Remove resource
world.resources.remove<Time>();
```

## 4. Declarative Concurrency & Scheduling
- Systems declare resource dependencies via `AccessBuilder`:
  - `reads_resource<Res...>()`
  - `writes_resource<Res...>()`
- Scheduler tracks resource hazards (`ResourceMask` intersection) to prevent data races across worker threads.
