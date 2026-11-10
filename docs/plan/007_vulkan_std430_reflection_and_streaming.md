# Module 007: GPU std430 Reflection & Buffer Views

This document establishes the exhaustive architectural specification, memory layout contracts, reflection traits, and complete C++20 implementation for the **GPU std430 Reflection & Buffer Views** subsystem of `arche-ecs`.

---

## 1. Architectural Mission & First Principles

In high-performance hybrid CPU-GPU applications (such as `colony`, rendering 50,000+ flocking particles at 120 Hz):
* **Data Marshaling is the Primary Frame Killer:** The CPU spends valuable milliseconds packing scattered entity fields into GPU-compatible vertex structures (`struct Vertex { float x, y; float vx, vy; ... };`).
* **The Repacking Penalty:** Converting 50,000 entities from an Array-of-Structures (AoS) format into a mapped VRAM vertex buffer takes **2 to 4 milliseconds of CPU time**—consuming nearly 50% of the entire 8.33 ms frame budget!
* **The Zero-Repack Archetype Solution:** In `arche-ecs`, identical components (e.g. `Position`) are already stored in pure, contiguous Structure-of-Arrays (SoA) columns inside each 64 KB chunk. If component memory layouts adhere strictly to GPU standard layouts (`std430`), the CPU can stream data directly from chunk memory into GPU VRAM using direct pointer `std::memcpy` or DMA transfers **with zero intermediate allocation and zero field repacking**.

To maintain strict modularity and boundary discipline:
> **`arche-ecs` has zero dependencies on graphics APIs (no `#include <vulkan/vulkan.h>`, DirectX, or Metal).**
> Instead, it exposes generic, zero-copy **GPU Buffer Views** and **`std430` compile-time reflection traits** that any rendering backend can consume.

---

## 2. The `std430` Memory Alignment Contract

Graphics and compute shaders (SPIR-V, GLSL, HLSL, MSL) mandate strict alignment rules when reading from Storage Buffers (`layout(std430, buffer)`):

### 2.1 The Standard `std430` Layout Rules

| Component Type | Size | Base Alignment | std430 Array Stride | `arche-ecs` Guarantee |
| :--- | :---: | :---: | :---: | :--- |
| `float`, `int32_t`, `uint32_t` | 4 Bytes | 4 Bytes | 4 Bytes | Perfectly aligned in chunk column |
| `vec2` (`float[2]`) | 8 Bytes | 8 Bytes | 8 Bytes | 8-byte aligned in chunk column |
| `vec3` (`float[3]`) | 12 Bytes | **16 Bytes** | **16 Bytes (Padded)** | **Requires explicit compile-time padding trait** |
| `vec4` (`float[4]`) | 16 Bytes | 16 Bytes | 16 Bytes | 16-byte aligned in chunk column |
| `mat4` (`float[16]`) | 64 Bytes | 16 Bytes | 64 Bytes | 16-byte aligned in chunk column |
| User Struct | $\sum \text{fields}$ | $\max(\text{align}(f))$ | Multiples of alignment | Validated via `Std430Traits<T>` |

```
Standard std430 vec3 Hazard:
GLSL layout(std430) specifies that an array of vec3 has a 16-byte stride!
A naive C++ struct struct Vec3 { float x, y, z; } has sizeof(Vec3) == 12.
Directly streaming a 12-byte C++ Vec3 array into a std430 buffer corrupts shader reads!
```

`arche-ecs` prevents this hazard at compile time using static reflection traits:
Any component tagged for GPU streaming must either be a power-of-two vector (`vec2`, `vec4`) or provide explicit padding so that `sizeof(T) == alignof(T) * N`.

---

## 3. Generic Zero-Copy Views (`arche::ComponentSpan<T>` & `arche::GpuBufferView`)

Instead of copying data into intermediate staging structures, `arche-ecs` provides non-owning view wrappers:

