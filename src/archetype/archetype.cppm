module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:archetype;

import :entity;
import :component_mask;
import :type_id;
import :hardware;
import :chunk;
import :chunk_pool;

export namespace arche {

class ArchetypeGraph;

class alignas(64) Archetype {
public:
  static constexpr size_t MAX_COMPONENTS = ComponentMask::MAX_COMPONENTS;
  static constexpr uint32_t SIMD_VECTOR_WIDTH = 16;
  static constexpr uint32_t SIMD_TAIL_PADDING = 16;

private:
  uint32_t archetype_id_{0};
  ComponentMask mask_;
  std::vector<TypeID> types_;
  std::vector<ComponentColumnLayout> columns_;
  std::array<int16_t, MAX_COMPONENTS> type_to_column_;
  uint32_t chunk_size_{65536};
  uint32_t chunk_capacity_{0};
  uint32_t total_entities_{0};
  std::vector<Chunk *> chunks_{};
  std::vector<Archetype *> edges_add_;
  std::vector<Archetype *> edges_remove_;

public:
  Archetype(
      uint32_t id,
      const ComponentMask &mask,
      std::vector<TypeID> sorted_types,
      const TypeRegistry &registry,
      uint32_t chunk_size = 65536
  )
      : archetype_id_(id), mask_(mask), types_(std::move(sorted_types)),
        chunk_size_(chunk_size) {
    type_to_column_.fill(-1);
    compute_layout(registry);
  }

  ~Archetype() = default;

  Archetype(const Archetype &) = delete;
  Archetype &operator=(const Archetype &) = delete;
  Archetype(Archetype &&) = delete;
  Archetype &operator=(Archetype &&) = delete;

  [[nodiscard]] inline bool matches(
      const ComponentMask &include,
      const ComponentMask &exclude
  ) const noexcept {
    return mask_.contains_all(include) &&
        (exclude.empty() || !mask_.contains_any(exclude));
  }

  [[nodiscard]] uint32_t id() const noexcept { return archetype_id_; }
  [[nodiscard]] const ComponentMask &mask() const noexcept { return mask_; }
  [[nodiscard]] std::span<const TypeID> types() const noexcept {
    return types_;
  }
  [[nodiscard]] size_t component_count() const noexcept {
    return types_.size();
  }
  [[nodiscard]] uint32_t chunk_capacity() const noexcept {
    return chunk_capacity_;
  }
  [[nodiscard]] uint32_t entity_count() const noexcept {
    return total_entities_;
  }
  [[nodiscard]] uint32_t chunk_size() const noexcept { return chunk_size_; }

  [[nodiscard]] inline bool has_component(TypeID type) const noexcept {
    return mask_.test(type);
  }

  [[nodiscard]] int16_t column_index(TypeID type) const noexcept {
    if (type >= MAX_COMPONENTS)
      return -1;
    return type_to_column_[type];
  }

  [[nodiscard]] const ComponentColumnLayout &column_layout(
      size_t index
  ) const noexcept {
    assert(index < columns_.size());
    return columns_[index];
  }

  [[nodiscard]] const ComponentColumnLayout *column_layout_for(
      TypeID type
  ) const noexcept {
    const int16_t idx = column_index(type);
    if (idx < 0)
      return nullptr;
    return &columns_[static_cast<size_t>(idx)];
  }

  [[nodiscard]] std::span<const ComponentColumnLayout>
  column_layouts() const noexcept {
    return columns_;
  }

  template <typename T>
  [[nodiscard]] inline uint32_t column_offset() const noexcept {
    const auto *layout = column_layout_for(type_id<std::decay_t<T>>());
    return layout != nullptr ? layout->chunk_byte_offset : UINT32_MAX;
  }

  template <typename T> [[nodiscard]] inline bool has() const noexcept {
    return has_component(type_id<std::decay_t<T>>());
  }

  [[nodiscard]] Archetype *get_add_edge(TypeID type) const noexcept {
    if (type >= edges_add_.size())
      return nullptr;
    return edges_add_[type];
  }

  [[nodiscard]] Archetype *get_remove_edge(TypeID type) const noexcept {
    if (type >= edges_remove_.size())
      return nullptr;
    return edges_remove_[type];
  }

  void set_add_edge(TypeID type, Archetype *target) {
    if (type >= edges_add_.size()) {
      edges_add_.resize(
          std::max<size_t>(type + 1, edges_add_.size() * 2), nullptr
      );
    }
    edges_add_[type] = target;
  }

  void set_remove_edge(TypeID type, Archetype *target) {
    if (type >= edges_remove_.size()) {
      edges_remove_.resize(
          std::max<size_t>(type + 1, edges_remove_.size() * 2), nullptr
      );
    }
    edges_remove_[type] = target;
  }

