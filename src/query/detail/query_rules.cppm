module;

#include <type_traits>

export module arche:query.detail.query_rules;

import :entity;
import :filters;

export namespace arche {

template <typename T> inline constexpr bool is_mutated_v = false;

template <typename T>
concept ComponentType = std::is_class_v<std::decay_t<T>> &&
    std::is_standard_layout_v<std::decay_t<T>> &&
    !std::is_same_v<std::decay_t<T>, Entity> && !is_with_v<std::decay_t<T>> &&
    !is_without_v<std::decay_t<T>> && !is_has_v<std::decay_t<T>> &&
    !is_or_v<std::decay_t<T>> && !is_mutated_v<std::decay_t<T>>;

template <typename T>
concept ValidOrBranch = is_with_v<std::decay_t<T>> ||
    is_without_v<std::decay_t<T>>;

template <typename T> inline constexpr bool are_valid_or_branches_v = false;

template <typename... Branches>
inline constexpr bool
    are_valid_or_branches_v<Or<Branches...>> = (ValidOrBranch<Branches> && ...);

template <typename T>
concept ValidFilterTag = is_with_v<std::decay_t<T>> ||
    is_without_v<std::decay_t<T>> || is_mutated_v<std::decay_t<T>> ||
    (is_or_v<std::decay_t<T>> && are_valid_or_branches_v<std::decay_t<T>>);

} // namespace arche
