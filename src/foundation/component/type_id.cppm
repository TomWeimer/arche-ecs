module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

export module arche:type_id;

import :entity;
import :component_mask;

export namespace arche {

// =============================================================================
// Internal Reflection & Identifier Generation
// =============================================================================

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
  if (start == std::string_view::npos) {
    return "Unknown";
  }
  const size_t end = sig.find(suffix, start);
  return sig.substr(start + prefix.size(), end - (start + prefix.size()));
#elif defined(_MSC_VER)
  constexpr std::string_view prefix = "get_type_name<";
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
// Public Type Identification API
// =============================================================================

/**
 * @brief Retrieves the unique 32-bit runtime identifier for component type `T`.
 *
 * @tparam T Component type to identify.
 * @return Monotonically assigned 0-indexed dense TypeID.
 */
template <typename T>
[[nodiscard]] inline TypeID type_id() noexcept {
  return Internal::get_or_allocate_type_id<std::decay_t<T>>();
}

/**
 * @brief Retrieves the human-readable demangled name string for component type `T`.
 *
 * @tparam T Component type to inspect.
 * @return String view representing the component type name.
 */
template <typename T>
[[nodiscard]] constexpr std::string_view type_name() noexcept {
  return Internal::get_type_name<std::decay_t<T>>();
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
  TypeID id{INVALID_TYPE_ID};               ///< Dense runtime type identifier.
  size_t size{0};                           ///< Size of the component in bytes (`sizeof(T)`).
  size_t alignment{0};                      ///< Required byte alignment (`alignof(T)`).
  std::string_view name{"Unknown"};         ///< Human-readable demangled type name.
  bool is_trivially_copyable{false};        ///< True if memory relocation can use `std::memcpy`.
  bool is_trivially_destructible{false};    ///< True if destructor invocation can be omitted.

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
// TypeRegistry (Component Type Registry & Descriptor Lookup)
// =============================================================================

/**
 * @class TypeRegistry
 * @brief Dynamic registry maintaining ComponentDescriptor metadata indexed by TypeID.
 *
 * Guarantees zero memory overhead over an internal `std::vector<ComponentDescriptor>`.
 */
struct TypeRegistry {
private:
  std::vector<ComponentDescriptor> descriptors;

public:
  static constexpr size_t MAX_TYPES = 128;

  TypeRegistry();
  ~TypeRegistry() = default;

  TypeRegistry(const TypeRegistry &other);
  TypeRegistry &operator=(const TypeRegistry &other);
  TypeRegistry(TypeRegistry &&) noexcept = default;
  TypeRegistry &operator=(TypeRegistry &&) noexcept = default;

  /**
   * @brief Registers component type `T` into the registry if not already registered.
   *
   * @tparam T Component type to register.
   * @return Immutable reference to the stored descriptor.
   */
  template <typename T>
  const ComponentDescriptor &register_type();

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
   * @brief Retrieves the descriptor for a runtime TypeID.
   *
   * @param id Type identifier.
   * @return Immutable reference to the stored descriptor.
   * @throws std::runtime_error If `id` is invalid or unregistered.
   */
  [[nodiscard]] const ComponentDescriptor &get_descriptor(TypeID id) const;

  /**
   * @brief Returns the total number of registered types in this registry.
   */
  [[nodiscard]] size_t registered_count() const noexcept;
};

// --- Invariant Guarantees ----------------------------------------------------

static_assert(
    sizeof(TypeRegistry) == sizeof(std::vector<ComponentDescriptor>),
    "TypeRegistry must introduce zero memory overhead over std::vector"
);

} // namespace internal

} // namespace arche
