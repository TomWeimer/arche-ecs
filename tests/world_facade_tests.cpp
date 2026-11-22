#include "arche/arche.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

struct GameConfig {
  float gravity{-9.81f};
  int max_players{64};
};

} // namespace

int main() {
  std::cout << "====================================================\n";
  std::cout << "Testing Dedicated World Sub-Facades (Snapshots, Resources, Archetypes, GPU)\n";
  std::cout << "====================================================\n";

  arche::World world;

  // 1. Test Archetypes Facade
  std::cout << "[Test 1/4] Archetypes Facade... ";
  assert(world.archetypes.count() == 1); // Root empty archetype
  assert(world.archetypes.count_with<Position>() == 0);

  auto e1 = world.spawn(Position{1.0f, 2.0f}, Velocity{0.5f, 0.5f});
  auto e2 = world.spawn(Position{3.0f, 4.0f});

  assert(world.archetypes.count() == 3); // Root + (Pos, Vel) + (Pos)
  assert(world.archetypes.count_with<Position>() == 2);
  assert(world.archetypes.count_with<Velocity>() == 1);
  std::cout << "PASSED\n";

  // 2. Test Resources Facade
  std::cout << "[Test 2/4] Resources Facade... ";
  assert(!world.resources.has<GameConfig>());
  world.resources.insert<GameConfig>(GameConfig{.gravity = -19.62f, .max_players = 32});
  assert(world.resources.has<GameConfig>());
  assert(world.resources.get<GameConfig>().gravity == -19.62f);
  assert(world.resources.get<GameConfig>().max_players == 32);

  const arche::World &cworld = world;
  assert(cworld.resources.has<GameConfig>());
  assert(cworld.resources.get<GameConfig>().gravity == -19.62f);

  assert(world.resources.remove<GameConfig>());
  assert(!world.resources.has<GameConfig>());
  std::cout << "PASSED\n";

  // 3. Test GPU Facade
  std::cout << "[Test 3/4] GPU Facade (view_chunks & stream_to)... ";
  auto views = world.gpu.view_chunks<Position>();
  assert(views.size() == 2); // 2 distinct archetypes possessing Position

  std::vector<Position> streamed_positions(2);
  size_t bytes = world.gpu.stream_to<Position>(streamed_positions.data());
  assert(bytes == 2 * sizeof(Position));
  std::cout << "PASSED\n";

  // 4. Test Snapshots Facade (Memory & File persistence)
  std::cout << "[Test 4/4] Snapshots Facade (File save & restore)... ";
  assert(world.snapshots.size_bytes() > 0);
  auto hdr = world.snapshots.header();
  assert(hdr.magic == 0x48435241);
  assert(hdr.active_entities == 2);

  const std::string file_path = "test_snapshot.arch";
  assert(world.snapshots.save_to_file(file_path));

  // Despawn all entities
  world.despawn(e1);
  world.despawn(e2);
  assert(world.entities.count() == 0);
  assert(world.entities.is_empty());
  assert(!world.entities.contains(e1));
  assert(!world.entities.contains(e2));
  assert(!world.entities.resolve(e1.index()).has_value());
  assert(world.archetypes.count_with<Position>() == 0);

  // Restore from file
  assert(world.snapshots.load_from_file(file_path));
  assert(world.entities.count() == 2);
  assert(!world.entities.is_empty());
  assert(world.entities.capacity() >= 2);
  assert(world.entities.contains(e1));
  assert(world.entities.contains(e2));
  assert(world.entities.resolve(e1.index()) == e1);
  assert(world.entities.resolve(e2.index()) == e2);
  assert(world.get<Position>(e1).x == 1.0f);
  assert(world.get<Position>(e2).x == 3.0f);

  std::filesystem::remove(file_path);
  std::cout << "PASSED\n";

  std::cout << "All Sub-Facade Tests Completed Successfully!\n";
  return 0;
}