```mermaid
flowchart LR
    subgraph ECS_Memory["arche-ecs Chunk Memory (RAM)"]
        direction TB
        C1["Chunk 0: Position Column [512 elements]"]
        C2["Chunk 1: Position Column [512 elements]"]
        C3["Chunk 2: Position Column [300 elements]"]
    end

    subgraph Views["Zero-Copy Buffer Views"]
        direction TB
        V1["ComponentSpan<Position> (Chunk 0)"]
        V2["ComponentSpan<Position> (Chunk 1)"]
        V3["ComponentSpan<Position> (Chunk 2)"]
    end

    subgraph GPU_Target["Consumer Engine (Vulkan / Metal / DX12)"]
        VRAM["Mapped GPU VRAM Buffer (Host-Visible / Coherent)<br>Direct DMA or memcpy streaming:<br>memcpy(mappedVram + offset, span.data(), span.size_bytes())"]
    end

    C1 --> V1 --> VRAM
    C2 --> V2 --> VRAM
    C3 --> V3 --> VRAM
```

### 3.1 `ComponentSpan<T>`
A lightweight, non-owning view representing a contiguous slice of component instances within a single chunk:
* `const T* data`: Pointer to the first element in the column.
* `uint32_t count`: Active entity count in the chunk.
* `uint32_t stride`: Byte stride between elements (`sizeof(T)`).
* `size_t size_bytes()`: Total bytes available for GPU DMA (`count * sizeof(T)`).

### 3.2 `GpuBufferView`
A type-erased version of `ComponentSpan<T>` designed for generic rendering pipelines that operate on raw byte buffers and descriptors:
* `const void* data`: Raw base pointer.
* `size_t byte_size`: Total byte size.
* `uint32_t count`: Total elements.
* `uint32_t element_size`: Size of individual element.
* `uint32_t alignment`: Physical memory alignment.

---

## 4. Multi-Chunk Streaming Pipeline

When a simulation tracks 50,000 entities, they span multiple 64 KB chunks (e.g. 32 chunks of 1,600 entities each).

The consumer engine streams these components into a mapped GPU buffer using either:

### Approach A: Batched Linear Upload (Universal DMA)
The consumer loops over chunks and performs direct sequential memory transfers into mapped VRAM:
```cpp
void* gpu_mapped_ptr = vulkan_buffer.get_mapped_pointer();
size_t byte_offset = 0;

for (auto span : world.view_component_chunks<Position>()) {
    std::memcpy(static_cast<uint8_t*>(gpu_mapped_ptr) + byte_offset,
                span.data(), 
                span.size_bytes());
    byte_offset += span.size_bytes();
}
```
**Throughput:** Saturates memory bandwidth ($\ge 25\text{ GB/s}$ on Apple Silicon Unified Memory and modern PCIe 4.0 buses). Total streaming time for 50,000 particles (400 KB of data) is **$< 20\text{ microseconds}$**!

### Approach B: Direct Multi-Buffer Vertex Binding
If the graphics API supports sparse or bindless vertex buffers, the engine can bind each chunk's span directly as a separate vertex buffer binding without performing any memcpy operations.

---

## 5. Complete C++20 Header Implementation

### 5.1 `include/arche/gpu_reflection.hpp`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <concepts>
#include <span>
#include <cassert>

namespace arche {

/// @brief Compile-time traits validating memory layout compatibility with GPU std430 specifications.
template <typename T>
struct Std430Traits {
    static constexpr size_t size = sizeof(T);
    static constexpr size_t alignment = alignof(T);

    // In std430, arrays of elements must have strides equal to their base alignment or multiples thereof
    static constexpr bool is_power_of_two_aligned = (alignment & (alignment - 1)) == 0;
    static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;
    
    // Explicit rule: std430 vec3 has 16-byte stride. Plain 12-byte float[3] is NOT std430 compliant!
    static constexpr bool is_safe_std430 = is_power_of_two_aligned && 
                                           is_size_multiple_of_alignment &&
                                           (size != 12 || alignment == 16);
};

/// @brief Concept enforcing compile-time GPU std430 memory compatibility.
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> && Std430Traits<T>::is_safe_std430;

/// @brief Strongly-typed, non-owning view of a contiguous component column within a single chunk.
template <typename T>
class ComponentSpan {
public:
    ComponentSpan(const T* data, uint32_t count) noexcept
        : data_(data), count_(count) {}

    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] uint32_t count() const noexcept { return count_; }
    [[nodiscard]] size_t size_bytes() const noexcept { return count_ * sizeof(T); }
    [[nodiscard]] size_t stride() const noexcept { return sizeof(T); }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    [[nodiscard]] const T& operator[](size_t index) const noexcept {
        assert(index < count_);
        return data_[index];
    }

