module;

#include <cstdint>
#include <type_traits>

export module arche:query.detail.filter;

import :component_mask;
import :type_id;
import :filters;
import :archetype;

export namespace arche {

template <typename T> struct Mutated {
  using component_type = T;
  uint32_t since_version{0};
  constexpr explicit Mutated(uint32_t version = 0) noexcept
      : since_version(version) {}
};

struct Added {
  uint32_t since_version{0};
  constexpr explicit Added(uint32_t version = 0) noexcept
      : since_version(version) {}
};

template <typename Comp, typename Col>
inline decltype(auto) access_query_row(Col col, uint32_t row) {
  if constexpr (is_has_v<std::decay_t<Comp>>) {
    return static_cast<bool>(col);
  } else if constexpr (std::is_pointer_v<std::decay_t<Comp>>) {
    return col != nullptr ? (col + row) : nullptr;
  } else {
    return col[row];
  }
}

template <typename Filter>
inline void apply_filter_to_mask(
    ComponentMask &mask,
    TypeRegistry &reg,
    Filter = {}
) {
  if constexpr (is_with_v<Filter>) {
    [&]<typename... Ts>(With<Ts...>) {
      ((reg.template register_type<std::decay_t<Ts>>(),
        mask.set(type_id<std::decay_t<Ts>>())),
       ...);
    }(Filter{});
  } else if constexpr (is_without_v<Filter>) {
    [&]<typename... Ts>(Without<Ts...>) {
      ((reg.template register_type<std::decay_t<Ts>>(),
        mask.set(type_id<std::decay_t<Ts>>())),
       ...);
    }(Filter{});
  }
}

} // namespace arche
