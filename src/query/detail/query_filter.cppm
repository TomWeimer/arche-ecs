module;

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

export module arche:query.detail.filter;

import :type_id;
import :filters;
import :chunk;
import :archetype;

export namespace arche::detail::query {

// =============================================================================
// Temporal Change Detection Engine State
// =============================================================================

/// Individual rule checking a component's column dirty version on chunks.
struct MutationFilterRule {
  TypeID type_id{INVALID_TYPE_ID};
  uint32_t since_version{0};
};

/**
 * @struct MutationFilterList
 * @brief Fixed-capacity inline buffer of active `Mutated<T>` rules.
 *
 * Stores up to 2 component mutation filter rules directly on the stack to
 * ensure queries remain 100% zero-allocation (avoiding heap allocations from
 * `std::vector`).
 */
struct MutationFilterList {
  /// Maximum simultaneous Mutated<T> filters supported on a single Query.
  static constexpr size_t CAPACITY = 2;

  /// Inline storage buffer for active mutation rules.
  std::array<MutationFilterRule, CAPACITY> rules{};

  /// Number of active mutation rules stored (0, 1, or 2).
  uint8_t count{0};

  /// Appends a new mutation rule; asserts that capacity is not exceeded.
  constexpr void add(TypeID id, uint32_t ver) noexcept {
    assert(count < CAPACITY && "Query supports at most 2 Mutated<T> filters");
    rules[count++] = MutationFilterRule{id, ver};
  }

  /// Returns the number of active rules.
  [[nodiscard]] constexpr size_t size() const noexcept { return count; }

  /// Returns true if no mutation filters are active.
  [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }

  /// Direct pointer to the underlying contiguous rule buffer.
  [[nodiscard]] constexpr const MutationFilterRule *data() const noexcept {
    return rules.data();
  }

  /// Zero-copy span over the active rules.
  [[nodiscard]] constexpr std::span<const MutationFilterRule>
  span() const noexcept {
    return std::span<const MutationFilterRule>(rules.data(), count);
  }

  /// Unchecked access to a mutation rule by index.
  [[nodiscard]] constexpr const MutationFilterRule &operator[](
      size_t idx
  ) const noexcept {
    assert(idx < count);
    return rules[idx];
  }
};

// =============================================================================
// Temporal Filter Extraction (Query Construction)
// =============================================================================

/// Applies Mutated<T> filter tracking the component dirty version threshold.
template <typename T>
inline void apply_temporal_filter(
    MutationFilterList &mutation_filters,
    uint32_t &,
    bool &,
    Mutated<T> f,
    uint32_t default_since_version
) {
  // If no specific value was given for the version in Mutated<T> the version of
  // the last frame is used.
  const uint32_t ver = (f.since_version == SINCE_LAST_FRAME)
      ? default_since_version
      : f.since_version;
  mutation_filters.add(type_id<std::decay_t<T>>(), ver);
}

/// Applies Added filter tracking structural entity creation version threshold.
inline void apply_temporal_filter(
    MutationFilterList &,
    uint32_t &added_since_version,
    bool &has_added_filter,
    Added f,
    uint32_t default_since_version
) {
  has_added_filter = true;
  // If no specific value was given for the version in Added<T> the version of
  // the last frame is used.
  added_since_version = (f.since_version == SINCE_LAST_FRAME)
      ? default_since_version
      : f.since_version;
}

/// Structural filters (With, Without, Or) do not affect temporal change
/// detection.
template <typename Filter>
inline void apply_temporal_filter(
    MutationFilterList &,
    uint32_t &,
    bool &,
    Filter,
    uint32_t
) {}

// =============================================================================
// Chunk Version Verification (Modular Sequence Distance)
// =============================================================================

/// Evaluates whether version `a` is strictly newer than version `b` using
/// modular sequence distance.
///
/// Versions cycle in [1, UINT32_MAX - 1] (0 and UINT32_MAX are reserved
/// sentinels). Modular distance correctly detects newer ticks across overflow
/// as long as a chunk has not remained dormant for more than 2^31 ticks (~1.14
/// years at 60 FPS).
[[nodiscard]] inline constexpr bool is_newer_version(
    uint32_t a,
    uint32_t b
) noexcept {
  return static_cast<int32_t>(a - b) > 0;
}

/// Evaluates chunk header version numbers against temporal filters (Added,
/// Mutated).
inline bool chunk_matches_filters(
    const Chunk *chunk,
    Archetype *arch,
    const MutationFilterList &mutation_filters,
    uint32_t added_since_version,
    bool has_added_filter
) noexcept {
  // 1. If Added filter is active, skip chunk if no entities were spawned or
  // moved into it.
  if (has_added_filter &&
      !is_newer_version(chunk->structure_version(), added_since_version)) {
    // Early-out on failure: we cannot return true on match because the query
    // may also require Mutated<T> filters below (logical AND).
    return false;
  }

  // 2. Check each Mutated<T> filter (up to 2 inline rules). All must be
  // satisfied.
  for (size_t i = 0; i < mutation_filters.size(); ++i) {
    // 3. Locate the component's column in this archetype.
    const auto &rule = mutation_filters[i];
    const int16_t col = arch->column_index(rule.type_id);

    // 4. Skip chunk if this component column was not modified since the
    // requested version.
    if (col >= 0 &&
        !is_newer_version(
            chunk->column_version(static_cast<uint16_t>(col)),
            rule.since_version
        )) {
      return false;
    }
  }

  // 5. All active filters passed: chunk contains updated data.
  return true;
}

} // namespace arche::detail::query
