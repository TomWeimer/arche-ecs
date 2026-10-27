#include "arche/arche.hpp"
#include <cstdint>

// --- Aliases -----------------------------------------------------------------

using arche::Entity;
using arche::World;

// --- World -------------------------------------------------------------------

Entity World::create_entity() noexcept {
  Entity entity{m_next_id++, 1};
  ++m_alive_count;
  return entity;
}

bool World::is_alive(Entity entity) const noexcept {
  return entity != null_entity && entity.id < m_next_id;
}

uint32_t World::entity_count() const noexcept { return m_alive_count; }
