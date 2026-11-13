#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

namespace arche {

// --- Forward Declarations ----------------------------------------------------

class Archetype;
struct Chunk;

// --- Entity Handle -----------------------------------------------------------

/**
 * @struct Entity
 * @brief Opaque, 64-bit generational handle identifying an entity in arche-ecs.
 *
 * @details
 * In an archetype-based Entity-Component-System:
 * - An Entity is not a container, class, or pointer. It is an opaque 64-bit
 *   generational integer key that directly indexes into a global SlotMap.
 * - Entities undergo rapid structural mutation: adding components moves an
 *   entity to a different archetype chunk, deleting an entity causes a
 *   neighboring entity to move via Swap-and-Pop, and dead entities are
 *   continuously recycled.
 * - Fits into a single 64-bit CPU register (`uint64_t`), allowing pass-by-value
 *   with zero stack spilling and optimal cache density.
 *
 * Binary Bitfield Allocation:
 * ```text
 * +----------------------------------+----------------------------------+
 * | Bits 63..32 (32 bits)            | Bits 31..0 (32 bits)             |
 * | Generation Counter (1..4.29B)    | Slot Index (0..4,294,967,295)    |
 * +----------------------------------+----------------------------------+
 * ```
 * - Bits 0–31 (32 bits): Dense slot index in the SlotMap (up to 4,294,967,295
 *   simultaneous entities).
 * - Bits 32–63 (32 bits): Generation counter (1–4,294,967,295), detecting stale
 *   handles and eliminating ABA hazards across high-churn simulations.
 * - `INDEX_NONE` (`0xFFFFFFFFFFFFFFFFULL`) is reserved for `null_entity`.
 *
 * ### Why a struct instead of a raw uint64_t?
 * We define Entity as a `struct` to attach convenient member functions
 * (`index()`, `generation()`, comparison operators) directly to the handle.
 *
 * The compiler optimizes this struct to be completely equivalent to a raw
 * `uint64_t` (passed in a single CPU register with zero overhead). The
 * `static_assert` checks below guarantee that this layout never changes in the
 * future.
 */
struct Entity {
private:
  /// Packed 64-bit representation (32-bit gen, 32-bit index).
  uint64_t value{INDEX_NONE};

public:
  // --- Bitfield Constants ----------------------------------------------------

  /// Number of bits dedicated to the dense slot index (32 bits).
  static constexpr uint32_t INDEX_BITS = 32;

  /// Number of bits dedicated to the generation counter (32 bits).
  static constexpr uint32_t GENERATION_BITS = 32;

  /// Bitmask for extracting the 32-bit slot index from the packed handle.
  static constexpr uint64_t INDEX_MASK = 0x00000000FFFFFFFFULL;

  /// Bitmask for extracting the 32-bit generation counter from the packed handle.
  static constexpr uint64_t GENERATION_MASK = 0xFFFFFFFF00000000ULL;

  /// Sentinel constant representing an invalid slot index or null entity (~0ULL).
  static constexpr uint64_t INDEX_NONE = 0xFFFFFFFFFFFFFFFFULL;

  /// Sentinel constant representing an invalid 32-bit slot index or generation (~0U).
  static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFFU;

  // --- Constructors & Factory Methods ----------------------------------------

  /**
   * @brief Default constructor creating a null/invalid entity handle.
   * @details Initializes the handle to the null sentinel value (INDEX_NONE),
   * making the default-constructed entity equivalent to null_entity.
   */
  constexpr Entity() noexcept : value(INDEX_NONE) {}

  /**
   * @brief Constructs an Entity from explicit index and generation bitfields.
   *
   * @param index 32-bit dense slot index into the SlotMap (0 .. 4,294,967,295).
   * @param generation 32-bit generation counter (1 .. 4,294,967,295; 0 reserved for null).
   * @return Encoded 64-bit Entity handle.
   */
  [[nodiscard]] static constexpr Entity from_parts(
      uint32_t index,
      uint32_t generation
  ) noexcept {
    Entity e;
    e.value = (static_cast<uint64_t>(generation) << INDEX_BITS) |
        (static_cast<uint64_t>(index) & INDEX_MASK);
    return e;
  }