  struct ChunkCoord {
    uint32_t chunk_index{0};
    uint32_t local_row{0};
  };

  [[nodiscard]] inline ChunkCoord decompose_index(
      uint32_t global_index
  ) const noexcept {
    return {global_index / chunk_capacity_, global_index % chunk_capacity_};
  }

  [[nodiscard]] inline std::span<Chunk *const> chunks() const noexcept {
    return chunks_;
  }
  [[nodiscard]] inline std::vector<Chunk *> &chunks() noexcept {
    return chunks_;
  }

  inline Chunk *ensure_writable_chunk(ChunkPool &pool) {
    if (chunks_.empty() || chunks_.back()->is_full()) {
      void *raw = pool.acquire();
      auto *chunk = Chunk::create(raw, chunk_capacity_, chunk_size_);
      chunk->set_chunk_index(static_cast<uint32_t>(chunks_.size()));
      chunks_.push_back(chunk);
    }
    return chunks_.back();
  }

  inline void recycle_chunk(ChunkPool &pool, Chunk *chunk) {
    assert(chunk != nullptr);
    auto it = std::find(chunks_.begin(), chunks_.end(), chunk);
    if (it != chunks_.end()) {
      pool.release(chunk);
      chunks_.erase(it);
      for (size_t i = 0; i < chunks_.size(); ++i) {
        chunks_[i]->set_chunk_index(static_cast<uint32_t>(i));
      }
    }
  }

  inline std::pair<Chunk *, uint32_t> allocate_slot(
      ChunkPool &pool,
      Entity entity
  ) {
    Chunk *active = ensure_writable_chunk(pool);
    const uint32_t row = active->append_entity(entity);
    return {active, row};
  }

  inline void clear_chunks(ChunkPool &pool) {
    for (Chunk *c : chunks_) {
      pool.release(c);
    }
    chunks_.clear();
  }

  void increment_entity_count(size_t n = 1) noexcept { total_entities_ += n; }
  void decrement_entity_count(size_t n = 1) noexcept {
    assert(total_entities_ > 0);
    total_entities_ -= n;
  }
  void set_entity_count(size_t count) noexcept {
    total_entities_ = static_cast<uint32_t>(count);
  }

private:
  [[nodiscard]] static constexpr uint32_t _align_up(
      uint32_t offset,
      uint32_t alignment
  ) noexcept {
    if (alignment <= 1)
      return offset;
    return arche::math::align_up(offset, alignment);
  }

  [[nodiscard]] inline uint32_t _calculate_entity_size_sum(
      const TypeRegistry &registry
  ) const {
    uint32_t total = static_cast<uint32_t>(sizeof(Entity));
    for (TypeID t : types_) {
      const auto &desc = registry.get_descriptor(t);
      total += desc.size;
    }
    return total;
  }

  [[nodiscard]] inline uint32_t _calculate_chunk_capacity(
      uint32_t entity_size_sum
  ) const noexcept {
    constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
    constexpr uint32_t MAX_ALIGN_PADDING_ALLOWANCE = 256;

    const uint32_t tail_padding_bytes = static_cast<uint32_t>(
        (sizeof(Entity) + entity_size_sum) * SIMD_TAIL_PADDING
    );

    const uint32_t total_overhead = CHUNK_HEADER_RESERVE +
        MAX_ALIGN_PADDING_ALLOWANCE + tail_padding_bytes;

    const uint32_t available_bytes = (chunk_size_ > total_overhead)
        ? (chunk_size_ - total_overhead)
        : 1;

    const uint32_t raw_capacity = available_bytes / entity_size_sum;
    const uint32_t aligned_capacity = (raw_capacity >= SIMD_VECTOR_WIDTH)
        ? arche::math::round_down_simd<SIMD_VECTOR_WIDTH>(raw_capacity)
        : raw_capacity;

    return (aligned_capacity == 0) ? 1 : aligned_capacity;
  }

  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;

    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
        static_cast<uint32_t>(sizeof(Entity));

    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.descriptor = &desc;

      const uint32_t column_span = (chunk_capacity_ + SIMD_TAIL_PADDING) *
          desc.size;
      current_offset += column_span;

      columns_.push_back(layout);
      type_to_column_[t] = static_cast<int16_t>(i);
    }

    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }

  void compute_layout(const TypeRegistry &registry) {
    columns_.clear();
    columns_.reserve(types_.size());

    if (types_.empty()) {
      chunk_capacity_ = 65536;
      return;
    }

    const uint32_t entity_size_sum = _calculate_entity_size_sum(registry);
    chunk_capacity_ = _calculate_chunk_capacity(entity_size_sum);
    _build_column_layouts(registry);
  }
};

} // namespace arche
