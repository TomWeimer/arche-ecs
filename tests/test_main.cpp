#include <arche/arche.hpp>
#include <cassert>
#include <iostream>

int main() {
  arche::World world;
  arche::Entity e1 = world.create_entity();
  arche::Entity e2 = world.create_entity();

  assert(e1.id == 1);
  assert(e2.id == 2);

  std::cout << "All arche-ecs unit tests passed.\n";
  return 0;
}