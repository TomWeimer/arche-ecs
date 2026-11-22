#pragma once

#include <cstdint>
#include <functional>

namespace arche {

// =============================================================================
// Forward Declarations
// =============================================================================

struct SystemContext;

// =============================================================================
// System Function Callback Signature
// =============================================================================

/**
 * @brief Callable target signature for system entrypoints.
 *
 * Wraps free functions, capturing lambdas, and callable objects accepting a
 * mutable reference to the system execution context (`SystemContext &`).
 *
 * ```cpp
 * // Example:
 * auto movement_system = [](SystemContext &ctx) {
 *     for (auto [pos, vel] : ctx.world.query<Position, const Velocity>()) {
 *         pos.x += vel.dx;
 *     }
 * };
 * ```
 */
using SystemFunction = std::function<void(SystemContext &)>;

// =============================================================================
// System Scale: Computational Weight Class
// =============================================================================

/**
 * @enum SystemScale
 * @brief Defines the computational weight class and parallelism model of a
 * system.
 *
 * Directs the Scheduler on how to schedule and allocate worker thread
 * resources:
 * - `Narrow`: Inter-system parallelism (runs concurrently with other Narrow
 * systems).
 * - `Wide`: Intra-system parallelism (exclusive ThreadPool access for chunk
 * parallel loops).
 * - `Sequential`: Synchronous execution pinned to the caller thread.
 *
 * ```cpp
 * // Example:
 * world.systems.add_system("physics", access, physics_step, SystemScale::Wide);
 * ```
 */
enum class SystemScale : uint8_t {
  /**
   * @brief Inter-System Parallelism.
   *
   * Executes concurrently alongside other independent Narrow systems in a
   * shared parallel batch across worker threads. Ideal for lightweight systems.
   */
  Narrow,

  /**
   * @brief Intra-System Parallelism.
   *
   * Granted exclusive access to the ThreadPool to parallelize massive entity
   * loops via chunk-level `parallel_for`. Never co-executes with other systems
   * in the same batch.
   */
  Wide,

  /**
   * @brief Synchronous Main-Thread Execution.
   *
   * Executes sequentially on the caller thread. Ideal for single-threaded
   * coordination, window events, or debug instrumentation.
   */
  Sequential
};

} // namespace arche
