module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

export module arche:scheduler;

import :system_types;
import :system_context;
import :access;
import :thread_pool;
import :command;
import :resource;
import :archetype_graph;
import :slot_map;
import :sort;
import :archetype;
import :filters;
import :component_id;

export namespace arche::internal {

// =============================================================================
// SystemExecutionTarget (Decoupled Execution View Contract)
// =============================================================================

/**
 * @struct SystemExecutionTarget
 * @brief Decoupled storage execution view contract provided to the Scheduler.
 *
 * Encapsulates resource access, archetype graph for version stamping, query cache
 * resolution, and deferred command buffer synchronization. Conforms strictly to
 * Layer 4 bounds without upward knowledge of World.
 */
struct SystemExecutionTarget {
  /// Reference to world resource registry.
  ResourceContainer &resources;

  /// Immutable reference to the archetype graph for version stamping.
  const ArchetypeGraph &graph;

  /// Mutable reference to world slot map for in-place sorting and entity routing.
  SlotMap &slot_map;

  /// Current world simulation tick sequence.
  uint32_t &current_version;

  /// Previous world simulation tick sequence.
  uint32_t &previous_version;

  /// Optional preallocated parallel command buffer pool.
  ParallelCommandBuffer *parallel_cmd{nullptr};

  /// Type-erased query cache provider context pointer.
  void *cache_ctx{nullptr};

  /// Function pointer resolving matching QueryCache entries.
  QueryCache *(*get_cache_fn)(
      void *ctx,
      ComponentMask inc,
      ComponentMask exc,
      ComponentMask any
  ){nullptr};

  /// Function pointer committing parallel command buffer recordings.
  void (*commit_parallel_cmd)(
      ParallelCommandBuffer &parallel_cmd,
      void *world_ptr
  ){nullptr};

  /// Optional function pointer advancing simulation tick sequence.
  void (*advance_version_fn)(void *world_ptr){nullptr};

  /// Type-erased pointer to the parent coordinator.
  void *world_ptr{nullptr};
};

// =============================================================================
// Scheduler (Declarative Dependency Graph Compiler & Parallel Runner)
// =============================================================================

/**
 * @class Scheduler
 * @brief Compiles a declarative system dependency graph into non-conflicting parallel batches.
 *
 * Automatically detects component and resource read/write conflicts, guarantees
 * deterministic execution order, and dispatches batches across worker threads.
 */
class Scheduler {
public:
  /**
   * @struct SystemNode
   * @brief Node entry representing an individual registered system.
   */
  struct SystemNode {
    std::string name;
    AccessDescriptor access;
    SystemFunction fn{nullptr};
    std::function<void(const SystemExecutionTarget &, SystemContext &)> stage_fn{nullptr};
    SystemScale scale{SystemScale::Narrow};
    bool enabled{true};
  };

  /**
   * @struct Batch
   * @brief Group of systems whose access masks do not conflict, safe for parallel execution.
   */
  struct Batch {
    std::vector<size_t> system_indices;
    AccessDescriptor access{};
    bool is_wide{false};
    bool is_sequential{false};
  };

private:
  std::vector<SystemNode> systems_;
  std::vector<Batch> batches_;
  bool dirty_{true};
  std::unique_ptr<ParallelCommandBuffer> fallback_parallel_cmd_{nullptr};

  void validate_system_resources(const ResourceContainer &resources);
  void partition_batches();
  void execute_batch(
      const Batch &batch,
      const SystemExecutionTarget &target,
      ThreadPool &pool,
      ParallelCommandBuffer &parallel_cmd
  );

public:
  Scheduler() = default;
  ~Scheduler() = default;
  Scheduler(const Scheduler &other)
      : systems_(other.systems_),
        batches_(other.batches_),
        dirty_(other.dirty_) {}
  Scheduler &operator=(const Scheduler &other) {
    if (this != &other) {
      systems_ = other.systems_;
      batches_ = other.batches_;
      dirty_ = other.dirty_;
      fallback_parallel_cmd_.reset();
    }
    return *this;
  }
  Scheduler(Scheduler &&) noexcept = default;
  Scheduler &operator=(Scheduler &&) noexcept = default;

  /**
   * @brief Registers a new system function into the execution schedule.
   *
   * @param name Diagnostic identifier for debugging and profiling.
   * @param access Component and resource read/write access bitmasks.
   * @param fn Executable system function callback.
   * @param scale Execution scale (Narrow for fine systems, Wide for exclusive batch).
   * @return Reference to this scheduler for chaining.
   */
  Scheduler &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  );

