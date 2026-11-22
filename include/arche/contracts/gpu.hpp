#pragma once

#include "arche/types/gpu_span.hpp"

#include <cstddef>
#include <vector>

namespace arche {

class World;

// =============================================================================
// GPU Streaming & Reflection Sub-Facade
// =============================================================================

/**
 * @class GpuFacade
 * @brief Domain sub-facade for contiguous component chunk reflection and GPU
 * streaming.
 *
 * Provides zero-copy buffer views and blit streaming over cache-aligned
 * archetype chunk columns for graphics pipelines (Vulkan, Metal, DirectX 12,
 * WebGPU).
 *
 * Accessible via `world.gpu`.
 *
 * ```cpp
 * // Example:
 * for (const auto &span : world.gpu.view_chunks<Position>()) {
 *     upload_to_vbo(vbo_id, span.data(), span.size_bytes());
 * }
 * world.gpu.stream_to<Position>(destination_memory_buffer);
 * ```
 *
 */
class GpuFacade {
public:
  explicit constexpr GpuFacade(World *world) noexcept : world_(world) {}

  /// Returns spans of contiguous component column slices across all chunks for
  /// GPU upload.
  template <typename T>
  [[nodiscard]] inline std::vector<ComponentSpan<T>> view_chunks();

  /// Streams all component columns of type T directly into a destination memory
  /// buffer.
  template <typename T> inline size_t stream_to(void *destination_buffer);

private:
  World *world_{nullptr};
};

} // namespace arche
