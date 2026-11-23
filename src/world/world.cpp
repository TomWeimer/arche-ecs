#include "arche/world.hpp"
#include "arche/command.hpp"
#include "command/command_playback.hpp"
#include "archetype/component_migration.hpp"
#include "system/scheduler.hpp"
#include "system/system_context.hpp"
#include "concurrency/thread_pool.hpp"
#include "component/component_access.hpp"
#include "world/world_access.hpp"
#include "world/world_storage.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace arche {

/// Monotonically increasing simulation version sequence, incremented on
/// mutations.
uint32_t World::version() const noexcept { return storage_->current_version; }

// =============================================================================
// World: Constructors & Destructor
// =============================================================================

World::World(size_t chunk_size)
    : storage_(new internal::WorldStorage(chunk_size)) {}

World::~World() {
  clear();
  delete storage_;
}

void World::clear() {
  storage_->slot_map.clear();

  for (auto &arch_ptr : storage_->graph.archetypes()) {
    arch_ptr->clear_chunks(storage_->chunk_pool);
  }

  storage_->chunk_pool.clear();
}

// =============================================================================
// Entity Lifecycle
// =============================================================================

bool World::despawn(Entity entity) {
  const EntityRecord *record = storage_->slot_map.find_record(entity);
  if (!record) {
    return false;
  }

  Archetype *arch = record->archetype;
  Chunk *chunk = record->chunk;
  assert(arch != nullptr && chunk != nullptr);

  const auto reloc = chunk->swap_and_pop(record->row, arch->column_layouts());
  if (reloc.has_relocation()) {
    storage_->slot_map.update_row(reloc.moved_entity, record->row);
  }
  arch->decrement_entity_count();

  chunk->set_structure_version(storage_->current_version);

  if (chunk->is_empty() && arch->chunks().size() > 1) {
    arch->recycle_chunk(storage_->chunk_pool, chunk);
  }

  storage_->slot_map.free(entity);
  return true;
}

void World::despawn(std::span<const Entity> entities) {
  if (entities.empty()) {
    return;
  }

  struct ChunkDeletions {
    Archetype *arch{nullptr};
    std::vector<uint32_t> rows;
  };
  std::unordered_map<Chunk *, ChunkDeletions> chunk_map;

  for (Entity e : entities) {
    const EntityRecord *rec = storage_->slot_map.find_record(e);
    if (!rec) {
      continue;
    }
    auto &entry = chunk_map[rec->chunk];
    entry.arch = rec->archetype;
    entry.rows.push_back(rec->row);
  }

  for (auto &[chunk, info] : chunk_map) {
    std::sort(info.rows.begin(), info.rows.end(), std::greater<uint32_t>());

    std::vector<EntityRelocation> relocations(info.rows.size());
    const uint32_t reloc_count = chunk->batch_swap_and_pop(
        info.rows, info.arch->column_layouts(), relocations
    );
    for (uint32_t i = 0; i < reloc_count; ++i) {
      storage_->slot_map.update_row(relocations[i].entity, relocations[i].new_row);
    }
    info.arch->decrement_entity_count(info.rows.size());

    chunk->set_structure_version(storage_->current_version);

    if (chunk->is_empty() && info.arch->chunks().size() > 1) {
      info.arch->recycle_chunk(storage_->chunk_pool, chunk);
    }
  }

  storage_->slot_map.free_batch(entities);
}

// =============================================================================
// Deferred Command Playback Implementation
// =============================================================================

