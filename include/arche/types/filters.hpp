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
// Filter Type Traits
// =============================================================================

/// Type trait detecting With<Ts...> filters.
template <typename T> struct is_with_filter : std::false_type {};
template <typename... Ts>
struct is_with_filter<With<Ts...>> : std::true_type {};
template <typename T>
inline constexpr bool is_with_filter_v = is_with_filter<T>::value;

/// Type trait detecting Without<Ts...> filters.
template <typename T> struct is_without_filter : std::false_type {};
template <typename... Ts>
struct is_without_filter<Without<Ts...>> : std::true_type {};
template <typename T>
inline constexpr bool is_without_filter_v = is_without_filter<T>::value;

/// Type trait detecting Has<T> query terms.
template <typename T> struct is_has : std::false_type {};
template <typename T> struct is_has<Has<T>> : std::true_type {
  using type = T;
};
template <typename T>
inline constexpr bool is_has_v = is_has<std::decay_t<T>>::value;

/// Type trait detecting Or<Filters...> filters.
template <typename T> struct is_or_filter : std::false_type {};
template <typename... Filters>
struct is_or_filter<Or<Filters...>> : std::true_type {};
template <typename T>
inline constexpr bool is_or_filter_v = is_or_filter<std::decay_t<T>>::value;

// =============================================================================
// Query Element & Column Storage Traits
// =============================================================================

/// Resolves the argument reference/pointer type unpacked in query callback
/// loops.
template <typename T> struct query_element {
  using type = T &;
};

template <typename T>
  requires(std::is_pointer_v<T>)
struct query_element<T> {
  using type = T;
};

template <typename T> struct query_element<Has<T>> {
  using type = bool;
};

template <typename T> using query_element_t = typename query_element<T>::type;

/// Resolves the raw column pointer type stored in QueryIterator.
template <typename T> struct column_pointer_type {
  using type = T *;
};

template <typename T>
  requires(std::is_pointer_v<T>)
struct column_pointer_type<T> {
  using type = T;
};

template <typename T> struct column_pointer_type<Has<T>> {
  using type = bool;
};

template <typename T>
using column_pointer_type_t = typename column_pointer_type<T>::type;

} // namespace arche
