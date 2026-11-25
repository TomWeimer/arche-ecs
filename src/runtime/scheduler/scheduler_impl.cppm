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
import :task;
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
  const size_t total_systems = system_indices.size();
  if (total_systems == 0) {
    return;
  }

  if (total_systems == 1 || pool.worker_count() <= 1) {
    for (size_t sys_idx : system_indices) {
      if (systems[sys_idx].enabled) {
        CommandBuffer &cmd = parallel_cmd.get(0);
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
    }
    return;
  }

  struct alignas(64) NarrowTaskItem {
    size_t system_idx;
    const SystemList *systems;
    const SystemExecutionTarget *target;
    ParallelCommandBuffer *parallel_cmd;
    TaskGroup *group;
  };

  TaskGroup group;
  group.pending.store(static_cast<uint32_t>(total_systems), std::memory_order_relaxed);

  auto task_fn = [](void *data, uint32_t thread_id) {
    auto *item = static_cast<NarrowTaskItem *>(data);
    const auto &sys = (*item->systems)[item->system_idx];
    if (sys.enabled) {
      CommandBuffer &cmd = item->parallel_cmd->get(thread_id);
      SystemContext ctx(
          cmd,
          item->target->resources,
          item->target->cache_ctx,
          item->target->get_cache_fn,
          item->target->current_version,
          item->target->previous_version
      );
      sys.fn(ctx);
    }
    item->group->pending.fetch_sub(1, std::memory_order_release);
  };

  const uint32_t workers = pool.worker_count();

  if (total_systems <= 128) {
    NarrowTaskItem stack_items[128];
    for (size_t i = 0; i < total_systems; ++i) {
      stack_items[i] = {
          .system_idx = system_indices[i],
          .systems = &systems,
          .target = &target,
          .parallel_cmd = &parallel_cmd,
          .group = &group
      };
      pool.schedule(
          Task{.execute = task_fn, .data = &stack_items[i]},
          static_cast<uint32_t>(i % workers)
      );
    }
    pool.wait(group);
  } else {
    std::vector<NarrowTaskItem> heap_items(total_systems);
    for (size_t i = 0; i < total_systems; ++i) {
      heap_items[i] = {
          .system_idx = system_indices[i],
          .systems = &systems,
          .target = &target,
          .parallel_cmd = &parallel_cmd,
          .group = &group
      };
      pool.schedule(
          Task{.execute = task_fn, .data = &heap_items[i]},
          static_cast<uint32_t>(i % workers)
      );
    }
    pool.wait(group);
  }
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

      // Sequential systems must execute isolated on thread 0 (main thread)
      if (systems_[i].scale == SystemScale::Sequential) {
        if (current_batch.system_indices.empty()) {
          current_batch.system_indices.push_back(i);
          current_batch.is_sequential = true;
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
  if (batch.is_sequential) {
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
  } else if (batch.is_wide) {
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

  ParallelCommandBuffer *cmd_ptr = target.parallel_cmd;
  if (!cmd_ptr) {
    const uint32_t required = pool.worker_count() + 1;
    if (!fallback_parallel_cmd_ || fallback_parallel_cmd_->thread_count() < required) {
      fallback_parallel_cmd_ = std::make_unique<ParallelCommandBuffer>(required);
    }
    cmd_ptr = fallback_parallel_cmd_.get();
  }

  for (const auto &batch : batches_) {
    execute_batch(batch, target, pool, *cmd_ptr);
  }
}

void Scheduler::run(const SystemExecutionTarget &target) {
  if (target.advance_version_fn) {
    target.advance_version_fn(target.world_ptr);
  }
  if (dirty_) {
    compile(target);
  }

  ParallelCommandBuffer *cmd_ptr = target.parallel_cmd;
  if (!cmd_ptr) {
    if (!fallback_parallel_cmd_) {
      fallback_parallel_cmd_ = std::make_unique<ParallelCommandBuffer>(1);
    }
    cmd_ptr = fallback_parallel_cmd_.get();
  }

  for (const auto &batch : batches_) {
    for (size_t sys_idx : batch.system_indices) {
      if (!systems_[sys_idx].enabled) {
        continue;
      }
      CommandBuffer &cmd = cmd_ptr->get(0);
      SystemContext ctx(
          cmd,
          target.resources,
          target.cache_ctx,
          target.get_cache_fn,
          target.current_version,
          target.previous_version
      );
      if (systems_[sys_idx].stage_fn) {
        systems_[sys_idx].stage_fn(target, ctx);
      } else if (systems_[sys_idx].fn) {
        systems_[sys_idx].fn(ctx);
      }
    }

    for (uint32_t id = 0; id < 128; ++id) {
      if (batch.access.write_components.test(id)) {
        detail::scheduler::stamp_component_version(
            target.graph, static_cast<ComponentID>(id), target.current_version
        );
      }
    }

    if (target.commit_parallel_cmd) {
      target.commit_parallel_cmd(*cmd_ptr, target.world_ptr);
    }
    cmd_ptr->clear();
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
