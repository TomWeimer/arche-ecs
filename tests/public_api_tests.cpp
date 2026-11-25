import arche;

#include <cassert>
#include <iostream>
#include <vector>

namespace {
using arche::CommandBuffer;
using arche::Entity;
using arche::World;
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
  std::cout << "Testing Clean Lifecycle API (spawn, despawn, get, has, add, "
               "remove)... ";
  World world;

  // Scalar spawn
  Entity e1 = world.spawn(Pos{1.0f, 2.0f});
  assert(world.entities.contains(e1));
  assert(world.has<Pos>(e1));
  assert(!world.has<Vel>(e1));
  assert(world.get<Pos>(e1).x == 1.0f);
  assert(world.get<Pos>(e1).y == 2.0f);

  // Const get test & type deduction
  const World &cworld = world;
  assert(cworld.get<Pos>(e1).x == 1.0f);

  // get vs get_mut distinction & auto-dirty tracking
  static_assert(std::is_same_v<decltype(world.get<Pos>(e1)), const Pos &>);
  static_assert(std::is_same_v<decltype(world.get_mut<Pos>(e1)), Pos &>);

  const auto &rec = world.entities.slot_map().get_record(
      e1
  );
  const int16_t pos_col = rec.archetype->column_index(arche::component_id<Pos>());
  assert(rec.chunk->column_version(pos_col) == 1);

  // Read via get does NOT bump version
  float read_x = world.get<Pos>(e1).x;
  assert(read_x == 1.0f);
  assert(rec.chunk->column_version(pos_col) == 1);

  // Advance world version and mutate via get_mut
  world.advance_version();
  world.advance_version();
  assert(world.version() == 3);

  world.get_mut<Pos>(e1).x = 42.0f;
  assert(world.get<Pos>(e1).x == 42.0f);
  assert(
      rec.chunk->column_version(pos_col) == 3
  ); // Automatically stamped dirty!

  // Add / remove
  world.add(e1, Vel{10.0f, 20.0f});
  assert(world.has<Vel>(e1));
  assert(world.get<Vel>(e1).vx == 10.0f);

  world.remove<Vel>(e1);
  assert(!world.has<Vel>(e1));

  // Unified Batch spawn (prototype broadcasting)
  auto ents = world.spawn({.count = 100}, Pos{0.0f, 0.0f}, Vel{1.0f, 1.0f});
  assert(ents.size() == 100);
  assert(world.entities.count() == 101);

  // Unified Batch spawn (SoA streaming overload)
  std::vector<Pos> pos_data(50, Pos{5.0f, 6.0f});
  std::vector<Entity> soa_ents(50);
  world.spawn({.count = 50}, std::span<Entity>(soa_ents), pos_data.data());
  assert(world.entities.count() == 151);
  assert(world.get<Pos>(soa_ents[0]).x == 5.0f);

  // Batch despawn
  world.despawn(ents);
  assert(world.entities.count() == 51);
  assert(!world.entities.contains(ents[0]));

  world.despawn(soa_ents);
  assert(world.entities.count() == 1);

  world.despawn(e1);
  assert(world.entities.count() == 0);
  std::cout << "PASSED\n";
}

void test_clean_frame_loop_api() {
  std::cout << "Testing Clean Frame Loop API (query_parallel, commit)... ";
  World world;

  world.spawn({.count = 1000}, Pos{0.0f, 0.0f}, Vel{1.0f, 2.0f});

  assert(world.version() == 1);

  world.query<Pos, const Vel>().each_parallel(
      [](uint32_t count, Pos *pos, const Vel *vel) {
        for (uint32_t i = 0; i < count; ++i) {
          pos[i].x += vel[i].vx;
          pos[i].y += vel[i].vy;
        }
      }
  );

  // Verify updated positions
  auto q = world.query<const Pos>();
  q.each_chunk([](uint32_t count, const Pos *p) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(p[i].x == 1.0f);
      assert(p[i].y == 2.0f);
    }
  });

  std::cout << "PASSED\n";
}

void test_sorting_and_snapshots_api() {
  std::cout << "Testing Sorting, GPU & Snapshots API (sort, save_snapshot, "
               "load_snapshot)... ";
  World world;
  world.spawn(Key{30});
  world.spawn(Key{10});
  world.spawn(Key{20});

  // In-place sort across all archetypes with Key
  arche::sort<Key>(world, [](const Key &a, const Key &b) {
    return a.val < b.val;
  });

  std::vector<uint32_t> vals;
  for (auto [k] : world.query<Key>()) {
    vals.push_back(k.val);
  }
  assert(vals.size() == 3);
  assert(vals[0] == 10 && vals[1] == 20 && vals[2] == 30);

  // Snapshot roundtrip
  std::vector<uint8_t> snap_buf(512 * 1024);
  size_t bytes = world.snapshots.save(snap_buf.data(), snap_buf.size());
  assert(bytes > 0);

  world.spawn(Key{99});
  assert(world.entities.count() == 4);

  world.snapshots.load(snap_buf.data(), bytes);
  assert(world.entities.count() == 3);

  std::cout << "PASSED\n";
}

