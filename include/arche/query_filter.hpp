#pragma once

#include <cstddef>
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

// =============================================================================
// Double-Buffer Write Target Query Wrapper
// =============================================================================

/**
 * @struct Write
 * @brief Query parameter wrapper indicating that a double-buffered component
 * should bind to the chunk's WRITE slice instead of the default READ slice.
 *
 * @tparam T Underlying component type.
 */
template <typename T> struct Write {
  T *data{nullptr};

  constexpr explicit Write(T *ptr = nullptr) noexcept : data(ptr) {}

  constexpr T &operator[](size_t index) noexcept { return data[index]; }
  constexpr const T &operator[](size_t index) const noexcept {
    return data[index];
  }
  constexpr T *get() noexcept { return data; }
  constexpr const T *get() const noexcept { return data; }
};

template <typename T> struct is_write_target : std::false_type {
  using underlying_type = T;
};

template <typename T> struct is_write_target<Write<T>> : std::true_type {
  using underlying_type = T;
};

template <typename T>
inline constexpr bool is_write_target_v = is_write_target<T>::value;

} // namespace arche
