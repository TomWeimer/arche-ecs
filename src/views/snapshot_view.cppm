module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module arche:snapshot_view;

export import :snapshot_header;
import :entity;
import :component_id;
import :slot_map;
import :chunk;
import :chunk_pool;
import :archetype;
import :archetype_graph;
import :resource;

export namespace arche {

// =============================================================================
// SnapshotStorageView: Non-owning assembly view over world storage
// =============================================================================

struct SnapshotStorageView {
  internal::SlotMap &slot_map;
  internal::ArchetypeGraph &graph;
  internal::ChunkPool &chunk_pool;
  internal::ComponentRegistry &registry;
  internal::ResourceContainer &resources;
  size_t chunk_size{65536};
  uint32_t &version;
};

// =============================================================================
// Snapshots Domain View
// =============================================================================

/**
 * @class SnapshotView
 * @brief Domain view for dual-mode binary serialization, checkpoints, and persistence.
 *
 * Supports ultra-fast in-memory raw checkpoints (< 1 ms rollback), portable
 * self-describing relocatable snapshot envelopes for cold boot, and additive package imports.
 *
 * Accessible via `world.snapshots`.
 */
class SnapshotView {
private:
  friend class World;
  explicit constexpr SnapshotView(
      SnapshotStorageView (*get_view)(void *ctx),
      void *ctx
  ) noexcept
      : get_view_(get_view), ctx_(ctx) {}

public:
  SnapshotView() = delete;
  SnapshotView(const SnapshotView &) noexcept = default;
  SnapshotView &operator=(const SnapshotView &) noexcept = default;
  SnapshotView(SnapshotView &&) noexcept = default;
  SnapshotView &operator=(SnapshotView &&) noexcept = default;
  ~SnapshotView() = default;

  // --- Raw Checkpoint Serialization (Ultra-Fast Rollback < 1 ms) -------------

  /**
   * @brief Computes required buffer capacity in bytes for a raw memory checkpoint.
   */
  [[nodiscard]] size_t checkpoint_size_bytes() const noexcept {
    auto storage = view();
    const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
    size_t total_chunks = 0;
    auto archs = storage.graph.archetypes();
    for (const auto &arch_ptr : archs) {
      total_chunks += arch_ptr->chunks().size();
    }
    const uint32_t arch_count = static_cast<uint32_t>(archs.size());
    const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
    const size_t chunk_bytes = total_chunks * storage.chunk_size;
    const size_t resource_bytes = storage.resources.snapshot_byte_size();
    return sizeof(SnapshotHeader) + slot_bytes + arch_metadata_bytes +
        chunk_bytes + resource_bytes;
  }

