module;

#include <cstddef>
#include <vector>

export module arche:domain.gpu;

import :gpu_layout;
import :gpu_span;
import :archetype_graph;

export namespace arche {

// =============================================================================
// GPU Streaming & Reflection Sub-Facade
// =============================================================================

/**
 * @class GpuFacade
 * @brief Domain sub-facade for contiguous component chunk reflection and GPU streaming.
 *
 * Provides zero-copy buffer views and blit streaming over cache-aligned
 * archetype chunk columns for graphics pipelines directly from ArchetypeGraph.
 *
 * Accessible via `world.gpu`.
 */
class GpuFacade {
public:
  constexpr GpuFacade() noexcept = default;
  explicit constexpr GpuFacade(ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns spans of contiguous component column slices across all chunks for GPU upload.
  template <typename T>
  [[nodiscard]] std::vector<ComponentSpan<T>> view_chunks();

  /// Streams all component columns of type T directly into a destination memory buffer.
  template <typename T>
  size_t stream_to(void *destination_buffer);

private:
  ArchetypeGraph *graph_{nullptr};
};

} // namespace arche
