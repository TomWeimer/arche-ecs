module;

#include <cassert>
#include <cstddef>
#include <cstdint>

export module arche:archetype_view;

import :component_id;
import :archetype;
import :archetype_graph;

export namespace arche {

// =============================================================================
// Archetypes & Memory Inspection Domain View
// =============================================================================

/**
 * @class ArchetypeView
 * @brief Domain view for inspecting structural archetype graphs and chunk allocation metrics.
 *
 * Provides read-only introspection over the global archetype transition graph,
 * entity distributions, and active chunk memory pages.
 *
 * Accessible via `world.archetypes`.
 */
class ArchetypeView {
public:
  constexpr ArchetypeView() noexcept = default;
  explicit constexpr ArchetypeView(internal::ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns the total number of distinct structural archetypes in the world.
  [[nodiscard]] size_t count() const noexcept {
    assert(graph_ != nullptr);
    return graph_->archetypes().size();
  }

  /// Returns the total number of allocated memory chunks across all archetypes.
  [[nodiscard]] size_t total_chunks() const noexcept {
    assert(graph_ != nullptr);
    size_t total = 0;
    for (const auto &arch_ptr : graph_->archetypes()) {
      total += arch_ptr->chunks().size();
    }
    return total;
  }

  /// Counts total living entities possessing component type T across all archetypes.
  template <typename T>
  [[nodiscard]] uint32_t count_with() const noexcept {
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

private:
  internal::ArchetypeGraph *graph_{nullptr};
};

using ArchetypeFacade = ArchetypeView;

} // namespace arche
