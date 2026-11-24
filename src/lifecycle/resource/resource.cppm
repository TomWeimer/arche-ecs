module;

#include <cstdint>
#include <type_traits>
#include <vector>

export module arche:resource;

import :types.detail.resource_id;

export namespace arche {

// =============================================================================
// Resource ID Generation & Reflection
// =============================================================================

namespace detail::resource {

inline ResourceID allocate_resource_id_sequential() noexcept {
  static ResourceID counter = 0;
  return counter++;
}

template <typename T> inline ResourceID get_or_allocate_resource_id() noexcept {
  static const ResourceID id = allocate_resource_id_sequential();
  return id;
}

} // namespace detail::resource

/**
 * @brief Computes a dense sequential ResourceID for a singleton resource type
 * T.
 *
 * Each distinct resource type is assigned a stable 0-indexed sequential
 * identifier at compile/first-call time. Decay-evaluated to ensure
 * const/reference variants map to the identical resource slot.
 *
 * @tparam T The resource type.
 * @return Unique dense sequential ResourceID for type T.
 */
template <typename T> [[nodiscard]] inline ResourceID resource_id() noexcept {
  return detail::resource::get_or_allocate_resource_id<std::decay_t<T>>();
}

// =============================================================================
// ResourceContainer: Zero-RTTI Monotonic Ambient Singleton Storage
// =============================================================================

/**
 * @class ResourceContainer
 * @brief High-performance, storage container for ambient world resources.
 *
 * Manages unique, type-indexed resources independent of entities (e.g.
 * SimulationClock, InputState, PhysicsConfig). Resources are stored in a
 * contiguous, dense slot array indexed by dense sequential 32-bit
 * `ResourceID`s.
 *
 * ### Quick Usage Guide
 * ```cpp
 * ResourceContainer resources;
 *
 * // 1. Emplace or replace a resource:
 * resources.insert<SimulationClock>(0.016f);
 *
 * // 2. Query existence:
 * if (resources.has<SimulationClock>()) {
 *     // 3. Mutable retrieval:
 *     SimulationClock &clock = resources.get<SimulationClock>();
 *     clock.dt = 0.033f;
 * }
 *
 * // 4. Remove resource:
 * resources.remove<SimulationClock>();
 * ```
 */
class ResourceContainer {
private:
  struct ResourceSlot {
    void *ptr{nullptr};
    void (*destructor)(void *){nullptr};
  };

  std::vector<ResourceSlot> slots_;

public:
  // --- Lifecycle -------------------------------------------------------------

  /// Constructs an empty ResourceContainer with zero allocated slots.
  ResourceContainer() = default;

  /// Destroys all stored resources and releases allocated slot memory.
  ~ResourceContainer();

  ResourceContainer(const ResourceContainer &) = delete;
  ResourceContainer &operator=(const ResourceContainer &) = delete;

  /// Move-constructs a ResourceContainer, transferring ownership of all
  /// resource slots.
  ResourceContainer(ResourceContainer &&other) noexcept;

  /// Move-assigns a ResourceContainer, releasing existing resources before
  /// taking ownership.
  ResourceContainer &operator=(ResourceContainer &&other) noexcept;

  /// Destroys all active resources and clears slot allocations.
  void clear();

  // --- Insertion & Modification ----------------------------------------------

  /**
   * @brief Inserts or replaces a global singleton resource of type T.
   *
   * If a resource of type `T` already exists in its assigned slot, its
   * destructor is invoked and existing heap memory is freed before constructing
   * the new instance in-place.
   *
   * @tparam T The resource type to instantiate.
   * @tparam Args Constructor argument types forwarded to `T`.
   * @param args Arguments forwarded to the constructor of `T`.
   * @return Lvalue reference to the newly constructed resource.
   */
  template <typename T, typename... Args> T &insert(Args &&...args);

  // --- Retrieval & Existence Queries -----------------------------------------

  /**
   * @brief Retrieves a mutable reference to the singleton resource of type T.
   *
   * Performs an O(1) direct array lookup using `resource_id<T>()`.
   *
   * @tparam T The resource type to retrieve.
   * @return Mutable lvalue reference to the stored resource.
   * @throws std::runtime_error if the resource of type T is not registered or
   * absent.
   */
  template <typename T> [[nodiscard]] T &get();

  /**
   * @brief Retrieves a read-only reference to the singleton resource of type T.
   *
   * Performs an O(1) direct array lookup using `resource_id<T>()`.
   *
   * @tparam T The resource type to retrieve.
   * @return Const lvalue reference to the stored resource.
   * @throws std::runtime_error if the resource of type T is not registered or
   * absent.
   */
  template <typename T> [[nodiscard]] const T &get() const;

  /**
   * @brief Checks whether a singleton resource of type T is present in the
   * container.
   *
   * @tparam T The resource type to query.
   * @return true if the resource exists and is active, false otherwise.
   */
  template <typename T> [[nodiscard]] bool has() const noexcept;

  /**
   * @brief Checks whether a resource associated with the given ResourceID is
   * present.
   *
   * Fast non-template overload when ResourceID has been pre-computed or cached.
   *
   * @param id The dense sequential resource identifier.
   * @return true if the slot is allocated and contains an active resource,
   * false otherwise.
   */
  [[nodiscard]] bool has(ResourceID id) const noexcept;

  // --- Deletion --------------------------------------------------------------

  /**
   * @brief Removes and destroys the singleton resource of type T if present.
   *
   * Invokes the resource's destructor and frees its heap memory. The assigned
   * slot remains allocated for future reuse to preserve monotonic indexing.
   *
   * @tparam T The resource type to remove.
   * @return true if a resource was present and removed, false if it was absent.
   */
  template <typename T> bool remove();
};

} // namespace arche
