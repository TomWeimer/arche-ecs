module;

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module arche:scheduler.detail.scheduler_impl;

import :scheduler;
import :system_context;
import :thread_pool;
import :command;
import :resource;
import :archetype;

namespace arche::internal::detail::scheduler {

inline void stamp_component_version(
    const ArchetypeGraph &graph,
    ComponentID tid,
    uint32_t current_version
) noexcept {
  for (const auto &arch_ptr : graph.archetypes()) {
    const int16_t col = arch_ptr->column_index(tid);
    if (col >= 0) {
      for (Chunk *chunk : arch_ptr->chunks()) {
        chunk->set_column_version(static_cast<uint16_t>(col), current_version);
      }
    }
  }
}

inline void merge_access(
    AccessDescriptor &dst,
    const AccessDescriptor &src
) noexcept {
  dst.read_components = dst.read_components | src.read_components;
  dst.write_components = dst.write_components | src.write_components;
  dst.read_resources = dst.read_resources | src.read_resources;
  dst.write_resources = dst.write_resources | src.write_resources;
}

template <typename SystemList>
inline void execute_parallel_narrow_nodes(
    const std::vector<size_t> &system_indices,
    const SystemList &systems,
    const SystemExecutionTarget &target,
    ThreadPool &pool,
    ParallelCommandBuffer &parallel_cmd
) {
  std::atomic<size_t> next_system{0};
  const size_t total_systems = system_indices.size();

  pool.parallel_for([&](uint32_t thread_id) {
    while (true) {
      const size_t idx = next_system.fetch_add(1, std::memory_order_relaxed);
      if (idx >= total_systems) {
        break;
      }
      const size_t sys_idx = system_indices[idx];
      if (!systems[sys_idx].enabled) {
        continue;
      }
      CommandBuffer &cmd = parallel_cmd.get(thread_id);
      SystemContext ctx(
          cmd,
          target.resources,
          target.cache_ctx,
          target.get_cache_fn,
          target.current_version,
          target.previous_version
      );
      systems[sys_idx].fn(ctx);
    }
  });
}

} // namespace arche::internal::detail::scheduler

export namespace arche::internal {

// =============================================================================
// Scheduler Implementation
// =============================================================================

Scheduler &Scheduler::add_system(
    std::string name,
    AccessDescriptor access,
    SystemFunction fn,
    SystemScale scale
) {
  systems_.push_back(
      SystemNode{
          .name = std::move(name),
          .access = access,
          .fn = std::move(fn),
          .stage_fn = nullptr,
          .scale = scale,
          .enabled = true,
      }
  );
  dirty_ = true;
  return *this;
}

void Scheduler::validate_system_resources(
    const ResourceContainer &resources
) {
  for (const auto &sys : systems_) {
    const ResourceMask required = sys.access.read_resources |
        sys.access.write_resources;
    for (uint32_t id = 0; id < 128; ++id) {
      if (required.test(id) &&
          !resources.has(static_cast<ResourceID>(id))) {
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
          current_batch.access = systems_[i].access;
          scheduled[i] = true;
          scheduled_count++;
        }
        break;
      }

      // Check conflict against everything already admitted into current_batch
      if (!batch_access.conflicts_with(systems_[i].access)) {
        current_batch.system_indices.push_back(i);
        detail::scheduler::merge_access(batch_access, systems_[i].access);

        scheduled[i] = true;
        scheduled_count++;
      }
    }

    if (!current_batch.system_indices.empty()) {
      if (!current_batch.is_wide) {
        current_batch.access = batch_access;
      }
      batches_.push_back(std::move(current_batch));
    }
  }
}

void Scheduler::compile(const SystemExecutionTarget &target) {
  compile(target.resources);
}

void Scheduler::compile(const ResourceContainer &resources) {
  if (!dirty_) {
    return;
  }
  batches_.clear();

  if (systems_.empty()) {
    dirty_ = false;
    return;
  }

  validate_system_resources(resources);
  partition_batches();
  dirty_ = false;
}

