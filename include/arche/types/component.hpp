#pragma once

#include <bit>
#include <compare> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace arche {

// =============================================================================
// Component ID Definitions
// =============================================================================

/// Dense 32-bit integer identifier for component types.
using ComponentID = uint32_t;

/// Sentinel value indicating an invalid or unallocated ComponentID / TypeID.
inline constexpr ComponentID INVALID_TYPE_ID = 0xFFFFFFFF;

/// Alias for ComponentID used interchangeably across type registries and
/// storage.
using TypeID = ComponentID;

// =============================================================================
// ComponentMask: 128-Bit SIMD Presence Bitset
// =============================================================================

/**
 * @struct ComponentMask
 * @brief 128-bit flat bitset identifying component composition in Arche-ECS.
 *
 * Represents component presence across two contiguous 64-bit words (16 bytes).
 * Supports up to 128 distinct component types with bitwise parallel operations,
 * subset/disjoint testing, and hardware population counting (`std::popcount`).
 *
 * Satisfies standard-layout and trivially copyable POD constraints.
 *
 * ```cpp
 * // Example:
 * ComponentMask mask;
 * mask.set(type_id<Position>());
 * mask.set(type_id<Velocity>());
 * if (mask.test(type_id<Position>())) {
 *     // Component is present
 * }
 * ```
 */
struct ComponentMask {
public:
  static constexpr size_t MAX_COMPONENTS = 128;
  static constexpr size_t WORDS_COUNT = 2;

private:
  uint64_t words[WORDS_COUNT]; ///< Two 64-bit integer words (128 bits total).

public:
  /// Constructs an empty mask (all bits cleared).
  constexpr ComponentMask() noexcept : words{0, 0} {}

  // --- Bit Modification ------------------------------------------------------

  /// Sets the bit corresponding to the specified ComponentID.
  constexpr void set(ComponentID id) noexcept {
    if (id < 64) {
      words[0] |= (1ULL << id);
    } else if (id < 128) {
      words[1] |= (1ULL << (id - 64));
    }
  }

  /// Clears the bit corresponding to the specified ComponentID.
  constexpr void reset(ComponentID id) noexcept {
    if (id < 64) {
      words[0] &= ~(1ULL << id);
    } else if (id < 128) {
      words[1] &= ~(1ULL << (id - 64));
    }
  }

  /// Resets all bits to 0.
  constexpr void clear() noexcept {
    words[0] = 0;
    words[1] = 0;
  }

  // --- Bit Queries -----------------------------------------------------------

  /// Tests whether the bit for ComponentID is set.
  [[nodiscard]] constexpr bool test(ComponentID id) const noexcept {
    if (id < 64) {
      return (words[0] & (1ULL << id)) != 0;
    } else if (id < 128) {
      return (words[1] & (1ULL << (id - 64))) != 0;
    }
    return false;
  }

  /// Checks if this mask contains all bits set in the query mask (subset test:
  /// (this & query) == query).
  [[nodiscard]] constexpr bool contains_all(
      const ComponentMask &query
  ) const noexcept {
    return ((words[0] & query.words[0]) == query.words[0]) &&
        ((words[1] & query.words[1]) == query.words[1]);
  }

  /// Checks if this mask shares zero bits with the excluded mask (disjoint
  /// test: (this & excluded) == 0).
  [[nodiscard]] constexpr bool contains_none(
      const ComponentMask &excluded
  ) const noexcept {
    return ((words[0] & excluded.words[0]) == 0) &&
        ((words[1] & excluded.words[1]) == 0);
  }

  /// Checks if this mask shares at least one bit with the filter mask ((this &
  /// filter) != 0).
  [[nodiscard]] constexpr bool contains_any(
      const ComponentMask &filter
  ) const noexcept {
    return ((words[0] & filter.words[0]) != 0) ||
        ((words[1] & filter.words[1]) != 0);
  }

  /// Returns the total number of set bits using hardware population count.
  [[nodiscard]] constexpr size_t count() const noexcept {
    return static_cast<size_t>(
        std::popcount(words[0]) + std::popcount(words[1])
    );
  }

  /// Checks if the mask contains zero set bits.
  [[nodiscard]] constexpr bool empty() const noexcept {
    return words[0] == 0 && words[1] == 0;
  }

  /// Accesses a 64-bit integer word of the mask by index (0 or 1).
  [[nodiscard]] constexpr uint64_t word(size_t index) const noexcept {
    return words[index];
  }

  // --- Bitwise Operators -----------------------------------------------------

  constexpr ComponentMask operator&(const ComponentMask &other) const noexcept {
    ComponentMask result;
    result.words[0] = words[0] & other.words[0];
    result.words[1] = words[1] & other.words[1];
    return result;
  }

  constexpr ComponentMask operator|(const ComponentMask &other) const noexcept {
    ComponentMask result;
    result.words[0] = words[0] | other.words[0];
    result.words[1] = words[1] | other.words[1];
    return result;
  }

  constexpr ComponentMask operator^(const ComponentMask &other) const noexcept {
    ComponentMask result;
    result.words[0] = words[0] ^ other.words[0];
    result.words[1] = words[1] ^ other.words[1];
    return result;
  }

  constexpr ComponentMask operator~() const noexcept {
    ComponentMask result;
    result.words[0] = ~words[0];
    result.words[1] = ~words[1];
    return result;
  }

  // --- Comparison Operators --------------------------------------------------

  constexpr auto operator<=>(const ComponentMask &other) const noexcept =
      default;
  constexpr bool operator==(const ComponentMask &other) const noexcept =
      default;
};

// =============================================================================
// POD / Standard-Layout Invariant Verification
// =============================================================================

static_assert(
    sizeof(ComponentMask) == 16,
    "ComponentMask must be exactly 16 bytes (128-bit SIMD register footprint)"
);
static_assert(
    std::is_standard_layout_v<ComponentMask>,
    "ComponentMask must satisfy standard-layout POD constraints"
);
static_assert(
    std::is_trivially_copyable_v<ComponentMask>,
    "ComponentMask must be trivially copyable for zero-cost register passing"
);
static_assert(
    std::is_trivially_destructible_v<ComponentMask>,
    "ComponentMask must have a trivial destructor"
);

// =============================================================================
// Hash Support for ComponentMask
// =============================================================================

/**
 * @struct ComponentMaskHash
 * @brief High-speed hash functor for 128-bit ComponentMask.
 *
 * Combines both 64-bit integer words using the golden ratio combiner.
 */
struct ComponentMaskHash {
  [[nodiscard]] size_t operator()(const ComponentMask &mask) const noexcept {
    const size_t h1 = std::hash<uint64_t>{}(mask.word(0));
    const size_t h2 = std::hash<uint64_t>{}(mask.word(1));
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

} // namespace arche

// Standard Hash Specialization for std::unordered_map / std::unordered_set
template <> struct std::hash<arche::ComponentMask> {
  [[nodiscard]] size_t operator()(
      const arche::ComponentMask &mask
  ) const noexcept {
    return arche::ComponentMaskHash{}(mask);
  }
};
