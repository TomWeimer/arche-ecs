module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

export module arche:query.detail.offsets;

import :entity;
import :archetype;
import :chunk;
import :type_id;
import :hardware;
import :filters;

export namespace arche::detail::query {

// =============================================================================
// Column Offset Resolution
// =============================================================================

/// Resolves the byte offset of a queried component column within an archetype
/// chunk layout.
template <typename Comp>
inline uint32_t resolve_column_offset(Archetype *arch) {
  using DecayComp = std::decay_t<Comp>;

  // 1. Entity handle: entities are always stored contiguously starting at byte
  // offset 0 of the chunk payload.
  if constexpr (std::is_same_v<DecayComp, Entity>) {
    return 0;
  }
  // 2. Has<T> presence flag: encodes boolean archetype membership
  // (1 = present, 0 = absent) without reading column memory.
  else if constexpr (is_has_v<DecayComp>) {
    using Inner = has_inner_t<DecayComp>;
    return arch->has_component(type_id<std::decay_t<Inner>>()) ? 1 : 0;
  }
  // 3. Optional component (T*, Opt<T>, OptMut<T>): returns byte offset if
  // present in archetype, or UINT32_MAX sentinel if absent.
  else if constexpr (std::is_pointer_v<DecayComp>) {
    using Raw = std::remove_pointer_t<DecayComp>;
    const auto *layout = arch->column_layout_for(type_id<std::decay_t<Raw>>());
    return layout ? layout->chunk_byte_offset : UINT32_MAX;
  }
  // 4. Required component (T, const T): guaranteed present by query matching;
  // returns column byte offset.
  else {
    const auto *layout = arch->column_layout_for(type_id<DecayComp>());
    assert(
        layout != nullptr &&
        "Queried component missing from matched archetype layout!"
    );
    return layout->chunk_byte_offset;
  }
}

// =============================================================================
// Typed Column Pointer Unpacking
// =============================================================================

/// Unpacks a typed component pointer from chunk memory using a precalculated
/// byte offset.
template <typename Comp>
inline auto get_column_ptr(Chunk *chunk, uint32_t offset) {
  using DecayComp = std::decay_t<Comp>;

  // 1. Entity handle: returns pointer to the chunk's contiguous entity array
  // (Entity*).
  if constexpr (std::is_same_v<DecayComp, Entity>) {
    return chunk->entity_slice();
  }
  // 2. Has<T> presence flag: converts offset (1 = present, 0 = absent) to a
  // boolean flag.
  else if constexpr (is_has_v<DecayComp>) {
    return offset != 0;
  }
  // 3. Optional component (T*, Opt<T>, OptMut<T>): returns nullptr if absent
  // (UINT32_MAX sentinel), or typed pointer if present.
  else if constexpr (std::is_pointer_v<DecayComp>) {
    if (offset == UINT32_MAX) {
      return static_cast<Comp>(nullptr);
    }
    return reinterpret_cast<Comp>(chunk->component_slice(offset));
  }
  // 4. Required component (T, const T): returns direct typed pointer (T* or
  // const T*) to the column memory.
  else {
    return reinterpret_cast<Comp *>(chunk->component_slice(offset));
  }
}

/// Unpacks a tuple of typed column pointers for a chunk and archetype.
template <typename... Components>
inline auto get_column_pointers(Chunk *chunk, Archetype *arch) {
  return std::make_tuple(
      get_column_ptr<Components>(
          chunk, resolve_column_offset<Components>(arch)
      )...
  );
}

// =============================================================================
// Hardware Cache Line Prefetching
// =============================================================================

/// Issues CPU hardware prefetch instructions for the next chunk header and
/// active column lines.
template <typename Tuple>
inline void prefetch_next_chunk_with_offsets(
    const Chunk *next_chunk,
    const Tuple &offsets
) noexcept {
  if (next_chunk == nullptr) {
    return;
  }
  const auto *base = reinterpret_cast<const uint8_t *>(next_chunk);
  // 1. Prefetch the first two cache lines (128 bytes) of the next chunk header
  Hardware::prefetch_read(base);
  Hardware::prefetch_read(base + 64);

  // 2. Unpack column offsets and prefetch the first cache line of each active
  // component column
  std::apply(
      [&](auto... offs) {
        auto prefetch_col = [&](uint32_t off) {
          // Skip missing optional components (UINT32_MAX) and entity handle
          // column (offset 0)
          if (off != UINT32_MAX && off != 0) {
            Hardware::prefetch_read(base + off);
          }
        };
        (prefetch_col(offs), ...);
      },
      offsets
  );
}

} // namespace arche::detail::query
