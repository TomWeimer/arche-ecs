# Memory Architecture & 64 KB Chunks

> **Status:** Documentation Skeleton (To be expanded)

## 1. Fixed-Size Chunk Page Allocation
- Each chunk is allocated as an aligned 64 KB memory block from `ChunkPool`.
- Cache-line alignment (64-byte / 128-byte) for CPU hardware prefetchers.

## 2. Structure of Arrays (SoA) Column Packing
- Header layout: entity count, capacity, column offsets, version ticks.
- Dense column slices: `Entity` handles array followed by contiguous component arrays.
- Zero gap packing guaranteeing maximum SIMD vector lane utilization.

## 3. Chunk Recycling & Pool Management
- Free chunk buffers are recycled back to `ChunkPool` without OS heap deallocations.
