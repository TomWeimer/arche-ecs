module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

export module arche:chunk.detail.impl;

import :chunk;

export namespace arche {

// =============================================================================
// Chunk Lifecycle & Creation Implementations
// =============================================================================

Chunk::Chunk(uint32_t capacity, uint32_t) noexcept {
  // 1. Initialize fixed-size header bookkeeping fields.
  header_.capacity = capacity;
  header_.count = 0;
  header_.chunk_index = 0;
  header_.next = nullptr;
  header_.prev = nullptr;
  header_.structure_version = 0;
}

Chunk *Chunk::create(
    void *buffer,
    uint32_t capacity,
    uint32_t chunk_size
) noexcept {
  // 1. Validate destination memory pointer.
  assert(buffer != nullptr && "Cannot initialize Chunk in null buffer!");

  // 2. Placement-new Chunk directly into the preallocated aligned buffer page.
  return new (buffer) Chunk(capacity, chunk_size);
}

// =============================================================================
// Header & Metadata Introspection Implementations
// =============================================================================

ChunkHeader &Chunk::header() noexcept { return header_; }
const ChunkHeader &Chunk::header() const noexcept { return header_; }

uint32_t Chunk::count() const noexcept { return header_.count; }
uint32_t Chunk::capacity() const noexcept { return header_.capacity; }
uint32_t Chunk::chunk_index() const noexcept { return header_.chunk_index; }

bool Chunk::is_full() const noexcept {
  return header_.count >= header_.capacity;
}

bool Chunk::is_empty() const noexcept {
  return header_.count == 0;
}

ChunkHeader *Chunk::next() const noexcept { return header_.next; }

void Chunk::set_chunk_index(uint32_t idx) noexcept {
  header_.chunk_index = idx;
}

void Chunk::set_next(ChunkHeader *n) noexcept { header_.next = n; }

// =============================================================================
// Column Slices & Data Pointer Implementations
// =============================================================================

Entity *Chunk::entity_slice() noexcept {
  // 1. Compute entity array pointer offset directly past reserved header memory.
  constexpr size_t HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
  return reinterpret_cast<Entity *>(
      reinterpret_cast<uint8_t *>(this) + HEADER_RESERVE
  );
}

const Entity *Chunk::entity_slice() const noexcept {
  // 1. Compute const entity array pointer offset past reserved header memory.
  constexpr size_t HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
  return reinterpret_cast<const Entity *>(
      reinterpret_cast<const uint8_t *>(this) + HEADER_RESERVE
  );
}

template <typename T>
inline T *Chunk::typed_slice(uint32_t byte_offset) noexcept {
  // 1. Return typed component column slice at specific byte offset.
  return reinterpret_cast<T *>(component_slice(byte_offset));
}

template <typename T>
inline const T *Chunk::typed_slice(uint32_t byte_offset) const noexcept {
  // 1. Return const typed component column slice at specific byte offset.
  return reinterpret_cast<const T *>(component_slice(byte_offset));
}

template <typename T> inline T &Chunk::user_metadata() noexcept {
  // 1. Validate size and alignment bounds for inline metadata.
  static_assert(
      sizeof(T) <= sizeof(ChunkHeader::user_metadata),
      "Metadata type exceeds user_metadata storage"
  );
  static_assert(
      alignof(T) <= 16,
      "Metadata alignment too strict"
  );
  return *reinterpret_cast<T *>(header_.user_metadata);
}

template <typename T> inline const T &Chunk::user_metadata() const noexcept {
  // 1. Validate size and alignment bounds for inline metadata.
  static_assert(
      sizeof(T) <= sizeof(ChunkHeader::user_metadata),
      "Metadata type exceeds user_metadata storage"
  );
  static_assert(
      alignof(T) <= 16,
      "Metadata alignment too strict"
  );
  return *reinterpret_cast<const T *>(header_.user_metadata);
}

// =============================================================================
// Change Detection & Versioning Implementations
// =============================================================================

uint32_t Chunk::column_version(uint16_t col) const noexcept {
  assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
  return header_.column_versions[col];
}

void Chunk::set_column_version(uint16_t col, uint32_t version) noexcept {
  assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
  header_.column_versions[col] = version;
}

uint32_t Chunk::structure_version() const noexcept {
  return header_.structure_version;
}

void Chunk::set_structure_version(uint32_t version) noexcept {
  header_.structure_version = version;
}

// =============================================================================
// Component Population & Raw Memory Implementations
// =============================================================================

template <typename Comp>
inline void Chunk::fill_column(
    const ComponentColumnLayout &layout,
    uint32_t start_row,
    uint32_t count,
    const Comp &component
) {
  // 1. Locate starting component element memory.
  Comp *col = static_cast<Comp *>(component_element(layout, start_row));

  // 2. Copy-construct uniform component value into each row.
  for (uint32_t i = 0; i < count; ++i) {
    new (&col[i]) Comp(component);
  }
}

template <typename Comp>
inline void Chunk::copy_column(
    const ComponentColumnLayout &layout,
    uint32_t start_row,
    const Comp *src_data,
    uint32_t count
) {
  // 1. Locate destination component element memory.
  void *dst = component_element(layout, start_row);

  // 2. Bitwise blit contiguous component array.
  std::memcpy(dst, src_data, count * sizeof(Comp));
}

void *Chunk::component_slice(uint32_t chunk_byte_offset) noexcept {
  return reinterpret_cast<uint8_t *>(this) + chunk_byte_offset;
}

const void *Chunk::component_slice(
    uint32_t chunk_byte_offset
) const noexcept {
  return reinterpret_cast<const uint8_t *>(this) + chunk_byte_offset;
}

void *Chunk::component_element(
    const ComponentColumnLayout &layout,
    uint32_t row
) noexcept {
  return reinterpret_cast<uint8_t *>(this) + layout.chunk_byte_offset +
      (row * layout.size);
}

const void *Chunk::component_element(
    const ComponentColumnLayout &layout,
    uint32_t row
) const noexcept {
  return reinterpret_cast<const uint8_t *>(this) + layout.chunk_byte_offset +
      (row * layout.size);
}

// =============================================================================
// Entity Ingestion Implementations
// =============================================================================

Entity Chunk::entity(uint32_t row) const noexcept {
  assert(row < header_.count);
  return entity_slice()[row];
}

uint32_t Chunk::append_entity(Entity entity) noexcept {
  // 1. Verify capacity bounds before advancing row counter.
  assert(header_.count < header_.capacity);
  const uint32_t row = header_.count++;

  // 2. Write entity handle at allocated row index.
  entity_slice()[row] = entity;
  return row;
}

uint32_t Chunk::append_entities(
    std::span<const Entity> entities
) noexcept {
  // 1. Verify batch bounds against remaining chunk capacity.
  assert(header_.count + entities.size() <= header_.capacity);
  const uint32_t start_row = header_.count;

  // 2. Bitwise blit entire entity handle span into contiguous slice.
  std::memcpy(
      entity_slice() + start_row,
      entities.data(),
      entities.size() * sizeof(Entity)
  );

  // 3. Advance active entity counter by batch size.
  header_.count += static_cast<uint32_t>(entities.size());
  return start_row;
}

// =============================================================================
// Swap-and-Pop Internal Relocation Helpers
// =============================================================================

namespace detail::chunk {

inline void relocate_component(
    const ComponentColumnLayout &layout,
    void *dst,
    void *src
) noexcept {
  layout.descriptor->relocate(dst, src);
  layout.descriptor->destruct(src);
}

inline void destruct_row_components(
    Chunk *chunk,
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  for (const auto &layout : layouts) {
    void *ptr = chunk->component_element(layout, row);
    layout.descriptor->destruct(ptr);
  }
}

inline void relocate_row_components(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row,
    std::span<const ComponentColumnLayout> layouts
) noexcept {
  for (const auto &layout : layouts) {
    void *dst = chunk->component_element(layout, dst_row);
    void *src = chunk->component_element(layout, src_row);
    relocate_component(layout, dst, src);
  }
}

inline Entity relocate_entity_handle(
    Chunk *chunk,
    uint32_t dst_row,
    uint32_t src_row
) noexcept {
  Entity *entities = chunk->entity_slice();
  Entity moved_entity = entities[src_row];
  entities[dst_row] = moved_entity;
  return moved_entity;
}

} // namespace detail::chunk

// =============================================================================
// Swap-and-Pop Deletion Implementations
// =============================================================================

ChunkRelocation Chunk::swap_and_pop(
    uint32_t row,
    std::span<const ComponentColumnLayout> layouts
) {
  // 1. Validate target row index bounds.
  assert(row < header_.count && "Row out of bounds in swap_and_pop!");

  // 2. Destruct all component instances residing in the target row.
  const uint32_t last_row = header_.count - 1;
  detail::chunk::destruct_row_components(this, row, layouts);

  // 3. If target row is not the tail, relocate the tail row into the vacated slot.
  ChunkRelocation result{};
  if (row != last_row) {
    detail::chunk::relocate_row_components(this, row, last_row, layouts);
    result.moved_entity =
        detail::chunk::relocate_entity_handle(this, row, last_row);
  }

  // 4. Decrement active entity count.
  header_.count--;
  return result;
}

uint32_t Chunk::batch_swap_and_pop(
    std::span<const uint32_t> rows_to_delete,
    std::span<const ComponentColumnLayout> layouts,
    std::span<EntityRelocation> out_relocations
) {
  // 1. Early exit if deletion set is empty.
  if (rows_to_delete.empty()) {
    return 0;
  }

  uint32_t write_idx = 0;
  int32_t current_tail = static_cast<int32_t>(header_.count) - 1;

  // 2. Process deletions sequentially, filling vacated slots from valid tail rows.
  for (uint32_t row : rows_to_delete) {
    assert(row < header_.count && "Row out of bounds in batch_swap_and_pop!");
    detail::chunk::destruct_row_components(this, row, layouts);

    // Skip tail rows that are themselves scheduled for deletion.
    while (current_tail >= 0) {
      bool is_also_deleted = false;
      for (uint32_t r : rows_to_delete) {
        if (static_cast<uint32_t>(current_tail) == r) {
          is_also_deleted = true;
          break;
        }
      }
      if (!is_also_deleted) {
        break;
      }
      current_tail--;
    }

    // Relocate tail row into vacated slot if tail is strictly past the deleted row.
    if (static_cast<int32_t>(row) < current_tail) {
      detail::chunk::relocate_row_components(
          this, row, static_cast<uint32_t>(current_tail), layouts
      );
      Entity moved = detail::chunk::relocate_entity_handle(
          this, row, static_cast<uint32_t>(current_tail)
      );

      if (write_idx < out_relocations.size()) {
        out_relocations[write_idx++] = EntityRelocation{
            .entity = moved,
            .new_row = row,
        };
      }
      current_tail--;
    }
  }

  // 3. Decrement active entity counter by total deletions.
  header_.count -= static_cast<uint32_t>(rows_to_delete.size());
  return write_idx;
}

} // namespace arche