void CommandBuffer::commit(World &world) {
  size_t offset = 0;
  auto read_value = [&]<typename T>(T &out) {
    std::memcpy(&out, &buffer_[offset], sizeof(T));
    offset += sizeof(T);
  };

  while (offset < buffer_.size()) {
    CommandType cmd_type{};
    read_value(cmd_type);

    switch (cmd_type) {
    case CommandType::CreateEntity: {
      CommandBuffer::SpawnFn spawn_fn{nullptr};
      uint32_t size{0};
      read_value(spawn_fn);
      read_value(size);

      const uint8_t *payload = (size > 0) ? &buffer_[offset] : nullptr;
      offset += size;

      CommandPlayback::apply_create_entity(world, spawn_fn, payload);
      break;
    }
    case CommandType::DestroyEntity: {
      Entity entity{};
      read_value(entity);
      world.despawn(entity);
      break;
    }
    case CommandType::AddComponent: {
      Entity entity{};
      TypeID tid{};
      CommandBuffer::RegisterTypeFn reg_fn{nullptr};
      uint32_t size{};
      read_value(entity);
      read_value(tid);
      read_value(reg_fn);
      read_value(size);

      const void *data = &buffer_[offset];
      offset += size;

      if (reg_fn != nullptr) {
        reg_fn(world);
      }
      CommandPlayback::apply_add_component(
          internal::WorldAccess::chunk_pool(world),
          internal::WorldAccess::slot_map(world),
          internal::WorldAccess::graph(world),
          entity, tid, data
      );
      break;
    }
    case CommandType::RemoveComponent: {
      Entity entity{};
      TypeID tid{};
      read_value(entity);
      read_value(tid);

      CommandPlayback::apply_remove_component(
          internal::WorldAccess::chunk_pool(world),
          internal::WorldAccess::slot_map(world),
          internal::WorldAccess::graph(world),
          entity, tid
      );
      break;
    }
    case CommandType::DestroyEntities: {
      uint32_t count{0};
      read_value(count);
      const Entity *entities = reinterpret_cast<const Entity *>(
          &buffer_[offset]
      );
      offset += count * sizeof(Entity);
      world.despawn(std::span<const Entity>(entities, count));
      break;
    }
    default:
      assert(false && "Corrupted CommandBuffer byte stream encountered!");
      return;
    }
  }
  clear();
}

// =============================================================================
// Scheduler: Frame Execution Pipeline
// =============================================================================

namespace {

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

void Scheduler::validate_system_resources(World &world) {
  for (const auto &sys : systems_) {
    const ResourceMask required = sys.access.read_resources |
        sys.access.write_resources;
    for (uint32_t id = 0; id < 128; ++id) {
      if (required.test(id) &&
          !world.resources.has(static_cast<ResourceID>(id))) {
        throw std::runtime_error(
            "arche::Scheduler: System '" + sys.name +
            "' requires unregistered World Resource ID " + std::to_string(id)
        );
      }
    }
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

  validate_system_resources(world);
  partition_batches();
  dirty_ = false;
}

void Scheduler::execute_batch(
    const Batch &batch,
    World &world,
    ThreadPool &pool,
    ParallelCommandBuffer &parallel_cmd
) {
  if (batch.is_wide) {
    const size_t idx = batch.system_indices[0];
    CommandBuffer &cmd = parallel_cmd.get(0);
    SystemContext ctx{world, cmd, pool, parallel_cmd};
    systems_[idx].fn(ctx);
  } else if (batch.system_indices.size() == 1) {
    const size_t idx = batch.system_indices[0];
    CommandBuffer &cmd = parallel_cmd.get(0);
    SystemContext ctx{world, cmd};
    systems_[idx].fn(ctx);
  } else {
    execute_parallel_narrow_nodes(
        batch.system_indices, systems_, world, pool, parallel_cmd
    );
  }

  for (uint32_t id = 0; id < 128; ++id) {
    if (batch.access.write_components.test(id)) {
      ComponentAccess::stamp_component_version(world, static_cast<TypeID>(id));
    }
  }

  parallel_cmd.commit(world);
  parallel_cmd.clear();
}

void Scheduler::run(World &world, ThreadPool &pool) {
  internal::WorldAccess::advance_version(world);
  if (dirty_) {
    compile(world);
  }

  ParallelCommandBuffer parallel_cmd(pool.worker_count() + 1);
  for (const auto &batch : batches_) {
    execute_batch(batch, world, pool, parallel_cmd);
  }
}

void Scheduler::run(World &world) {
  internal::WorldAccess::advance_version(world);
  if (dirty_) {
    compile(world);
  }

  ParallelCommandBuffer parallel_cmd(1);
  for (const auto &batch : batches_) {
    for (size_t sys_idx : batch.system_indices) {
      CommandBuffer &cmd = parallel_cmd.get(0);
      SystemContext ctx{world, cmd};
      systems_[sys_idx].fn(ctx);
    }

    for (uint32_t id = 0; id < 128; ++id) {
      if (batch.access.write_components.test(id)) {
        ComponentAccess::stamp_component_version(world, static_cast<TypeID>(id));
      }
    }

    parallel_cmd.commit(world);
    parallel_cmd.clear();
  }
}

} // namespace arche
