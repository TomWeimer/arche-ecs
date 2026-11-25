module;

#include <tuple>
#include <type_traits>
#include <utility>

export module arche:query.detail.masks;

import :component_mask;
import :component_id;
import :filters;
import :query.detail.filter;

export namespace arche::detail::query {

// =============================================================================
// Query Mask Container
// =============================================================================

/// Composite bitmasks defining a query's archetype filtering criteria.
struct QueryMasks {
  ComponentMask include{};
  ComponentMask exclude{};
  ComponentMask any{};
};

// =============================================================================
// Component Bitmask Registration
// =============================================================================

/// Sets the bit in mask for a single component type if valid.
template <typename T>
inline void set_mask_for_type(ComponentMask &mask) {
  using Decay = std::decay_t<T>;
  if constexpr (Component<Decay>) {
    mask.set(component_id<Decay>());
  }
}

/// Registers types into the target bitmask via compile-time reflection.
template <typename... Ts>
inline void register_and_set_mask(ComponentMask &mask) {
  (set_mask_for_type<Ts>(mask), ...);
}

// =============================================================================
// Mask Filters (Used to build QueryCache bitmasks)
// =============================================================================

/// Applies With<Ts...> filter to query include mask.
template <typename... Ts>
inline void apply_mask_filter(QueryMasks &m, With<Ts...>) {
  register_and_set_mask<Ts...>(m.include);
}

/// Applies Without<Ts...> filter to query exclude mask.
template <typename... Ts>
inline void apply_mask_filter(QueryMasks &m, Without<Ts...>) {
  register_and_set_mask<Ts...>(m.exclude);
}

/// Helper to unpack a branch of an Or filter into any_mask.
template <typename... Ts>
inline void add_to_any_mask(ComponentMask &any_mask, With<Ts...>) {
  register_and_set_mask<Ts...>(any_mask);
}

template <typename T>
inline void add_to_any_mask(ComponentMask &any_mask, T) {
  set_mask_for_type<T>(any_mask);
}

/// Applies Or<Branches...> filter constructing the any_mask.
template <typename... SubFilters>
inline void apply_mask_filter(QueryMasks &m, Or<SubFilters...>) {
  (add_to_any_mask(m.any, SubFilters{}), ...);
}

/// Applies Mutated<T> component requirement to query include mask.
template <typename T>
inline void apply_mask_filter(QueryMasks &m, Mutated<T>) {
  set_mask_for_type<T>(m.include);
}

/// Added filter does not alter component presence bitmasks (entity version checked on chunks).
inline void apply_mask_filter(QueryMasks &, Added) {}

// =============================================================================
// Mask Computation Entry Point
// =============================================================================

/// Evaluates query masks for factory lookups without constructing a full Query.
template <typename... Components, typename... FilterArgs>
inline QueryMasks compute_query_masks(const FilterArgs &...filters) {
  QueryMasks masks{};
  register_and_set_mask<Components...>(masks.include);
  (apply_mask_filter(masks, filters), ...);
  return masks;
}

} // namespace arche::detail::query
