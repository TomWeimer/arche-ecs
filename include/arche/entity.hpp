#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace arche {

// Forward declarations
namespace Internal {
struct Chunk;
struct Archetype;
} // namespace Internal

/**
 * @class Entity
 * @brief Opaque, 32-bit generational handle identifying an entity in arche-ecs.
 *
 * Bitfield Encoding:
 * - Bits 0–23 (24 bits): Dense slot index (up to 16,777,215 entities).
 * - Bits 24–31 (8 bits): Generation counter (detects stale recycled handles).
 */
class Entity {
public:
  static constexpr uint32_t INDEX_BITS = 24;
  static constexpr uint32_t GENERATION_BITS = 8;
  static constexpr uint32_t INDEX_MASK = 0x00FFFFFF;
  static constexpr uint32_t GENERATION_MASK = 0xFF000000;
  static constexpr uint32_t INDEX_NONE = 0x00FFFFFF;

  constexpr Entity() noexcept : value(0) {}

  [[nodiscard]] static constexpr Entity
  from_parts(uint32_t index, uint8_t generation) noexcept {
    Entity e;
    e.value = (static_cast<uint32_t>(generation) << INDEX_BITS) |
              (index & INDEX_MASK);
    return e;
  }

  [[nodiscard]] static constexpr Entity from_raw(uint32_t rawValue) noexcept {
    Entity e;
    e.value = rawValue;
    return e;
  }

  [[nodiscard]] constexpr uint32_t id() const noexcept {
    return value & INDEX_MASK;
  }

  [[nodiscard]] constexpr uint32_t index() const noexcept {
    return value & INDEX_MASK;
  }

  [[nodiscard]] constexpr uint8_t generation() const noexcept {
    return static_cast<uint8_t>((value >> INDEX_BITS) & 0xFF);
  }

  [[nodiscard]] constexpr uint32_t raw() const noexcept { return value; }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value != 0;
  }

  // C++20 Three-Way Comparison (Spaceship Operator)
  constexpr auto operator<=>(const Entity &other) const noexcept = default;
  constexpr bool operator==(const Entity &other) const noexcept = default;

private:
  uint32_t value{0};
};

/**
 * @brief Canonical sentinel representing an invalid or non-existent entity.
 */
inline constexpr Entity null_entity = Entity::from_parts(0, 0);

} // namespace arche

// Standard Hash Specialization for std::unordered_map / std::unordered_set
template <> struct std::hash<arche::Entity> {
  [[nodiscard]] std::size_t operator()(const arche::Entity &e) const noexcept {
    return std::hash<uint32_t>{}(e.raw());
  }
};