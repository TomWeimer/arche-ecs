#pragma once

#include <compare>
#include <cstdint>

namespace arche {

// --- Entity ------------------------------------------------------------------

/// Strongly typed identifier
/// Bits 0-31: index
/// Bits 32-63: generation
struct Entity {
  uint32_t id{0};
  uint32_t generation{0};

  [[nodiscard]] constexpr std::strong_ordering
  operator<=>(const Entity &) const noexcept = default;
};

inline constexpr Entity null_entity{0, 0};

// --- World -------------------------------------------------------------------

/// Central registry managing entity lifecycle and storage tables.
class World {
public:
  World() noexcept = default;
  ~World() noexcept = default;

  // Non-copyable for clear ownership semantics; movable when needed.
  World(const World &) = delete;
  World &operator=(const World &) = delete;
  World(World &&) noexcept = default;
  World &operator=(World &&) noexcept = default;

  /// Generates a valid entity handle.
  [[nodiscard]] Entity create_entity() noexcept;

  /// Returns whether the handle points to an active entity.
  [[nodiscard]] bool is_alive(Entity entity) const noexcept;

  /// Returns the total active entities in the world.
  [[nodiscard]] uint32_t entity_count() const noexcept;

private:
  uint32_t m_next_id{1};
  uint32_t m_alive_count{0};
};

} // namespace arche