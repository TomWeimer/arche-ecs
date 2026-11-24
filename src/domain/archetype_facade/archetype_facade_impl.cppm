module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module arche:domain.archetype_impl;

import :domain.archetype;
import :type_id;
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

std::vector<Chunk *> &ArchetypeFacade::chunks(Archetype *arch) {
  assert(arch != nullptr);
  return arch->chunks();
}

std::span<Chunk *const> ArchetypeFacade::chunks(const Archetype *arch) const {
  assert(arch != nullptr);
  return arch->chunks();
}

const ArchetypeGraph &ArchetypeFacade::graph() const noexcept {
  assert(graph_ != nullptr);
  return *graph_;
}

ArchetypeGraph &ArchetypeFacade::graph() noexcept {
  assert(graph_ != nullptr);
  return *graph_;
}

template <typename T>
inline uint32_t ArchetypeFacade::count_with() const noexcept {
  assert(graph_ != nullptr);
  const TypeID tid = type_id<T>();
  uint32_t total = 0;
  for (const auto &arch_ptr : graph_->archetypes()) {
    if (arch_ptr->has_component(tid)) {
      total += arch_ptr->entity_count();
    }
  }
  return total;
}

} // namespace arche
