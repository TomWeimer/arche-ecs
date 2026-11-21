#pragma once

#include "arche/access.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file scheduler.hpp
 * @brief Declarative DAG Scheduler and system execution pipeline.
 *
 * @details
 * ## The Multithreading Conundrum in High-Performance ECS
 * In a 120 FPS physics and entity simulation, parallel execution across CPU
 * worker threads is essential. However, runtime locking (e.g. `std::mutex`,
 * `std::shared_mutex`, atomic spinlocks) causes catastrophic performance
 * degradation:
 * - **Thread stalls and cache ping-pong**: Multiple threads contending on cache
 *   lines destroy CPU L1/L2 efficiency.
 * - **Deadlock hazards**: N systems acquiring multiple component locks in
 *   divergent orders create deadlock cycles.
 * - **Non-deterministic mutations**: Unsynchronized structural modifications
 *   (spawning/despawning entities, adding/removing components) invalidate
 *   chunk memory while other threads are iterating.
 *
 * ## The Solution: Compile-Time Declarative DAG Scheduling
 * `arche::Scheduler` solves this through **Static Dependency Analysis**:
 * 1. **Declarative Access Descriptors**: Each system declares its component and
 *    resource read/write bitmasks using `arche::Access` (`AccessDescriptor`).
 * 2. **DAG Compilation**: At initialization (`compile()`), the scheduler uses a
 *    greedy level-partitioning algorithm to analyze bitmask intersections
 *    (using 128-bit SIMD bitwise math) and group non-conflicting systems into
 *    parallel batches.
 * 3. **Lock-Free Batch Execution**: During `run()`, systems in the same batch
 *    execute concurrently across `arche::ThreadPool` with **zero mutexes**.
 * 4. **Deterministic Structural Synchronization**: Each worker thread records
 *    structural mutations into a thread-local `CommandBuffer`. At batch
 *    barriers, these commands are merged and applied deterministically on the
 *    main thread.
 *
 * ## Two-Tier Execution Model: Wide vs. Narrow Systems
 * Systems have fundamentally different computational profiles:
 * - **Narrow Systems**: Systems that touch few entities or perform lightweight
 *   logic (e.g., input polling, camera follow, audio playback). The scheduler
 *   packs multiple independent Narrow systems into an **Inter-System Parallel
 *   Batch**, executing distinct systems concurrently across worker threads.
 * - **Wide Systems**: Heavy systems iterating over hundreds of thousands or
 *   millions of entities (e.g., flocking physics, collision detection,
 * transform hierarchies). The scheduler grants Wide systems exclusive control
 * over the entire `ThreadPool` to run chunks in parallel via **Intra-System
 *   parallel_for**.
 * - **Sequential Systems**: Systems requiring deterministic single-threaded
 *   execution on the main thread.
 */
namespace arche {

class World;
class ThreadPool;
class CommandBuffer;
class ParallelCommandBuffer;

// --- System Execution Context ------------------------------------------------

/**
 * @struct SystemContext
 * @brief Execution context delivered to each system callback.
 *
 * @details
 * Provides systems with direct access to:
 * - `world`: The simulation World for querying archetypes and reading
 resources.
 * - `cmd`: A thread-local `CommandBuffer` for deferring structural mutations
 *   (entity spawn/despawn, component additions/removals) safely without
 *   invalidating chunk memory during parallel iteration.
 */
struct SystemContext {
  /// Reference to the central simulation World.
  World &world;

  /// Thread-local CommandBuffer for recording deferred structural commands.
  CommandBuffer &cmd;
};

/**
 * @brief Callable target signature for system entrypoints.
 *
 * @details
 * Uses `std::function<void(SystemContext &)>` to allow free functions,
 * capturing lambdas, and callable objects to be registered as systems.
 */
using SystemFunction = std::function<void(SystemContext &)>;

// --- System Scale / Work Division --------------------------------------------

/**
 * @enum SystemScale
 * @brief Defines the computational weight class and thread scheduling strategy.
 */
enum SystemScale : uint8_t {
  /**
   * @brief Inter-System Parallelism.
   *
   * Runs concurrently alongside other independent Narrow systems in a shared
   * parallel batch across `ThreadPool` worker threads. Ideal for lightweight or
   * specialized systems touching small entity sets.
   */
  Narrow,

  /**
   * @brief Intra-System Parallelism.
   *
   * Heavy throughput system granted exclusive access to the `ThreadPool` to
   * execute massive entity loops via chunk-level `parallel_for`. Never runs
   * concurrently with other systems in the same batch.
   */
  Wide,

