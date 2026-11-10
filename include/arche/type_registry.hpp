#pragma once

#include "arche/type_id.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace arche {

// --- Component Descriptor ----------------------------------------------------

/**
 * @struct ComponentDescriptor
 * @brief Type-erased memory dimensions, triviality traits, and lifecycle
 * function pointers.
 *
 * @details
 * In an archetype ECS:
 * - Chunk storage allocates and moves component data using type-erased memory
 * buffers.
 * - For trivial Plain-Old-Data (POD) types (e.g., `struct Position { float x,
 * y; };`), `is_trivially_copyable` and `is_trivially_destructible` allow chunk
 * operations to use ultra-fast `std::memcpy` and skip destructors completely.
 * - For non-trivial types (or types with custom move semantics), function
 * pointers
 *   (`constructor`, `destructor`, `move_construct`, `move_assign`) provide
 * deterministic lifecycle execution without virtual tables or RTTI overhead.
 */
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

  /**
   * @brief Factory method creating a type descriptor from a concrete component
   * type T.
   *
   * @tparam T Component struct or class.
   * @return ComponentDescriptor with populated layout traits and lifecycle
   * function pointers.
   */
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
  // --- Inlined Lifecycle Function Pointer Helpers ----------------------------

  /**
   * @brief Generates a type-erased placement default constructor.
   *
   * In arche-ecs, chunks allocate large blocks of raw, unformatted byte memory.
   * This lambda uses C++ placement-new (`new (target) C()`) to construct an
   * instance of component type C directly in-place inside the raw memory buffer
   * without performing any heap allocation.
   */
  template <typename C>
  [[nodiscard]] static inline auto make_constructor() noexcept
      -> void (*)(void *) {
    return [](void *target) {
      new (target) C(); // Construct type C in-place at the target byte address
    };
  }

  /**
   * @brief Generates a type-erased destructor (or nullptr if trivial).
   *
   * Fast-path optimization:
   * - If type C is trivially destructible (e.g. plain POD structs with
   * floats/ints), its destructor does nothing. We return `nullptr` so chunk
   * loops can skip calling destructors entirely, eliminating millions of dead
   * CPU cycles.
   * - If type C is non-trivial (e.g. manages resources), we cast the raw
   * pointer back to `C*` and explicitly invoke its destructor: `ptr->~C()`.
   */
  template <typename C>
  [[nodiscard]] static inline auto make_destructor() noexcept
      -> void (*)(void *) {
    if constexpr (std::is_trivially_destructible_v<C>) {
      return nullptr; // Skip destructor invocation for trivial types
    } else {
      return [](void *target) {
        static_cast<C *>(target)->~C(); // Explicitly call destructor on target
      };
    }
  }

  /**
   * @brief Generates a type-erased move constructor.
   *
   * Used when relocating an entity to an uninitialized chunk slot (e.g. during
   * archetype transitions or chunk Swap-and-Pop compaction):
   * 1. Casts raw `src` pointer to `C*` and moves from it.
   * 2. Placement-news a new `C` directly into the uninitialized `dest` address.
   */
  template <typename C>
  [[nodiscard]] static inline auto make_move_construct() noexcept
      -> void (*)(void *, void *) {
    return [](void *dest, void *src) {
      new (dest) C(std::move(*static_cast<C *>(src)));
    };
  }

  /**
   * @brief Generates a type-erased move assignment operator.
   *
   * Used when transferring component data into a slot that has already been
   * constructed. Casts both raw pointers to `C*` and invokes `dest =
   * std::move(src)`.
   */
  template <typename C>
  [[nodiscard]] static inline auto make_move_assign() noexcept
      -> void (*)(void *, void *) {
    return [](void *dest, void *src) {
      *static_cast<C *>(dest) = std::move(*static_cast<C *>(src));
    };
  }
};

// --- Type Registry -----------------------------------------------------------

/**
 * @struct TypeRegistry
 * @brief Centralized registry storing component descriptors indexed directly by
 * TypeID.
 *
 * @details
 * Key architectural guarantees:
 * 1. O(1) Descriptor Lookup:
 *    - Descriptors are stored in a contiguous array indexed by monotonic
 *      0-indexed `TypeID`.
 *    - Lookup is a single C-array index dereference with zero hashing or string
 *      comparison.
 * 2. Dynamic Monotonic Expansion:
 *    - As new types are registered, the descriptor vector expands to fit the
 *      highest TypeID.
 * 3. Zero Polymorphic Overhead:
 *    - Defined as a `struct` with zero virtual methods and zero inheritance.
 *    - Its memory footprint is 100% identical to
 * `std::vector<ComponentDescriptor>`.
 *    - The static_assert below guarantees zero wrapper overhead.
 */
struct TypeRegistry {
private:
  // --- Member Data -----------------------------------------------------------

  std::vector<ComponentDescriptor>
      descriptors; ///< Dense descriptor array indexed by TypeID.

public:
  // --- Constructors & Destructors --------------------------------------------

  TypeRegistry() = default;
  ~TypeRegistry() = default;

  TypeRegistry(const TypeRegistry &) = default;
  TypeRegistry &operator=(const TypeRegistry &) = default;
  TypeRegistry(TypeRegistry &&) noexcept = default;
  TypeRegistry &operator=(TypeRegistry &&) noexcept = default;

  // --- Type Registration -----------------------------------------------------

  /**
   * @brief Registers or retrieves the descriptor for component type T.
   *
   * @tparam T Component type to register.
   * @return Const reference to the registered ComponentDescriptor.
   */
  template <typename T> const ComponentDescriptor &register_type() {
    const TypeID id = type_id<T>();
    // Resize the vector if needed, resizing one by one do not hurt performance
    // as it is done only at the start of the programm.
    if (id >= descriptors.size()) {
      descriptors.resize(id + 1);
    }
    if (descriptors[id].id == INVALID_TYPE_ID) {
      descriptors[id] = ComponentDescriptor::create<T>();
    }
    return descriptors[id];
  }

  // --- Descriptor Inspection -------------------------------------------------

  /**
   * @brief Retrieves the descriptor for component type T.
   *
   * @tparam T Component type to inspect.
   * @return Const reference to ComponentDescriptor.
   * @throws std::runtime_error If the type has not been registered.
   */
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

  /**
   * @brief Retrieves the descriptor for a raw TypeID.
   *
   * @param id TypeID to inspect.
   * @return Const reference to ComponentDescriptor.
   * @throws std::runtime_error If TypeID is out of range or unregistered.
   */
  [[nodiscard]] const ComponentDescriptor &get_descriptor(TypeID id) const {
    if (id >= descriptors.size() || descriptors[id].id == INVALID_TYPE_ID) {
      throw std::runtime_error("arche::TypeRegistry: Invalid TypeID requested");
    }
    return descriptors[id];
  }

  /**
   * @brief Returns the total count of registered component types.
   */
  [[nodiscard]] size_t registered_count() const noexcept {
    return descriptors.size();
  }
};

// --- Struct Memory Layout Invariant Verification -----------------------------

static_assert(
    sizeof(TypeRegistry) == sizeof(std::vector<ComponentDescriptor>),
    "TypeRegistry must introduce zero memory overhead over std::vector"
);

} // namespace arche