void test_entity_view_api() {
  std::cout << "Testing EntityView Handle API (world.entity(e), get, get_mut, "
               "has, add, remove, despawn)... ";
  World world;

  Entity e1 = world.spawn(Pos{10.0f, 20.0f});

  static_assert(std::is_same_v<decltype(world.entity(e1)), arche::EntityMut>);
  static_assert(std::is_same_v<arche::EntityMut, arche::EntityMut>);

  auto view = world.entity(e1);

  assert(view.id() == e1);
  assert(view.is_alive());
  assert(view.has<Pos>());
  assert(!view.has<Vel>());

  // Read-only get
  assert(view.get<Pos>().x == 10.0f);
  assert(view.get<Pos>().y == 20.0f);

  // Const World inspection yields EntityRef
  const World &cworld = world;
  static_assert(std::is_same_v<decltype(cworld.entity(e1)), arche::EntityRef>);
  arche::EntityRef cref = cworld.entity(e1);
  assert(cref.id() == e1);
  assert(cref.is_alive());
  assert(cref.has<Pos>());
  assert(cref.get<Pos>().x == 10.0f);

  // Implicit conversion from EntityMut to EntityRef
  arche::EntityRef converted_ref = view;
  assert(converted_ref.get<Pos>().x == 10.0f);

  // Mutable get_mut & dirty tracking
  uint32_t v_before = world.version();
  view.get_mut<Pos>().x = 99.0f;
  assert(view.get<Pos>().x == 99.0f);

  const auto &rec = world.entities.slot_map().get_record(
      e1
  );
  const int16_t pos_col = rec.archetype->column_index(arche::component_id<Pos>());
  assert(rec.chunk->column_version(static_cast<uint16_t>(pos_col)) == v_before);

  // add component
  view.add(Vel{1.0f, 2.0f});
  assert(view.has<Vel>());
  assert(view.get<Vel>().vx == 1.0f);
  assert(view.get<Pos>().x == 99.0f);

  // remove component
  view.remove<Vel>();
  assert(!view.has<Vel>());
  assert(view.has<Pos>());

  // despawn
  bool despawned = view.despawn();
  assert(despawned);
  assert(!view.is_alive());
  assert(!world.entities.contains(e1));

  std::cout << "PASSED\n";
}

void test_public_encapsulation_boundary() {
  std::cout << "Testing Public Encapsulation Boundary (Zero Internal Leaks)... ";

  // Verify that all core public types reside directly in namespace arche
  static_assert(std::is_same_v<arche::Entity, arche::Entity>);
  static_assert(std::is_same_v<arche::Batch, arche::Batch>);
  static_assert(std::is_same_v<arche::EntityRef, arche::EntityRef>);
  static_assert(std::is_same_v<arche::EntityMut, arche::EntityMut>);
  static_assert(std::is_same_v<arche::CommandBuffer, arche::CommandBuffer>);
  static_assert(std::is_same_v<arche::World, arche::World>);
  static_assert(std::is_same_v<arche::ComponentID, arche::ComponentID>);
  static_assert(std::is_same_v<arche::Access, arche::AccessBuilder>);
  static_assert(std::is_same_v<arche::AccessDescriptor, arche::AccessDescriptor>);
  static_assert(std::is_same_v<arche::SystemContext, arche::SystemContext>);

  // Verify internal namespace isolation
  static_assert(std::is_same_v<arche::internal::Chunk, arche::internal::Chunk>);
  static_assert(std::is_same_v<arche::internal::SlotMap, arche::internal::SlotMap>);
  static_assert(std::is_same_v<arche::internal::Archetype, arche::internal::Archetype>);
  static_assert(std::is_same_v<arche::internal::Scheduler, arche::internal::Scheduler>);
  static_assert(std::is_same_v<arche::internal::ThreadPool, arche::internal::ThreadPool>);
  static_assert(std::is_same_v<arche::internal::ParallelCommandBuffer, arche::internal::ParallelCommandBuffer>);

  std::cout << "PASSED\n";
}

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Clean Public API Tests\n";
  std::cout << "====================================================\n";

  test_clean_lifecycle_api();
  test_entity_view_api();
  test_clean_frame_loop_api();
  test_sorting_and_snapshots_api();
  test_public_encapsulation_boundary();

  std::cout << "====================================================\n";
  std::cout << "All Clean Public API tests passed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
