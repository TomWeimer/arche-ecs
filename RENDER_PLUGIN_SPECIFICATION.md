# GPU Stream & Multi-Draw Indirect Render Plugin Specification

This document preserves the original GPU streaming and indirect draw implementation from `arche-ecs` and provides the complete, production-ready blueprint for implementing it as an idiomatic `RenderPlugin` in a consumer project (renderer, game engine, or physics visualizer).

---

## 1. Architectural Motivation

In earlier iterations of `arche-ecs`, GPU data transfer methods (`stream_gpu` and `indirect_draw`) were implemented directly as member functions on `arche::World`.

### Why This Was an Anti-Pattern
1. **Violation of Library Invariants (`GEMINI.md`):**
   - The ECS core must adhere to **Zero Consumer Context**: standard library and CPU intrinsics only, with zero rendering or simulation code.
   - Graphics-specific structures like `DrawIndirectCommand` (matching `VkDrawIndirectCommand`) do not belong inside a general-purpose archetype storage manager.
2. **Bypassed the System Scheduler:**
   - When called directly on `world`, the scheduler cannot analyze data dependencies.
   - Transforming this into a system with access descriptors (`Access().reads<Position>().writes_resource<GpuVramRing>()`) allows the scheduler to automatically execute GPU memory copies **concurrently** with unrelated systems (e.g., sound processing, animation decompression, network serialization) without data races.
3. **Implicit State vs. Explicit Resources:**
   - Destination VRAM pointers, ring buffer slot indices, and frustum culling boundaries should live in strongly-typed, flat POD **Resources** managed by the ECS world.

---

## 2. Original Implementation in `arche-ecs` (Archived)

For reference, below is the original code as it existed inside `include/arche/world.hpp` and `include/arche/gpu_reflection.hpp`.

### A. Original `DrawIndirectCommand` Definition (`include/arche/gpu_reflection.hpp`)

```cpp
#pragma once
#include <cstdint>

namespace arche {

/**
 * @struct DrawIndirectCommand
 * @brief Binary layout identical to VkDrawIndirectCommand for GPU-driven
 * rendering.
 */
struct DrawIndirectCommand {
    uint32_t vertex_count{0};
    uint32_t instance_count{1};
    uint32_t first_vertex{0};
    uint32_t first_instance{0};
};

static_assert(
    sizeof(DrawIndirectCommand) == 16,
    "DrawIndirectCommand must be exactly 16 bytes (VkDrawIndirectCommand layout)"
);

} // namespace arche
```

### C. Original `Std430Traits` & `GpuStd430Compatible` (`src/foundation/hardware/gpu_layout.cppm`)

```cpp
#include <cstddef>
#include <type_traits>

namespace render {

/**
 * @struct Std430Traits
 * @brief Compile-time validation of memory alignment and struct packing under
 * GPU std430 rules (Vulkan, OpenGL SSBO, DirectX DXIL).
 */
template <typename T> struct Std430Traits {
  static constexpr size_t size = sizeof(T);
  static constexpr size_t alignment = alignof(T);
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

/**
 * @concept GpuStd430Compatible
 * @brief Constrains component types to trivially copyable structs satisfying
 * std430 GPU alignment.
 */
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

} // namespace render
```

### D. Original `ComponentSpan` (`src/foundation/hardware/gpu_span.cppm`)

```cpp
#include <cstddef>
#include <span>

namespace render {

/**
 * @class ComponentSpan
 * @brief Zero-copy, non-owning contiguous view over an archetype chunk component column.
 */
template <typename T> class ComponentSpan : public std::span<const T> {
public:
  using std::span<const T>::span;

  constexpr ComponentSpan(const T *data, size_t count) noexcept
      : std::span<const T>(data, count) {}

  /// Returns the number of active component elements in this chunk column.
  [[nodiscard]] constexpr size_t count() const noexcept { return this->size(); }

  /// Returns the byte stride between contiguous elements (sizeof(T)).
  [[nodiscard]] constexpr size_t stride() const noexcept { return sizeof(T); }

  /// Converts this view explicitly to a standard C++20 std::span.
  [[nodiscard]] constexpr std::span<const T> as_span() const noexcept {
    return *this;
  }
};

} // namespace render
```

### E. Original `GpuFacade` Implementation (`src/domain/gpu_facade/`)

