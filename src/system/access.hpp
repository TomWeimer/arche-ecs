#pragma once

#include "arche/types/access.hpp"
#include "component/type_id.hpp"

namespace arche {

class AccessBuilder {
public:
  template <typename... Comps> AccessBuilder &reads() noexcept;
  template <typename... Comps> AccessBuilder &writes() noexcept;
  template <typename... Res> AccessBuilder &reads_resource() noexcept;
  template <typename... Res> AccessBuilder &writes_resource() noexcept;
  [[nodiscard]] AccessDescriptor build() const noexcept;

private:
  ComponentMask read_components_{};
  ComponentMask write_components_{};
  ResourceMask read_resources_{};
  ResourceMask write_resources_{};
};

using Access = AccessBuilder;

} // namespace arche

#include "system/access.inl"
