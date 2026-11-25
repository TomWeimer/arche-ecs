module;

#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:resource.detail.impl;

import :resource;

export namespace arche::internal {

// =============================================================================
// ResourceContainer Lifecycle & Storage Implementations
// =============================================================================

ResourceContainer::~ResourceContainer() {
  clear();
}

ResourceContainer::ResourceContainer(ResourceContainer &&other) noexcept
    : slots_(std::move(other.slots_)) {}

ResourceContainer &ResourceContainer::operator=(
    ResourceContainer &&other
) noexcept {
  if (this != &other) {
    clear();
    slots_ = std::move(other.slots_);
  }
  return *this;
}

void ResourceContainer::clear() {
  // 1. Iterate over all allocated slots and invoke registered destructors.
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

  // 2. Release slot vector storage.
  slots_.clear();
}

// =============================================================================
// Resource Retrieval & Existence Queries
// =============================================================================

template <typename T>
[[nodiscard]] inline T &ResourceContainer::get() {
  // 1. Resolve dense ResourceID for type T.
  const ResourceID id = resource_id<T>();

  // 2. Verify slot bounds and instance presence.
  if (id >= slots_.size() || slots_[id].ptr == nullptr) {
    throw std::runtime_error(
        "arche::ResourceContainer: requested resource not found"
    );
  }

  // 3. Return typed reference to stored memory.
  return *static_cast<T *>(slots_[id].ptr);
}

template <typename T>
[[nodiscard]] inline const T &ResourceContainer::get() const {
  // 1. Resolve dense ResourceID for type T.
  const ResourceID id = resource_id<T>();

  // 2. Verify slot bounds and instance presence.
  if (id >= slots_.size() || slots_[id].ptr == nullptr) {
    throw std::runtime_error(
        "arche::ResourceContainer: requested resource not found"
    );
  }

  // 3. Return typed const reference to stored memory.
  return *static_cast<const T *>(slots_[id].ptr);
}

template <typename T>
[[nodiscard]] inline bool ResourceContainer::has() const noexcept {
  const ResourceID id = resource_id<T>();
  return (id < slots_.size()) && (slots_[id].ptr != nullptr);
}

[[nodiscard]] bool ResourceContainer::has(ResourceID id) const noexcept {
  return (id < slots_.size()) && (slots_[id].ptr != nullptr);
}

// =============================================================================
// Resource Insertion & Removal
// =============================================================================

template <typename T, typename... Args>
inline T &ResourceContainer::insert(Args &&...args) {
  // 1. Resolve dense sequential ResourceID.
  const ResourceID id = resource_id<T>();

  // 2. Resize slot array if this resource type has never been allocated.
  if (id >= slots_.size()) {
    slots_.resize(id + 1, {nullptr, nullptr});
  }

  // 3. Destroy previous instance if slot was already occupied.
  if (slots_[id].ptr != nullptr) {
    if (slots_[id].destructor) {
      slots_[id].destructor(slots_[id].ptr);
    }
    ::operator delete(slots_[id].ptr);
    slots_[id].ptr = nullptr;
    slots_[id].destructor = nullptr;
  }

  // 4. Construct new instance in heap storage and bind to slot.
  T *raw_ptr = new T(std::forward<Args>(args)...);
  slots_[id].ptr = raw_ptr;

  // 5. Register type-erased destructor function pointer if non-trivially destructible.
  if constexpr (!std::is_trivially_destructible_v<T>) {
    slots_[id].destructor = [](void *p) { static_cast<T *>(p)->~T(); };
  } else {
    slots_[id].destructor = nullptr;
  }

  return *raw_ptr;
}

template <typename T>
inline bool ResourceContainer::remove() {
  // 1. Resolve dense sequential ResourceID.
  const ResourceID id = resource_id<T>();

  // 2. If slot is allocated and active, destroy instance and deallocate memory.
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

} // namespace arche