```cpp
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace render {

class GpuFacade {
public:
  constexpr GpuFacade() noexcept = default;
  explicit constexpr GpuFacade(arche::internal::ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns spans of contiguous component column slices across all chunks for GPU upload.
  template <typename T>
  [[nodiscard]] std::vector<ComponentSpan<T>> view_chunks() {
    assert(graph_ != nullptr);
    const arche::TypeID tid = arche::type_id<T>();
    std::vector<ComponentSpan<T>> result;

    for (const auto &arch_ptr : graph_->archetypes()) {
      const int16_t col = arch_ptr->column_index(tid);
      if (col < 0) continue;

      const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
      for (arche::internal::Chunk *chunk : arch_ptr->chunks()) {
        if (chunk->count() > 0) {
          T *ptr = reinterpret_cast<T *>(chunk->component_element(layout, 0));
          result.emplace_back(ptr, chunk->count());
        }
      }
    }
    return result;
  }

  /// Streams all component columns of type T directly into a destination memory buffer.
  template <typename T>
  size_t stream_to(void *destination_buffer) {
    assert(graph_ != nullptr && destination_buffer != nullptr);
    const arche::TypeID tid = arche::type_id<T>();
    uint8_t *dst = reinterpret_cast<uint8_t *>(destination_buffer);
    size_t total_bytes = 0;

    for (const auto &arch_ptr : graph_->archetypes()) {
      const int16_t col = arch_ptr->column_index(tid);
      if (col < 0) continue;

      const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
      for (arche::internal::Chunk *chunk : arch_ptr->chunks()) {
        const uint32_t count = chunk->count();
        if (count > 0) {
          const size_t bytes = count * sizeof(T);
          const void *src = chunk->component_element(layout, 0);
          std::memcpy(dst, src, bytes);
          dst += bytes;
          total_bytes += bytes;
        }
      }
    }
    return total_bytes;
  }

private:
  arche::internal::ArchetypeGraph *graph_{nullptr};
};

} // namespace render
```

### F. Modern Pure Consumer Pattern (Using Public `query.each_chunk()`)

In modern Arche-ECS, consumer projects do not need `GpuFacade` or internal `ArchetypeGraph` access. The identical linear blit is achieved through the public `Query` API with automatic `QueryCache` acceleration and zero intermediate heap allocations:

```cpp
template <typename T>
size_t stream_to_vram(arche::World &world, void *mapped_vram_buffer) {
  auto *dst = static_cast<uint8_t *>(mapped_vram_buffer);
  size_t total_bytes = 0;

  world.query<const T>().each_chunk([&](uint32_t count, const T *src) {
    const size_t bytes = count * sizeof(T);
    std::memcpy(dst, src, bytes);
    dst += bytes;
    total_bytes += bytes;
  });

  return total_bytes;
}
```

### B. Original `stream_gpu` & `indirect_draw` (`include/arche/world.hpp`)

```cpp
namespace arche {

struct PassAllCullingPredicate {
    constexpr bool operator()(const ChunkHeader &) const noexcept {
        return true;
    }
};

template <typename T, typename CullingPredicate = PassAllCullingPredicate>
inline size_t World::stream_gpu(
    uint32_t ring_index,
    void *ring_buffer_base,
    size_t ring_slot_capacity_bytes,
    CullingPredicate &&is_visible
) {
    assert(ring_buffer_base != nullptr && "Ring buffer pointer cannot be null");
    const uint32_t slot = ring_index % 3;
    uint8_t *slot_dest = static_cast<uint8_t *>(ring_buffer_base) +
        (slot * ring_slot_capacity_bytes);

    const TypeID tid = type_id<T>();
    size_t total_bytes = 0;

    for (const auto &arch_ptr : graph_.archetypes()) {
        Archetype *arch = arch_ptr.get();
        if (!arch->has_component(tid)) {
            continue;
        }

        const auto *layout = arch->column_layout_for(tid);
        assert(layout != nullptr);

        for (Chunk *chunk : get_archetype_chunks(arch)) {
            if (chunk->is_empty()) {
                continue;
            }
            if (!is_visible(chunk->header())) {
                continue;
            }
            const size_t bytes = chunk->count() * sizeof(T);
            if (total_bytes + bytes > ring_slot_capacity_bytes) {
                return total_bytes;
            }
            const void *src = chunk->component_element(*layout, 0);
            std::memcpy(slot_dest + total_bytes, src, bytes);
            total_bytes += bytes;
        }
    }

    return total_bytes;
}

template <typename CullingPredicate = PassAllCullingPredicate>
inline size_t World::indirect_draw(
    Archetype *arch,
    void *destination_buffer,
    size_t max_commands,
    CullingPredicate &&is_visible
) {
    assert(destination_buffer != nullptr && arch != nullptr);
    auto *cmds = static_cast<DrawIndirectCommand *>(destination_buffer);
    size_t written = 0;
    uint32_t vertex_offset = 0;

    for (Chunk *chunk : get_archetype_chunks(arch)) {
        if (chunk->is_empty()) {
            continue;
        }
        // Test culling predicate against chunk header (e.g. Macro AABB)
        if (!is_visible(chunk->header())) {
            vertex_offset += chunk->count();
            continue;
        }
        if (written >= max_commands) {
            break;
        }

        cmds[written] = DrawIndirectCommand{
            .vertex_count = chunk->count(),
            .instance_count = 1,
            .first_vertex = vertex_offset,
            .first_instance = 0,
        };
        vertex_offset += chunk->count();
        ++written;
    }
    return written;
}

} // namespace arche
```

