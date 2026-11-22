# Guide: GPU Reflection & Buffer Streaming

> **Status:** Documentation Skeleton (To be expanded)

## 1. Contiguous Component Columns for GPU Uploads
- Direct pointer extraction from archetype chunk memory.
- Zero copy transformations needed before `glBufferSubData` / Vulkan staging buffers.

## 2. Using GpuFacade
```cpp
// Inspect contiguous chunk slices
std::vector<arche::ComponentSpan<Position>> chunks = world.gpu.view_chunks<Position>();

for (const auto &span : chunks) {
    upload_to_vbo(vbo_id, span.data, span.count * sizeof(Position));
}

// Or stream directly into a single continuous buffer
world.gpu.stream_to<Position>(destination_memory_buffer);
```
