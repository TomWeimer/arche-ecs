#include "arche/archetype/component_migration.hpp"
#include "arche/chunk/chunk_distributor.hpp"
#include "arche/world.hpp"
#include "arche/world_access.hpp"

#include <cassert>
#include <cstring>

namespace arche {

namespace {

inline void copy_shared_components(
    Chunk *src_chunk,
    uint32_t src_row,
    Archetype *src_arch,
    Chunk *dst_chunk,
    uint32_t dst_row,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) noexcept {
  for (size_t i = 0; i < dst_arch->component_count(); ++i) {
    const auto &dst_layout = dst_arch->column_layout(i);
    const TypeID tid = dst_layout.type_id;
    void *dst_ptr = dst_chunk->component_element(dst_layout, dst_row);

    if (tid == mutated_type && added_component_data) {
      std::memcpy(dst_ptr, added_component_data, dst_layout.size);
      continue;
    }

    const int16_t src_col_idx = src_arch->column_index(tid);
    if (src_col_idx >= 0) {
      const auto &src_layout = src_arch->column_layout(
          static_cast<size_t>(src_col_idx)
      );
      void *src_ptr = src_chunk->component_element(src_layout, src_row);
      src_layout.descriptor->relocate(dst_ptr, src_ptr);
    }
  }
}

} // namespace

void ComponentMigration::migrate(
    World &world,
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  auto [dst_chunk, dst_row] = ChunkDistributor::allocate_slot(world, dst_arch, entity);
  Chunk *src_chunk = old_record.chunk;
  assert(src_chunk != nullptr);

  copy_shared_components(
      src_chunk,
      old_record.row,
      src_arch,
      dst_chunk,
      dst_row,
      dst_arch,
      mutated_type,
      added_component_data
  );

  dst_arch->increment_entity_count();

  src_chunk->swap_and_pop(
      old_record.row, internal::WorldAccess::slot_map(world), src_arch->column_layouts()
  );
  src_arch->decrement_entity_count();

  internal::WorldAccess::slot_map(world).update_location(entity, dst_arch, dst_chunk, dst_row);
}

} // namespace arche
