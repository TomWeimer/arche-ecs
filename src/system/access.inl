#pragma once

#include "component/type_id.hpp"

namespace arche {

template <typename... Comps> AccessBuilder &AccessBuilder::reads() noexcept {
  (read_components_.set(type_id<Comps>()), ...);
  return *this;
}

template <typename... Comps> AccessBuilder &AccessBuilder::writes() noexcept {
  (write_components_.set(type_id<Comps>()), ...);
  return *this;
}

template <typename... Res>
AccessBuilder &AccessBuilder::reads_resource() noexcept {
  (read_resources_.set(resource_id<Res>()), ...);
  return *this;
}

template <typename... Res>
AccessBuilder &AccessBuilder::writes_resource() noexcept {
  (write_resources_.set(resource_id<Res>()), ...);
  return *this;
}

[[nodiscard]] inline AccessDescriptor AccessBuilder::build() const noexcept {
  return {
      read_components_, write_components_, read_resources_, write_resources_
  };
}

} // namespace arche
