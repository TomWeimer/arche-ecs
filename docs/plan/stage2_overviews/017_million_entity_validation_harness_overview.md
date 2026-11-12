# Module 017: 1M+ Scale Validation & Profiling Harness — Overview

## 1. Architectural Mission
Scaling to 1,000,000+ entities pushes the boundaries of system memory bandwidth, hardware cache eviction, and TLB hit rates.

The mission of Module 017 is to build an **exhaustive, cycle-accurate validation and profiling harness** specifically designed to stress-test `arche-ecs` at 1M to 2M entity scale:
- Measure hardware time-stamp counters across 1M entities under multi-threaded chunk execution.
- Quantify cache miss rates and cycles/entity before and after archetype permutation.
- Validate that the **$\le 533$ CPU cycles/entity budget** is easily satisfied across full frames at 120 Hz on multi-core hardware.
- Verify 64-bit handle safety, bulk allocation speed, and triple-buffered VRAM streaming bandwidth.

---

## 2. Key Capabilities & Benchmarks

### 2.1 Multi-Threaded 1,000,000 Entity Physics Benchmark
- Spawns 1,000,000 entities across ~625 chunks.
- Evaluates `parallel_query` across 1, 2, 4, 8, and 16 worker threads.
- Measures frametime, cycles per entity, and multi-core scaling efficiency.

### 2.2 Cache Locality: Pre-Sort vs. Post-Sort Profiling
- Generates a simulated spatial scatter where entities physically close in space are randomized in chunk memory.
- Measures neighbor access latency (simulating Colony's flocking radius checks).
- Applies `world.apply_permutation` (simulating Morton Z-order sorting) and re-evaluates neighbor access latency.
- Demonstrates a measurable $2\times \text{ to } 4\times$ cache miss reduction and latency drop.

### 2.3 Massive Wave Bulk Lifecycle Stress
- Spawns 1,000,000 entities in bulk via `world.create_entities`.
- Despawns 500,000 entities via `world.destroy_entities`.
- Validates that surviving entities remain completely intact and that `SlotMap` free-lists and chunk pools remain uncorrupted.
- Validates that 64-bit generation counters never wrap or cause ABA collisions.

### 2.4 Triple-Buffered VRAM Streaming Throughput
- Streams 1,000,000 entity positions (8 MB payload) into a 3-ring mapped VRAM buffer.
- Measures sustained memory copy bandwidth (GB/s) and frame completion latency.

---

## 3. Subsystem Modifications
- **`benchmarks/arche_benchmarks.cpp`:** Expand harness with multi-threaded scaling test suites (50k, 100k, 500k, 1M, and 2M entities).
- **`tests/stage2_validation_tests.cpp`:** Regression test suite verifying that all Stage 1 invariants remain unbroken under Stage 2 concurrency and 64-bit handles.
- **`CMakeLists.txt`:** Register multi-threaded benchmark and test targets.

---

## 4. Invariants & Performance Target
- **Hard Invariants:** Cycle budget verification ($\le 533$ cycles/entity); 0 memory leaks across 1M entity allocations/deallocations; 0 race conditions.
- **Performance Budget:** 
  - 1,000,000 entities trivial integration: $< 0.1\text{ ms}$ on 8-core CPU ($< 0.35$ normalized cycles/entity).
  - Bulk spawn of 1,000,000 entities: $< 25\text{ ms}$.
  - VRAM streaming of 1,000,000 entities (8 MB): $< 0.5\text{ ms}$.

---

## 5. Verification & Testing Strategy
- Run automated benchmarks reporting:
  - Frametime at 50k, 100k, 500k, 1M, and 2M entities.
  - Multi-threaded scaling factor vs single-thread baseline.
  - VRAM streaming throughput for 1,000,000 entities (8 MB payload).
  - Pre-sort vs. post-sort latency drop.

