#include "arche/arche.hpp"
#include <cassert>

namespace {
using arche::Entity;
using arche::null_entity;
namespace Internal = arche::Internal;
} // namespace

void test_entity_bitfield() {
  Entity e = Entity::from_parts(42, 7);
  assert(e.index() == 42);
  assert(e.id() == 42);
  assert(e.generation() == 7);
  assert(static_cast<bool>(e) == true);

  assert(null_entity.raw() == 0);
  assert(null_entity.index() == 0);
  assert(null_entity.generation() == 0);
  assert(static_cast<bool>(null_entity) == false);
}

void test_slotmap_lifecycle_and_stale_handles() {
  Internal::SlotMap map;
  assert(map.size() == 0);

  // 1. Allocate initial entities
  Entity e1 = map.allocate();
  Entity e2 = map.allocate();
  assert(e1.index() == 0 && e1.generation() == 1);
  assert(e2.index() == 1 && e2.generation() == 1);
  assert(map.is_alive(e1));
  assert(map.is_alive(e2));

  // 2. Free e1
  map.free(e1);
  assert(!map.is_alive(e1)); // Stale handle detection
  assert(map.is_alive(e2));

  // 3. Re-allocate: slot 0 must be recycled with incremented generation
  Entity e1_recycled = map.allocate();
  assert(e1_recycled.index() == 0);
  assert(e1_recycled.generation() == 2); // Generation advanced
  assert(map.is_alive(e1_recycled));
  assert(!map.is_alive(e1)); // Original handle e1 remains permanently dead!
}

void test_million_entity_stress() {
  Internal::SlotMap map;
  std::vector<Entity> active;
  active.reserve(100'000);

  // Spawn 100,000 entities
  for (int i = 0; i < 100'000; ++i) {
    active.push_back(map.allocate());
  }
  assert(map.size() == 100'000);

  // Despawn every second entity (50,000 entities freed)
  for (int i = 0; i < 100'000; i += 2) {
    map.free(active[i]);
  }
  assert(map.size() == 50'000);

  // Verify survivors are alive and deleted are dead
  for (int i = 0; i < 100'000; ++i) {
    if (i % 2 == 0) {
      assert(!map.is_alive(active[i]));
    } else {
      assert(map.is_alive(active[i]));
    }
  }

  // Respawn 50,000 entities into recycled slots
  for (int i = 0; i < 50'000; ++i) {
    Entity recycled = map.allocate();
    assert(recycled.generation() >= 2);
  }
  assert(map.size() == 100'000);
}

void test_generation_wrap_around() {
  Internal::SlotMap map;
  Entity e = map.allocate();
  assert(e.generation() == 1);

  // Recycle slot 255 times
  for (int i = 0; i < 254; ++i) {
    map.free(e);
    e = map.allocate();
  }
  assert(e.generation() == 255);

  // Trigger wrap-around: must advance to 1, skipping 0 (null_entity)
  map.free(e);
  Entity wrapped = map.allocate();
  assert(wrapped.generation() == 1);
  assert(wrapped != null_entity);
}

#include <iostream>

int main() {
  test_entity_bitfield();
  test_slotmap_lifecycle_and_stale_handles();
  test_million_entity_stress();
  test_generation_wrap_around();

  std::cout << "All Entity & SlotMap unit tests passed successfully!\n";
  return 0;
}