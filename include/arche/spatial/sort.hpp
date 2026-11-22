#pragma once

#include "arche/archetype/archetype.hpp"
#include "arche/chunk/chunk.hpp"
#include "arche/component/component_mask.hpp"
#include "arche/query/filter.hpp"
#include "arche/query/query_cache.hpp"
#include "arche/world.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace arche {

void permute(World &world, Archetype *arch, std::span<const uint32_t> target_indices);

template <typename... Components>
inline void permute(World &world, std::span<const uint32_t> target_indices) {
  (internal::WorldAccess::registry(world).register_type<std::decay_t<Components>>(), ...);
  std::vector<TypeID> types = {type_id<std::decay_t<Components>>()...};
  Archetype *arch = internal::WorldAccess::graph(world).get_or_create(types);
  permute(world, arch, target_indices);
}

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
inline void sort_archetype(World &world, Archetype *arch, Compare &&comp) {
  const size_t total = arch->entity_count();
  if (total <= 1) {
    return;
  }

  const int16_t col = arch->column_index(type_id<Component>());
  if (col < 0) {
    return;
  }

  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  const auto chunks = world.archetypes.chunks(arch);

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

  permute(world, arch, permutation);
}

template <typename Component, typename KeyExtractor>
inline void sort_archetype_by_key(
    World &world, Archetype *arch, KeyExtractor &&key_fn
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
  const auto chunks = world.archetypes.chunks(arch);

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

  permute(world, arch, permutation);
}

template <typename Component, typename Compare>
inline void sort(World &world, Archetype *arch, Compare &&comp) {
  sort_archetype<Component>(world, arch, std::forward<Compare>(comp));
}

template <typename Component, typename... Filters, typename Compare>
inline void sort(World &world, Compare &&comp) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;

  include_mask.set(type_id<Component>());

  if constexpr (sizeof...(Filters) > 0) {
    ([&]<typename Filter>() {
      if constexpr (requires { typename Filter::Include; }) {
        with_unpacker<Filter>::apply(include_mask, internal::WorldAccess::registry(world));
      } else if constexpr (requires { typename Filter::Exclude; }) {
        without_unpacker<Filter>::apply(exclude_mask, internal::WorldAccess::registry(world));
      }
    }.template operator()<Filters>(), ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(world, include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    sort_archetype<Component>(world, arch, comp);
  }
}

template <typename Component, typename... Filters, typename KeyExtractor>
inline void sort_by_key(World &world, KeyExtractor &&key_fn) {
  ComponentMask include_mask;
  ComponentMask exclude_mask;

  include_mask.set(type_id<Component>());

  if constexpr (sizeof...(Filters) > 0) {
    ([&]<typename Filter>() {
      if constexpr (requires { typename Filter::Include; }) {
        with_unpacker<Filter>::apply(include_mask, internal::WorldAccess::registry(world));
      } else if constexpr (requires { typename Filter::Exclude; }) {
        without_unpacker<Filter>::apply(exclude_mask, internal::WorldAccess::registry(world));
      }
    }.template operator()<Filters>(), ...);
  }

  QueryCache *cache = internal::WorldAccess::query_cache(world, include_mask, exclude_mask);
  for (Archetype *arch : cache->matching_archetypes) {
    sort_archetype_by_key<Component>(world, arch, key_fn);
  }
}

} // namespace arche
