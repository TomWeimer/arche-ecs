```
======================================================================
   Comprehensive ECS Benchmark Suite: arche-ecs vs. Flecs vs. EnTT    
======================================================================
|               ns/op |                op/s |    err% | Scenario
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|           52,671.30 |           18,985.67 |   14.6% | 1. Simple Iteration (100k Entities: Pos + Vel)
|                     |                     |         |   • arche-ecs (Chunked SoA)           :  52.6 µs
|           27,702.70 |           36,097.56 |    1.2% |   • Flecs (Archetype Table)           :  27.7 µs
|          203,036.85 |            4,925.21 |    0.8% |   • EnTT (Sparse Set)                 : 203.0 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|           44,775.86 |           22,333.46 |    0.2% | 2. Heavy Iteration (100k Entities: 4 Components)
|                     |                     |         |   • arche-ecs (Chunked SoA)           :  44.7 µs  (~11.4x faster than EnTT)
|           45,253.19 |           22,097.89 |    0.1% |   • Flecs (Archetype Table)           :  45.2 µs  (~11.3x faster than EnTT)
|          511,566.10 |            1,954.78 |    0.2% |   • EnTT (Sparse Set)                 : 511.5 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|           24,788.46 |           40,341.35 |    0.4% | 3. Fragmented Iteration (100k across 5 Archetypes)
|                     |                     |         |   • arche-ecs (QueryCache)            :  24.7 µs  (~8.6x faster than EnTT)
|           23,157.31 |           43,182.92 |    0.1% |   • Flecs (Archetype Table)           :  23.1 µs  (~9.2x faster than EnTT)
|          214,121.77 |            4,670.24 |    4.2% |   • EnTT (Sparse Set)                 : 214.1 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|          456,148.63 |            2,192.27 |    9.2% | 4. Random Access (100k Component Lookups on Shuffled Handles)
|                     |                     |         |   • arche-ecs (SlotMap Indirection)   : 456.1 µs
|          623,663.59 |            1,603.43 |    0.4% |   • EnTT (Sparse Set Direct Lookup)   : 623.6 µs
|        1,330,420.83 |              751.64 |    3.6% |   • Flecs (Entity Index)              : 1,330.4 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|           68,125.00 |           14,678.90 |    0.3% | 5. Entity Creation (20,000 Entities: Pos + Vel)
|                     |                     |         |   • arche-ecs (Chunked Bulk Spawn)    :  68.1 µs  (14.7k creations/s)
|          488,522.20 |            2,046.99 |    2.2% |   • EnTT (Registry Emplace)           : 488.5 µs  (2.0k creations/s)
|        5,469,102.00 |              182.85 |    0.8% |   • Flecs (Table Insertion)          : 5,469.1 µs (182 creations/s)
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|          470,940.69 |            2,123.41 |    0.3% | 6. Structural Mutation: Add Component (20k Entities)
|                     |                     |         |   • EnTT (Sparse Pool Append)         : 470.9 µs  (No entity data moved)
|        1,033,931.93 |              967.18 |    0.2% |   • arche-ecs (Archetype Migration)   : 1,033.9 µs (Moves rows to new chunk)
|        5,401,732.76 |              185.13 |    0.6% |   • Flecs (Table Transition)          : 5,401.7 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|          693,994.28 |            1,440.93 |    0.5% | 7. Structural Mutation: Remove Component (20k Entities)
|                     |                     |         |   • EnTT (Sparse Pool Pop)            : 693.9 µs
|        1,000,145.14 |              999.85 |    0.2% |   • arche-ecs (Archetype Migration)   : 1,000.1 µs
|        6,460,262.93 |              154.79 |    0.7% |   • Flecs (Table Transition)          : 6,460.2 µs
|--------------------:|--------------------:|--------:|:-----------------------------------------------------
|          843,573.73 |            1,185.43 |    0.5% | 8. Entity Destruction (20,000 Entities Despawn)
|                     |                     |         |   • arche-ecs (Chunk Swap-Remove)     : 843.5 µs
|          903,741.96 |            1,106.51 |    0.5% |   • EnTT (Sparse Pool Swap-Pop)       : 903.7 µs
|        5,297,790.24 |              188.76 |    0.5% |   • Flecs (Table Swap-Remove)         : 5,297.8 µs
======================================================================
```