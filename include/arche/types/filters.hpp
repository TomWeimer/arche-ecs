#pragma once

#include <type_traits>

namespace arche {

// =============================================================================
// Compile-Time Structural Query Filter Tags
// =============================================================================

/**
 * @struct With
 * @brief Compile-time query filter requiring components without binding them to
 * data arguments.
 *
 * Matches only archetypes possessing all specified component types `Ts...`.
 *
 * ```cpp
 * // Example: match entities with Position and Velocity, but only iterate
 * Position for (auto [pos] : world.query<Position, With<Velocity>>()) {
 *     // pos is Position&
 * }
 * ```
 */
template <typename... Ts> struct With {};

/**
 * @struct Without
 * @brief Compile-time query filter excluding entities possessing any specified
 * component.
 *
 * Rejects archetypes that contain any of the excluded types `Ts...`.
 *
 * ```cpp
 * // Example: match active entities that do not have the Frozen tag
 * for (auto [pos] : world.query<Position, Without<Frozen>>()) {
 *     // pos is Position&
 * }
 * ```
 */
template <typename... Ts> struct Without {};

/**
 * @struct Has
 * @brief Query parameter yielding a boolean presence flag without filtering out
 * entities.
 *
 * Enables branch decisions inside query loops without excluding entities
 * lacking component `T`.
 *
 * ```cpp
 * // Example: iterate all entities with Position, checking if Shield is present
 * for (auto [pos, has_shield] : world.query<Position, Has<Shield>>()) {
 *     if (has_shield) {
 *         // Entity possesses Shield
 *     }
 * }
 * ```
 */
template <typename T> struct Has {
  using component_type = T;
};

/**
 * @struct Or
 * @brief Disjunctive query filter matching archetypes satisfying at least one
 * branch.
 *
 * ```cpp
 * // Example: match entities that have Player OR Enemy
 * for (auto [pos] : world.query<Position, Or<With<Player>, With<Enemy>>>()) {
 *     // pos is Position&
 * }
 * ```
 */
template <typename... Filters> struct Or {};

// =============================================================================
// Global Constexpr Tag Variables
// =============================================================================

/// Global constexpr tag instance for With<Ts...>. Allows `world.query<Position>(with<Velocity>)`.
template <typename... Ts> inline constexpr With<Ts...> with{};

/// Global constexpr tag instance for Without<Ts...>. Allows `world.query<Position>(without<Dead>)`.
template <typename... Ts> inline constexpr Without<Ts...> without{};

// =============================================================================
// Filter Type Traits (Modern C++20 Variable Templates)
// =============================================================================

/// Type trait detecting With<Ts...> filters.
template <typename T> inline constexpr bool is_with_v = false;
template <typename... Ts> inline constexpr bool is_with_v<With<Ts...>> = true;
template <typename T> inline constexpr bool is_with_filter_v = is_with_v<std::decay_t<T>>;

/// Type trait detecting Without<Ts...> filters.
template <typename T> inline constexpr bool is_without_v = false;
template <typename... Ts> inline constexpr bool is_without_v<Without<Ts...>> = true;
template <typename T> inline constexpr bool is_without_filter_v = is_without_v<std::decay_t<T>>;

/// Type trait detecting Has<T> query terms.
template <typename T> inline constexpr bool is_has_v = false;
template <typename T> inline constexpr bool is_has_v<Has<T>> = true;

template <typename T> struct has_inner { using type = void; };
template <typename T> struct has_inner<Has<T>> { using type = T; };
template <typename T> using has_inner_t = typename has_inner<std::decay_t<T>>::type;

template <typename T> struct is_has : std::bool_constant<is_has_v<std::decay_t<T>>> {
  using type = has_inner_t<T>;
};

/// Type trait detecting Or<Filters...> filters.
template <typename T> inline constexpr bool is_or_v = false;
template <typename... Filters> inline constexpr bool is_or_v<Or<Filters...>> = true;
template <typename T> inline constexpr bool is_or_filter_v = is_or_v<std::decay_t<T>>;

// =============================================================================
// Query Element & Column Storage Traits (Modern std::conditional_t)
// =============================================================================

/// Resolves the argument reference/pointer type unpacked in query callback loops.
template <typename T>
using query_element_t = std::conditional_t<
    is_has_v<std::decay_t<T>>, bool,
    std::conditional_t<std::is_pointer_v<std::decay_t<T>>, std::decay_t<T>, T &>
>;

template <typename T> struct query_element {
  using type = query_element_t<T>;
};

/// Resolves the raw column pointer type stored in QueryIterator.
template <typename T>
using column_pointer_type_t = std::conditional_t<
    is_has_v<std::decay_t<T>>, bool,
    std::conditional_t<std::is_pointer_v<std::decay_t<T>>, std::decay_t<T>, T *>
>;

template <typename T> struct column_pointer_type {
  using type = column_pointer_type_t<T>;
};

} // namespace arche