  /**
   * @brief Registers a scheduled sorting stage targeting archetypes matching Component and Filters.
   *
   * Executes as an exclusive pipeline barrier batch. Evaluates `condition(ctx)`; if true,
   * inspects matching archetypes via ColumnView<Component>, invokes `sorter`, and immediately
   * applies in-place chunk column permutation while data is hot in CPU cache.
   *
   * @tparam Component Primary component to sort by.
   * @tparam Filters Variadic compile-time filter tags (`With<...>`, `Without<...>`).
   * @tparam Predicate Callable evaluating `bool(const SystemContext&)`.
   * @tparam Sorter Callable accepting `(ColumnView<Component>, std::vector<uint32_t>&)`.
   * @param name Diagnostic identifier for debugging and profiling.
   * @param condition Predicate controlling whether sorting triggers this tick.
   * @param sorter Algorithm generating the target permutation index vector.
   * @return Reference to this scheduler for chaining.
   */
  template <typename Component, typename... Filters, typename Predicate, typename Sorter>
  Scheduler &add_sort_stage(
      std::string name,
      Predicate &&condition,
      Sorter &&sorter
  );

  /**
   * @brief Compiles system nodes into conflict-free batches against an execution target.
   */
  void compile(const SystemExecutionTarget &target);

  /**
   * @brief Compiles system nodes into conflict-free batches validating required resources.
   */
  void compile(const ResourceContainer &resources);

  /**
   * @brief Executes all compiled system batches in parallel using the provided ThreadPool.
   */
  void run(const SystemExecutionTarget &target, ThreadPool &pool);

  /**
   * @brief Executes all compiled system batches sequentially in the current thread.
   */
  void run(const SystemExecutionTarget &target);

  /**
   * @brief Looks up a system index by diagnostic name.
   */
  [[nodiscard]] std::optional<size_t> find_system_index(
      std::string_view name
  ) const noexcept;

  /**
   * @brief Checks whether the system at the specified index is enabled.
   */
  [[nodiscard]] bool is_system_enabled(size_t index) const noexcept;

  /**
   * @brief Enables or disables the system at the specified index.
   */
  void set_system_enabled(size_t index, bool enabled) noexcept;

  /**
   * @brief Retrieves the SystemNode at the specified index.
   */
  [[nodiscard]] const SystemNode &get_system(size_t index) const noexcept;

  /**
   * @brief Executes a single system immediately on the calling thread.
   */
  void run_system(size_t index, const SystemExecutionTarget &target);
};

// =============================================================================
// Sort Stage Template Implementation
// =============================================================================

template <typename Component, typename... Filters, typename Predicate, typename Sorter>
inline Scheduler &Scheduler::add_sort_stage(
    std::string name,
    Predicate &&condition,
    Sorter &&sorter
) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  include_mask.set(component_id<Component>());
  if constexpr (sizeof...(Filters) > 0) {
    ([&]<typename Filter>() {
      if constexpr (is_with_v<Filter>) {
        [&]<typename... Ts>(With<Ts...> *) {
          (include_mask.set(component_id<Ts>()), ...);
        }(static_cast<Filter *>(nullptr));
      } else if constexpr (is_without_v<Filter>) {
        [&]<typename... Ts>(Without<Ts...> *) {
          (exclude_mask.set(component_id<Ts>()), ...);
        }(static_cast<Filter *>(nullptr));
      }
    }.template operator()<Filters>(), ...);
  }

  AccessDescriptor access;
  access.write_components = include_mask;

  auto stage_fn = [include_mask,
                   exclude_mask,
                   cond = std::forward<Predicate>(condition),
                   sort_cb = std::forward<Sorter>(sorter)](
                      const SystemExecutionTarget &target,
                      SystemContext &ctx
                  ) mutable {
    if (!cond(ctx)) {
      return;
    }
    QueryCache *cache = target.get_cache_fn(
        target.cache_ctx, include_mask, exclude_mask, {}
    );
    if (!cache || cache->matching_archetypes.empty()) {
      return;
    }

    std::vector<uint32_t> permutation;
    for (Archetype *arch : cache->matching_archetypes) {
      if (arch->entity_count() <= 1) {
        continue;
      }
      ColumnView<Component> columns(arch);
      permutation.clear();
      sort_cb(columns, permutation);
      if (permutation.size() == arch->entity_count()) {
        Sort::permute(
            SortTarget{
                .arch = arch,
                .chunks = arch->chunks(),
                .slot_map = target.slot_map,
                .version = target.current_version,
            },
            permutation
        );
      }
    }
  };

  systems_.push_back(
      SystemNode{
          .name = std::move(name),
          .access = access,
          .fn = nullptr,
          .stage_fn = std::move(stage_fn),
          .scale = SystemScale::Wide,
          .enabled = true
      }
  );
  dirty_ = true;
  return *this;
}

} // namespace arche::internal

export namespace arche {
using internal::Scheduler;
} // namespace arche
