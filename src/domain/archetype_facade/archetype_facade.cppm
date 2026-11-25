module;

#include <cstddef>
#include <cstdint>

export module arche:domain.archetype;

import :archetype;
import :archetype_graph;

export namespace arche {

// =============================================================================
// Archetypes & Memory Inspection Domain Sub-Facade
// =============================================================================

/**
 * @class ArchetypeFacade
 * @brief Domain sub-facade for inspecting structural archetype graphs and chunk allocation metrics.
 *
 * Provides read-only introspection over the global archetype transition graph,
 * entity distributions, and active chunk memory pages.
 *
 * Accessible via `world.archetypes`.
 */
class ArchetypeFacade {
public:
  constexpr ArchetypeFacade() noexcept = default;
  explicit constexpr ArchetypeFacade(internal::ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns the total number of distinct structural archetypes in the world.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the total number of allocated memory chunks across all archetypes.
  [[nodiscard]] size_t total_chunks() const noexcept;

  /// Counts total living entities possessing component type T across all archetypes.
  template <typename T>
  [[nodiscard]] uint32_t count_with() const noexcept;

private:
  internal::ArchetypeGraph *graph_{nullptr};
};

} // namespace arche
