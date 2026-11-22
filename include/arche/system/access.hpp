#pragma once

#include "arche/component/component_mask.hpp"
#include "arche/resource/resource.hpp"
#include "arche/component/type_id.hpp"

/**
 * @file access.hpp
 * @brief Declarative component and resource access descriptors for DAG
 * scheduling.
 *
 * @details
 * In a high-performance multithreaded ECS, data races occur if two systems
 * executing in parallel touch the same memory, and at least one of them
 * performs a write.
 *
 * Traditional game engines solve this with runtime mutexes or reader-writer
 * locks (`std::shared_mutex`), which causes catastrophic cache invalidation,
 * thread stalls, and deadlock hazards on 120 FPS simulation loops.
 *
 * `arche-ecs` eliminates runtime locking completely through **Declarative
 * AccessBuilder Descriptors**:
 * - Systems declare their component and resource access intentions at
 *   initialization (e.g. `AccessBuilder{}.reads<Mass>().writes<Velocity>()`).
 * - Intentions are stored as 128-bit SIMD bitmasks (`ComponentMask` and
 *   `ResourceMask`).
 * - Conflict detection between any two systems executes in **1 to 2 CPU vector
 *   instructions** with zero branching or allocations.
 * - The scheduler compiles this into a Directed Acyclic Graph (DAG) of
 *   guaranteed conflict-free parallel batches.
 */
namespace arche {

/**
 * @brief 128-bit SIMD bitset representing presence and access to World
 * Resources.
 *
 * Reuses the 128-bit fixed-width `ComponentMask` layout for instant bitwise
 * operations.
 */
using ResourceMask = ComponentMask;

// --- AccessDescriptor -------------------------------------------------------

/**
 * @struct AccessDescriptor
 * @brief Exact bitmask footprint of components and resources accessed by a
 * system.
 *
 * @details
 * Stored as four 128-bit masks:
 * 1. `read_components`: Entities' components accessed read-only (`const T&`).
 * 2. `write_components`: Entities' components modified mutably (`T&`).
 * 3. `read_resources`: World singletons accessed read-only (`const T&`).
 * 4. `write_resources`: World singletons modified mutably (`T&`).
 */
struct AccessDescriptor {
  /// Bitmask of component types read by the system.
  ComponentMask read_components{};

  /// Bitmask of component types written by the system.
  ComponentMask write_components{};

  /// Bitmask of world resource types read by the system.
  ResourceMask read_resources{};

  /// Bitmask of world resource types written by the system.
  ResourceMask write_resources{};

  /**
   * @brief Evaluates whether this access descriptor conflicts with another in
   * parallel execution.
   *
   * @details
   * Compiles down to hardware vector instructions (`pand`, `por`, `ptest` on
   * x86-64, or `and`, `orr` on ARM NEON).
   *
   * @param other The other system's access descriptor.
   * @return true If a data race would occur by running both systems
   * concurrently.
   * @return false If both systems can safely execute in parallel without locks.
   */
  [[nodiscard]] inline bool conflicts_with(
      const AccessDescriptor &other
  ) const noexcept {
    // 1. Component conflicts: Read-Write, Write-Read, Write-Write
    if (write_components.contains_any(
            other.read_components | other.write_components
        )) {
      return true;
    }
    if (other.write_components.contains_any(
            read_components | write_components
        )) {
      return true;
    }

    // 2. Resource conflicts: Read-Write, Write-Read, Write-Write
    if (write_resources.contains_any(
            other.read_resources | other.write_resources
        )) {
      return true;
    }
    if (other.write_resources.contains_any(read_resources | write_resources)) {
      return true;
    }

    return false;
  }

  /**
   * @brief Checks if the descriptor specifies zero component and resource
   * access.
   */
  [[nodiscard]] inline bool is_empty() const noexcept {
    return read_components.empty() && write_components.empty() &&
        read_resources.empty() && write_resources.empty();
  }
};

// --- Fluent AccessBuilder Builder
// ---------------------------------------------------

/**
 * @class AccessBuilder
 * @brief Fluent, type-safe builder for constructing `AccessDescriptor`
 * bitmasks.
 *
 * @details
 * Eliminates manual TypeID bit calculation. Converts C++ component and resource
 * types into 128-bit bitmasks using compile-time static reflection
 * (`type_id<T>()` and `resource_id<T>()`).
 *
 * ### Example Usage:
 * ```cpp
 * // System that reads GravityConfig and Mass, and writes Velocity:
 * auto access = arche::AccessBuilder{}
 *     .reads_resource<GravityConfig>()
 *     .reads<Mass>()
 *     .writes<Velocity>()
 *     .build();
 * ```
 */
class AccessBuilder {
public:
  /**
   * @brief Declares read-only access (`const Comps&...`) to entity components.
   * @tparam Comps Variadic pack of component types read by the system.
   * @return Reference to this builder for method chaining.
   */
  template <typename... Comps> AccessBuilder &reads() noexcept {
    (read_components_.set(type_id<Comps>()), ...);
    return *this;
  }

  /**
   * @brief Declares mutable write access (`Comps&...`) to entity components.
   * @tparam Comps Variadic pack of component types written by the system.
   * @return Reference to this builder for method chaining.
   */
  template <typename... Comps> AccessBuilder &writes() noexcept {
    (write_components_.set(type_id<Comps>()), ...);
    return *this;
  }

  /**
   * @brief Declares read-only access (`const Res&...`) to World resources.
   * @tparam Res Variadic pack of resource types read by the system.
   * @return Reference to this builder for method chaining.
   */
  template <typename... Res> AccessBuilder &reads_resource() noexcept {
    (read_resources_.set(resource_id<Res>()), ...);
    return *this;
  }

  /**
   * @brief Declares mutable write access (`Res&...`) to World resources.
   * @tparam Res Variadic pack of resource types written by the system.
   * @return Reference to this builder for method chaining.
   */
  template <typename... Res> AccessBuilder &writes_resource() noexcept {
    (write_resources_.set(resource_id<Res>()), ...);
    return *this;
  }

  /**
   * @brief Constructs and returns the final immutable `AccessDescriptor`.
   */
  [[nodiscard]] AccessDescriptor build() const noexcept {
    return {
        read_components_, write_components_, read_resources_, write_resources_
    };
  }

private:
  ComponentMask read_components_{};
  ComponentMask write_components_{};
  ResourceMask read_resources_{};
  ResourceMask write_resources_{};
};

/// Type alias for concise syntax matching engine conventions.
using Access = AccessBuilder;

} // namespace arche