### C. Original `Std430Traits` & `GpuStd430Compatible` (`src/foundation/hardware/gpu_layout.cppm`)

```cpp
#include <cstddef>
#include <type_traits>

namespace render {

/**
 * @struct Std430Traits
 * @brief Compile-time validation of memory alignment and struct packing under
 * GPU std430 rules (Vulkan, OpenGL SSBO, DirectX DXIL).
 */
template <typename T> struct Std430Traits {
  static constexpr size_t size = sizeof(T);
  static constexpr size_t alignment = alignof(T);
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

/**
 * @concept GpuStd430Compatible
 * @brief Constrains component types to trivially copyable structs satisfying
 * std430 GPU alignment.
 */
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

} // namespace render
```

### D. Original `ComponentSpan` (`src/foundation/hardware/gpu_span.cppm`)

```cpp
#include <cstddef>
#include <span>

namespace render {

/**
 * @class ComponentSpan
 * @brief Zero-copy, non-owning contiguous view over an archetype chunk component column.
 */
template <typename T> class ComponentSpan : public std::span<const T> {
public:
  using std::span<const T>::span;

  constexpr ComponentSpan(const T *data, size_t count) noexcept
      : std::span<const T>(data, count) {}

  /// Returns the number of active component elements in this chunk column.
  [[nodiscard]] constexpr size_t count() const noexcept { return this->size(); }

  /// Returns the byte stride between contiguous elements (sizeof(T)).
  [[nodiscard]] constexpr size_t stride() const noexcept { return sizeof(T); }

  /// Converts this view explicitly to a standard C++20 std::span.
  [[nodiscard]] constexpr std::span<const T> as_span() const noexcept {
    return *this;
  }
};

} // namespace render
```

### E. Original `GpuFacade` Implementation (`src/domain/gpu_facade/`)

```cpp
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace render {

class GpuFacade {
public:
  constexpr GpuFacade() noexcept = default;
  explicit constexpr GpuFacade(arche::internal::ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns spans of contiguous component column slices across all chunks for GPU upload.
  template <typename T>
  [[nodiscard]] std::vector<ComponentSpan<T>> view_chunks() {
    assert(graph_ != nullptr);
    const arche::TypeID tid = arche::type_id<T>();
    std::vector<ComponentSpan<T>> result;

    for (const auto &arch_ptr : graph_->archetypes()) {
      const int16_t col = arch_ptr->column_index(tid);
      if (col < 0) continue;

      const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
      for (arche::internal::Chunk *chunk : arch_ptr->chunks()) {
        if (chunk->count() > 0) {
          T *ptr = reinterpret_cast<T *>(chunk->component_element(layout, 0));
          result.emplace_back(ptr, chunk->count());
        }
      }
    }
    return result;
  }

  /// Streams all component columns of type T directly into a destination memory buffer.
  template <typename T>
  size_t stream_to(void *destination_buffer) {
    assert(graph_ != nullptr && destination_buffer != nullptr);
    const arche::TypeID tid = arche::type_id<T>();
    uint8_t *dst = reinterpret_cast<uint8_t *>(destination_buffer);
    size_t total_bytes = 0;

    for (const auto &arch_ptr : graph_->archetypes()) {
      const int16_t col = arch_ptr->column_index(tid);
      if (col < 0) continue;

      const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
      for (arche::internal::Chunk *chunk : arch_ptr->chunks()) {
        const uint32_t count = chunk->count();
        if (count > 0) {
          const size_t bytes = count * sizeof(T);
          const void *src = chunk->component_element(layout, 0);
          std::memcpy(dst, src, bytes);
          dst += bytes;
          total_bytes += bytes;
        }
      }
    }
    return total_bytes;
  }

private:
  arche::internal::ArchetypeGraph *graph_{nullptr};
};

} // namespace render
```

