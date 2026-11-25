# Arche ECS — Comparative Benchmark Results

> **System Configuration:** Apple Silicon (ARM64), Clang C++20, `-O3 -DNDEBUG`
> **Automated Export:** `36` microbenchmark configurations serialized.

## 1. Memory Footprint & Overhead per Entity (1,000,000 Entities)

| Engine & Storage Architecture | Total Allocated | Bytes / Entity | Overhead vs Payload (16 B) | Packing Efficiency |
| :--- | :---: | :---: | :---: | :---: |
| **`arche-ecs (64KB Chunk Pages)`** | **38.73 MB** | **40.6 B** | **+24.6 B** | **39.4%** |
| **`EnTT (Dense Sparse Pools)`** | 50.79 MB | 53.3 B | +37.3 B | 30.0% |
| **`Flecs (Table Blocks)`** | 90.17 MB | 94.5 B | +78.5 B | 16.9% |

## 3. 2M Entities: Multi-Threaded Scaling (1, 2, 4, 8 Threads)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (1-Thread query_parallel)` | **965.8 µs** | 965.83 |
| `Flecs (1-Thread multi_threaded)` | **530.2 µs** | 530.20 |
| `arche-ecs (2-Thread query_parallel)` | **369.4 µs** | 369.44 |
| `Flecs (2-Thread multi_threaded)` | **538.5 µs** | 538.51 |
| `arche-ecs (4-Thread query_parallel)` | **295.1 µs** | 295.15 |
| `Flecs (4-Thread multi_threaded)` | **998.7 µs** | 998.66 |
| `arche-ecs (8-Thread query_parallel)` | **373.7 µs** | 373.68 |
| `Flecs (8-Thread multi_threaded)` | **462.8 µs** | 462.76 |

## 4. Wide Entity Simulation (500k Entities: 8 Components)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Chunked SoA)` | **978.7 µs** | 978.73 |
| `arche-ecs (range-for idiomatic)` | **1.05 ms** | 1053.59 |
| `Flecs (Table iter batch)` | **741.5 µs** | 741.52 |
| `Flecs (each idiomatic)` | **723.7 µs** | 723.74 |
| `EnTT (8 Sparse Pools)` | **6.30 ms** | 6297.46 |

## 6. Spatial Locality Query Speedup (500k Entities)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Before Sort: Random Coordinates)` | **133.0 µs** | 132.99 |
| `arche-ecs (After Sort: Spatially Monotonic)` | **132.4 µs** | 132.45 |

## 8. Exclusion Filter Query (Without<FrozenTag>, 100k Entities)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Query Bitmask Exclusion)` | **12.1 µs** | 12.14 |
| `Flecs (Table iter batch)` | **11.4 µs** | 11.42 |
| `Flecs (each idiomatic)` | **11.4 µs** | 11.41 |
| `EnTT (View with Exclude)` | **202.9 µs** | 202.89 |

## 12. Entity Destruction (20k Entities Despawn)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Chunk Swap-Remove)` | **7.53 ms** | 7534.02 |
| `EnTT (Sparse Pool Swap-Pop)` | **719.0 µs** | 719.04 |
| `Flecs (Table Swap-Remove)` | **5.18 ms** | 5182.56 |

## 13. Full-Frame 10-System Game Loop (100k Entities)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Scheduler 8 Threads)` | **767.2 µs** | 767.23 |
| `arche-ecs (Scheduler 1 Thread)` | **755.5 µs** | 755.54 |
| `Flecs (Pipeline 8 Threads)` | **349.6 µs** | 349.55 |
| `Flecs (Pipeline 1 Thread)` | **315.8 µs** | 315.76 |
| `EnTT (Sequential 1 Thread)` | **842.5 µs** | 842.55 |

## 16. Sparse Activity (500k Entities, 5% Mutated)

| Benchmark Candidate | Latency / op | Normalized (µs) |
| :--- | :---: | :---: |
| `arche-ecs (Mutated<Position> chunk skipping)` | **1.0 µs** | 1.04 |
| `arche-ecs (Full Scan baseline without filter)` | **139.7 µs** | 139.71 |
| `Flecs (Filter iter.changed() table skipping)` | **779.1 µs** | 779.06 |
| `Flecs (Full Scan baseline without filter)` | **120.0 µs** | 119.98 |
| `EnTT (ActiveFlag Sparse View 5%)` | **69.9 µs** | 69.92 |
| `EnTT (Full Scan baseline 500k)` | **901.9 µs** | 901.86 |

## 18. Multi-Archetype Alternating Churn (50k Entities)

| Benchmark Candidate | Latency / op | Normalized (ms) |
| :--- | :---: | :---: |
| `arche-ecs (4-Way Cycle)` | **23.05 ms** | 23.05 |
| `Flecs (4-Way Cycle)` | **8.04 ms** | 8.04 |
| `EnTT (4-Way Cycle)` | **875.5 µs** | 0.88 |

