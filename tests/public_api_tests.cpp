#include "arche/arche.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
using arche::ParallelCommandBuffer;
using arche::ThreadPool;
using arche::World;
using arche::Write;
} // namespace

struct Pos {
  float x{0.0f};
  float y{0.0f};
};

struct Vel {
  float vx{0.0f};
  float vy{0.0f};
};

struct Key {
  uint32_t val{0};
};

void test_clean_lifecycle_api() {
  std::cout << "Testing Clean Lifecycle API (spawn, despawn, get, has, add, remove)... ";
  World world;

  // Scalar spawn
  Entity e1 = world.spawn(Pos{1.0f, 2.0f});
  assert(world.is_alive(e1));
  assert(world.has<Pos>(e1));
  assert(!world.has<Vel>(e1));
  assert(world.get<Pos>(e1).x == 1.0f);
  assert(world.get<Pos>(e1).y == 2.0f);

  // Const get test
  const World &cworld = world;
  assert(cworld.get<Pos>(e1).x == 1.0f);

  // Add / remove
  world.add(e1, Vel{10.0f, 20.0f});
  assert(world.has<Vel>(e1));
  assert(world.get<Vel>(e1).vx == 10.0f);

  world.remove<Vel>(e1);
  assert(!world.has<Vel>(e1));

  // Batch spawn (prototype broadcasting)
  auto ents = world.spawn_batch(100, Pos{0.0f, 0.0f}, Vel{1.0f, 1.0f});
  assert(ents.size() == 100);
  assert(world.entity_count() == 101);

  // Batch spawn (SoA streaming overload)
  std::vector<Pos> pos_data(50, Pos{5.0f, 6.0f});
  std::vector<Entity> soa_ents(50);
  world.spawn_batch(50, std::span<Entity>(soa_ents), pos_data.data());
  assert(world.entity_count() == 151);
  assert(world.get<Pos>(soa_ents[0]).x == 5.0f);

  // Batch despawn
  world.despawn_batch(ents);
  assert(world.entity_count() == 51);
  assert(!world.is_alive(ents[0]));

  world.despawn_batch(soa_ents);
  assert(world.entity_count() == 1);

  world.despawn(e1);
  assert(world.entity_count() == 0);
  std::cout << "PASSED\n";
}

void test_clean_frame_loop_api() {
  std::cout << "Testing Clean Frame Loop API (tick, query_parallel, commit, flip)... ";
  World world;
  ThreadPool pool(2);
  ParallelCommandBuffer cmd_buffers(2);

  world.enable_double_buffer<Pos>();
  world.spawn_batch(1000, Pos{0.0f, 0.0f}, Vel{1.0f, 2.0f});

  const uint32_t t1 = world.tick();
  assert(t1 == 2); // Initial tick is 1; advance makes it 2

  world.query_parallel<const Pos, const Vel, Write<Pos>>(
      pool,
      cmd_buffers,
      [](uint32_t count, CommandBuffer &, const Pos *pos, const Vel *vel, Write<Pos> next) {
        for (uint32_t i = 0; i < count; ++i) {
          next[i].x = pos[i].x + vel[i].vx;
          next[i].y = pos[i].y + vel[i].vy;
        }
      }
  );

  world.commit(cmd_buffers);
  world.flip<Pos>();

  // Verify updated positions after flip
  auto q = world.query<const Pos>();
  q.for_each_chunk([](uint32_t count, const Pos *p) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(p[i].x == 1.0f);
      assert(p[i].y == 2.0f);
    }
  });

  std::cout << "PASSED\n";
}

void test_sorting_and_snapshots_api() {
  std::cout << "Testing Sorting, GPU & Snapshots API (sort, permute, save_snapshot, load_snapshot)... ";
  World world;
  Entity e1 = world.spawn(Key{30});
  world.spawn(Key{10});
  world.spawn(Key{20});

  arche::Archetype *arch = world.get_record(e1).archetype;
  assert(arch != nullptr);

  // In-place sort
  world.sort<Key>(arch, [](const Key &a, const Key &b) { return a.val < b.val; });

  std::vector<uint32_t> vals;
  world.query<Key>().for_each([&](const Key &k) { vals.push_back(k.val); });
  assert(vals.size() == 3);
  assert(vals[0] == 10 && vals[1] == 20 && vals[2] == 30);

  // Snapshot roundtrip
  std::vector<uint8_t> snap_buf(512 * 1024);
  size_t bytes = world.save_snapshot(snap_buf.data(), snap_buf.size());
  assert(bytes > 0);

  world.spawn(Key{99});
  assert(world.entity_count() == 4);

  world.load_snapshot(snap_buf.data(), bytes);
  assert(world.entity_count() == 3);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Clean Public API Tests\n";
  std::cout << "====================================================\n";

  test_clean_lifecycle_api();
  test_clean_frame_loop_api();
  test_sorting_and_snapshots_api();

  std::cout << "====================================================\n";
  std::cout << "All Clean Public API tests passed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
