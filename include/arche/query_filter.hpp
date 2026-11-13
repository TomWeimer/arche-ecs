#pragma once

#include <cstdint>

namespace arche {

// =============================================================================
// Query Filter Tags
// =============================================================================

/**
 * @struct With
 * @brief Compile-time tag for requiring components on an entity without
 * including them in the iteration data tuple.
 *
 * @details
 * Used to filter entities by component presence without the overhead of
 * reading or binding those components inside iteration loops:
 * ```cpp
 * for (auto [pos, vel] : world.query<Position, Velocity>(arche::With<Mass>{}))
 * {
 *     // Only iterates entities that have Mass, but Mass is not in the tuple
 * }
 * ```
 *
 * @tparam Ts Component types that must be present on matching archetypes.
 */
template <typename... Ts> struct With {};

/**
 * @struct Without
 * @brief Compile-time tag for excluding entities that possess any of the
 * specified components.
 *
 * @details
 * Matches archetypes in 1-2 CPU clock cycles using 128-bit bitwise AND tests,
 * completely skipping any archetype containing forbidden components:
 * ```cpp
 * for (auto [pos, vel] : world.query<Position,
 * Velocity>(arche::Without<TagDead>{})) {
 *     // Rejects all archetypes possessing TagDead
 * }
 * ```
 *
 * @tparam Ts Forbidden component types; matching archetypes must not have any.
 */
template <typename... Ts> struct Without {};

// =============================================================================
// Runtime Change Detection Filter Tags
// =============================================================================

/**
 * @struct Mutated
 * @brief Runtime filter tag requiring that component T was modified in the
 * chunk strictly after the specified simulation tick.
 *
 * @details
 * Evaluates `chunk->column_version(col) > since_tick` in O(1) at the chunk
 * boundary. If false, bypasses the entire 1,600-entity chunk in a single cycle
 * without iterating rows.
 *
 * @tparam T Component type to inspect for mutations.
 */
template <typename T> struct Mutated {
  uint32_t since_tick{0};
  constexpr explicit Mutated(uint32_t tick = 0) noexcept : since_tick(tick) {}
};

/**
 * @struct Added
 * @brief Runtime filter tag requiring that entities were added or relocated in
 * the chunk strictly after the specified simulation tick.
 *
 * @details
 * Evaluates `chunk->structure_version() > since_tick` in O(1) at the chunk
 * boundary.
 */
struct Added {
  uint32_t since_tick{0};
  constexpr explicit Added(uint32_t tick = 0) noexcept : since_tick(tick) {}
};

} // namespace arche
