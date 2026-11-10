#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace arche {

// --- Type ID Definitions -----------------------------------------------------

using TypeID = uint32_t;
inline constexpr TypeID INVALID_TYPE_ID = 0xFFFFFFFF;

// --- Internal Monotonic Type Counter & Name Demangling -----------------------

namespace Internal {

/**
 * @brief Generates dense, monotonic 0-indexed TypeIDs across the application.
 *
 * @details
 * ### How it works:
 * - Maintains a persistent `static TypeID global_counter = 0` stored in static
 *   memory across the lifetime of the program.
 * - Each time a brand-new, unseen component type calls
 * `get_or_allocate_type_id<T>()`, this function is invoked exactly once to
 * return the current counter value and increment it (`0, 1, 2, 3, ...`).
 *
 * ### Why dense 0-indexed IDs are critical:
 * 1. **Direct Array Indexing (O(1)):** Unlike C++ RTTI (`typeid`), which yields
 *    sparse non-contiguous hashes, dense sequential integers allow component
 *    descriptors to be indexed directly into flat C-arrays (`descriptors[id]`)
 *    without hash tables or pointer chasing.
 * 2. **Bitmask Positioning:** The resulting integer maps directly to the bit
 *    index in a 128-bit `ComponentMask` (bit 0, bit 1, bit 2...).
 *
 * @return TypeID The next available sequential integer ID.
 */
inline TypeID allocate_type_id_sequential() noexcept {
  static TypeID global_counter = 0;
  return global_counter++;
}

/**
 * @brief Retrieves or allocates a persistent static TypeID for type T.
 *
 * @details
 * ### How "Get or Allocate" works via C++ `static` semantics:
 * In C++, a `static` local variable inside a function is initialized **only
 * once**, the very first time control passes through its definition:
 *
 * 1. **First call for type T ("Allocate"):**
 *    - The static variable `id` is not yet initialized.
 *    - The compiler invokes `allocate_type_id_sequential()`, pulling the next
 *      sequential integer (0, 1, 2...) from `global_counter`.
 *    - The result is stored permanently in `id`.
 *
 * 2. **Subsequent calls for type T ("Get"):**
 *    - C++ guarantees initialization is completely skipped.
 *    - `allocate_type_id_sequential()` is **never called again** for this type.
 *    - The function directly returns the cached `id` from memory in a single
 *      CPU clock cycle (O(1)).
 *
 * 3. **Per-Type Specialization:**
 *    - Because this is a template function, the compiler generates a distinct
 *      copy of `get_or_allocate_type_id<T>` for each unique type `T`.
 *    - Thus, `Position` gets its own `static id = 0`, `Velocity` gets its own
 *      `static id = 1`, and so on.
 */
template <typename T> inline TypeID get_or_allocate_type_id() noexcept {
  static const TypeID id = allocate_type_id_sequential();
  return id;
}

/**
 * @brief Compile-time demangled type name helper for diagnostics and debugging.
 */
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

// --- Public Type Reflection API ----------------------------------------------

/**
 * @brief Retrieves the unique, sequential 0-indexed TypeID for a component
 * type.
 *
 * @details
 * - Employs a template-specialized inline static local variable.
 * - Initial call assigns the next available monotonic integer (0, 1, 2, ...).
 * - Subsequent calls resolve in a single memory read instruction ($O(1)$) with
 * zero hashing.
 * - Strips cv-qualifiers and references via `std::decay_t`.
 *
 * @tparam T Component struct or class.
 * @return TypeID Dense sequential integer (0, 1, 2, ...).
 */
template <typename T> [[nodiscard]] inline TypeID type_id() noexcept {
  return Internal::get_or_allocate_type_id<std::decay_t<T>>();
}

/**
 * @brief Retrieves the human-readable string name of a component type at
 * compile time.
 *
 * @tparam T Component struct or class.
 * @return std::string_view Demangled type name.
 */
template <typename T>
[[nodiscard]] constexpr std::string_view type_name() noexcept {
  return Internal::get_type_name<std::decay_t<T>>();
}

} // namespace arche
