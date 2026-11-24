module;

#include <cstdint>
#include <type_traits>

export module arche:filters;

import :entity;

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
 * @brief Optional read-only component query parameter (equivalent to `const
 * T*`).
 *
 * Does not filter out entities lacking T; yields `const T*` (or `nullptr`).
 */
template <typename T>
using Opt = const std::remove_pointer_t<std::remove_cv_t<T>> *;

/**
 * @brief Optional mutable component query parameter (equivalent to `T*`).
 *
 * Does not filter out entities lacking T; yields `T*` (or `nullptr`).
 */
template <typename T>
using OptMut = std::remove_pointer_t<std::remove_cv_t<T>> *;

/**
 * @struct Or
 * @brief Disjunctive query filter matching archetypes satisfying at least one
 * branch.
 */
template <typename... Filters> struct Or {};

// =============================================================================
// Temporal Change Detection Filter Tags
// =============================================================================

/// Sentinel representing automatic version tracking since the previous frame.
inline constexpr uint32_t SINCE_LAST_FRAME = UINT32_MAX;

/**
 * @struct Mutated
 * @brief Filter that skips 64 KB chunks whose component column dirty version
 * is older than `since_version`.
 *
 * @tparam T Component type being monitored for modifications.
 */
template <typename T> struct Mutated {
  using component_type = T;
  uint32_t since_version{SINCE_LAST_FRAME};

  constexpr explicit Mutated(uint32_t version = SINCE_LAST_FRAME) noexcept
      : since_version(version) {}
};

/**
 * @struct Added
 * @brief Filter that skips 64 KB chunks where no entities were created or
 * structurally moved since `since_version`.
 */
struct Added {
  uint32_t since_version{SINCE_LAST_FRAME};

  constexpr explicit Added(uint32_t version = SINCE_LAST_FRAME) noexcept
      : since_version(version) {}
};

// =============================================================================
// Global Constexpr Tag Variables
// =============================================================================

/// Global template tag instances allowing natural syntax: `with<Position>`,
/// `without<Dead>`
template <typename... Ts> inline constexpr With<Ts...> with{};
template <typename... Ts> inline constexpr Without<Ts...> without{};

// =============================================================================
// Filter Type Traits & Introspection (C++20 Compile-Time Meta-Functions)
// =============================================================================

/**
 * @defgroup FilterTypeTraits Filter Type Traits
 * @brief Compile-time predicates and type extractors for query filter tags.
 * @{
 */

// -----------------------------------------------------------------------------
// With<Ts...> Traits
// -----------------------------------------------------------------------------

/**
 * @brief Checks whether exact type `T` is an instantiation of `With<Ts...>`.
 * @tparam T Type to inspect.
 */
template <typename T> inline constexpr bool is_with_v = false;

template <typename... Ts> inline constexpr bool is_with_v<With<Ts...>> = true;

/**
 * @brief Checks whether cvref-decayed type `T` is a `With<Ts...>` filter tag.
 * @tparam T Type to inspect (cvref-decayed before matching).
 */
template <typename T>
inline constexpr bool is_with_filter_v = is_with_v<std::decay_t<T>>;

// -----------------------------------------------------------------------------
// Without<Ts...> Traits
// -----------------------------------------------------------------------------

/**
 * @brief Checks whether exact type `T` is an instantiation of `Without<Ts...>`.
 * @tparam T Type to inspect.
 */
template <typename T> inline constexpr bool is_without_v = false;

template <typename... Ts>
inline constexpr bool is_without_v<Without<Ts...>> = true;

/**
 * @brief Checks whether cvref-decayed type `T` is a `Without<Ts...>` filter
 * tag.
 * @tparam T Type to inspect (cvref-decayed before matching).
 */
template <typename T>
inline constexpr bool is_without_filter_v = is_without_v<std::decay_t<T>>;

// -----------------------------------------------------------------------------
// Has<Comp> Traits & Inner Type Extractor
// -----------------------------------------------------------------------------

/**
 * @struct is_has
 * @brief Type trait testing whether `T` is a `Has<Comp>` query term.
 *
 * Exposes member `using type = Comp` when matched, or `type = void` when
 * unmatched.
 */
template <typename T> struct is_has : std::false_type {
  using type = void;
};

template <typename Comp> struct is_has<Has<Comp>> : std::true_type {
  using type = Comp;
};

/**
 * @brief Checks whether exact type `T` is a `Has<Comp>` query term.
 */
template <typename T> inline constexpr bool is_has_v = is_has<T>::value;

/**
 * @brief Checks whether cvref-decayed type `T` is a `Has<Comp>` query term.
 */
template <typename T>
inline constexpr bool is_has_filter_v = is_has_v<std::decay_t<T>>;

/**
 * @brief Helper alias extracting the inner component type from `Has<Comp>`.
 * Returns `void` if `T` is not an instantiation of `Has<Comp>`.
 */
template <typename T>
using has_inner_t = typename is_has<std::decay_t<T>>::type;

