module;

#include <type_traits>

export module arche:filters;

export namespace arche {

// =============================================================================
// Compile-Time Structural Query Filter Tags
// =============================================================================

/**
 * @struct With
 * @brief Compile-time query filter requiring components without binding them to
 * data arguments.
 */
template <typename... Ts> struct With {};

/**
 * @struct Without
 * @brief Compile-time query filter excluding entities possessing any specified
 * component.
 */
template <typename... Ts> struct Without {};

/**
 * @struct Has
 * @brief Query parameter yielding a boolean presence flag without filtering out
 * entities.
 */
template <typename T> struct Has {
  using component_type = T;
};

/**
 * @struct Or
 * @brief Disjunctive query filter matching archetypes satisfying at least one
 * branch.
 */
template <typename... Filters> struct Or {};

// =============================================================================
// Global Constexpr Tag Variables
// =============================================================================

template <typename... Ts> inline constexpr With<Ts...> with{};
template <typename... Ts> inline constexpr Without<Ts...> without{};

// =============================================================================
// Filter Type Traits (Modern C++20 Variable Templates)
// =============================================================================

template <typename T> inline constexpr bool is_with_v = false;
template <typename... Ts> inline constexpr bool is_with_v<With<Ts...>> = true;
template <typename T>
inline constexpr bool is_with_filter_v = is_with_v<std::decay_t<T>>;

template <typename T> inline constexpr bool is_without_v = false;
template <typename... Ts>
inline constexpr bool is_without_v<Without<Ts...>> = true;
template <typename T>
inline constexpr bool is_without_filter_v = is_without_v<std::decay_t<T>>;

template <typename T> inline constexpr bool is_has_v = false;
template <typename Comp> inline constexpr bool is_has_v<Has<Comp>> = true;
template <typename T>
inline constexpr bool is_has_filter_v = is_has_v<std::decay_t<T>>;

template <typename T> inline constexpr bool is_or_v = false;
template <typename... Fs> inline constexpr bool is_or_v<Or<Fs...>> = true;
template <typename T>
inline constexpr bool is_or_filter_v = is_or_v<std::decay_t<T>>;

template <typename T>
inline constexpr bool is_query_filter_v = is_with_filter_v<T> ||
    is_without_filter_v<T> || is_has_filter_v<T> || is_or_filter_v<T>;

} // namespace arche