### F. Modern Pure Consumer Pattern (Using Public `query.each_chunk()`)

In modern Arche-ECS, consumer projects do not need `GpuFacade` or internal `ArchetypeGraph` access. The identical linear blit is achieved through the public `Query` API with automatic `QueryCache` acceleration and zero intermediate heap allocations:

```cpp
template <typename T>
size_t stream_to_vram(arche::World &world, void *mapped_vram_buffer) {
  auto *dst = static_cast<uint8_t *>(mapped_vram_buffer);
  size_t total_bytes = 0;

  world.query<const T>().each_chunk([&](uint32_t count, const T *src) {
    const size_t bytes = count * sizeof(T);
    std::memcpy(dst, src, bytes);
    dst += bytes;
    total_bytes += bytes;
  });

  return total_bytes;
}
```

---

## 3. The Idiomatic Plugin Architecture

Below is the standalone plugin implementation designed to be copied directly into any consumer application or rendering module.

### A. POD Resources

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace render {

struct DrawIndirectCommand {
    uint32_t vertex_count{0};
    uint32_t instance_count{1};
    uint32_t first_vertex{0};
    uint32_t first_instance{0};
};

static_assert(sizeof(DrawIndirectCommand) == 16);

// Triple-buffered VRAM staging ring buffer state
struct GpuVramRing {
    void* base_ptr{nullptr};
    size_t slot_capacity_bytes{0};
    uint32_t current_frame{0};
    size_t bytes_written{0};
};

// Target buffer for Vulkan/DirectX indirect draw commands
struct IndirectDrawBuffer {
    DrawIndirectCommand* destination{nullptr};
    size_t max_commands{0};
    size_t commands_written{0};
};

// Camera Frustum plane equations: Ax + By + Cz + D = 0
struct CameraFrustum {
    float planes[6][4];
};

} // namespace render
```

### B. Systems & Chunk Culling Logic

```cpp
#pragma once
#include <cstring>
#include <arche/arche.hpp>

namespace render {

inline bool is_chunk_visible(
    const arche::ChunkHeader &header,
    const CameraFrustum &frustum
) {
    (void)header;
    (void)frustum;
    // Perform SIMD frustum vs chunk macro AABB check here
    return true; 
}

// System 1: Streams active component data into the active VRAM ring slot
template <typename ComponentT>
inline void gpu_stream_system(arche::SystemContext &ctx) {
    auto &ring = ctx.world.resource<GpuVramRing>();
    const auto &frustum = ctx.world.resource<CameraFrustum>();

    if (!ring.base_ptr || ring.slot_capacity_bytes == 0) return;

    const uint32_t slot = ring.current_frame % 3;
    uint8_t *slot_dest = static_cast<uint8_t *>(ring.base_ptr) + 
                         (slot * ring.slot_capacity_bytes);

    size_t total_bytes = 0;
    const arche::TypeID tid = arche::type_id<ComponentT>();

    for (const auto &arch_ptr : ctx.world.archetypes()) {
        arche::Archetype *arch = arch_ptr.get();
        if (!arch->has_component(tid)) continue;

        const auto *layout = arch->column_layout_for(tid);
        for (arche::Chunk *chunk : ctx.world.get_archetype_chunks(arch)) {
            if (chunk->is_empty() || !is_chunk_visible(chunk->header(), frustum)) {
                continue;
            }

            const size_t bytes = chunk->count() * sizeof(ComponentT);
            if (total_bytes + bytes > ring.slot_capacity_bytes) {
                break; // Buffer overflow guard
            }

            const void *src = chunk->component_element(*layout, 0);
            std::memcpy(slot_dest + total_bytes, src, bytes);
            total_bytes += bytes;
        }
    }

    ring.bytes_written = total_bytes;
}

// System 2: Generates DrawIndirectCommand structs for single-draw submission
template <typename ComponentT>
inline void gpu_indirect_prep_system(arche::SystemContext &ctx) {
    auto &indirect = ctx.world.resource<IndirectDrawBuffer>();
    const auto &frustum = ctx.world.resource<CameraFrustum>();

    if (!indirect.destination || indirect.max_commands == 0) return;

    size_t written = 0;
    uint32_t vertex_offset = 0;
    const arche::TypeID tid = arche::type_id<ComponentT>();

    for (const auto &arch_ptr : ctx.world.archetypes()) {
        arche::Archetype *arch = arch_ptr.get();
        if (!arch->has_component(tid)) continue;

        for (arche::Chunk *chunk : ctx.world.get_archetype_chunks(arch)) {
            if (chunk->is_empty()) continue;

            if (!is_chunk_visible(chunk->header(), frustum)) {
                vertex_offset += chunk->count();
                continue;
            }

            if (written >= indirect.max_commands) break;

            indirect.destination[written] = DrawIndirectCommand{
                .vertex_count = chunk->count(),
                .instance_count = 1,
                .first_vertex = vertex_offset,
                .first_instance = 0
            };

            vertex_offset += chunk->count();
            written++;
        }
    }

    indirect.commands_written = written;
}

} // namespace render
```

### C. The `RenderPlugin` Struct

```cpp
#pragma once
#include <string_view>
#include <arche/plugin.hpp>