  /**
   * @brief Constructs an Entity directly from an existing raw 64-bit packed integer.
   *
   * @param rawValue Raw packed 64-bit integer representation.
   * @return Decoded Entity handle.
   */
  [[nodiscard]] static constexpr Entity from_raw(uint64_t rawValue) noexcept {
    Entity e;
    e.value = rawValue;
    return e;
  }

  // --- Field Accessors -------------------------------------------------------

  /**
   * @brief Retrieves the 32-bit slot index (convenience alias for index()).
   * @return uint32_t Slot index in range [0, 4,294,967,295].
   */
  [[nodiscard]] constexpr uint32_t id() const noexcept {
    return static_cast<uint32_t>(value & INDEX_MASK);
  }

  /**
   * @brief Retrieves the 32-bit dense slot index into the SlotMap.
   * @return uint32_t Slot index in range [0, 4,294,967,295].
   */
  [[nodiscard]] constexpr uint32_t index() const noexcept {
    return static_cast<uint32_t>(value & INDEX_MASK);
  }

  /**
   * @brief Retrieves the 32-bit generation counter.
   * @return uint32_t Generation in range [0, 4,294,967,295].
   */
  [[nodiscard]] constexpr uint32_t generation() const noexcept {
    return static_cast<uint32_t>((value & GENERATION_MASK) >> INDEX_BITS);
  }

  /**
   * @brief Returns the raw packed 64-bit integer representation.
   * @return uint64_t Packed bitfield containing both generation and index.
   */
  [[nodiscard]] constexpr uint64_t raw() const noexcept { return value; }

  // --- Boolean & Comparison Operators ----------------------------------------

  /**
   * @brief Checks if this handle represents a valid, non-null entity handle.
   * @note Does NOT check if the entity is currently alive in the world;
   * use SlotMap::is_alive(entity) for world liveness verification.
   * @return true if handle is not null (value != INDEX_NONE), false otherwise.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != INDEX_NONE;
  }

  /**
   * @brief Explicitly tests if this handle represents the null sentinel entity.
   * @return true if handle is equal to null_entity (INDEX_NONE), false
   * otherwise.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return value == INDEX_NONE;
  }

  /**
   * @brief C++20 three-way comparison operator for natural ordering and
   * sorting.
   */
  constexpr auto operator<=>(const Entity &other) const noexcept = default;

  /**
   * @brief Equality comparison operator.
   */
  constexpr bool operator==(const Entity &other) const noexcept = default;
};

// --- POD / Standard-Layout Invariant Verification ----------------------------

static_assert(
    sizeof(Entity) == sizeof(uint64_t),
    "Entity must be exactly 8 bytes (single 64-bit register footprint)"
);

static_assert(
    std::is_standard_layout_v<Entity>,
    "Entity must satisfy standard-layout POD constraints"
);
static_assert(
    std::is_trivially_copyable_v<Entity>,
    "Entity must be trivially copyable for zero-cost register passing"
);
static_assert(
    std::is_trivially_destructible_v<Entity>,
    "Entity must have a trivial destructor"
);

// --- Canonical Sentinel Values -----------------------------------------------

/**
 * @brief Canonical sentinel representing an invalid, unbound, or dead entity.
 * Initialized to INDEX_NONE (0xFFFFFFFFFFFFFFFFULL), where both index and
 * generation are set to all ones.
 */
inline constexpr Entity null_entity = Entity::from_raw(Entity::INDEX_NONE);

} // namespace arche

// --- Standard Hash Specialization --------------------------------------------

/**
 * @brief std::hash specialization enabling Entity usage in std::unordered_map
 * and std::unordered_set with zero-overhead identity hashing.
 */
template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint64_t>{}(e.raw());
  }
};

// =============================================================================
// Monotonic Nonzero Counter Helper
// =============================================================================

/**
 * @brief Advances a 32-bit monotonic counter, skipping 0 on wrap-around.
 * @details Defined in `include/arche/entity.hpp`. Replaces ad-hoc wrap-around
 * checks
 * `(next == 0) ? 1 : next` across SlotMap generations and World simulation
 * ticks.
 *
 * @param val Current counter value.
 * @return Incremented value skipping 0.
 */
[[nodiscard]] inline constexpr uint32_t advance_nonzero(uint32_t val) noexcept {
  const uint32_t next = val + 1;
  return (next == 0) ? 1 : next;
}
