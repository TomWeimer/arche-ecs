module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:type_id;

import :entity;
import :component_mask;
import :filters;

export namespace arche {

namespace Internal {

inline TypeID allocate_type_id_sequential() noexcept {
  static TypeID global_counter = 0;
  return global_counter++;
}

template <typename T> inline TypeID get_or_allocate_type_id() noexcept {
  static const TypeID id = allocate_type_id_sequential();
  return id;
}

template <typename T> constexpr std::string_view get_type_name() noexcept {
#if defined(__clang__) || defined(__GNUC__)
  constexpr std::string_view prefix = "[with T = ";
  constexpr std::string_view suffix = "]";
  constexpr std::string_view sig = __PRETTY_FUNCTION__;
  const size_t start = sig.find(prefix);
  if (start == std::string_view::npos)
    return "Unknown";
  const size_t end = sig.find(suffix, start);
  return sig.substr(start + prefix.size(), end - (start + prefix.size()));
#elif defined(_MSC_VER)
  constexpr std::string_view prefix = "get_type_name<";
  constexpr std::string_view suffix = ">(";
  constexpr std::string_view sig = __FUNCSIG__;
  const size_t start = sig.find(prefix);
  if (start == std::string_view::npos)
    return "Unknown";
  const size_t end = sig.find(suffix, start);
  return sig.substr(start + prefix.size(), end - (start + prefix.size()));
#else
  return "Unknown";
#endif
}

} // namespace Internal

template <typename T> [[nodiscard]] inline TypeID type_id() noexcept {
  return Internal::get_or_allocate_type_id<std::decay_t<T>>();
}

template <typename T>
[[nodiscard]] constexpr std::string_view type_name() noexcept {
  return Internal::get_type_name<std::decay_t<T>>();
}

struct ComponentDescriptor {
  TypeID id{INVALID_TYPE_ID};
  size_t size{0};
  size_t alignment{0};
  std::string_view name{"Unknown"};
  bool is_trivially_copyable{false};
  bool is_trivially_destructible{false};

  void (*constructor)(void *target){nullptr};
  void (*destructor)(void *target){nullptr};
  void (*move_construct)(void *dest, void *src){nullptr};
  void (*move_assign)(void *dest, void *src){nullptr};

  inline void destruct(void *ptr) const noexcept {
    if (!is_trivially_destructible && destructor) {
      destructor(ptr);
    }
  }

  inline void relocate(void *dst, void *src) const noexcept {
    if (!is_trivially_copyable && move_assign) {
      move_assign(dst, src);
    } else {
      std::memcpy(dst, src, size);
    }
  }

  template <typename T>
  [[nodiscard]] static ComponentDescriptor create() noexcept {
    using ComponentType = std::decay_t<T>;

    ComponentDescriptor desc;
    desc.id = type_id<ComponentType>();
    desc.size = sizeof(ComponentType);
    desc.alignment = alignof(ComponentType);
    desc.name = type_name<ComponentType>();
    desc.is_trivially_copyable = std::is_trivially_copyable_v<ComponentType>;
    desc.is_trivially_destructible = std::is_trivially_destructible_v<
        ComponentType>;

    desc.constructor = make_constructor<ComponentType>();
    desc.destructor = make_destructor<ComponentType>();
    desc.move_construct = make_move_construct<ComponentType>();
    desc.move_assign = make_move_assign<ComponentType>();

    return desc;
  }

private:
  template <typename C>
  [[nodiscard]] static inline auto make_constructor() noexcept
      -> void (*)(void *) {
    return [](void *target) { new (target) C(); };
  }

  template <typename C>
  [[nodiscard]] static inline auto make_destructor() noexcept
      -> void (*)(void *) {
    if constexpr (std::is_trivially_destructible_v<C>) {
      return nullptr;
    } else {
      return [](void *target) { static_cast<C *>(target)->~C(); };
    }
  }

  template <typename C>
  [[nodiscard]] static inline auto make_move_construct() noexcept
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
  [[nodiscard]] static inline auto make_move_assign() noexcept
      -> void (*)(void *, void *) {
    if constexpr (std::is_trivially_copyable_v<C>) {
      return nullptr;
    } else {
      return [](void *dest, void *src) {
        *static_cast<C *>(dest) = std::move(*static_cast<C *>(src));
      };
    }
  }
};

struct TypeRegistry {
private:
  std::vector<ComponentDescriptor> descriptors;

public:
  static constexpr size_t MAX_TYPES = 128;

  TypeRegistry() { descriptors.reserve(MAX_TYPES); }
  ~TypeRegistry() = default;

  TypeRegistry(const TypeRegistry &other) : descriptors(other.descriptors) {
    descriptors.reserve(MAX_TYPES);
  }
  TypeRegistry &operator=(const TypeRegistry &other) {
    if (this != &other) {
      descriptors = other.descriptors;
      descriptors.reserve(MAX_TYPES);
    }
    return *this;
  }
  TypeRegistry(TypeRegistry &&) noexcept = default;
  TypeRegistry &operator=(TypeRegistry &&) noexcept = default;

  template <typename T> const ComponentDescriptor &register_type() {
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
  [[nodiscard]] const ComponentDescriptor &get_descriptor() const {
    const TypeID id = type_id<T>();
    if (id >= descriptors.size() || descriptors[id].id == INVALID_TYPE_ID) {
      throw std::runtime_error(
          "arche::TypeRegistry: Unregistered component type requested"
      );
    }
    return descriptors[id];
  }

  [[nodiscard]] const ComponentDescriptor &get_descriptor(TypeID id) const {
    if (id >= descriptors.size() || descriptors[id].id == INVALID_TYPE_ID) {
      throw std::runtime_error("arche::TypeRegistry: Invalid TypeID requested");
    }
    return descriptors[id];
  }

  [[nodiscard]] size_t registered_count() const noexcept {
    return descriptors.size();
  }
};

static_assert(
    sizeof(TypeRegistry) == sizeof(std::vector<ComponentDescriptor>),
    "TypeRegistry must introduce zero memory overhead over std::vector"
);

} // namespace arche
