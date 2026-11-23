# Quickstart Guide

> **Status:** Documentation Skeleton (To be expanded)

## 1. Defining Components
Components in Arche ECS are plain old data (POD) structs:
```cpp
struct Position { float x{0.0f}, y{0.0f}; };
struct Velocity { float dx{0.0f}, dy{0.0f}; };
struct Health   { int current{100}, max{100}; };
```

## 2. Initializing the World & Spawning Entities
```cpp
import arche;

arche::World world;

// Individual spawn
arche::Entity player = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 0.0f}, Health{100, 100});

// Batch spawn (e.g. 10,000 entities)
world.spawn({.count = 10000}, Position{0.0f, 0.0f}, Velocity{0.5f, 0.5f});
```

## 3. Querying & Iterating Entities
```cpp
// Streaming query with optional filters
for (auto [pos, vel] : world.query<Position, const Velocity*>()) {
    pos.x += vel->dx;
    pos.y += vel->dy;
}
```

## 4. Entity Handles & Structural Mutation
```cpp
auto e = world.entity(player);
if (e.is_alive() && e.has<Health>()) {
    e.get_mut<Health>().current -= 10;
    e.add(Velocity{0.0f, 0.0f});
}
```
