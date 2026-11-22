#pragma once

#include "arche/component/component_mask.hpp"
#include "arche/component/type_id.hpp"

#include <cstdint>
#include <type_traits>

namespace arche {

// =============================================================================
// Compile-Time Structural Query Filter Tags
// =============================================================================

/**
 * @struct With
 * @brief Compile-time tag requiring components on an entity without including
 * them in data iteration.
 */
template <typename... Ts> struct With {};

/**
 * @struct Without
 * @brief Compile-time tag excluding entities that possess any of the specified
 * components.
 */
template <typename... Ts> struct Without {};

/**
 * @struct Has
 * @brief Query parameter yielding a boolean indicating whether an entity
 * possesses component T without filtering out entities that lack it.
 */
template <typename T> struct Has {
  using component_type = T;
};

/**
 * @struct Or
 * @brief Disjunctive query filter: matches archetypes satisfying at least one
 * of the specified filter branches (e.g. Or<With<Player>, With<Enemy>>).
 */
template <typename... Filters> struct Or {};

// --- Filter Type Traits ------------------------------------------------------

template <typename T> struct is_with_filter : std::false_type {};
template <typename... Ts>
struct is_with_filter<With<Ts...>> : std::true_type {};
template <typename T>
inline constexpr bool is_with_filter_v = is_with_filter<T>::value;

template <typename T> struct is_without_filter : std::false_type {};
template <typename... Ts>
struct is_without_filter<Without<Ts...>> : std::true_type {};
template <typename T>
inline constexpr bool is_without_filter_v = is_without_filter<T>::value;

template <typename T> struct is_has : std::false_type {};
template <typename T>
struct is_has<Has<T>> : std::true_type {
  using type = T;
};
template <typename T>
inline constexpr bool is_has_v = is_has<std::decay_t<T>>::value;

template <typename T> struct is_or_filter : std::false_type {};
template <typename... Filters>
struct is_or_filter<Or<Filters...>> : std::true_type {};
template <typename T>
inline constexpr bool is_or_filter_v = is_or_filter<std::decay_t<T>>::value;

// --- Query Element & Column Storage Traits -----------------------------------

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

template <typename T>
using query_element_t = typename query_element<T>::type;

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

template <typename Comp, typename Col>
inline decltype(auto) access_query_row(Col col, uint32_t row) {
  if constexpr (is_has_v<Comp>) {
    return static_cast<bool>(col);
  } else if constexpr (std::is_pointer_v<std::decay_t<Comp>>) {
    return col != nullptr ? (col + row) : nullptr;
  } else {
    return col[row];
  }
}

// --- Filter Unpackers --------------------------------------------------------

template <typename T> struct with_unpacker;
template <typename... Ts> struct with_unpacker<With<Ts...>> {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    (
        [&] {
          reg.template register_type<std::decay_t<Ts>>();
          mask.set(type_id<std::decay_t<Ts>>());
        }(),
        ...);
  }
};

template <typename T> struct without_unpacker;
template <typename... Ts> struct without_unpacker<Without<Ts...>> {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    (
        [&] {
          reg.template register_type<std::decay_t<Ts>>();
          mask.set(type_id<std::decay_t<Ts>>());
        }(),
        ...);
  }
};

// =============================================================================
// Runtime Change Detection Filter Tags
// =============================================================================

/**
 * @struct Mutated
 * @brief Runtime filter tag requiring that component T was modified in the
 * chunk strictly after the specified sequence version.
 *
 * @tparam T Component type to inspect for mutations.
 */
template <typename T> struct Mutated {
  uint32_t since_version{0};
  constexpr explicit Mutated(uint32_t version = 0) noexcept
      : since_version(version) {}
};

/**
 * @struct Added
 * @brief Runtime filter tag requiring that entities were added or relocated in
 * the chunk strictly after the specified sequence version.
 */
struct Added {
  uint32_t since_version{0};
  constexpr explicit Added(uint32_t version = 0) noexcept
      : since_version(version) {}
};

} // namespace arche
