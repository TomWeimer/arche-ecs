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

module arche;

namespace arche {

// =============================================================================
// SnapshotFacade Implementation
// =============================================================================

size_t SnapshotFacade::size_bytes() const noexcept {
  assert(world_ != nullptr);
  const size_t slot_bytes = internal::WorldAccess::slot_map(*world_)
                                .snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
    total_chunk_count += arch_ptr->chunks().size();
  }
  const uint32_t arch_count = static_cast<uint32_t>(
      internal::WorldAccess::graph(*world_).archetypes().size()
  );
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * Hardware::optimal_chunk_size();
  return sizeof(SnapshotHeader) + slot_bytes + arch_metadata_bytes +
      chunk_bytes;
}

SnapshotHeader SnapshotFacade::header() const noexcept {
  assert(world_ != nullptr);
  const size_t slot_bytes = internal::WorldAccess::slot_map(*world_)
                                .snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
    total_chunk_count += arch_ptr->chunks().size();
  }
  const uint32_t arch_count = static_cast<uint32_t>(
      internal::WorldAccess::graph(*world_).archetypes().size()
  );
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * Hardware::optimal_chunk_size();
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes +
      arch_metadata_bytes + chunk_bytes;

  SnapshotHeader hdr{
      .magic = 0x48435241,
      .version = 1,
      .current_version = internal::WorldAccess::version(*world_),
      .slot_count = static_cast<uint32_t>(
          internal::WorldAccess::slot_map(*world_).capacity()
      ),
      .free_list_head = internal::WorldAccess::slot_map(*world_)
                            .free_head_index(),
      .active_entities = static_cast<uint32_t>(
          internal::WorldAccess::slot_map(*world_).size()
      ),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
      .archetype_count = arch_count,
  };
  return hdr;
}

