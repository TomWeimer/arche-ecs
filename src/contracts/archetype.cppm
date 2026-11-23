module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module arche:contracts.archetype;

export namespace arche {

class World;
class Archetype;
struct Chunk;

// =============================================================================
// Archetype & Storage Introspection Sub-Facade
// =============================================================================

/**
 * @class ArchetypeFacade
 * @brief Domain sub-facade for low-level archetype graph metrics and chunk
 * memory layout.
 *
 * Provides direct access to structural graph nodes, allocated chunk memory
 * arrays, and component frequency counts without exposing internal graph
 * storage details.
 *
 * Accessible via `world.archetypes`.
 *
 * ```cpp
 * // Example:
 * size_t total_archetypes = world.archetypes.count();
 * uint32_t active_enemies = world.archetypes.count_with<Enemy>();
 * ```
 */
class ArchetypeFacade {
public:
  explicit constexpr ArchetypeFacade(World *world) noexcept : world_(world) {}

  /// Returns the total number of unique component archetypes registered in the
  /// graph.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the mutable list of allocated memory chunks for the specified
  /// archetype.
  [[nodiscard]] std::vector<Chunk *> &chunks(Archetype *arch);

  /// Returns a read-only span of memory chunks for the specified archetype.
  [[nodiscard]] std::span<Chunk *const> chunks(const Archetype *arch) const;

  /// Returns the number of entities across all archetypes containing component
  /// T.
  template <typename T> [[nodiscard]] uint32_t count_with() const noexcept;

private:
  World *world_{nullptr};
};

} // namespace arche