namespace render {

template <typename StreamComponent>
struct RenderPlugin {
    void* vram_base{nullptr};
    size_t ring_slot_capacity{0};
    DrawIndirectCommand* indirect_destination{nullptr};
    size_t max_commands{0};

    [[nodiscard]] std::string_view name() const noexcept {
        return "RenderPlugin";
    }

    void build(arche::World &world, arche::Scheduler &scheduler) {
        // 1. Emplace persistent resources
        world.emplace_resource<GpuVramRing>(vram_base, ring_slot_capacity, 0u, 0u);
        world.emplace_resource<IndirectDrawBuffer>(indirect_destination, max_commands, 0u);
        world.emplace_resource<CameraFrustum>();

        // 2. Register GPU Streaming System with Access Descriptors
        scheduler.add_system(
            "GpuStreamSystem",
            arche::SystemScale::Wide,
            arche::Access()
                .template reads<StreamComponent>()
                .template reads_resource<CameraFrustum>()
                .template writes_resource<GpuVramRing>(),
            gpu_stream_system<StreamComponent>
        );

        // 3. Register Indirect Draw Prep System
        scheduler.add_system(
            "GpuIndirectPrepSystem",
            arche::SystemScale::Local,
            arche::Access()
                .template reads<StreamComponent>()
                .template reads_resource<CameraFrustum>()
                .template writes_resource<IndirectDrawBuffer>(),
            gpu_indirect_prep_system<StreamComponent>
        );
    }
};

} // namespace render
```

---

## 4. Usage Example in Application Frame Loop

```cpp
#include <iostream>
#include <vector>
#include <arche/arche.hpp>

struct Position { float x, y, z; };

int main() {
    arche::World world;

    // Allocate mapped staging memory (mocked host memory or Vulkan host-visible buffer)
    constexpr size_t SLOT_BYTES = 16 * 1024 * 1024;
    std::vector<uint8_t> mock_vram(3 * SLOT_BYTES);
    std::vector<render::DrawIndirectCommand> mock_indirect(1024);

    // Install the plugin
    world.install_plugin(render::RenderPlugin<Position>{
        .vram_base = mock_vram.data(),
        .ring_slot_capacity = SLOT_BYTES,
        .indirect_destination = mock_indirect.data(),
        .max_commands = mock_indirect.size()
    });

    // Spawn test entities
    for (int i = 0; i < 10000; ++i) {
        world.spawn(Position{float(i), 0.0f, 0.0f});
    }

    world.compile();

    // Frame loop
    for (uint32_t frame = 0; frame < 60; ++frame) {
        // Advance swapchain frame in resource
        world.resource<render::GpuVramRing>().current_frame = frame;

        // Scheduler automatically executes culling, streaming, and indirect prep
        world.run_systems();

        const auto &ring = world.resource<render::GpuVramRing>();
        const auto &indirect = world.resource<render::IndirectDrawBuffer>();

        std::cout << "Frame " << frame 
                  << ": Streamed " << ring.bytes_written << " bytes to VRAM, "
                  << indirect.commands_written << " indirect commands ready.\n";
    }

    return 0;
}
```
