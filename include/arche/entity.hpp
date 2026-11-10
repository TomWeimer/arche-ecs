#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

namespace arche {

// --- Forward Declarations ----------------------------------------------------

namespace Internal {
struct Chunk;
struct Archetype;
} // namespace Internal

// --- Entity Handle -----------------------------------------------------------

/**
 * @struct Entity
 * @brief Opaque, 32-bit generational handle identifying an entity in arche-ecs.
 *
 * @details
 * In an archetype-based Entity-Component-System:
 * - An Entity is not a container, class, or pointer. It is an opaque 32-bit
 *   generational integer key that directly indexes into a global SlotMap.
 * - Entities undergo rapid structural mutation: adding components moves an
 *   entity to a different archetype chunk, deleting an entity causes a
 *   neighboring entity to move via Swap-and-Pop, and dead entities are
 *   continuously recycled.
 * - Fits into a single 32-bit CPU register (`uint32_t`), allowing pass-by-value
 *   with zero stack spilling and optimal cache density.
 *
 * Binary Bitfield Allocation:
 * ```text
 * +----------------------------------+----------------------------------+
 * | Bits 31..24 (8 bits)             | Bits 23..0 (24 bits)             |
 * | Generation Counter (1..255)      | Slot Index (0..16,777,215)       |
 * +----------------------------------+----------------------------------+
 * ```
 * - Bits 0–23 (24 bits): Dense slot index in the SlotMap (up to 16,777,215
 * entities).
 * - Bits 24–31 (8 bits): Generation counter (1–255), detecting stale handles.
 *   Generation 0 is strictly reserved for `null_entity`.
 *
 * ### Why a struct instead of a raw uint32_t?
 * We define Entity as a `struct` to attach convenient member functions
 * (`index()`, `generation()`, comparison operators) directly to the handle.
 *
 * The compiler optimizes this struct to be completely equivalent to a raw
 * `uint32_t` (passed in a single CPU register with zero overhead). The
 * `static_assert` checks below guarantee that this layout never changes in the
 * future.
 *
 */
struct Entity {

private:
  /// Packed 32-bit representation (8-bit gen, 24-bit index).
  uint32_t value{0};

public:
  // --- Bitfield Constants ----------------------------------------------------

  static constexpr uint32_t INDEX_BITS = 24;
  static constexpr uint32_t GENERATION_BITS = 8;
  static constexpr uint32_t INDEX_MASK = 0x00FFFFFF;
  static constexpr uint32_t GENERATION_MASK = 0xFF000000;
  static constexpr uint32_t INDEX_NONE = 0x00FFFFFF;

  // --- Constructors & Factory Methods ----------------------------------------

  /**
   * @brief Default constructor creating a null/invalid entity handle.
   */
  constexpr Entity() noexcept : value(0) {}

  /**
   * @brief Constructs an Entity from explicit index and generation bitfields.
   *
   * @param index 24-bit slot index (0 .. 16,777,215).
   * @param generation 8-bit generation counter (1 .. 255; 0 reserved for null).
   * @return Encoded 32-bit Entity handle.
   */
  [[nodiscard]] static constexpr Entity from_parts(
      uint32_t index,
      uint8_t generation
  ) noexcept {
    Entity e;
    e.value = (static_cast<uint32_t>(generation) << INDEX_BITS) |
              (index & INDEX_MASK);
    return e;
  }

  /**
   * @brief Constructs an Entity directly from an existing raw 32-bit integer.
   *
   * @param rawValue Raw packed 32-bit integer.
   * @return Decoded Entity handle.
   */
  [[nodiscard]] static constexpr Entity from_raw(uint32_t rawValue) noexcept {
    Entity e;
    e.value = rawValue;
    return e;
  }

  // --- Field Accessors -------------------------------------------------------

  /**
   * @brief Retrieves the 24-bit slot index (alias for index()).
   * @return uint32_t Slot index in range [0, 16,777,215].
   */
  [[nodiscard]] constexpr uint32_t id() const noexcept {
    return value & INDEX_MASK;
  }

  /**
   * @brief Retrieves the 24-bit dense slot index into the SlotMap.
   * @return uint32_t Slot index in range [0, 16,777,215].
   */
  [[nodiscard]] constexpr uint32_t index() const noexcept {
    return value & INDEX_MASK;
  }

  /**
   * @brief Retrieves the 8-bit generation counter.
   * @return uint8_t Generation in range [0, 255].
   */
  [[nodiscard]] constexpr uint8_t generation() const noexcept {
    return static_cast<uint8_t>((value >> INDEX_BITS) & 0xFF);
  }

  /**
   * @brief Returns the raw packed 32-bit integer representation.
   * @return uint32_t Packed bitfield.
   */
  [[nodiscard]] constexpr uint32_t raw() const noexcept { return value; }

  // --- Boolean & Comparison Operators ----------------------------------------

  /**
   * @brief Checks if this handle represents a non-null entity.
   * @note Does NOT check if the entity is alive in the world; use
   * SlotMap::is_alive.
   * @return true if value != 0, false otherwise.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
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
    sizeof(Entity) == sizeof(uint32_t),
    "Entity must be exactly 4 bytes (single 32-bit register footprint)"
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
 * Guaranteed to have index 0, generation 0, and raw value 0.
 */
inline constexpr Entity null_entity = Entity::from_parts(0, 0);

} // namespace arche

// --- Standard Hash Specialization --------------------------------------------

/**
 * @brief std::hash specialization enabling Entity usage in std::unordered_map
 * and std::unordered_set with zero-overhead identity hashing.
 */
template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint32_t>{}(e.raw());
  }
};
