#pragma once

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
 * for (auto [pos, vel] : world.query<Position, Velocity>(arche::With<Mass>{})) {
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
 * for (auto [pos, vel] : world.query<Position, Velocity>(arche::Without<TagDead>{})) {
 *     // Rejects all archetypes possessing TagDead
 * }
 * ```
 *
 * @tparam Ts Forbidden component types; matching archetypes must not have any.
 */
template <typename... Ts> struct Without {};

} // namespace arche

