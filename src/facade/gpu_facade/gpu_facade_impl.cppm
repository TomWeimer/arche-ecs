module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

export module arche:facade.gpu_impl;

import :facade.gpu;
import :type_id;
import :archetype;
import :archetype_graph;
import :chunk;
import :gpu_span;

export namespace arche {

// =============================================================================
// GpuFacade Method Implementations
// =============================================================================

template <typename T>
inline std::vector<ComponentSpan<T>> GpuFacade::view_chunks() {
  assert(graph_ != nullptr);
  const TypeID tid = type_id<T>();
  std::vector<ComponentSpan<T>> result;

  for (const auto &arch_ptr : graph_->archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : arch_ptr->chunks()) {
      if (chunk->count() > 0) {
        T *ptr = reinterpret_cast<T *>(chunk->component_element(layout, 0));
        result.emplace_back(ptr, chunk->count());
      }
    }
  }

  return result;
}

template <typename T>
inline size_t GpuFacade::stream_to(void *destination_buffer) {
  assert(graph_ != nullptr);
  assert(destination_buffer != nullptr);

  const TypeID tid = type_id<T>();
  uint8_t *dst = reinterpret_cast<uint8_t *>(destination_buffer);
  size_t total_bytes = 0;

  for (const auto &arch_ptr : graph_->archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col < 0) {
      continue;
    }

    const auto &layout = arch_ptr->column_layout(static_cast<size_t>(col));
    for (Chunk *chunk : arch_ptr->chunks()) {
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

} // namespace arche
