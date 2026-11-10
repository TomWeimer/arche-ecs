#pragma once

#include "arche/type_id.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace arche {

// --- Component Mask ----------------------------------------------------------

/**
 * @struct ComponentMask
 * @brief 128-bit fixed-width component signature bitmask for instant archetype
 * matching.
 *
 * @details
 * Architectural rationale:
 * - Supports up to 128 distinct component types (`MAX_COMPONENTS = 128`).
 * - Fits within two 64-bit integer registers (`uint64_t words[2]`), allowing
 *   bitwise operations (`contains_all`, `contains_none`) to execute in 1 to 2
 * CPU clock cycles.
 * - Auto-vectorizes into 128-bit SIMD registers (SSE/AVX or ARM NEON).
 * - Avoids dynamic memory allocation (`std::vector<bool>` or dynamic bitsets)
 *   during inner query filtering loops.
 *
 * ### Why a struct instead of a raw uint64_t[2]?
 * We define ComponentMask as a `struct` to:
 * 1. Attach convenient member functions (`contains_all()`, `test()`, `count()`)
 *    directly to the mask.
 * 2. Enable operator overloads (`&`, `|`, `^`, `~`, `==`, `<=>`), which C++
 *    forbids on raw C-arrays.
 * 3. Allow passing and returning 128-bit masks by value with zero overhead.
 *
 * The compiler optimizes this 16-byte struct to be 100% equivalent to a raw
 * 128-bit vector register (`%xmm` / ARM NEON `q0`). The `static_assert` checks
 * below guarantee that this POD layout never changes in the future.
 */
struct ComponentMask {

public:
  // --- Constants -------------------------------------------------------------

  static constexpr size_t MAX_COMPONENTS = 128;
  static constexpr size_t WORDS_COUNT = 2;

private:
  // --- Member Data -----------------------------------------------------------

  uint64_t words[WORDS_COUNT]; ///< Two 64-bit integer words (128 bits total).

public:
  // --- Constructors ----------------------------------------------------------

  /**
   * @brief Constructs an empty mask (all bits cleared).
   */
  constexpr ComponentMask() noexcept : words{0, 0} {}

  // --- Bit Modification ------------------------------------------------------

  /**
   * @brief Sets the bit corresponding to the specified TypeID.
   * @param id Component TypeID (0 .. 127).
   */
  constexpr void set(TypeID id) noexcept {
    if (id < 64) {
      words[0] |= (1ULL << id); // 1ULL <=> uint64_t(1)
    } else if (id < 128) {
      words[1] |= (1ULL << (id - 64));
    }
  }

  /**
   * @brief Clears the bit corresponding to the specified TypeID.
   * @param id Component TypeID (0 .. 127).
   */
  constexpr void reset(TypeID id) noexcept {
    if (id < 64) {
      words[0] &= ~(1ULL << id);
    } else if (id < 128) {
      words[1] &= ~(1ULL << (id - 64));
    }
  }

  /**
   * @brief Resets all bits to 0.
   */
  constexpr void clear() noexcept {
    words[0] = 0;
    words[1] = 0;
  }

  // --- Bit Queries -----------------------------------------------------------

  /**
   * @brief Tests whether the bit for TypeID is set.
   * @param id Component TypeID (0 .. 127).
   * @return true if set, false otherwise.
   */
  [[nodiscard]] constexpr bool test(TypeID id) const noexcept {
    if (id < 64) {
      return (words[0] & (1ULL << id)) != 0;
    } else if (id < 128) {
      return (words[1] & (1ULL << (id - 64))) != 0;
    }
    return false;
  }

  /**
   * @brief Checks if this mask contains all bits set in the query mask (subset
   * test).
   * @param query Target bitmask to test against.
   * @return true if (this & query) == query.
   */
  [[nodiscard]] constexpr bool contains_all(
      const ComponentMask &query
  ) const noexcept {
    return ((words[0] & query.words[0]) == query.words[0]) &&
           ((words[1] & query.words[1]) == query.words[1]);
  }

  /**
   * @brief Checks if this mask shares zero bits with the excluded mask
   * (disjoint test).
   * @param excluded Mask containing forbidden component bits.
   * @return true if (this & excluded) == 0.
   */
  [[nodiscard]] constexpr bool contains_none(
      const ComponentMask &excluded
  ) const noexcept {
    return ((words[0] & excluded.words[0]) == 0) &&
           ((words[1] & excluded.words[1]) == 0);
  }

  /**
   * @brief Checks if this mask shares at least one bit with the filter mask.
   * @param filter Mask to test intersection with.
   * @return true if (this & filter) != 0.
   */
  [[nodiscard]] constexpr bool contains_any(
      const ComponentMask &filter
  ) const noexcept {
    return ((words[0] & filter.words[0]) != 0) ||
           ((words[1] & filter.words[1]) != 0);
  }

  /**
   * @brief Returns the total number of set bits (population count) in 2 CPU
   * instructions.
   */
  [[nodiscard]] constexpr size_t count() const noexcept {
    return static_cast<size_t>(
        std::popcount(words[0]) + std::popcount(words[1])
    );
  }

  /**
   * @brief Checks if the mask contains zero set bits.
   */
  [[nodiscard]] constexpr bool empty() const noexcept {
    return words[0] == 0 && words[1] == 0;
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

// --- POD / Standard-Layout Invariant Verification ----------------------------

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

} // namespace arche
