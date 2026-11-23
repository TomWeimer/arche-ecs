module;

#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:resource;

import :types.detail.resource_id;

export namespace arche {

namespace Internal {

inline ResourceID allocate_resource_id_sequential() noexcept {
  static ResourceID counter = 0;
  return counter++;
}

template <typename T> inline ResourceID get_or_allocate_resource_id() noexcept {
  static const ResourceID id = allocate_resource_id_sequential();
  return id;
}

} // namespace Internal

template <typename T> [[nodiscard]] inline ResourceID resource_id() noexcept {
  return Internal::get_or_allocate_resource_id<std::decay_t<T>>();
}

class ResourceContainer {
private:
  struct ResourceSlot {
    void *ptr{nullptr};
    void (*destructor)(void *){nullptr};
  };

  std::vector<ResourceSlot> slots_;

public:
  ResourceContainer() = default;
  ~ResourceContainer() { clear(); }

  ResourceContainer(const ResourceContainer &) = delete;
  ResourceContainer &operator=(const ResourceContainer &) = delete;

  ResourceContainer(ResourceContainer &&other) noexcept
      : slots_(std::move(other.slots_)) {}

  ResourceContainer &operator=(ResourceContainer &&other) noexcept {
    if (this != &other) {
      clear();
      slots_ = std::move(other.slots_);
    }
    return *this;
  }

  void clear() {
    for (auto &slot : slots_) {
      if (slot.ptr) {
        if (slot.destructor) {
          slot.destructor(slot.ptr);
        }
        ::operator delete(slot.ptr);
        slot.ptr = nullptr;
        slot.destructor = nullptr;
      }
    }
    slots_.clear();
  }

  template <typename T> [[nodiscard]] T &get() {
    const ResourceID id = resource_id<T>();
    if (id >= slots_.size() || slots_[id].ptr == nullptr) {
      throw std::runtime_error(
          "arche::ResourceContainer: requested resource not found"
      );
    }
    return *static_cast<T *>(slots_[id].ptr);
  }

  template <typename T> [[nodiscard]] const T &get() const {
    const ResourceID id = resource_id<T>();
    if (id >= slots_.size() || slots_[id].ptr == nullptr) {
      throw std::runtime_error(
          "arche::ResourceContainer: requested resource not found"
      );
    }
    return *static_cast<const T *>(slots_[id].ptr);
  }

  template <typename T> [[nodiscard]] bool has() const noexcept {
    const ResourceID id = resource_id<T>();
    return (id < slots_.size()) && (slots_[id].ptr != nullptr);
  }

  [[nodiscard]] inline bool has(ResourceID id) const noexcept {
    return (id < slots_.size()) && (slots_[id].ptr != nullptr);
  }

  template <typename T, typename... Args> T &insert(Args &&...args) {
    const ResourceID id = resource_id<T>();

    if (id >= slots_.size()) {
      slots_.resize(id + 1, {nullptr, nullptr});
    }

    if (slots_[id].ptr != nullptr) {
      if (slots_[id].destructor) {
        slots_[id].destructor(slots_[id].ptr);
      }
      ::operator delete(slots_[id].ptr);
      slots_[id].ptr = nullptr;
      slots_[id].destructor = nullptr;
    }

    T *raw_ptr = new T(std::forward<Args>(args)...);
    slots_[id].ptr = raw_ptr;

    if constexpr (!std::is_trivially_destructible_v<T>) {
      slots_[id].destructor = [](void *p) { static_cast<T *>(p)->~T(); };
    } else {
      slots_[id].destructor = nullptr;
    }

    return *raw_ptr;
  }

  template <typename T> bool remove() {
    const ResourceID id = resource_id<T>();

    if (id < slots_.size() && slots_[id].ptr != nullptr) {
      if (slots_[id].destructor) {
        slots_[id].destructor(slots_[id].ptr);
      }
      ::operator delete(slots_[id].ptr);
      slots_[id].ptr = nullptr;
      slots_[id].destructor = nullptr;
      return true;
    }
    return false;
  }
};

} // namespace arche