size_t SnapshotFacade::save(void *destination_buffer, size_t max_bytes) {
  assert(world_ != nullptr);
  assert(
      destination_buffer != nullptr && "Snapshot destination cannot be null"
  );

  const size_t slot_bytes = internal::WorldAccess::slot_map(*world_)
                                .snapshot_byte_size();
  size_t total_chunk_count = 0;
  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
    total_chunk_count += arch_ptr->chunks().size();
  }

  const uint32_t arch_count = static_cast<uint32_t>(
      internal::WorldAccess::graph(*world_).archetypes().size()
  );
  const size_t arch_metadata_bytes = arch_count * sizeof(uint32_t);
  const size_t chunk_bytes = total_chunk_count * Hardware::optimal_chunk_size();
  const size_t required_bytes = sizeof(SnapshotHeader) + slot_bytes +
      arch_metadata_bytes + chunk_bytes;

  if (max_bytes < required_bytes) {
    throw std::runtime_error("save_snapshot: buffer capacity too small");
  }

  uint8_t *cursor = static_cast<uint8_t *>(destination_buffer);

  SnapshotHeader hdr{
      .magic = 0x48435241,
      .version = 1,
      .current_version = internal::WorldAccess::version(*world_),
      .slot_count = static_cast<uint32_t>(
          internal::WorldAccess::slot_map(*world_).capacity()
      ),
      .free_list_head = internal::WorldAccess::slot_map(*world_)
                            .free_head_index(),
      .active_entities = static_cast<uint32_t>(
          internal::WorldAccess::slot_map(*world_).size()
      ),
      .chunk_count = static_cast<uint32_t>(total_chunk_count),
      .total_payload_bytes = required_bytes,
      .archetype_count = arch_count,
  };
  std::memcpy(cursor, &hdr, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  internal::WorldAccess::slot_map(*world_).write_snapshot_data(cursor);
  cursor += slot_bytes;

  for (const auto &arch_ptr :
       internal::WorldAccess::graph(*world_).archetypes()) {
    const auto &chunks = arch_ptr->chunks();
    uint32_t c_count = static_cast<uint32_t>(chunks.size());
    std::memcpy(cursor, &c_count, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    for (Chunk *chunk : chunks) {
      std::memcpy(cursor, chunk, Hardware::optimal_chunk_size());
      cursor += Hardware::optimal_chunk_size();
    }
  }

  return required_bytes;
}

void SnapshotFacade::load(const void *snapshot_buffer, size_t bytes) {
  assert(world_ != nullptr);
  assert(snapshot_buffer != nullptr && "Snapshot source cannot be null");
  if (bytes < sizeof(SnapshotHeader)) {
    throw std::runtime_error("load_snapshot: buffer smaller than header");
  }

  const uint8_t *cursor = static_cast<const uint8_t *>(snapshot_buffer);
  SnapshotHeader header;
  std::memcpy(&header, cursor, sizeof(SnapshotHeader));
  cursor += sizeof(SnapshotHeader);

  if (header.magic != 0x48435241 || header.version != 1) {
    throw std::runtime_error(
        "load_snapshot: invalid snapshot header magic or version"
    );
  }

  internal::WorldAccess::version(*world_) = static_cast<uint32_t>(
      header.current_version
  );

  const size_t slot_bytes = header.slot_count * sizeof(SlotMap::Slot);
  internal::WorldAccess::slot_map(*world_).read_snapshot_data(
      cursor, header.slot_count, header.free_list_head, header.active_entities
  );
  cursor += slot_bytes;

  const auto &archetypes = internal::WorldAccess::graph(*world_).archetypes();
  for (size_t arch_idx = 0; arch_idx < archetypes.size(); ++arch_idx) {
    Archetype *arch = archetypes[arch_idx].get();
    auto &chunks = arch->chunks();

    if (arch_idx < header.archetype_count) {
      uint32_t snapshot_chunk_count = 0;
      std::memcpy(&snapshot_chunk_count, cursor, sizeof(uint32_t));
      cursor += sizeof(uint32_t);

      while (chunks.size() > snapshot_chunk_count) {
        internal::WorldAccess::chunk_pool(*world_).release_buffer(
            chunks.back()
        );
        chunks.pop_back();
      }

      while (chunks.size() < snapshot_chunk_count) {
        void *raw = internal::WorldAccess::chunk_pool(*world_).acquire_buffer();
        chunks.push_back(
            Chunk::create(
                raw,
                arch->chunk_capacity(),
                internal::WorldAccess::chunk_size(*world_)
            )
        );
      }

      uint32_t arch_entity_count = 0;
      for (size_t c_idx = 0; c_idx < snapshot_chunk_count; ++c_idx) {
        Chunk *chunk = chunks[c_idx];
        std::memcpy(chunk, cursor, Hardware::optimal_chunk_size());
        cursor += Hardware::optimal_chunk_size();

        arch_entity_count += chunk->count();

        for (uint32_t r = 0; r < chunk->count(); ++r) {
          Entity e = chunk->entity(r);
          internal::WorldAccess::slot_map(*world_).update_location(
              e, arch, chunk, r
          );
        }
      }

      arch->set_entity_count(arch_entity_count);
    } else {
      while (!chunks.empty()) {
        internal::WorldAccess::chunk_pool(*world_).release_buffer(
            chunks.back()
        );
        chunks.pop_back();
      }
      arch->set_entity_count(0);
    }
  }
}

bool SnapshotFacade::save_to_file(std::string_view file_path) {
  size_t bytes = size_bytes();
  std::vector<uint8_t> buffer(bytes);
  save(buffer.data(), bytes);
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

bool SnapshotFacade::load_from_file(std::string_view file_path) {
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

// =============================================================================
// ArchetypeFacade Implementation
// =============================================================================

size_t ArchetypeFacade::count() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::graph(*world_).archetypes().size();
}

std::vector<Chunk *> &ArchetypeFacade::chunks(Archetype *arch) {
  assert(arch != nullptr);
  return arch->chunks();
}

std::span<Chunk *const> ArchetypeFacade::chunks(const Archetype *arch) const {
  assert(arch != nullptr);
  return arch->chunks();
}

// =============================================================================
// SystemFacade Implementation
// =============================================================================

void SystemFacade::run(ThreadPool &pool) {
  assert(world_ != nullptr);
  internal::WorldAccess::scheduler(*world_).run(*world_, pool);
}

void SystemFacade::run() {
  assert(world_ != nullptr);
  internal::WorldAccess::scheduler(*world_).run(*world_);
}

void SystemFacade::compile() {
  assert(world_ != nullptr);
  internal::WorldAccess::scheduler(*world_).compile(*world_);
}

SystemFacade &SystemFacade::add_system(
    std::string name,
    AccessDescriptor access,
    SystemFunction fn,
    SystemScale scale
) {
  assert(world_ != nullptr);
  internal::WorldAccess::scheduler(*world_).add_system(
      std::move(name), access, std::move(fn), scale
  );
  return *this;
}

Scheduler &SystemFacade::scheduler() noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::scheduler(*world_);
}

const Scheduler &SystemFacade::scheduler() const noexcept {
  assert(world_ != nullptr);
  return internal::WorldAccess::scheduler(*world_);
}

} // namespace arche
