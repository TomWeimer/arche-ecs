
#include "arche/scheduler.hpp"
#include "arche/command_buffer.hpp"

namespace arche {

namespace {

// =============================================================================
// Internal Static Helpers
// =============================================================================

/**
 * @brief Merges a system's access footprint into the accumulated batch bitmask.
 */
inline void merge_access(
    AccessDescriptor &batch_access,
    const AccessDescriptor &sys_access
) noexcept {
  batch_access.read_components = batch_access.read_components |
      sys_access.read_components;
  batch_access.write_components = batch_access.write_components |
      sys_access.write_components;
  batch_access.read_resources = batch_access.read_resources |
      sys_access.read_resources;
  batch_access.write_resources = batch_access.write_resources |
      sys_access.write_resources;
}

/**
 * @brief Executes a single Wide or Sequential system synchronously on thread 0.
 */
inline void execute_sequential_or_wide_node(
    const SystemFunction &fn,
    World &world,
    CommandBuffer &cmd
) {
  SystemContext ctx{world, cmd};
  fn(ctx);
}

/**
 * @brief Dispatches multiple non-conflicting Narrow systems concurrently
 * across worker threads in the ThreadPool.
 */
template <typename SystemList>
inline void execute_parallel_narrow_nodes(
    const std::vector<size_t> &system_indices,
    const SystemList &systems,
    World &world,
    ThreadPool &pool,
    ParallelCommandBuffer &parallel_cmd
) {
  pool.parallel_for([&](uint32_t thread_id) {
    if (thread_id < system_indices.size()) {
      const size_t sys_idx = system_indices[thread_id];
      CommandBuffer &cmd = parallel_cmd.get(thread_id);
      SystemContext ctx{world, cmd};
      systems[sys_idx].fn(ctx);
    }
  });
}

} // namespace

// =============================================================================
// Scheduler: DAG Compilation Pipeline
// =============================================================================

void Scheduler::validate_system_resources(World &world) {
  for (const auto &sys : systems_) {
    const ResourceMask required =
        sys.access.read_resources | sys.access.write_resources;
    for (uint32_t id = 0; id < 128; ++id) {
      if (required.test(id) && !world.has_resource(static_cast<ResourceID>(id))) {
        throw std::runtime_error(
            "arche::Scheduler: System '" + sys.name +
            "' requires unregistered World Resource ID " + std::to_string(id)
        );
      }
    }
  }
}

void Scheduler::partition_batches() {
  const size_t n = systems_.size();
  std::vector<bool> scheduled(n, false);
  size_t scheduled_count = 0;

  while (scheduled_count < n) {
    Batch current_batch;
    AccessDescriptor batch_access{};

    for (size_t i = 0; i < n; ++i) {
      if (scheduled[i]) {
        continue;
      }

      // Wide systems get dedicated exclusive-pool batches
      if (systems_[i].scale == SystemScale::Wide) {
        if (current_batch.system_indices.empty()) {
          current_batch.system_indices.push_back(i);
          current_batch.is_wide = true;
          scheduled[i] = true;
          scheduled_count++;
        }
        break;
      }

      // Check conflict against everything already admitted into current_batch
      if (!batch_access.conflicts_with(systems_[i].access)) {
        current_batch.system_indices.push_back(i);
        merge_access(batch_access, systems_[i].access);

        scheduled[i] = true;
        scheduled_count++;
      }
    }

    batches_.push_back(std::move(current_batch));
  }
}

void Scheduler::compile(World &world) {
  if (!dirty_) {
    return;
  }
  batches_.clear();

  if (systems_.empty()) {
    dirty_ = false;
    return;
  }

  // 1. Init-time Validation: Verify all required resources exist in World
  validate_system_resources(world);

  // 2. Greedy level-partitioning algorithm (Interleaving across all plugins)
  partition_batches();

  dirty_ = false;
}

// =============================================================================
// Scheduler: Frame Execution Pipeline
// =============================================================================

//
void Scheduler::execute_batch(
    const Batch &batch,
    World &world,
    ThreadPool &pool,
    ParallelCommandBuffer &parallel_cmd
) {

  if (batch.is_wide || batch.system_indices.size() == 1) {
    // Wide system or single sequential system
    const size_t idx = batch.system_indices[0];
    CommandBuffer &cmd = parallel_cmd.get(0);
    execute_sequential_or_wide_node(systems_[idx].fn, world, cmd);
  } else {
    // Inter-System Parallel: run distinct systems on separate worker threads
    execute_parallel_narrow_nodes(
        batch.system_indices, systems_, world, pool, parallel_cmd
    );
  }

  // Batch barrier: commit deferred structural commands deterministically
  world.commit(parallel_cmd);
  parallel_cmd.clear();
}

void Scheduler::run(World &world, ThreadPool &pool, float dt) {
  (void)dt;
  if (dirty_) {
    compile(world);
  }

  // Parallel CommandBuffer per worker thread for deferred mutations
  ParallelCommandBuffer parallel_cmd(pool.worker_count() + 1);

  for (const auto &batch : batches_) {
    execute_batch(batch, world, pool, parallel_cmd);
  }
}

} // namespace arche