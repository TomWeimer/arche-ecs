module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

export module arche:component_id;

import :component_mask;

export namespace arche {

// =============================================================================
// Internal Reflection & Identifier Generation
// =============================================================================

namespace Internal {

inline ComponentID allocate_component_id_sequential() noexcept {
  static ComponentID global_counter = 0;
  return global_counter++;
}

template <typename T> inline ComponentID get_or_allocate_component_id() noexcept {
  static const ComponentID id = allocate_component_id_sequential();
  return id;
}

template <typename T> constexpr std::string_view get_component_name() noexcept {
#if defined(__clang__) || defined(__GNUC__)
  constexpr std::string_view prefix = "[with T = ";
  constexpr std::string_view suffix = "]";
  constexpr std::string_view sig = __PRETTY_FUNCTION__;
  const size_t start = sig.find(prefix);
  if (start == std::string_view::npos) {
    return "Unknown";
  }
  const size_t end = sig.find(suffix, start);
  return sig.substr(start + prefix.size(), end - (start + prefix.size()));
#elif defined(_MSC_VER)
  constexpr std::string_view prefix = "get_component_name<";
  constexpr std::string_view suffix = ">(";
  constexpr std::string_view sig = __FUNCSIG__;
  const size_t start = sig.find(prefix);
  if (start == std::string_view::npos) {
    return "Unknown";
  }
  const size_t end = sig.find(suffix, start);
  return sig.substr(start + prefix.size(), end - (start + prefix.size()));
#else
  return "Unknown";
#endif
}

} // namespace Internal

// =============================================================================
// Public Component Identification API
// =============================================================================

/**
 * @brief Retrieves the unique 32-bit runtime identifier for component type `T`.
 *
 * @tparam T Component type to identify.
 * @return Monotonically assigned 0-indexed dense ComponentID.
 */
template <typename T>
[[nodiscard]] inline ComponentID component_id() noexcept {
  return Internal::get_or_allocate_component_id<std::decay_t<T>>();
}

/**
 * @brief Retrieves the human-readable demangled name string for component type `T`.
 *
 * @tparam T Component type to inspect.
 * @return String view representing the component type name.
 */
template <typename T>
[[nodiscard]] constexpr std::string_view component_name() noexcept {
  return Internal::get_component_name<std::decay_t<T>>();
}

// =============================================================================
// Component Descriptor (Type Erased VTable & Layout metadata)
// =============================================================================

namespace internal {

/**
 * @struct ComponentDescriptor
 * @brief Runtime metadata and type-erased lifecycle function pointers for a component.
 *
 * Encapsulates size, alignment, triviality traits, and function pointers for
 * in-place construction, destruction, and relocation across archetype chunk boundaries.
 */
struct ComponentDescriptor {
  ComponentID id{INVALID_COMPONENT_ID};         ///< Dense runtime component identifier.
  size_t size{0};                               ///< Size of the component in bytes (`sizeof(T)`).
  size_t alignment{0};                          ///< Required byte alignment (`alignof(T)`).
  std::string_view name{"Unknown"};             ///< Human-readable demangled component name.
  bool is_trivially_copyable{false};            ///< True if memory relocation can use `std::memcpy`.
  bool is_trivially_destructible{false};        ///< True if destructor invocation can be omitted.

  void (*constructor)(void *target){nullptr};              ///< Default placement-new constructor.
  void (*destructor)(void *target){nullptr};               ///< Explicit destructor invocation.
  void (*move_construct)(void *dest, void *src){nullptr};  ///< Placement move constructor.
  void (*move_assign)(void *dest, void *src){nullptr};     ///< Move assignment operator.

  /**
   * @brief Destructs an instance of this component if not trivially destructible.
   *
   * @param ptr Pointer to raw memory containing the constructed component.
   */
  void destruct(void *ptr) const noexcept;

  /**
   * @brief Relocates a component from source to destination memory.
   *
   * Uses `std::memcpy` if trivially copyable, otherwise invokes move assignment.
   *
   * @param dst Pointer to destination memory buffer.
   * @param src Pointer to source memory buffer.
   */
  void relocate(void *dst, void *src) const noexcept;