  /**
   * @brief Serializes complete simulation state as an ultra-fast raw memory checkpoint.
   */
  size_t save_checkpoint(void *destination_buffer, size_t max_bytes) {
    assert(destination_buffer != nullptr && "Destination buffer cannot be null");
    auto storage = view();
    const size_t required_bytes = checkpoint_size_bytes();

    if (max_bytes < required_bytes) {
      throw std::runtime_error("save_checkpoint: buffer capacity too small");
    }

    uint8_t *cursor = static_cast<uint8_t *>(destination_buffer);
    size_t total_chunks = 0;
    auto archs = storage.graph.archetypes();
    for (const auto &arch_ptr : archs) {
      total_chunks += arch_ptr->chunks().size();
    }

    SnapshotHeader hdr{
        .magic = SnapshotHeader::MAGIC,
        .version = SnapshotHeader::FORMAT_VERSION,
        .mode = SnapshotHeader::CHECKPOINT_RAW,
        .flags = 0,
        .current_version = storage.version,
        .slot_count = static_cast<uint32_t>(storage.slot_map.capacity()),
        .free_list_head = storage.slot_map.free_head_index(),
        .active_entities = static_cast<uint32_t>(storage.slot_map.size()),
        .chunk_count = static_cast<uint32_t>(total_chunks),
        .total_payload_bytes = required_bytes,
        .archetype_count = static_cast<uint32_t>(archs.size()),
        .resource_count = storage.resources.active_resource_count(),
    };
    std::memcpy(cursor, &hdr, sizeof(SnapshotHeader));
    cursor += sizeof(SnapshotHeader);

    const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
    storage.slot_map.write_snapshot_data(cursor);
    cursor += slot_bytes;

    for (const auto &arch_ptr : archs) {
      const auto &chunks = arch_ptr->chunks();
      uint32_t c_count = static_cast<uint32_t>(chunks.size());
      std::memcpy(cursor, &c_count, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      for (internal::Chunk *chunk : chunks) {
        std::memcpy(cursor, chunk, storage.chunk_size);
        cursor += storage.chunk_size;
      }
    }

    const size_t res_written = storage.resources.write_snapshot_data(cursor);
    cursor += res_written;

    return static_cast<size_t>(cursor - static_cast<uint8_t *>(destination_buffer));
  }

  /**
   * @brief Restores simulation state instantly from a raw memory checkpoint.
   */
  void restore_checkpoint(const void *snapshot_buffer, size_t bytes) {
    assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
    if (bytes < sizeof(SnapshotHeader)) {
      throw std::runtime_error("restore_checkpoint: buffer smaller than header");
    }

    const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
    SnapshotHeader hdr;
    std::memcpy(&hdr, cursor, sizeof(SnapshotHeader));
    cursor += sizeof(SnapshotHeader);

    if (hdr.magic != SnapshotHeader::MAGIC || hdr.version != SnapshotHeader::FORMAT_VERSION) {
      throw std::runtime_error("restore_checkpoint: invalid header magic or version");
    }

    auto storage = view();
    storage.version = static_cast<uint32_t>(hdr.current_version);

    const size_t slot_bytes = hdr.slot_count * sizeof(internal::SlotMap::Slot);
    storage.slot_map.read_snapshot_data(
        cursor,
        hdr.slot_count,
        hdr.free_list_head,
        hdr.active_entities
    );
    cursor += slot_bytes;

    auto archs = storage.graph.archetypes();
    for (size_t arch_idx = 0; arch_idx < archs.size(); ++arch_idx) {
      internal::Archetype *arch = archs[arch_idx].get();
      auto &chunks = arch->chunks();

      if (arch_idx < hdr.archetype_count) {
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
              internal::Chunk::create(raw, arch->chunk_capacity(), storage.chunk_size)
          );
        }

        uint32_t arch_entity_count = 0;
        for (size_t c_idx = 0; c_idx < snapshot_chunk_count; ++c_idx) {
          internal::Chunk *chunk = chunks[c_idx];
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

    if (hdr.resource_count > 0) {
      storage.resources.read_snapshot_data(cursor, hdr.resource_count);
    }
  }

  // --- Portable Relocatable Snapshot Envelope --------------------------------

  /**
   * @brief Computes required buffer capacity in bytes for a portable relocatable snapshot.
   */
  [[nodiscard]] size_t size_bytes() const noexcept {
    auto storage = view();
    size_t bytes = sizeof(SnapshotHeader) + sizeof(uint32_t); // header + arch count

    for (const auto &arch_ptr : storage.graph.archetypes()) {
      if (arch_ptr->entity_count() == 0) {
        continue;
      }
      const uint32_t comp_count = static_cast<uint32_t>(arch_ptr->component_count());
      bytes += sizeof(uint32_t); // comp_count
      bytes += comp_count * (sizeof(ComponentID) + 2 * sizeof(uint32_t)); // id, size, align
      bytes += sizeof(uint32_t); // chunk_count

      for (const internal::Chunk *chunk : arch_ptr->chunks()) {
        const uint32_t cnt = chunk->count();
        bytes += sizeof(uint32_t); // count
        bytes += cnt * sizeof(Entity);
        for (size_t c = 0; c < comp_count; ++c) {
          const auto &layout = arch_ptr->column_layout(c);
          bytes += cnt * layout.size;
        }
      }
    }

    // Slot table state
    bytes += storage.slot_map.snapshot_byte_size();
    // Resources
    bytes += storage.resources.snapshot_byte_size();

    return bytes;
  }

  /**
   * @brief Serializes world simulation state into a portable relocatable snapshot envelope.
   */
  size_t save(void *destination_buffer, size_t max_bytes) {
    assert(destination_buffer != nullptr && "Destination buffer cannot be null");
    auto storage = view();
    const size_t required_bytes = size_bytes();

    if (max_bytes < required_bytes) {
      throw std::runtime_error("save: buffer capacity too small");
    }

    uint8_t *cursor = static_cast<uint8_t *>(destination_buffer);

    uint32_t non_empty_archetypes = 0;
    size_t total_chunks = 0;
    auto archs = storage.graph.archetypes();
    for (const auto &arch_ptr : archs) {
      if (arch_ptr->entity_count() > 0) {
        ++non_empty_archetypes;
        total_chunks += arch_ptr->chunks().size();
      }
    }

    SnapshotHeader hdr{
        .magic = SnapshotHeader::MAGIC,
        .version = SnapshotHeader::FORMAT_VERSION,
        .mode = SnapshotHeader::RELOCATABLE_ENVELOPE,
        .flags = 0,
        .current_version = storage.version,
        .slot_count = static_cast<uint32_t>(storage.slot_map.capacity()),
        .free_list_head = storage.slot_map.free_head_index(),
        .active_entities = static_cast<uint32_t>(storage.slot_map.size()),
        .chunk_count = static_cast<uint32_t>(total_chunks),
        .total_payload_bytes = required_bytes,
        .archetype_count = non_empty_archetypes,
        .resource_count = storage.resources.active_resource_count(),
    };
    std::memcpy(cursor, &hdr, sizeof(SnapshotHeader));
    cursor += sizeof(SnapshotHeader);

    // Write archetypes
    std::memcpy(cursor, &non_empty_archetypes, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (const auto &arch_ptr : archs) {
      if (arch_ptr->entity_count() == 0) {
        continue;
      }
      const uint32_t comp_count = static_cast<uint32_t>(arch_ptr->component_count());
      std::memcpy(cursor, &comp_count, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      for (size_t c = 0; c < comp_count; ++c) {
        const auto &layout = arch_ptr->column_layout(c);
        ComponentID cid = layout.component_id;
        uint32_t sz = layout.size;
        uint32_t align = layout.alignment;
        std::memcpy(cursor, &cid, sizeof(ComponentID));
        cursor += sizeof(ComponentID);
        std::memcpy(cursor, &sz, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
        std::memcpy(cursor, &align, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
      }

      const auto &chunks = arch_ptr->chunks();
      uint32_t c_count = static_cast<uint32_t>(chunks.size());
      std::memcpy(cursor, &c_count, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      for (const internal::Chunk *chunk : chunks) {
        uint32_t cnt = chunk->count();
        std::memcpy(cursor, &cnt, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        const Entity *entities = chunk->entity_slice();
        std::memcpy(cursor, entities, cnt * sizeof(Entity));
        cursor += cnt * sizeof(Entity);

        for (size_t c = 0; c < comp_count; ++c) {
          const auto &layout = arch_ptr->column_layout(c);
          const void *col_data = chunk->component_element(layout, 0);
          std::memcpy(cursor, col_data, cnt * layout.size);
          cursor += cnt * layout.size;
        }
      }
    }

    // Write slot map state
    const size_t slot_bytes = storage.slot_map.snapshot_byte_size();
    storage.slot_map.write_snapshot_data(cursor);
    cursor += slot_bytes;

    // Write resources
    const size_t res_written = storage.resources.write_snapshot_data(cursor);
    cursor += res_written;

    return static_cast<size_t>(cursor - static_cast<uint8_t *>(destination_buffer));
  }

  /**
   * @brief Restores world simulation state from either a raw checkpoint or relocatable envelope.
   */
  void load(const void *snapshot_buffer, size_t bytes) {
    assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
    if (bytes < sizeof(SnapshotHeader)) {
      throw std::runtime_error("load: buffer smaller than header");
    }

    const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
    SnapshotHeader hdr;
    std::memcpy(&hdr, cursor, sizeof(SnapshotHeader));

    if (hdr.magic != SnapshotHeader::MAGIC || hdr.version != SnapshotHeader::FORMAT_VERSION) {
      throw std::runtime_error("load: invalid header magic or version");
    }

    if (hdr.mode == SnapshotHeader::CHECKPOINT_RAW) {
      restore_checkpoint(snapshot_buffer, bytes);
      return;
    }

    cursor += sizeof(SnapshotHeader);
    auto storage = view();

    // 1. Reset existing archetype chunks and release back to pool
    for (const auto &arch_ptr : storage.graph.archetypes()) {
      auto &chunks = arch_ptr->chunks();
      while (!chunks.empty()) {
        storage.chunk_pool.release(chunks.back());
        chunks.pop_back();
      }
      arch_ptr->set_entity_count(0);
    }
    storage.slot_map.clear();
    storage.version = static_cast<uint32_t>(hdr.current_version);

    // 2. Read archetypes
    uint32_t num_archetypes = 0;
    std::memcpy(&num_archetypes, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (uint32_t a = 0; a < num_archetypes; ++a) {
      uint32_t comp_count = 0;
      std::memcpy(&comp_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      std::vector<ComponentID> comp_ids(comp_count);
      for (uint32_t c = 0; c < comp_count; ++c) {
        ComponentID cid = 0;
        uint32_t sz = 0;
        uint32_t align = 0;
        std::memcpy(&cid, cursor, sizeof(ComponentID));
        cursor += sizeof(ComponentID);
        std::memcpy(&sz, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
        std::memcpy(&align, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        storage.registry.register_pod(cid, sz, align);
        comp_ids[c] = cid;
      }

      internal::Archetype *arch = storage.graph.get_or_create(comp_ids);

      uint32_t chunk_count = 0;
      std::memcpy(&chunk_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      uint32_t total_arch_entities = 0;
      for (uint32_t ch = 0; ch < chunk_count; ++ch) {
        uint32_t cnt = 0;
        std::memcpy(&cnt, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        void *raw = storage.chunk_pool.acquire();
        internal::Chunk *chunk = internal::Chunk::create(
            raw, arch->chunk_capacity(), storage.chunk_size
        );

        chunk->append_entities(
            std::span<const Entity>(reinterpret_cast<const Entity *>(cursor), cnt)
        );
        cursor += cnt * sizeof(Entity);

        for (size_t c = 0; c < comp_count; ++c) {
          const auto &layout = arch->column_layout(c);
          void *col_data = chunk->component_element(layout, 0);
          std::memcpy(col_data, cursor, cnt * layout.size);
          cursor += cnt * layout.size;
        }

        arch->chunks().push_back(chunk);
        total_arch_entities += cnt;
      }

      arch->set_entity_count(total_arch_entities);
    }

    // 3. Restore slot map
    const size_t slot_bytes = hdr.slot_count * sizeof(internal::SlotMap::Slot);
    storage.slot_map.read_snapshot_data(
        cursor,
        hdr.slot_count,
        hdr.free_list_head,
        hdr.active_entities
    );
    cursor += slot_bytes;

    // 4. Relink all entity coordinates in slot map
    for (const auto &arch_ptr : storage.graph.archetypes()) {
      internal::Archetype *arch = arch_ptr.get();
      for (internal::Chunk *chunk : arch->chunks()) {
        for (uint32_t r = 0; r < chunk->count(); ++r) {
          Entity e = chunk->entity(r);
          storage.slot_map.update_location(e, arch, chunk, r);
        }
      }
    }

    // 5. Restore resources
    if (hdr.resource_count > 0) {
      storage.resources.read_snapshot_data(cursor, hdr.resource_count);
    }
  }

  // --- Additive Package Import -----------------------------------------------

  /**
   * @brief Additively imports entities from a snapshot envelope into the current world.
   *
   * Existing entities and resources are preserved. All imported entities receive new
   * distinct entity handles allocated from the current world's slot map.
   *
   * @return Vector of newly allocated Entity handles corresponding to the imported entities.
   */
  std::vector<Entity> load_additive(const void *snapshot_buffer, size_t bytes) {
    assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
    if (bytes < sizeof(SnapshotHeader)) {
      throw std::runtime_error("load_additive: buffer smaller than header");
    }

    const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
    SnapshotHeader hdr;
    std::memcpy(&hdr, cursor, sizeof(SnapshotHeader));

    if (hdr.magic != SnapshotHeader::MAGIC || hdr.version != SnapshotHeader::FORMAT_VERSION) {
      throw std::runtime_error("load_additive: invalid header magic or version");
    }

    if (hdr.mode != SnapshotHeader::RELOCATABLE_ENVELOPE) {
      throw std::runtime_error("load_additive: only relocatable envelopes support additive import");
    }

    cursor += sizeof(SnapshotHeader);
    auto storage = view();

    std::vector<Entity> imported_handles;
    imported_handles.reserve(hdr.active_entities);

    uint32_t num_archetypes = 0;
    std::memcpy(&num_archetypes, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (uint32_t a = 0; a < num_archetypes; ++a) {
      uint32_t comp_count = 0;
      std::memcpy(&comp_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      std::vector<ComponentID> comp_ids(comp_count);
      for (uint32_t c = 0; c < comp_count; ++c) {
        ComponentID cid = 0;
        uint32_t sz = 0;
        uint32_t align = 0;
        std::memcpy(&cid, cursor, sizeof(ComponentID));
        cursor += sizeof(ComponentID);
        std::memcpy(&sz, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
        std::memcpy(&align, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        storage.registry.register_pod(cid, sz, align);
        comp_ids[c] = cid;
      }

      internal::Archetype *arch = storage.graph.get_or_create(comp_ids);

      uint32_t chunk_count = 0;
      std::memcpy(&chunk_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      for (uint32_t ch = 0; ch < chunk_count; ++ch) {
        uint32_t cnt = 0;
        std::memcpy(&cnt, cursor, sizeof(uint32_t));
        cursor += sizeof(uint32_t);

        // Skip old entity handles
        cursor += cnt * sizeof(Entity);

        // Column source pointers
        std::vector<const void *> col_ptrs(comp_count);
        for (size_t c = 0; c < comp_count; ++c) {
          const auto &layout = arch->column_layout(c);
          col_ptrs[c] = cursor;
          cursor += cnt * layout.size;
        }

        // Allocate new handles
        std::vector<Entity> new_batch(cnt);
        storage.slot_map.allocate_batch(cnt, new_batch.data());

        // Append to archetype chunks
        for (uint32_t i = 0; i < cnt; ++i) {
          Entity new_e = new_batch[i];
          internal::Chunk *target_chunk = arch->ensure_writable_chunk(storage.chunk_pool);
          uint32_t row = target_chunk->append_entity(new_e);

          for (size_t c = 0; c < comp_count; ++c) {
            const auto &layout = arch->column_layout(c);
            const void *src_elem =
                static_cast<const uint8_t *>(col_ptrs[c]) + (i * layout.size);
            void *dst_elem = target_chunk->component_element(layout, row);
            std::memcpy(dst_elem, src_elem, layout.size);
          }

          arch->set_entity_count(arch->entity_count() + 1);
          storage.slot_map.update_location(new_e, arch, target_chunk, row);

          imported_handles.push_back(new_e);
        }
      }
    }

    return imported_handles;
  }

  // --- File Persistence ------------------------------------------------------

  /// Saves a portable relocatable snapshot directly to a file on disk.
  bool save_to_file(std::string_view file_path) {
    const size_t bytes = size_bytes();
    std::vector<uint8_t> buffer(bytes);
    const size_t written = save(buffer.data(), bytes);
    std::ofstream out(std::string(file_path), std::ios::binary);
    if (!out.is_open()) {
      return false;
    }
    out.write(
        reinterpret_cast<const char *>(buffer.data()),
        static_cast<std::streamsize>(written)
    );
    return out.good();
  }

  /// Restores complete simulation state from a file on disk (checkpoint or relocatable).
  bool load_from_file(std::string_view file_path) {
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
    load(buffer.data(), buffer.size());
    return true;
  }

  /// Saves an ultra-fast raw memory checkpoint to a file on disk.
  bool save_checkpoint_to_file(std::string_view file_path) {
    const size_t bytes = checkpoint_size_bytes();
    std::vector<uint8_t> buffer(bytes);
    const size_t written = save_checkpoint(buffer.data(), bytes);
    std::ofstream out(std::string(file_path), std::ios::binary);
    if (!out.is_open()) {
      return false;
    }
    out.write(
        reinterpret_cast<const char *>(buffer.data()),
        static_cast<std::streamsize>(written)
    );
    return out.good();
  }

  /// Restores complete simulation state from a raw memory checkpoint file on disk.
  bool load_checkpoint_from_file(std::string_view file_path) {
    return load_from_file(file_path);
  }

  /// Returns current snapshot header containing format version and entity counts.
  [[nodiscard]] SnapshotHeader header() const noexcept {
    auto storage = view();
    size_t total_chunks = 0;
    auto archs = storage.graph.archetypes();
    for (const auto &arch_ptr : archs) {
      total_chunks += arch_ptr->chunks().size();
    }
    return SnapshotHeader{
        .magic = SnapshotHeader::MAGIC,
        .version = SnapshotHeader::FORMAT_VERSION,
        .mode = SnapshotHeader::RELOCATABLE_ENVELOPE,
        .flags = 0,
        .current_version = storage.version,
        .slot_count = static_cast<uint32_t>(storage.slot_map.capacity()),
        .free_list_head = storage.slot_map.free_head_index(),
        .active_entities = static_cast<uint32_t>(storage.slot_map.size()),
        .chunk_count = static_cast<uint32_t>(total_chunks),
        .total_payload_bytes = size_bytes(),
        .archetype_count = static_cast<uint32_t>(archs.size()),
        .resource_count = storage.resources.active_resource_count(),
    };
  }

private:
  [[nodiscard]] SnapshotStorageView view() const noexcept {
    assert(get_view_ != nullptr);
    return get_view_(ctx_);
  }

  SnapshotStorageView (*get_view_)(void *ctx){nullptr};
  void *ctx_{nullptr};
};

using SnapshotFacade = SnapshotView;

} // namespace arche
