module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module arche:domain.archetype;

import :archetype;
import :archetype_graph;
import :chunk;

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
  explicit constexpr ArchetypeFacade(ArchetypeGraph &graph) noexcept
      : graph_(&graph) {}

  /// Returns the total number of distinct structural archetypes in the world.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the vector of memory chunks allocated to an archetype.
  [[nodiscard]] std::vector<Chunk *> &chunks(Archetype *arch);

  /// Returns the immutable span of memory chunks allocated to an archetype.
  [[nodiscard]] std::span<Chunk *const> chunks(const Archetype *arch) const;

  /// Counts total living entities possessing component type T across all archetypes.
  template <typename T>
  [[nodiscard]] uint32_t count_with() const noexcept;

  /// Direct immutable reference to the underlying ArchetypeGraph.
  [[nodiscard]] const ArchetypeGraph &graph() const noexcept;

  /// Direct mutable reference to the underlying ArchetypeGraph.
  [[nodiscard]] ArchetypeGraph &graph() noexcept;

private:
  ArchetypeGraph *graph_{nullptr};
};

} // namespace arche