  /**
   * @brief Constructs a fully populated descriptor for component type `T`.
   *
   * @tparam T Component type to describe.
   * @return Initialized ComponentDescriptor instance.
   */
  template <typename T>
  [[nodiscard]] static ComponentDescriptor create() noexcept;

private:
  template <typename C>
  [[nodiscard]] static auto make_constructor() noexcept -> void (*)(void *);

  template <typename C>
  [[nodiscard]] static auto make_destructor() noexcept -> void (*)(void *);

  template <typename C>
  [[nodiscard]] static auto make_move_construct() noexcept -> void (*)(void *, void *);

  template <typename C>
  [[nodiscard]] static auto make_move_assign() noexcept -> void (*)(void *, void *);
};

// =============================================================================
// Component Column Layout Descriptor
// =============================================================================

/**
 * @struct ComponentColumnLayout
 * @brief Memory layout and alignment descriptor for a component column within an archetype chunk.
 *
 * Defines contiguous array parameters (component ID, byte size, required alignment,
 * starting byte offset inside the 64KB chunk page, and lifecycle metadata pointer).
 */
struct ComponentColumnLayout {
  ComponentID component_id{INVALID_COMPONENT_ID}; ///< Component type identifier.
  uint32_t size{0};                               ///< Byte size of each component instance.
  uint32_t alignment{0};                          ///< Byte alignment constraint.
  uint32_t chunk_byte_offset{0};                  ///< Starting byte offset within the chunk memory page.
  const ComponentDescriptor *descriptor{nullptr}; ///< Pointer to type-erased lifecycle vtable.
};

// =============================================================================
// ComponentRegistry (Component Type Registry & Descriptor Lookup)
// =============================================================================

/**
 * @class ComponentRegistry
 * @brief Dynamic registry maintaining ComponentDescriptor metadata indexed by ComponentID.
 *
 * Guarantees zero memory overhead over an internal `std::vector<ComponentDescriptor>`.
 */
struct ComponentRegistry {
private:
  std::vector<ComponentDescriptor> descriptors;

public:
  static constexpr size_t MAX_TYPES = 128;

  ComponentRegistry();
  ~ComponentRegistry() = default;

  ComponentRegistry(const ComponentRegistry &other);
  ComponentRegistry &operator=(const ComponentRegistry &other);
  ComponentRegistry(ComponentRegistry &&) noexcept = default;
  ComponentRegistry &operator=(ComponentRegistry &&) noexcept = default;

  /**
   * @brief Registers component type `T` into the registry if not already registered.
   *
   * @tparam T Component type to register.
   * @return Immutable reference to the stored descriptor.
   */
  template <typename T>
  const ComponentDescriptor &register_component();

  /**
   * @brief Retrieves the descriptor for component type `T`.
   *
   * @tparam T Component type.
   * @return Immutable reference to the stored descriptor.
   * @throws std::runtime_error If `T` has not been registered.
   */
  template <typename T>
  [[nodiscard]] const ComponentDescriptor &get_descriptor() const;

  /**
   * @brief Registers a raw POD component type by ID, size, and alignment.
   *
   * Used for restoring archetypes from snapshots without static type instantiation.
   */
  const ComponentDescriptor &register_pod(
      ComponentID id,
      size_t size,
      size_t alignment
  );

  /**
   * @brief Retrieves the descriptor for a runtime ComponentID.
   *
   * @param id Component identifier.
   * @return Immutable reference to the stored descriptor.
   * @throws std::runtime_error If `id` is invalid or unregistered.
   */
  [[nodiscard]] const ComponentDescriptor &get_descriptor(ComponentID id) const;

  /**
   * @brief Returns the total number of registered types in this registry.
   */
  [[nodiscard]] size_t registered_count() const noexcept;
};

// --- Invariant Guarantees ----------------------------------------------------

static_assert(
    sizeof(ComponentRegistry) == sizeof(std::vector<ComponentDescriptor>),
    "ComponentRegistry must introduce zero memory overhead over std::vector"
);

} // namespace internal

} // namespace arche
