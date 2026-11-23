#pragma once

#include "component/component_mask.hpp"
#include "component/type_id.hpp"

#include "arche/types/filters.hpp"
#include "archetype/archetype.hpp"

#include <cstdint>
#include <type_traits>

namespace arche {

template <typename Comp>
inline uint32_t resolve_query_offset(const Archetype *arch) {
  using Decay = std::decay_t<Comp>;

  // 1. Component type is the Entity
  if constexpr (std::is_same_v<Decay, Entity>) {
    // Entity slice is always at offset 0
    return 0;
  }
  // 2. Component type is included in the archetype
  else if constexpr (is_has_v<Decay>) {
    using Inner = typename is_has<Decay>::type;
    return arch->has<Inner>() ? 1 : 0; // Boolean presence flag
  }
  // 3. Component type is a pointer
  else if constexpr (std::is_pointer_v<Decay>) {
    using Raw = std::remove_pointer_t<Decay>;
    return arch->column_offset<Raw>();
  }
  // 4.
  else {
    uint32_t off = arch->column_offset<Decay>();
    assert(
        off != UINT32_MAX && "Queried component missing from matched archetype!"
    );
    return off;
  }
}

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

// --- Filter Unpackers (Modern C++20 Lambda Pack Expansion) ------------------

template <typename Filter>
inline void apply_filter_to_mask(ComponentMask &mask, TypeRegistry &reg, Filter = {}) {
  if constexpr (is_with_v<Filter>) {
    [&]<typename... Ts>(With<Ts...>) {
      ((reg.template register_type<std::decay_t<Ts>>(), mask.set(type_id<std::decay_t<Ts>>())), ...);
    }(Filter{});
  } else if constexpr (is_without_v<Filter>) {
    [&]<typename... Ts>(Without<Ts...>) {
      ((reg.template register_type<std::decay_t<Ts>>(), mask.set(type_id<std::decay_t<Ts>>())), ...);
    }(Filter{});
  }
}

template <typename T> struct with_unpacker {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    apply_filter_to_mask<T>(mask, reg);
  }
};

template <typename T> struct without_unpacker {
  static void apply(ComponentMask &mask, TypeRegistry &reg) {
    apply_filter_to_mask<T>(mask, reg);
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
  using component_type = T;
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
