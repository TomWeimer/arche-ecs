module;

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

export module arche:entity;

export namespace arche {

// =============================================================================
// Batch Allocation Descriptor
// =============================================================================

/// Configuration descriptor for bulk entity allocation operations.
struct Batch {
  size_t count{0};
};

// =============================================================================
// Entity: 64-Bit Generational Handle
// =============================================================================

/**
 * @struct Entity
 * @brief Opaque 64-bit generational handle identifying an entity in arche-ecs.
 *
 * Encodes a dense slot index and a generation counter in a single 64-bit
 * register:
 * - Bits 0–31  (32 bits): Dense slot index in the SlotMap (up to 4.29B
 * entities).
 * - Bits 32–63 (32 bits): Monotonic generation counter (eliminates ABA
 * hazards).
 *
 * ```cpp
 * // Example:
 * Entity e = world.spawn(Position{0.0f, 0.0f});
 * if (e) {
 *     uint32_t slot = e.index();
 *     uint32_t gen  = e.generation();
 * }
 * ```
 */
struct Entity {
private:
  uint64_t value{INDEX_NONE};

public:
  // --- Bitfield Constants ----------------------------------------------------

  /// Number of bits dedicated to the dense slot index (32 bits).
  static constexpr uint32_t INDEX_BITS = 32;

  /// Number of bits dedicated to the generation counter (32 bits).
  static constexpr uint32_t GENERATION_BITS = 32;

  /// Bitmask for extracting the 32-bit slot index from the packed handle.
  static constexpr uint64_t INDEX_MASK = 0x00000000FFFFFFFFULL;

  /// Bitmask for extracting the 32-bit generation counter from the packed
  /// handle.
  static constexpr uint64_t GENERATION_MASK = 0xFFFFFFFF00000000ULL;

  /// Sentinel constant representing an invalid slot index or null entity
  /// (~0ULL).
  static constexpr uint64_t INDEX_NONE = 0xFFFFFFFFFFFFFFFFULL;

  /// Sentinel constant representing an invalid 32-bit slot index or generation
  /// (~0U).
  static constexpr uint32_t INDEX_NONE_32 = 0xFFFFFFFFU;

  // --- Constructors & Factory Methods ----------------------------------------

  /// Default constructor creating a null/invalid entity handle.
  constexpr Entity() noexcept : value(INDEX_NONE) {}

  /// Constructs an Entity from explicit 32-bit index and generation bitfields.
  [[nodiscard]] static constexpr Entity from_parts(
      uint32_t index,
      uint32_t generation
  ) noexcept {
    Entity e;
    e.value = (static_cast<uint64_t>(generation) << INDEX_BITS) |
        (static_cast<uint64_t>(index) & INDEX_MASK);
    return e;
  }

  /// Constructs an Entity directly from an existing raw 64-bit packed integer.
  [[nodiscard]] static constexpr Entity from_raw(uint64_t rawValue) noexcept {
    Entity e;
    e.value = rawValue;
    return e;
  }

  // --- Field Accessors -------------------------------------------------------

  /// Retrieves the 32-bit slot index (convenience alias for index()).
  [[nodiscard]] constexpr uint32_t id() const noexcept {
    return static_cast<uint32_t>(value & INDEX_MASK);
  }

  /// Retrieves the 32-bit dense slot index into the SlotMap.
  [[nodiscard]] constexpr uint32_t index() const noexcept {
    return static_cast<uint32_t>(value & INDEX_MASK);
  }

  /// Retrieves the 32-bit generation counter.
  [[nodiscard]] constexpr uint32_t generation() const noexcept {
    return static_cast<uint32_t>((value & GENERATION_MASK) >> INDEX_BITS);
  }

  /// Returns the raw packed 64-bit integer representation.
  [[nodiscard]] constexpr uint64_t raw() const noexcept { return value; }

  // --- Boolean & Comparison Operators ----------------------------------------

  /// Checks if this handle represents a valid, non-null handle (value !=
  /// INDEX_NONE).
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != INDEX_NONE;
  }

  /// Explicitly tests if this handle represents the null sentinel entity.
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return value == INDEX_NONE;
  }

  /// C++20 three-way comparison operator for natural ordering and sorting.
  constexpr auto operator<=>(const Entity &other) const noexcept = default;

  /// Equality comparison operator.
  constexpr bool operator==(const Entity &other) const noexcept = default;
};

// --- Invariant Guarantees ----------------------------------------------------

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

// --- Canonical Sentinels -----------------------------------------------------

/// Canonical sentinel representing an invalid, unbound, or dead entity.
inline constexpr Entity null_entity = Entity::from_raw(Entity::INDEX_NONE);

} // namespace arche

// --- Standard Hash Specialization --------------------------------------------

template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint64_t>{}(e.raw());
  }
};
