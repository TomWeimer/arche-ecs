module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module arche:domain.archetype_impl;

import :domain.archetype;
import :component_id;
import :archetype;
import :archetype_graph;
import :chunk;

export namespace arche {

using namespace internal;

// =============================================================================
// ArchetypeFacade Method Implementations
// =============================================================================

size_t ArchetypeFacade::count() const noexcept {
  assert(graph_ != nullptr);
  return graph_->archetypes().size();
}

size_t ArchetypeFacade::total_chunks() const noexcept {
  assert(graph_ != nullptr);
  size_t total = 0;
  for (const auto &arch_ptr : graph_->archetypes()) {
    total += arch_ptr->chunks().size();
  }
  return total;
}

template <typename T>
inline uint32_t ArchetypeFacade::count_with() const noexcept {
  assert(graph_ != nullptr);
  const ComponentID tid = component_id<T>();
  uint32_t total = 0;
  for (const auto &arch_ptr : graph_->archetypes()) {
    if (arch_ptr->has_component(tid)) {
      total += arch_ptr->entity_count();
    }
  }
  return total;
}

} // namespace arche