void Scheduler::execute_batch(
    const Batch &batch,
    const SystemExecutionTarget &target,
    ThreadPool &pool,
    ParallelCommandBuffer &parallel_cmd
) {
  if (batch.is_wide) {
    const size_t idx = batch.system_indices[0];
    if (systems_[idx].enabled) {
      CommandBuffer &cmd = parallel_cmd.get(0);
      SystemContext ctx(
          cmd,
          target.resources,
          target.cache_ctx,
          target.get_cache_fn,
          target.current_version,
          target.previous_version,
          &pool,
          &parallel_cmd
      );
      if (systems_[idx].stage_fn) {
        systems_[idx].stage_fn(target, ctx);
      } else if (systems_[idx].fn) {
        systems_[idx].fn(ctx);
      }
    }
  } else if (batch.system_indices.size() == 1) {
    const size_t idx = batch.system_indices[0];
    if (systems_[idx].enabled) {
      CommandBuffer &cmd = parallel_cmd.get(0);
      SystemContext ctx(
          cmd,
          target.resources,
          target.cache_ctx,
          target.get_cache_fn,
          target.current_version,
          target.previous_version
      );
      if (systems_[idx].stage_fn) {
        systems_[idx].stage_fn(target, ctx);
      } else if (systems_[idx].fn) {
        systems_[idx].fn(ctx);
      }
    }
  } else {
    detail::scheduler::execute_parallel_narrow_nodes(
        batch.system_indices, systems_, target, pool, parallel_cmd
    );
  }

  for (uint32_t id = 0; id < 128; ++id) {
    if (batch.access.write_components.test(id)) {
      detail::scheduler::stamp_component_version(
          target.graph, static_cast<ComponentID>(id), target.current_version
      );
    }
  }

  if (target.commit_parallel_cmd) {
    target.commit_parallel_cmd(parallel_cmd, target.world_ptr);
  }
  parallel_cmd.clear();
}

void Scheduler::run(
    const SystemExecutionTarget &target,
    ThreadPool &pool
) {
  if (target.advance_version_fn) {
    target.advance_version_fn(target.world_ptr);
  }
  if (dirty_) {
    compile(target);
  }

  ParallelCommandBuffer parallel_cmd(pool.worker_count() + 1);
  for (const auto &batch : batches_) {
    execute_batch(batch, target, pool, parallel_cmd);
  }
}

void Scheduler::run(const SystemExecutionTarget &target) {
  if (target.advance_version_fn) {
    target.advance_version_fn(target.world_ptr);
  }
  if (dirty_) {
    compile(target);
  }

  ParallelCommandBuffer parallel_cmd(1);
  for (const auto &batch : batches_) {
    for (size_t sys_idx : batch.system_indices) {
      if (!systems_[sys_idx].enabled) {
        continue;
      }
      CommandBuffer &cmd = parallel_cmd.get(0);
      SystemContext ctx(
          cmd,
          target.resources,
          target.cache_ctx,
          target.get_cache_fn,
          target.current_version,
          target.previous_version
      );
      systems_[sys_idx].fn(ctx);
    }

    for (uint32_t id = 0; id < 128; ++id) {
      if (batch.access.write_components.test(id)) {
        detail::scheduler::stamp_component_version(
            target.graph, static_cast<ComponentID>(id), target.current_version
        );
      }
    }

    if (target.commit_parallel_cmd) {
      target.commit_parallel_cmd(parallel_cmd, target.world_ptr);
    }
    parallel_cmd.clear();
  }
}

std::optional<size_t> Scheduler::find_system_index(
    std::string_view name
) const noexcept {
  for (size_t i = 0; i < systems_.size(); ++i) {
    if (systems_[i].name == name) {
      return i;
    }
  }
  return std::nullopt;
}

bool Scheduler::is_system_enabled(size_t index) const noexcept {
  assert(index < systems_.size());
  return systems_[index].enabled;
}

void Scheduler::set_system_enabled(size_t index, bool enabled) noexcept {
  assert(index < systems_.size());
  systems_[index].enabled = enabled;
}

const Scheduler::SystemNode &Scheduler::get_system(size_t index) const noexcept {
  assert(index < systems_.size());
  return systems_[index];
}

void Scheduler::run_system(
    size_t index,
    const SystemExecutionTarget &target
) {
  assert(index < systems_.size());
  ParallelCommandBuffer parallel_cmd(1);
  CommandBuffer &cmd = parallel_cmd.get(0);
  SystemContext ctx(
      cmd,
      target.resources,
      target.cache_ctx,
      target.get_cache_fn,
      target.current_version,
      target.previous_version
  );
  if (systems_[index].stage_fn) {
    systems_[index].stage_fn(target, ctx);
  } else if (systems_[index].fn) {
    systems_[index].fn(ctx);
  }

  for (uint32_t id = 0; id < 128; ++id) {
    if (systems_[index].access.write_components.test(id)) {
      detail::scheduler::stamp_component_version(
          target.graph, static_cast<ComponentID>(id), target.current_version
      );
    }
  }

  if (target.commit_parallel_cmd) {
    target.commit_parallel_cmd(parallel_cmd, target.world_ptr);
  }
}

} // namespace arche