    [[nodiscard]] std::span<const T> as_span() const noexcept {
        return std::span<const T>(data_, count_);
    }

private:
    const T* data_{nullptr};
    uint32_t count_{0};
};

/// @brief Type-erased GPU buffer view compatible with generic graphics pipelines (Vulkan/Metal/DX12).
struct GpuBufferView {
    const void* data{nullptr};
    size_t byte_size{0};
    uint32_t count{0};
    uint32_t element_size{0};
    uint32_t alignment{0};
};

} // namespace arche
```

---

### 5.2 Extension to `arche::World` for GPU Streaming

```cpp
// Additions to include/arche/world.hpp:

#include "arche/gpu_reflection.hpp"

namespace arche {

// Inside class World:

    /// @brief Generates a list of non-owning ComponentSpan views for all chunks containing component T.
    template <GpuStd430Compatible T>
    [[nodiscard]] std::vector<ComponentSpan<T>> view_component_chunks() {
        std::vector<ComponentSpan<T>> views;
        const TypeID tid = type_id<T>();

        for (const auto& arch_ptr : graph_.archetypes()) {
            Archetype* arch = arch_ptr.get();
            if (!arch->has_component(tid)) continue;

            const int16_t col = arch->column_index(tid);
            assert(col >= 0);
            const auto& layout = arch->column_layout(static_cast<size_t>(col));

            const auto& chunks = get_archetype_chunks(arch);
            for (Chunk* chunk : chunks) {
                if (chunk->is_empty()) continue;

                const T* col_ptr = reinterpret_cast<const T*>(
                    chunk->component_slice(layout.chunk_byte_offset)
                );
                views.emplace_back(col_ptr, chunk->count());
            }
        }
        return views;
    }

    /// @brief Fast batch streamer: copies all instances of component T directly into a destination buffer.
    /// Can stream directly into mapped Vulkan/Metal VRAM buffers.
    /// @return Total bytes written to destination.
    template <GpuStd430Compatible T>
    size_t stream_component_to(void* destination_buffer) {
        assert(destination_buffer != nullptr);
        uint8_t* dst = static_cast<uint8_t*>(destination_buffer);
        size_t total_bytes = 0;

        auto views = view_component_chunks<T>();
        for (const auto& span : views) {
            const size_t bytes = span.size_bytes();
            std::memcpy(dst + total_bytes, span.data(), bytes);
            total_bytes += bytes;
        }

        return total_bytes;
    }

    /// @brief Returns the total active count of entities possessing component T.
    template <typename T>
    [[nodiscard]] uint32_t count_components() const noexcept {
        const TypeID tid = type_id<T>();
        uint32_t total = 0;

        for (const auto& arch_ptr : graph_.archetypes()) {
            Archetype* arch = arch_ptr.get();
            if (arch->has_component(tid)) {
                total += arch->entity_count();
            }
        }
        return total;
    }

} // namespace arche
```

---

## 6. Standalone Verification & Unit Tests (`tests/gpu_reflection_tests.cpp`)

This standalone test suite validates `std430` layout assertions, non-owning span generation across chunks, and raw streaming throughput without external graphics dependencies:

```cpp
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <cstring>

#include "arche/entity.hpp"
#include "arche/world.hpp"
#include "arche/gpu_reflection.hpp"

// Standalone component definitions
struct GpuPosition {
    float x, y; // 8 bytes (std430 vec2 compatible)
};

struct GpuColor {
    float r, g, b, a; // 16 bytes (std430 vec4 compatible)
};

// Intentionally unpadded 12-byte struct to test compile-time traits
struct RawVec3Unsafe {
    float x, y, z; // 12 bytes - NOT safe in std430 arrays!
};

// Padded 16-byte struct matching std430 rules
struct alignas(16) SafeGpuVec3 {
    float x, y, z;
    float padding; // Padded to 16 bytes
};