  /**
   * @brief Synchronous Main-Thread Execution.
   *
   * Executed sequentially on the main thread. Ideal for un-parallelizable
   * coordination, window events, or debug instrumentation.
   */
  Sequential
};

// --- DAG Scheduler -----------------------------------------------------------

/**
 * @class Scheduler
 * @brief Declarative dependency graph compiler and lock-free parallel runner.
 *
 * @details
 * Manages system registration, dependency graph construction, and parallel
 * dispatch across `ThreadPool`.
 *
 * ### Example:
 * ```cpp
 * arche::Scheduler scheduler;
 *
 * scheduler
 *     .add_system("ClockUpdate",
 *         arche::Access{}.writes_resource<SimulationClock>().build(),
 *         clock_update_system,
 *         arche::SystemScale::Narrow)
 *     .add_system("Gravity",
 * arche::Access{}.reads_resource<GravityConfig>().writes<Velocity>().build(),
 *         gravity_system,
 *         arche::SystemScale::Wide)
 *     .add_system("Movement",
 *         arche::Access{}.reads<Velocity>().writes<Position>().build(),
 *         movement_system,
 *         arche::SystemScale::Wide);
 *
 * scheduler.compile(world);
 * scheduler.run(world, pool, 1.0f / 120.0f);
 * ```
 */
class Scheduler {

  /// Internal descriptor node for registered systems.
  struct SystemNode {
    std::string name;
    AccessDescriptor access;
    SystemFunction fn{nullptr};
    SystemScale scale{SystemScale::Narrow};
  };

  /// Pre-computed batch of systems guaranteed to be safe for concurrent
  /// execution.
  struct Batch {
    std::vector<size_t> system_indices;
    bool is_wide{false};
  };

private:
  std::vector<SystemNode> systems_;
  std::vector<Batch> batches_;
  bool dirty_{true};

  /// Validates that all World Resources required by systems exist in World.
  void validate_system_resources(World &world);

  /// Greedy level-partitioning algorithm that groups non-conflicting systems into parallel batches.
  void partition_batches();

  /// Executes a single batch and applies deferred structural mutations at the batch barrier.
  void execute_batch(
      const Batch &batch,
      World &world,
      ThreadPool &pool,
      ParallelCommandBuffer &parallel_cmd
  );

public:
  Scheduler() = default;
  ~Scheduler() = default;
  Scheduler(const Scheduler &) = default;
  Scheduler &operator=(const Scheduler &) = default;
  Scheduler(Scheduler &&) noexcept = default;
  Scheduler &operator=(Scheduler &&) noexcept = default;

  /**
   * @brief Registers a system callback with its declarative component and
   * resource access footprint.
   *
   * @param name Diagnostic identifier for debugging and profiling.
   * @param access Exact 128-bit component/resource read and write bitmask.
   * @param fn Function pointer to the system implementation.
   * @param scale Computational scale (Narrow, Wide, or Sequential). Defaults to
   * Narrow.
   * @return Reference to this scheduler for method chaining.
   */
  Scheduler &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  ) {
    systems_.push_back(
        SystemNode{std::move(name), access, std::move(fn), scale}
    );
    dirty_ = true;
    return *this;
  }

  /**
   * @brief Compiles the Directed Acyclic Graph (DAG) and partitions systems
   * into conflict-free parallel batches.
   *
   * @details
   * Uses a greedy level-partitioning algorithm:
   * 1. Evaluates pairwise `AccessDescriptor::conflicts_with()` in 1-2 CPU
   * vector instructions.
   * 2. Groups non-conflicting Narrow systems into concurrent parallel batches.
   * 3. Isolates Wide systems into dedicated exclusive-pool batches.
   *
   * @param world The World instance used for init-time resource validation.
   */
  void compile(World &world);

  /**
   * @brief Executes one simulation frame across `arche::ThreadPool` with zero
   * mutexes.
   *
   * @details
   * Iterates through pre-compiled batches:
   * - Narrow batches: Dispatches distinct systems across worker threads.
   * - Wide batches: Executes the heavy system with full thread pool access.
   * - Batch barriers: Flushes and applies deferred `ParallelCommandBuffer`
   *   mutations deterministically to the World before advancing to the next
   * batch.
   *
   * @param world The target World instance.
   * @param pool Worker thread pool for parallel execution.
   * @param dt Delta time for this simulation tick (forwarded to systems or
   * clock).
   */
  void run(World &world, ThreadPool &pool, float dt);
};

} // namespace arche