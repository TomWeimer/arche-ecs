module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

module arche;

import :archetype.detail.component_migration;

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
    ChunkPool &chunk_pool,
    SlotMap &slot_map,
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  assert(src_arch != nullptr && dst_arch != nullptr);
  Chunk *src_chunk = old_record.chunk;
  const uint32_t src_row = old_record.row;

  auto [dst_chunk, dst_row] = dst_arch->allocate_slot(chunk_pool, entity);

  copy_shared_components(
      src_chunk,
      src_row,
      src_arch,
      dst_chunk,
      dst_row,
      dst_arch,
      mutated_type,
      added_component_data
  );

  const auto reloc = src_chunk->swap_and_pop(
      src_row, src_arch->column_layouts()
  );
  if (reloc.has_relocation()) {
    slot_map.update_row(reloc.moved_entity, src_row);
  }
  src_arch->decrement_entity_count();
  dst_arch->increment_entity_count();

  slot_map.update_location(entity, dst_arch, dst_chunk, dst_row);

  if (src_chunk->is_empty() && src_arch->chunks().size() > 1) {
    src_arch->recycle_chunk(chunk_pool, src_chunk);
  }
}

} // namespace arche