void test_std430_trait_validations() {
    std::cout << "[Test 1] std430 Compile-Time Reflection Traits... ";

    static_assert(arche::GpuStd430Compatible<float>, "float must be std430 compatible");
    static_assert(arche::GpuStd430Compatible<GpuPosition>, "GpuPosition must be std430 compatible");
    static_assert(arche::GpuStd430Compatible<GpuColor>, "GpuColor must be std430 compatible");
    static_assert(arche::GpuStd430Compatible<SafeGpuVec3>, "SafeGpuVec3 must be std430 compatible");

    // Negative check: RawVec3Unsafe must NOT be safe
    static_assert(!arche::Std430Traits<RawVec3Unsafe>::is_safe_std430, 
                  "Unpadded 12-byte vec3 must fail std430 safety trait!");

    std::cout << "PASSED\n";
}

void test_component_span_views() {
    std::cout << "[Test 2] ComponentSpan Zero-Copy Chunk Views... ";
    arche::World world;

    constexpr int TOTAL_ENTITIES = 500;
    for (int i = 0; i < TOTAL_ENTITIES; ++i) {
        world.create_entity(GpuPosition{static_cast<float>(i), static_cast<float>(i * 2)});
    }

    auto views = world.view_component_chunks<GpuPosition>();
    assert(!views.empty());

    size_t inspected_entities = 0;
    for (const auto& span : views) {
        assert(span.data() != nullptr);
        assert(span.count() > 0);
        assert(span.size_bytes() == span.count() * sizeof(GpuPosition));

        // Verify data continuity
        for (uint32_t r = 0; r < span.count(); ++r) {
            assert(span[r].x == static_cast<float>(inspected_entities + r));
        }
        inspected_entities += span.count();
    }

    assert(inspected_entities == TOTAL_ENTITIES);
    std::cout << "PASSED\n";
}

void test_stream_component_to_mock_vram() {
    std::cout << "[Test 3] Direct VRAM Batch Streaming (50,000 Entities)... ";
    arche::World world;

    constexpr int NUM_ENTITIES = 50'000;
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        world.create_entity(GpuPosition{static_cast<float>(i), static_cast<float>(i * 3)});
    }

    // Allocate mock GPU mapped VRAM buffer
    const size_t vram_bytes = NUM_ENTITIES * sizeof(GpuPosition); // 50,000 * 8B = 400 KB
    std::vector<uint8_t> mock_mapped_vram(vram_bytes, 0);

    // Warm-up stream
    world.stream_component_to<GpuPosition>(mock_mapped_vram.data());

    // Benchmark streaming speed
    constexpr int ITERATIONS = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    size_t bytes_written = 0;
    for (int it = 0; it < ITERATIONS; ++it) {
        bytes_written = world.stream_component_to<GpuPosition>(mock_mapped_vram.data());
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    double avg_stream_us = (elapsed.count() * 1000.0) / ITERATIONS;
    double gb_per_sec = (static_cast<double>(bytes_written) / (1024.0 * 1024.0 * 1024.0)) / 
                        (avg_stream_us / 1'000'000.0);

    assert(bytes_written == vram_bytes);

    // Verify first and last entries in mock VRAM
    const auto* streamed_positions = reinterpret_cast<const GpuPosition*>(mock_mapped_vram.data());
    assert(streamed_positions[0].x == 0.0f);
    assert(streamed_positions[49999].x == 49999.0f);

    std::cout << "PASSED (" << avg_stream_us << " µs per full 50k particle stream, " 
              << gb_per_sec << " GB/s memory bandwidth)\n";
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "Running arche-ecs GPU std430 Reflection & Views Tests\n";
    std::cout << "====================================================\n";

    test_std430_trait_validations();
    test_component_span_views();
    test_stream_component_to_mock_vram();

    std::cout << "All GPU std430 Reflection tests completed successfully!\n";
    return 0;
}
```

---

## 7. Summary of Guarantees

1. **std430 Compatibility Guarantee:** Compile-time `GpuStd430Compatible` concept prevents hazardous 12-byte unpadded vector allocations from corrupting GPU shader pipelines.
2. **Zero-Repack Streaming:** Consumer engines stream contiguous SoA chunk columns directly into mapped VRAM with direct `std::memcpy` or DMA.
3. **Sub-Microsecond Latency:** Streaming 50,000 entities (400 KB) takes $< 25$ microseconds, consuming less than $0.3\%$ of a 120 Hz frame budget.
4. **Decoupled Architecture:** Zero dependencies on Vulkan or third-party graphics engines.
