module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

export module arche:chunk;

export import :entity;
export import :component.detail.column_layout;
export import :chunk.detail.chunk_header;
export import :chunk.detail.chunk_geometry;

export namespace arche {

// =============================================================================
// Relocation Records (Delta Stream)
// =============================================================================

struct ChunkRelocation {
  Entity moved_entity{};
  [[nodiscard]] constexpr bool has_relocation() const noexcept {
    return !moved_entity.is_null();
  }
};

struct EntityRelocation {
  Entity entity{};
  uint32_t new_row{0};
};

// =============================================================================
// Hardware-Adaptive Chunk Storage (Structure of Arrays)
// =============================================================================

struct Chunk {
private:
  ChunkHeader header_;

  inline Chunk(uint32_t capacity, uint32_t = 0) noexcept {
    header_.capacity = capacity;
    header_.count = 0;
    header_.chunk_index = 0;
    header_.next = nullptr;
    header_.prev = nullptr;
  }

  ~Chunk() = default;

public:
  Chunk(const Chunk &) = delete;
  Chunk &operator=(const Chunk &) = delete;
  Chunk(Chunk &&) = delete;
  Chunk &operator=(Chunk &&) = delete;

  [[nodiscard]] inline static Chunk *create(
      void *buffer,
      uint32_t capacity,
      uint32_t chunk_size = 0
  ) noexcept {
    assert(buffer != nullptr && "Cannot initialize Chunk in null buffer!");
    return new (buffer) Chunk(capacity, chunk_size);
  }

  [[nodiscard]] inline ChunkHeader &header() noexcept { return header_; }
  [[nodiscard]] inline const ChunkHeader &header() const noexcept {
    return header_;
  }
  [[nodiscard]] inline uint32_t count() const noexcept { return header_.count; }
  [[nodiscard]] inline uint32_t capacity() const noexcept {
    return header_.capacity;
  }
  [[nodiscard]] inline uint32_t chunk_index() const noexcept {
    return header_.chunk_index;
  }
  [[nodiscard]] inline bool is_full() const noexcept {
    return header_.count >= header_.capacity;
  }
  [[nodiscard]] inline bool is_empty() const noexcept {
    return header_.count == 0;
  }
  [[nodiscard]] inline ChunkHeader *next() const noexcept {
    return header_.next;
  }
  inline void set_chunk_index(uint32_t idx) noexcept {
    header_.chunk_index = idx;
  }
  inline void set_next(ChunkHeader *n) noexcept { header_.next = n; }

  [[nodiscard]] inline Entity *entity_slice() noexcept {
    constexpr size_t HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
    return reinterpret_cast<Entity *>(
        reinterpret_cast<uint8_t *>(this) + HEADER_RESERVE
    );
  }

  [[nodiscard]] inline const Entity *entity_slice() const noexcept {
    constexpr size_t HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
    return reinterpret_cast<const Entity *>(
        reinterpret_cast<const uint8_t *>(this) + HEADER_RESERVE
    );
  }

  template <typename T>
  [[nodiscard]] inline T *typed_slice(uint32_t byte_offset) noexcept {
    return reinterpret_cast<T *>(component_slice(byte_offset));
  }

  template <typename T>
  [[nodiscard]] inline const T *typed_slice(
      uint32_t byte_offset
  ) const noexcept {
    return reinterpret_cast<const T *>(component_slice(byte_offset));
  }

  template <typename T> [[nodiscard]] inline T &user_metadata() noexcept {
    static_assert(sizeof(T) <= sizeof(header_.user_metadata));
    static_assert(alignof(T) <= 16);
    return *reinterpret_cast<T *>(header_.user_metadata);
  }

  template <typename T>
  [[nodiscard]] inline const T &user_metadata() const noexcept {
    static_assert(sizeof(T) <= sizeof(header_.user_metadata));
    static_assert(alignof(T) <= 16);
    return *reinterpret_cast<const T *>(header_.user_metadata);
  }

  [[nodiscard]] inline uint32_t column_version(uint16_t col) const noexcept {
    assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
    return header_.column_versions[col];
  }

  inline void set_column_version(uint16_t col, uint32_t version) noexcept {
    assert(col < ChunkHeader::MAX_COMPONENTS_PER_CHUNK);
    header_.column_versions[col] = version;
  }

  inline void mark_column_dirty(uint16_t col, uint32_t version) noexcept {
    set_column_version(col, version);
  }

  [[nodiscard]] inline uint32_t structure_version() const noexcept {
    return header_.structure_version;
  }

  inline void set_structure_version(uint32_t version) noexcept {
    header_.structure_version = version;
  }

  inline void advance_structure_version(uint32_t version) noexcept {
    set_structure_version(version);
  }

  template <typename Comp>
  inline void fill_column(
      const ComponentColumnLayout &layout,
      uint32_t start_row,
      uint32_t count,
      const Comp &component
  ) {
    Comp *col = static_cast<Comp *>(component_element(layout, start_row));
    for (uint32_t i = 0; i < count; ++i) {
      new (&col[i]) Comp(component);
    }
  }

  template <typename Comp>
  inline void copy_column(
      const ComponentColumnLayout &layout,
      uint32_t start_row,
      const Comp *src_data,
      uint32_t count
  ) {
    void *dst = component_element(layout, start_row);
    std::memcpy(dst, src_data, count * sizeof(Comp));
  }

  [[nodiscard]] inline void *component_slice(
      uint32_t chunk_byte_offset
  ) noexcept {
    return reinterpret_cast<uint8_t *>(this) + chunk_byte_offset;
  }

  [[nodiscard]] inline const void *component_slice(
      uint32_t chunk_byte_offset
  ) const noexcept {
    return reinterpret_cast<const uint8_t *>(this) + chunk_byte_offset;
  }

  [[nodiscard]] inline void *component_element(
      const ComponentColumnLayout &layout,
      uint32_t row
  ) noexcept {
    return reinterpret_cast<uint8_t *>(this) + layout.chunk_byte_offset +
        (row * layout.size);
  }

  [[nodiscard]] inline const void *component_element(
      const ComponentColumnLayout &layout,
      uint32_t row
  ) const noexcept {
    return reinterpret_cast<const uint8_t *>(this) + layout.chunk_byte_offset +
        (row * layout.size);
  }

  [[nodiscard]] inline Entity entity(uint32_t row) const noexcept {
    assert(row < header_.count);
    return entity_slice()[row];
  }

  inline uint32_t append_entity(Entity entity) noexcept {
    assert(header_.count < header_.capacity);
    const uint32_t row = header_.count++;
    entity_slice()[row] = entity;
    return row;
  }

  inline uint32_t append_entities(std::span<const Entity> entities) noexcept {
    assert(header_.count + entities.size() <= header_.capacity);
    const uint32_t start_row = header_.count;
    std::memcpy(
        entity_slice() + start_row,
        entities.data(),
        entities.size() * sizeof(Entity)
    );
    header_.count += static_cast<uint32_t>(entities.size());
    return start_row;
  }

  [[nodiscard]] ChunkRelocation swap_and_pop(
      uint32_t row,
      std::span<const ComponentColumnLayout> layouts
  );

  uint32_t batch_swap_and_pop(
      std::span<const uint32_t> rows_to_delete,
      std::span<const ComponentColumnLayout> layouts,
      std::span<EntityRelocation> out_relocations
  );
};

} // namespace arche
