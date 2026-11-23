#pragma once

#include "archetype/archetype.hpp"
#include "chunk/chunk.hpp"
#include "component/component_mask.hpp"
#include "query/filter.hpp"
#include "spatial/permute.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace arche {

namespace detail {

template <typename KeyIndex>
inline void radix_sort_key_indices(std::vector<KeyIndex> &elements) {
  const size_t n = elements.size();
  if (n <= 1) {
    return;
  }

  using KeyType = decltype(elements[0].key);
  constexpr size_t PASSES = sizeof(KeyType);
  constexpr size_t BUCKETS = 256;

  std::vector<KeyIndex> temp(n);
  KeyIndex *src = elements.data();
  KeyIndex *dst = temp.data();

  for (size_t pass = 0; pass < PASSES; ++pass) {
    size_t count[BUCKETS] = {0};
    const size_t shift = pass * 8;

    for (size_t i = 0; i < n; ++i) {
      uint8_t byte = static_cast<uint8_t>((src[i].key >> shift) & 0xFF);
      count[byte]++;
    }

    size_t offset[BUCKETS] = {0};
    for (size_t i = 1; i < BUCKETS; ++i) {
      offset[i] = offset[i - 1] + count[i - 1];
    }

    for (size_t i = 0; i < n; ++i) {
      uint8_t byte = static_cast<uint8_t>((src[i].key >> shift) & 0xFF);
      dst[offset[byte]++] = src[i];
    }

    std::swap(src, dst);
  }

  if (src != elements.data()) {
    std::copy(src, src + n, elements.data());
  }
}

} // namespace detail

template <typename Component, typename Compare>
inline void sort_archetype(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    Compare &&comp
) {
  const size_t total = arch->entity_count();
  if (total <= 1) {
    return;
  }

  const int16_t col = arch->column_index(type_id<Component>());
  if (col < 0) {
    return;
  }

  const auto &layout = arch->column_layout(static_cast<size_t>(col));

  struct ElementRef {
    const Component *ptr;
    uint32_t index;
  };

  std::vector<ElementRef> refs;
  refs.reserve(total);

  uint32_t global_idx = 0;
  for (Chunk *chunk : chunks) {
    const uint32_t count = chunk->count();
    const Component *comp_ptr = reinterpret_cast<const Component *>(
        chunk->component_element(layout, 0)
    );
    for (uint32_t r = 0; r < count; ++r) {
      refs.push_back({&comp_ptr[r], global_idx++});
    }
  }

  std::stable_sort(
      refs.begin(), refs.end(),
      [&](const ElementRef &a, const ElementRef &b) {
        return comp(*a.ptr, *b.ptr);
      }
  );

  std::vector<uint32_t> permutation(total);
  for (size_t i = 0; i < total; ++i) {
    permutation[i] = refs[i].index;
  }

  permute(arch, chunks, slot_map, version, permutation);
}

template <typename Component, typename KeyExtractor>
inline void sort_archetype_by_key(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    KeyExtractor &&key_fn
) {
  const size_t total = arch->entity_count();
  if (total <= 1) {
    return;
  }

  const int16_t col = arch->column_index(type_id<Component>());
  if (col < 0) {
    return;
  }

  const auto &layout = arch->column_layout(static_cast<size_t>(col));

  using KeyType = std::decay_t<decltype(key_fn(std::declval<const Component &>()))>;

  struct KeyIndexPair {
    KeyType key;
    uint32_t index;
  };

  std::vector<KeyIndexPair> elements;
  elements.reserve(total);

  uint32_t global_idx = 0;
  for (Chunk *chunk : chunks) {
    const uint32_t count = chunk->count();
    const Component *comp_ptr = reinterpret_cast<const Component *>(
        chunk->component_element(layout, 0)
    );
    for (uint32_t r = 0; r < count; ++r) {
      elements.push_back({key_fn(comp_ptr[r]), global_idx++});
    }
  }

  if constexpr (std::is_integral_v<KeyType> && sizeof(KeyType) <= 8) {
    detail::radix_sort_key_indices(elements);
  } else {
    std::stable_sort(
        elements.begin(), elements.end(),
        [](const KeyIndexPair &a, const KeyIndexPair &b) {
          return a.key < b.key;
        }
    );
  }

  std::vector<uint32_t> permutation(total);
  for (size_t i = 0; i < total; ++i) {
    permutation[i] = elements[i].index;
  }

  permute(arch, chunks, slot_map, version, permutation);
}

} // namespace arche