// -----------------------------------------------------------------------------
// Or<Fs...> Traits
// -----------------------------------------------------------------------------

/**
 * @brief Checks whether exact type `T` is an instantiation of `Or<Fs...>`.
 * @tparam T Type to inspect.
 */
template <typename T> inline constexpr bool is_or_v = false;

template <typename... Fs> inline constexpr bool is_or_v<Or<Fs...>> = true;

/**
 * @brief Checks whether cvref-decayed type `T` is an `Or<Fs...>` filter tag.
 */
template <typename T>
inline constexpr bool is_or_filter_v = is_or_v<std::decay_t<T>>;

// -----------------------------------------------------------------------------
// Mutated<Comp> Traits & Inner Type Extractor
// -----------------------------------------------------------------------------

/**
 * @struct is_mutated
 * @brief Type trait testing whether `T` is a `Mutated<Comp>` change-detection
 * filter.
 *
 * Exposes member `using type = Comp` when matched, or `type = void` when
 * unmatched.
 */
template <typename T> struct is_mutated : std::false_type {
  using type = void;
};

template <typename Comp> struct is_mutated<Mutated<Comp>> : std::true_type {
  using type = Comp;
};

/**
 * @brief Checks whether exact type `T` is a `Mutated<Comp>` filter tag.
 */
template <typename T> inline constexpr bool is_mutated_v = is_mutated<T>::value;

/**
 * @brief Checks whether cvref-decayed type `T` is a `Mutated<Comp>` filter tag.
 */
template <typename T>
inline constexpr bool is_mutated_filter_v = is_mutated_v<std::decay_t<T>>;

/**
 * @brief Helper alias extracting the inner component type from `Mutated<Comp>`.
 * Returns `void` if `T` is not an instantiation of `Mutated<Comp>`.
 */
template <typename T>
using mutated_inner_t = typename is_mutated<std::decay_t<T>>::type;

// -----------------------------------------------------------------------------
// Added Traits
// -----------------------------------------------------------------------------

/**
 * @brief Checks whether exact type `T` is the `Added` filter tag.
 */
template <typename T> inline constexpr bool is_added_v = false;

template <> inline constexpr bool is_added_v<Added> = true;

/**
 * @brief Checks whether cvref-decayed type `T` is the `Added` filter tag.
 */
template <typename T>
inline constexpr bool is_added_filter_v = is_added_v<std::decay_t<T>>;

// -----------------------------------------------------------------------------
// Composite Query Filter Predicate
// -----------------------------------------------------------------------------

/**
 * @brief Evaluates whether `T` matches any supported query filter tag
 * (`With`, `Without`, `Has`, `Or`, `Mutated`, or `Added`).
 */
template <typename T>
inline constexpr bool is_query_filter_v = is_with_filter_v<T> ||
    is_without_filter_v<T> || is_has_filter_v<T> || is_or_filter_v<T> ||
    is_mutated_filter_v<T> || is_added_filter_v<T>;

/** @} */

// =============================================================================
// Core ECS Concepts
// =============================================================================

/**
 * @concept Component
 *
 * Valid components must be standard-layout class or struct types,
 * non-polymorphic (no virtual methods), and distinct from Entity handles and
 * query filter tags.
 */
template <typename T>
concept Component = std::is_class_v<std::decay_t<T>> &&
    std::is_standard_layout_v<std::decay_t<T>> &&
    !std::is_polymorphic_v<std::decay_t<T>> &&
    !std::is_same_v<std::decay_t<T>, Entity> &&
    !is_query_filter_v<std::decay_t<T>>;

/**
 * @concept QueryTerm
 * @brief Constrains data types inspected by a Query (Component, Entity, Has<T>,
 * or optional pointers).
 */
template <typename T>
concept QueryTerm = Component<std::remove_pointer_t<std::remove_cv_t<T>>> ||
    std::is_same_v<std::decay_t<T>, Entity> || is_has_v<std::decay_t<T>>;

// =============================================================================
// Query Filter Concepts
// =============================================================================

/**
 * @concept ValidOrBranch
 * @brief Restricts disjunctive Or<...> filter branches to With<...> and
 * Without<...>.
 */
template <typename T>
concept ValidOrBranch = is_with_v<std::decay_t<T>> ||
    is_without_v<std::decay_t<T>> || Component<std::decay_t<T>>;

template <typename T> inline constexpr bool are_valid_or_branches_v = false;
template <typename... Branches>
inline constexpr bool
    are_valid_or_branches_v<Or<Branches...>> = (ValidOrBranch<Branches> && ...);

/**
 * @concept QueryFilter
 * @brief Constrains query filter arguments to valid filter tags (With, Without,
 * Or, Mutated, Added).
 */
template <typename T>
concept QueryFilter = is_with_filter_v<T> || is_without_filter_v<T> ||
    is_has_filter_v<T> || is_mutated_filter_v<T> || is_added_filter_v<T> ||
    (is_or_filter_v<T> && are_valid_or_branches_v<std::decay_t<T>>);

} // namespace arche
