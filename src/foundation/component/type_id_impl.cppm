module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:component.detail.type_id_impl;

import :type_id;

export namespace arche::internal {

// =============================================================================
// ComponentDescriptor Methods
// =============================================================================

void ComponentDescriptor::destruct(void *ptr) const noexcept {
  if (!is_trivially_destructible && destructor) {
    destructor(ptr);
  }
}

void ComponentDescriptor::relocate(void *dst, void *src) const noexcept {
  if (!is_trivially_copyable && move_assign) {
    move_assign(dst, src);
  } else {
    std::memcpy(dst, src, size);
  }
}

template <typename T>
[[nodiscard]] inline ComponentDescriptor ComponentDescriptor::create() noexcept {
  using Comp = std::decay_t<T>;

  ComponentDescriptor desc;
  desc.id = type_id<Comp>();
  desc.size = sizeof(Comp);
  desc.alignment = alignof(Comp);
  desc.name = type_name<Comp>();
  desc.is_trivially_copyable = std::is_trivially_copyable_v<Comp>;
  desc.is_trivially_destructible = std::is_trivially_destructible_v<Comp>;

  desc.constructor = make_constructor<Comp>();
  desc.destructor = make_destructor<Comp>();
  desc.move_construct = make_move_construct<Comp>();
  desc.move_assign = make_move_assign<Comp>();

  return desc;
}

template <typename C>
[[nodiscard]] inline auto ComponentDescriptor::make_constructor() noexcept
    -> void (*)(void *) {
  return [](void *target) { new (target) C(); };
}

template <typename C>
[[nodiscard]] inline auto ComponentDescriptor::make_destructor() noexcept
    -> void (*)(void *) {
  if constexpr (std::is_trivially_destructible_v<C>) {
    return nullptr;
  } else {
    return [](void *target) { static_cast<C *>(target)->~C(); };
  }
}

template <typename C>
[[nodiscard]] inline auto ComponentDescriptor::make_move_construct() noexcept
    -> void (*)(void *, void *) {
  if constexpr (std::is_trivially_copyable_v<C>) {
    return nullptr;
  } else {
    return [](void *dest, void *src) {
      new (dest) C(std::move(*static_cast<C *>(src)));
    };
  }
}

template <typename C>
[[nodiscard]] inline auto ComponentDescriptor::make_move_assign() noexcept
    -> void (*)(void *, void *) {
  if constexpr (std::is_trivially_copyable_v<C>) {
    return nullptr;
  } else {
    return [](void *dest, void *src) {
      *static_cast<C *>(dest) = std::move(*static_cast<C *>(src));
    };
  }
}

// =============================================================================
// TypeRegistry Implementation
// =============================================================================

TypeRegistry::TypeRegistry() {
  descriptors.reserve(MAX_TYPES);
}

TypeRegistry::TypeRegistry(const TypeRegistry &other)
    : descriptors(other.descriptors) {
  descriptors.reserve(MAX_TYPES);
}

TypeRegistry &TypeRegistry::operator=(const TypeRegistry &other) {
  if (this != &other) {
    descriptors = other.descriptors;
    descriptors.reserve(MAX_TYPES);
  }
  return *this;
}

template <typename T>
inline const ComponentDescriptor &TypeRegistry::register_type() {
  const TypeID id = type_id<T>();
  if (id >= descriptors.size()) {
    descriptors.resize(id + 1);
  }
  if (descriptors[id].id == INVALID_TYPE_ID) {
    descriptors[id] = ComponentDescriptor::create<T>();
  }
  return descriptors[id];
}

template <typename T>
[[nodiscard]] inline const ComponentDescriptor &TypeRegistry::get_descriptor() const {
  const TypeID id = type_id<T>();
  if (id >= descriptors.size() || descriptors[id].id == INVALID_TYPE_ID) {
    throw std::runtime_error(
        "arche::TypeRegistry: Unregistered component type requested"
    );
  }
  return descriptors[id];
}

const ComponentDescriptor &TypeRegistry::get_descriptor(TypeID id) const {
  if (id >= descriptors.size() || descriptors[id].id == INVALID_TYPE_ID) {
    throw std::runtime_error("arche::TypeRegistry: Invalid TypeID requested");
  }
  return descriptors[id];
}

[[nodiscard]] size_t TypeRegistry::registered_count() const noexcept {
  return descriptors.size();
}

} // namespace arche::internal
