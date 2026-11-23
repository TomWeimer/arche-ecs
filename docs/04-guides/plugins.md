# Guide: Engine Plugins & Feature Modularization

> **Status:** Documentation Skeleton (To be expanded)

## 1. What is a Plugin?
- Modular self-contained bundle for registering components, systems, and singleton resources.
- Enforces compile-time structural contracts via the `arche::Plugin` C++20 concept.
- Zero polymorphic overhead: static dispatch without virtual method calls.

## 2. Implementing the Plugin Interface
```cpp
import arche;

struct PhysicsPlugin {
    // 1. Mandatory unique name identifier for diagnostics
    static constexpr std::string_view name() { return "PhysicsPlugin"; }

    // 2. Mandatory build entrypoint configuring the World and Scheduler
    void build(arche::World &world, arche::Scheduler &scheduler) {
        world.resources.insert(Gravity{9.81f});
        scheduler.add_system(
            step_physics,
            arche::Access{}.reads_resource<Gravity>().writes<Position, Velocity>()
        );
    }
};

static_assert(arche::Plugin<PhysicsPlugin>);
```

## 3. Registering Plugins with the World
```cpp
arche::World world;

// Add plugin
world.add_plugin<PhysicsPlugin>();
```

## 4. Diagnostics & Duplicate Prevention
- Scheduler/World tracks installed plugin names.
- Duplicate additions are safely rejected or reported via diagnostics.
