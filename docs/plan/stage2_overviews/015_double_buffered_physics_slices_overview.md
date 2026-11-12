# Module 015: Double-Buffered Physics Component Slices — Overview

## 1. Architectural Mission
In high-density flocking and swarm simulations, step $t+1$ depends on the state of all neighbors at step $t$.

### The In-Place Mutation Hazard
If boid positions are updated in-place during chunk iteration:
1. **Order-Dependent Simulation Bias:** Boids processed later in the chunk loop read the updated $t+1$ positions of earlier boids instead of their $t$ positions, introducing artificial steering asymmetry and numerical drift.
2. **Multi-Threaded Race Conditions:** In parallel execution, when Thread 1 reads neighbor positions while Thread 2 is writing updated positions to the same chunk, a data race occurs.

The mission of Module 015 is to introduce **Double-Buffered Component Columns**:
- Allocate dual contiguous column buffers (`Buffer A` Read / `Buffer B` Write) inside chunks for tagged components.
- Worker threads read neighbor positions from A and write updated positions to B with zero data races and zero mutexes.
- At the end of the physics sub-step, a single $O(1)$ pointer flip swaps the read and write slices.

---

## 2. Key Capabilities & Interfaces

### 2.1 Double-Buffered Component Registration
```cpp
// Registers component T with dual column slices in chunk layouts
world.register_double_buffered_component<Position>();
```
- Allocates two identical contiguous memory slices in chunk layout: `slice_A` and `slice_B`.
- Doubles the memory footprint *only* for explicitly designated components (e.g. `Position`). Non-physics components (e.g. `Color`, `TagBoid`) remain single-buffered.

### 2.2 Dual Slices Query Access
```cpp
// Explicit read-state vs write-target access:
world.query<const Position&, PositionWriteTarget, Velocity>(...);
```
- Const references bind to the current `Read` slice.
- Write target references bind to the current `Write` slice.

### 2.3 $O(1)$ Pointer-Flip Physics Sub-Steps
```cpp
// At the end of the physics sub-step:
world.flip_double_buffer<Position>();
```
- Swaps the `read_offset` and `write_offset` values in the Archetype layout table in $O(1)$ time.
- Zero bytes copied; zero entity relocations.

---

## 3. Subsystem Modifications
- **`Archetype` (`archetype.hpp`):** Support double-buffered column layouts with interchangeable read/write byte offsets.
- **`Chunk` (`chunk.hpp`):** Provide `read_slice(col)` and `write_slice(col)` accessors.
- **`World` (`world.hpp`):** Expose `register_double_buffered_component<T>()` and `flip_double_buffer<T>()`.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Zero data races between read and write physics slices; $O(1)$ pointer swap latency; zero per-entity allocation overhead.
- **Performance Budget:** `flip_double_buffer` executes in $< 10\text{ ns}$ across all archetypes.

---

## 5. Verification & Testing Strategy
- **`double_buffer_tests`:**
  - Verify that reads and writes target distinct physical memory addresses.
  - Verify exact $O(1)$ pointer flipping behavior across multiple consecutive frames.
  - Stress-test multi-threaded read/write safety using ThreadSanitizer.

