module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module arche:snapshot.detail.impl;

import :snapshot;

namespace arche::internal {

// =============================================================================
// Size Inspection & Header Construction
// =============================================================================

/**
 * @brief Computes required byte capacity to serialize all slots, archetype chunk headers, and chunk pages.
 */
size_t Snapshot::size_bytes(const SnapshotStorageView &storage) noexcept {
  const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr : storage.archetypes) {
    total_chunk_count += arch_ptr->chunks().size();
  }
  const uint32_t arch_count = static_cast<uint32_t>(storage.archetypes.size());
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * storage.chunk_size;
  return sizeof(SnapshotHeader) + slot_bytes + arch_metadata_bytes +
      chunk_bytes;
}

/**
 * @brief Constructs a SnapshotHeader POD populated with current storage metrics.
 */
SnapshotHeader Snapshot::header(const SnapshotStorageView &storage) noexcept {
  const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr : storage.archetypes) {
    total_chunk_count += arch_ptr->chunks().size();
  }
  const uint32_t arch_count = static_cast<uint32_t>(storage.archetypes.size());
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * storage.chunk_size;
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes +
      arch_metadata_bytes + chunk_bytes;

  SnapshotHeader hdr{
      .magic = 0x48435241,
      .version = 1,
      .current_version = storage.version,
      .slot_count = static_cast<uint32_t>(storage.slot_map.capacity()),
      .free_list_head = storage.slot_map.free_head_index(),
      .active_entities = static_cast<uint32_t>(storage.slot_map.size()),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
      .archetype_count = arch_count,
  };
  return hdr;
}

// =============================================================================
// Buffer Serialization & Deserialization
// =============================================================================

/**
 * @brief Serializes complete simulation state into a contiguous memory buffer.
 */
size_t Snapshot::save(const SnapshotStorageView &storage, SnapshotTarget target) {
  assert(target.buffer != nullptr && "Snapshot destination cannot be null");

  const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr : storage.archetypes) {
    total_chunk_count += arch_ptr->chunks().size();
  }

  const uint32_t arch_count = static_cast<uint32_t>(storage.archetypes.size());
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * storage.chunk_size;
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes +
      arch_metadata_bytes + chunk_bytes;

  if (target.max_bytes < required_bytes) {
    throw std::runtime_error("save_snapshot: buffer capacity too small");
  }

  uint8_t *cursor = static_cast<uint8_t *>(target.buffer);

  SnapshotHeader hdr{
      .magic = 0x48435241,
      .version = 1,
      .current_version = storage.version,
      .slot_count = static_cast<uint32_t>(storage.slot_map.capacity()),
      .free_list_head = storage.slot_map.free_head_index(),
      .active_entities = static_cast<uint32_t>(storage.slot_map.size()),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
      .archetype_count = arch_count,
  };
  std::memcpy(cursor, &hdr, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  storage.slot_map.write_snapshot_data(cursor);
  cursor += slot_bytes;

  for (const auto &arch_ptr : storage.archetypes) {
    const auto &chunks = arch_ptr->chunks();
    uint32_t c_count = static_cast<uint32_t>(chunks.size());
    std::memcpy(cursor, &c_count, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (Chunk *chunk : chunks) {
      std::memcpy(cursor, chunk, storage.chunk_size);
      cursor += storage.chunk_size;
    }
  }

  return required_bytes;
}

/**
 * @brief Restores complete simulation state from a serialized snapshot buffer.
 */
void Snapshot::load(const SnapshotStorageView &storage, SnapshotSource source) {
  assert(source.buffer != nullptr && "Snapshot source cannot be null");
  if (source.byte_count < sizeof(SnapshotHeader)) {
    throw std::runtime_error("load_snapshot: buffer smaller than header");
  }

  const uint8_t *cursor = static_cast<const uint8_t *>(source.buffer);
  SnapshotHeader header_data;
  std::memcpy(&header_data, cursor, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  if (header_data.magic != 0x48435241 || header_data.version != 1) {
    throw std::runtime_error(
        "load_snapshot: invalid snapshot header magic or version"
    );
  }

  storage.version = static_cast<uint32_t>(header_data.current_version);

  const size_t slot_bytes = header_data.slot_count * sizeof(SlotMap::Slot);
  storage.slot_map.read_snapshot_data(
      cursor,
      header_data.slot_count,
      header_data.free_list_head,
      header_data.active_entities
  );
  cursor += slot_bytes;

  for (size_t arch_idx = 0; arch_idx < storage.archetypes.size(); ++arch_idx) {
    Archetype *arch = storage.archetypes[arch_idx].get();
    auto &chunks = arch->chunks();

    if (arch_idx < header_data.archetype_count) {
      uint32_t snapshot_chunk_count = 0;
      std::memcpy(&snapshot_chunk_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      while (chunks.size() > snapshot_chunk_count) {
        storage.chunk_pool.release(chunks.back());
        chunks.pop_back();
      }

      while (chunks.size() < snapshot_chunk_count) {
        void *raw = storage.chunk_pool.acquire();
        chunks.push_back(
            Chunk::create(raw, arch->chunk_capacity(), storage.chunk_size)
        );
      }

      uint32_t arch_entity_count = 0;
      for (size_t c_idx = 0; c_idx < snapshot_chunk_count; ++c_idx) {
        Chunk *chunk = chunks[c_idx];
        std::memcpy(chunk, cursor, storage.chunk_size);
        cursor += storage.chunk_size;

        arch_entity_count += chunk->count();

        for (uint32_t r = 0; r < chunk->count(); ++r) {
          Entity e = chunk->entity(r);
          storage.slot_map.update_location(e, arch, chunk, r);
        }
      }

      arch->set_entity_count(arch_entity_count);
    } else {
      while (!chunks.empty()) {
        storage.chunk_pool.release(chunks.back());
        chunks.pop_back();
      }
      arch->set_entity_count(0);
    }
  }
}

// =============================================================================
// File Checkpoint Persistence
// =============================================================================

/**
 * @brief Saves complete simulation state to a binary checkpoint file.
 */
bool Snapshot::save_to_file(
    const SnapshotStorageView &storage,
    std::string_view file_path
) {
  size_t bytes = size_bytes(storage);
  std::vector<uint8_t> buffer(bytes);
  save(storage, SnapshotTarget{buffer.data(), bytes});
  std::ofstream out(std::string(file_path), std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out.write(
      reinterpret_cast<const char *>(buffer.data()),
      static_cast<std::streamsize>(bytes)
  );
  return out.good();
}

/**
 * @brief Restores complete simulation state from a binary checkpoint file.
 */
bool Snapshot::load_from_file(
    const SnapshotStorageView &storage,
    std::string_view file_path
) {
  std::ifstream in(std::string(file_path), std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    return false;
  }
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  if (!in.read(reinterpret_cast<char *>(buffer.data()), size)) {
    return false;
  }
  load(storage, SnapshotSource{buffer.data(), buffer.size()});
  return true;
}

} // namespace arche
