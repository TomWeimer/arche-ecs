# Module 018 Overview: Unified Public Facade & Clean Consumer API

## 1. Problem Statement & Motivation
Across Modules 001 through 017, features were designed incrementally. While functionally complete, the public method names on `World` and `CommandBuffer` accumulated verbosity, redundant nouns, and mental friction:
* `create_entity` vs `create_entities` differed by a single letter `'s'`.
* Repetitive nouns: `get_component<T>()`, `has_component<T>()`, `add_component<T>()`, `remove_component<T>()`.
* Clunky, hard-to-retain identifiers: `playback_parallel_commands()`, `register_double_buffered_component<T>()`, `flip_double_buffer<T>()`.
* Multi-threaded execution was disconnected from standard query vocabulary (`parallel_query` vs `query`).
* Consumers lacked a single, canonical umbrella header (`<arche/arche.hpp>`) and had to manually include 10+ internal headers.

Module 018 establishes the **official, modernized public API surface of `arche-ecs`**, providing short, expressive active verbs, symmetrical lifecycle methods, and a single umbrella include while maintaining 100% backward compatibility via inline deprecated aliases.

---

## 2. Key Architecture & API Deltas

### 2.1 The Modernized Public Surface
* **Symmetrical Spawning & Despawning:**
  * `world.spawn(comps...)` (replaces `create_entity`)
  * `world.spawn_batch(count, ...)` (replaces `create_entities`)
  * `world.despawn(entity)` (replaces `destroy_entity`)
  * `world.despawn_batch(span)` (replaces `destroy_entities`)
* **Concise Component Access:**
  * `world.get<T>(entity)`, `world.has<T>(entity)`, `world.add<T>(entity, val)`, `world.remove<T>(entity)`
* **Symmetrical Query Execution:**
  * `world.query<Components...>()` for single-threaded chunk iteration.
  * `world.query_parallel<Components...>(pool, fn)` and `world.query_parallel<Components...>(pool, cmds, fn)` for multi-threaded chunk work-stealing.
* **Unified Command Playback:**
  * `world.commit(cmd_buffers)` (replaces `playback_parallel_commands`).
* **Simulation Ticking & Double-Buffering:**
  * `world.tick()` (replaces `advance_tick`).
  * `world.enable_double_buffer<T>()` and `world.flip<T>()`.
* **Locality & GPU Operations:**
  * `world.sort<Comp>(arch, comp)`, `world.permute(arch, indices)`.
  * `world.stream_gpu<T>(...)`, `world.indirect_draw(...)`.
  * `world.save_snapshot(...)`, `world.load_snapshot(...)`.

### 2.2 Umbrella Header: `<arche/arche.hpp>`
Provides all public types and functions in a single, lightweight C++20 header with zero heavy STL dependencies:
* `namespace arche`: `World`, `Entity`, `null_entity`, `Query`, `Mutated`, `Added`, `Write`, `CommandBuffer`, `ParallelCommandBuffer`, `ThreadPool`, `DrawIndirectCommand`, `SnapshotHeader`.

### 2.3 100% Backward Compatibility
All previous Stage 1 and Stage 2 method names remain supported as `[[deprecated]]` inline forwarding aliases on `World` and `CommandBuffer`. Existing unit tests and benchmarks compile and pass without modification.

---

## 3. Deliverables

* **`include/arche/arche.hpp`:** Single public umbrella header.
* **`include/arche/world.hpp` & `include/arche/command_buffer.hpp`:** Implementation of clean API methods and deprecated legacy bridges.
* **`examples/colony_simulation_loop.cpp`:** Runnable, end-to-end 100k-entity Colony swarm simulation loop using the clean API.
* **`tests/public_api_tests.cpp`:** Dedicated test suite verifying that all modernized methods and deprecated bridges work seamlessly.

