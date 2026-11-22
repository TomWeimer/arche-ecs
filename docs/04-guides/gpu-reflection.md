# Guide: GPU Reflection & Buffer Streaming

> **Status:** Documentation Skeleton (To be expanded)

## 1. Contiguous Component Columns
- Direct pointer extraction from contiguous archetype chunk memory.
- Zero-copy transfers to graphics APIs (Vulkan staging buffers, `glBufferSubData`, Metal blit encoders).

## 2. Std430 Layout Validation
- `Std430Traits<T>`: Validates size, alignment, and power-of-two constraints at compile time.
- `GpuStd430Compatible` concept: Rejects GPU bus misalignment hazards (e.g. unpadded 12-byte `vec3`).

## 3. Type-Erased GPU Buffer Descriptors
- `GpuBufferView`: Encapsulates raw data pointer, byte size, item count, element stride, and alignment.
- Decouples graphics backend pipelines from compile-time C++ ECS component types.

## 4. Vulkan-Compatible Indirect Draw
- `DrawIndirectCommand`: 16-byte structure binary-compatible with `VkDrawIndirectCommand`.
- Directly populated from chunk entity counts for GPU-driven multi-draw calls.

## 5. Using GpuFacade
```cpp
// Inspect contiguous chunk slices
std::vector<arche::ComponentSpan<Position>> chunks = world.gpu.view_chunks<Position>();

for (const auto &span : chunks) {
    upload_to_vbo(vbo_id, span.data(), span.size_bytes());
}

// Stream directly into a single mapped destination buffer
world.gpu.stream_to<Position>(destination_memory_buffer);
```
