module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:archetype.detail.impl;

import :archetype;
import :hardware;

export namespace arche::internal {

// =============================================================================
// Archetype Construction & Layout Computation
// =============================================================================

Archetype::Archetype(
    uint32_t id,
    const ComponentMask &mask,
    std::vector<ComponentID> sorted_types,
    const ComponentRegistry &registry,
    uint32_t chunk_size
)
    : archetype_id_(id), mask_(mask), types_(std::move(sorted_types)),
      chunk_size_(chunk_size) {
  // 1. Initialize component-to-column index mapping table to invalid (-1).
  type_to_column_.fill(-1);

  // 2. Compute SIMD-aligned column offsets, tail padding, and chunk capacity.
  compute_layout(registry);
}

// =============================================================================
// Filtering & Matching Implementations
// =============================================================================

bool Archetype::matches(
    const ComponentMask &include,
    const ComponentMask &exclude,
    const ComponentMask &any
) const noexcept {
  // 1. All include components must be present in this archetype.
  // 2. No exclude components may be present in this archetype.
  // 3. At least one 'any' component must be present (if any mask is non-empty).
  return mask_.contains_all(include) &&
      (exclude.empty() || !mask_.contains_any(exclude)) &&
      (any.empty() || mask_.contains_any(any));
}

// =============================================================================
// Property & Metadata Introspection Implementations
// =============================================================================

uint32_t Archetype::id() const noexcept { return archetype_id_; }
const ComponentMask &Archetype::mask() const noexcept { return mask_; }
std::span<const ComponentID> Archetype::types() const noexcept { return types_; }
size_t Archetype::component_count() const noexcept { return types_.size(); }
uint32_t Archetype::chunk_capacity() const noexcept { return chunk_capacity_; }
uint32_t Archetype::entity_count() const noexcept { return total_entities_; }
uint32_t Archetype::chunk_size() const noexcept { return chunk_size_; }

bool Archetype::has_component(ComponentID type) const noexcept {
  return mask_.test(type);
}

template <typename T> inline bool Archetype::has() const noexcept {
  return has_component(component_id<std::decay_t<T>>());
}

// =============================================================================
// Column Layout Lookups Implementations
// =============================================================================

int16_t Archetype::column_index(ComponentID type) const noexcept {
  if (type >= MAX_COMPONENTS) {
    return -1;
  }
  return type_to_column_[type];
}

const ComponentColumnLayout &Archetype::column_layout(
    size_t index
) const noexcept {
  assert(index < columns_.size());
  return columns_[index];
}

const ComponentColumnLayout *Archetype::column_layout_for(
    ComponentID type
) const noexcept {
  const int16_t idx = column_index(type);
  if (idx < 0) {
    return nullptr;
  }
  return &columns_[static_cast<size_t>(idx)];
}

std::span<const ComponentColumnLayout>
Archetype::column_layouts() const noexcept {
  return columns_;
}

template <typename T>
inline uint32_t Archetype::column_offset() const noexcept {
  const auto *layout = column_layout_for(component_id<std::decay_t<T>>());
  return layout != nullptr ? layout->chunk_byte_offset : UINT32_MAX;
}

// =============================================================================
// Transition Graph Edges Implementations
// =============================================================================

Archetype *Archetype::get_add_edge(ComponentID type) const noexcept {
  if (type >= edges_add_.size()) {
    return nullptr;
  }
  return edges_add_[type];
}

Archetype *Archetype::get_remove_edge(ComponentID type) const noexcept {
  if (type >= edges_remove_.size()) {
    return nullptr;
  }
  return edges_remove_[type];
}

void Archetype::set_add_edge(ComponentID type, Archetype *target) {
  // 1. Dynamically resize sparse transition vector if needed.
  if (type >= edges_add_.size()) {
    edges_add_.resize(
        std::max<size_t>(type + 1, edges_add_.size() * 2), nullptr
    );
  }
  // 2. Cache direct pointer edge to destination archetype.
  edges_add_[type] = target;
}

void Archetype::set_remove_edge(ComponentID type, Archetype *target) {
  // 1. Dynamically resize sparse transition vector if needed.
  if (type >= edges_remove_.size()) {
    edges_remove_.resize(
        std::max<size_t>(type + 1, edges_remove_.size() * 2), nullptr
    );
  }
  // 2. Cache direct pointer edge to destination archetype.
  edges_remove_[type] = target;
}

// =============================================================================
// Chunk Page Management Implementations
// =============================================================================

Archetype::ChunkCoord Archetype::decompose_index(
    uint32_t global_index
) const noexcept {
  return {global_index / chunk_capacity_, global_index % chunk_capacity_};
}

std::span<Chunk *const> Archetype::chunks() const noexcept {
  return chunks_;
}

std::vector<Chunk *> &Archetype::chunks() noexcept {
  return chunks_;
}

Chunk *Archetype::ensure_writable_chunk(ChunkPool &pool) {
  // 1. If no chunks exist or the tail chunk is full, allocate a new page from pool.
  if (chunks_.empty() || chunks_.back()->is_full()) {
    void *raw = pool.acquire();
    auto *chunk = Chunk::create(raw, chunk_capacity_, chunk_size_);
    chunk->set_chunk_index(static_cast<uint32_t>(chunks_.size()));
    chunks_.push_back(chunk);
  }
  return chunks_.back();
}

void Archetype::recycle_chunk(ChunkPool &pool, Chunk *chunk) {
  // 1. Validate chunk pointer and locate in active chunk list.
  assert(chunk != nullptr);
  auto it = std::find(chunks_.begin(), chunks_.end(), chunk);
  if (it != chunks_.end()) {
    // 2. Return memory page to the pool and erase from active list.
    pool.release(chunk);
    chunks_.erase(it);

    // 3. Re-index remaining chunks to maintain contiguous 0-based indices.
    for (size_t i = 0; i < chunks_.size(); ++i) {
      chunks_[i]->set_chunk_index(static_cast<uint32_t>(i));
    }
  }
}

std::pair<Chunk *, uint32_t> Archetype::allocate_slot(
    ChunkPool &pool,
    Entity entity
) {
  // 1. Obtain writable chunk with available capacity.
  Chunk *active = ensure_writable_chunk(pool);

  // 2. Append entity handle into chunk entity column.
  const uint32_t row = active->append_entity(entity);
  return {active, row};
}

void Archetype::clear_chunks(ChunkPool &pool) {
  // 1. Release every allocated chunk page back to the slab pool.
  for (Chunk *c : chunks_) {
    pool.release(c);
  }
  chunks_.clear();
}

void Archetype::increment_entity_count(size_t n) noexcept {
  total_entities_ += static_cast<uint32_t>(n);
}

void Archetype::decrement_entity_count(size_t n) noexcept {
  assert(total_entities_ >= n);
  total_entities_ -= static_cast<uint32_t>(n);
}

void Archetype::set_entity_count(size_t count) noexcept {
  total_entities_ = static_cast<uint32_t>(count);
}

// =============================================================================
// Internal Layout Geometry Calculations
// =============================================================================

uint32_t Archetype::_calculate_entity_size_sum(
    const ComponentRegistry &registry
) const {
  // 1. Base entity size sum begins with the Entity handle column (8 bytes).
  uint32_t total = static_cast<uint32_t>(sizeof(Entity));

  // 2. Accumulate byte size of each registered component type.
  for (ComponentID t : types_) {
    const auto &desc = registry.get_descriptor(t);
    total += desc.size;
  }
  return total;
}

uint32_t Archetype::_calculate_chunk_capacity(
    uint32_t entity_size_sum
) const noexcept {
  // 1. Reserve fixed header and maximum SIMD alignment padding allowances.
  constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;
  constexpr uint32_t MAX_ALIGN_PADDING_ALLOWANCE = 256;

  // 2. Calculate tail padding allowance for SIMD over-read buffer safety.
  const uint32_t tail_padding_bytes = static_cast<uint32_t>(
      (sizeof(Entity) + entity_size_sum) * SIMD_TAIL_PADDING
  );

  const uint32_t total_overhead = CHUNK_HEADER_RESERVE +
      MAX_ALIGN_PADDING_ALLOWANCE + tail_padding_bytes;

  const uint32_t available_bytes = (chunk_size_ > total_overhead)
      ? (chunk_size_ - total_overhead)
      : 1;

  // 3. Compute raw capacity and round down to nearest SIMD vector width boundary.
  const uint32_t raw_capacity = available_bytes / entity_size_sum;
  const uint32_t aligned_capacity = (raw_capacity >= SIMD_VECTOR_WIDTH)
      ? arche::math::round_down_simd<SIMD_VECTOR_WIDTH>(raw_capacity)
      : raw_capacity;

  return (aligned_capacity == 0) ? 1 : aligned_capacity;
}

void Archetype::_build_column_layouts(const ComponentRegistry &registry) {
  constexpr uint32_t CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES;

  // 1. Lay out the Entity handle array directly past the ChunkHeader.
  uint32_t current_offset = CHUNK_HEADER_RESERVE;
  current_offset = arche::math::align_up(current_offset, alignof(Entity));
  current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
      static_cast<uint32_t>(sizeof(Entity));

  // 2. Sequentially place each component column aligned to its type requirements.
  for (size_t i = 0; i < types_.size(); ++i) {
    ComponentID t = types_[i];
    const auto &desc = registry.get_descriptor(t);

    current_offset = arche::math::align_up(current_offset, desc.alignment);

    ComponentColumnLayout layout;
    layout.component_id = t;
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

  // 3. Assert total chunk layout fits within the page boundary.
  assert(
      current_offset <= chunk_size_ &&
      "Chunk layout calculation exceeded chunk boundaries!"
  );
}

void Archetype::compute_layout(const ComponentRegistry &registry) {
  // 1. Reset column layout list.
  columns_.clear();
  columns_.reserve(types_.size());

  // 2. Handle empty archetype (root node: no components, capacity equals chunk size).
  if (types_.empty()) {
    chunk_capacity_ = chunk_size_;
    return;
  }

  // 3. Calculate packed entity size sum and determine SIMD-aligned chunk capacity.
  const uint32_t entity_size_sum = _calculate_entity_size_sum(registry);
  chunk_capacity_ = _calculate_chunk_capacity(entity_size_sum);

  // 4. Build column layout offsets.
  _build_column_layouts(registry);
}

} // namespace arche
