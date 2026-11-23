module;

#include <type_traits>

export module arche:access_builder;

import :access;
import :type_id;
import :resource;

export namespace arche {

class AccessBuilder {
public:
  template <typename... Comps> AccessBuilder &reads() noexcept {
    (read_components_.set(type_id<std::decay_t<Comps>>()), ...);
    return *this;
  }

  template <typename... Comps> AccessBuilder &writes() noexcept {
    (write_components_.set(type_id<std::decay_t<Comps>>()), ...);
    return *this;
  }

  template <typename... Res> AccessBuilder &reads_resource() noexcept {
    (read_resources_.set(resource_id<std::decay_t<Res>>()), ...);
    return *this;
  }

  template <typename... Res> AccessBuilder &writes_resource() noexcept {
    (write_resources_.set(resource_id<std::decay_t<Res>>()), ...);
    return *this;
  }

  [[nodiscard]] AccessDescriptor build() const noexcept {
    return AccessDescriptor{
        .read_components = read_components_,
        .write_components = write_components_,
        .read_resources = read_resources_,
        .write_resources = write_resources_,
    };
  }

private:
  ComponentMask read_components_{};
  ComponentMask write_components_{};
  ResourceMask read_resources_{};
  ResourceMask write_resources_{};
};

using Access = AccessBuilder;

} // namespace arche
