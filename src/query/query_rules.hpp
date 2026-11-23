#pragma once

#include "arche/types/entity.hpp"
#include "arche/types/filters.hpp"
#include "query/filter.hpp"

#include <type_traits>

namespace arche {

// Forward declaration of the Entity handle type (no header include needed!)
struct Entity;

// Type traits for dynamic change-detection filters
template <typename T> inline constexpr bool is_mutated_v = false;
template <typename T> inline constexpr bool is_mutated_v<Mutated<T>> = true;

// Rule 1 & 3: Component purity (Standard-layout class/struct, not a filter or
// tag, and not Entity handle)
template <typename T>
concept ComponentType = std::is_class_v<std::decay_t<T>> &&
    std::is_standard_layout_v<std::decay_t<T>> &&
    !std::is_same_v<std::decay_t<T>, Entity> && !is_with_v<std::decay_t<T>> &&
    !is_without_v<std::decay_t<T>> && !is_has_v<std::decay_t<T>> &&
    !is_or_v<std::decay_t<T>> && !is_mutated_v<std::decay_t<T>> &&
    !std::is_same_v<std::decay_t<T>, Added>;

// Rule 1: Valid Data Terms (Components, Pointers, Entity, Has<Component>)
template <typename T>
concept ValidDataTerm = ComponentType<std::remove_pointer_t<std::decay_t<T>>> ||
    std::is_same_v<std::decay_t<T>, Entity> ||
    (is_has_v<std::decay_t<T>> && ComponentType<has_inner_t<T>>);

// Rule 2: Explicit OR Branches (Only With<...> or Without<...>)
template <typename T>
concept ValidOrBranch = is_with_v<std::decay_t<T>> ||
    is_without_v<std::decay_t<T>>;

// Helper to check all branches in an Or<...> pack
template <typename T> inline constexpr bool are_valid_or_branches_v = false;

template <typename... Branches>
inline constexpr bool
    are_valid_or_branches_v<Or<Branches...>> = (ValidOrBranch<Branches> && ...);

// Rule 3: Valid Filter Tags
template <typename T>
concept ValidFilterTag = is_with_v<std::decay_t<T>> ||
    is_without_v<std::decay_t<T>> || is_mutated_v<std::decay_t<T>> ||
    std::is_same_v<std::decay_t<T>, Added> ||
    (is_or_v<std::decay_t<T>> && are_valid_or_branches_v<std::decay_t<T>>);

} // namespace arche