module;

#include <type_traits>

export module arche:access.detail.access_builder_impl;

import :access_builder;
import :access;
import :component_id;
import :resource;

export namespace arche {

// =============================================================================
// AccessBuilder Implementation
// =============================================================================

template <typename... Comps>
inline AccessBuilder &AccessBuilder::reads() noexcept {
  (read_components_.set(component_id<std::decay_t<Comps>>()), ...);
  return *this;
}

template <typename... Comps>
inline AccessBuilder &AccessBuilder::writes() noexcept {
  (write_components_.set(component_id<std::decay_t<Comps>>()), ...);
  return *this;
}

template <typename... Res>
inline AccessBuilder &AccessBuilder::reads_resource() noexcept {
  (read_resources_.set(resource_id<std::decay_t<Res>>()), ...);
  return *this;
}

template <typename... Res>
inline AccessBuilder &AccessBuilder::writes_resource() noexcept {
  (write_resources_.set(resource_id<std::decay_t<Res>>()), ...);
  return *this;
}

[[nodiscard]] AccessDescriptor AccessBuilder::build() const noexcept {
  return AccessDescriptor{
      .read_components = read_components_,
      .write_components = write_components_,
      .read_resources = read_resources_,
      .write_resources = write_resources_,
  };
}

} // namespace arche
