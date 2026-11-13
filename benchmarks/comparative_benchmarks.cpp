#include "arche/arche.hpp"

#if __has_include(<nanobench.h>) && __has_include(<entt/entt.hpp>) && __has_include(<flecs.h>)
#include <entt/entt.hpp>
#include <flecs.h>
#include <nanobench.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// =============================================================================
// Benchmark Component Definitions (Standard Plain-Old-Data)
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

struct Acceleration {
  float ax{0.0f};
  float ay{0.0f};
};

struct Rotation {
  float angle{0.0f};
};

struct Scale {
  float sx{1.0f};
  float sy{1.0f};
};

struct Mass {
  float inv_mass{1.0f};
};

struct Radius {
  float r{5.0f};
};

struct BoidState {
  uint32_t flags{0};
  uint32_t group_id{1};
};

struct FrozenTag {
  uint32_t reason{1};
};

// 20 distinct tag components for deep fragmentation testing
struct Tag0  { uint32_t id{0}; };  struct Tag1  { uint32_t id{1}; };
struct Tag2  { uint32_t id{2}; };  struct Tag3  { uint32_t id{3}; };
struct Tag4  { uint32_t id{4}; };  struct Tag5  { uint32_t id{5}; };
struct Tag6  { uint32_t id{6}; };  struct Tag7  { uint32_t id{7}; };
struct Tag8  { uint32_t id{8}; };  struct Tag9  { uint32_t id{9}; };
struct Tag10 { uint32_t id{10}; }; struct Tag11 { uint32_t id{11}; };
struct Tag12 { uint32_t id{12}; }; struct Tag13 { uint32_t id{13}; };
struct Tag14 { uint32_t id{14}; }; struct Tag15 { uint32_t id{15}; };
struct Tag16 { uint32_t id{16}; }; struct Tag17 { uint32_t id{17}; };
struct Tag18 { uint32_t id{18}; }; struct Tag19 { uint32_t id{19}; };

struct ExtraData {
  float values[4]{1.0f, 2.0f, 3.0f, 4.0f};
};

int main() {
  std::cout << "==============================================================================\n";
  std::cout << "  Comprehensive ECS Benchmark: arche-ecs vs. Flecs vs. EnTT (2M Scale Edition)\n";
  std::cout << "==============================================================================\n\n";

  constexpr size_t COUNT_2M   = 2'000'000;
  constexpr size_t COUNT_500K =   500'000;
  constexpr size_t COUNT_200K =   200'000;
  constexpr size_t COUNT_100K =   100'000;
  constexpr size_t MUTATION_COUNT = 20'000;

  // ---------------------------------------------------------------------------
  // SECTION I: EXTREME SCALE — 2,000,000 (2M) ENTITIES
  // ---------------------------------------------------------------------------
  std::cout << ">>> Running Section I: 2,000,000 (2M) Entity Benchmarks...\n\n";
  {
    ankerl::nanobench::Bench bench_2m;
    bench_2m.warmup(3).minEpochIterations(10);

    // 1A. 2,000,000 Entities: Simple Streaming (Pos += Vel * dt)
    {
      using EnTTRegistry2M = entt::basic_registry<std::uint64_t>;

      arche::World world;
      world.spawn_batch(COUNT_2M, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
      auto query = world.query<Position, const Velocity>();

      bench_2m.title("1. 2M Entities: Simple Streaming (Pos + Vel)")
          .run("arche-ecs (Chunked SoA)", [&] {
            query.for_each_chunk(
                [](uint32_t count, Position * __restrict__ pos, const Velocity * __restrict__ vel) {
                  #pragma clang loop vectorize(enable)
                  for (uint32_t i = 0; i < count; ++i) {
                    pos[i].x += vel[i].vx * 0.016f;
                    pos[i].y += vel[i].vy * 0.016f;
                  }
                }
            );
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
      }
      auto f_query = ecs.query<Position, const Velocity>();

      bench_2m.run("Flecs (Archetype Table)", [&] {
        f_query.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });

      EnTTRegistry2M registry;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        const auto e = registry.create();
        registry.emplace<Position>(e, 0.0f, 0.0f);
        registry.emplace<Velocity>(e, 1.0f, 2.0f);
      }
      auto view = registry.view<Position, const Velocity>();

      bench_2m.run("EnTT (Standard View)", [&] {
        view.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });

      EnTTRegistry2M group_reg;
      auto group = group_reg.group<Position>(entt::get<Velocity>);
      for (size_t i = 0; i < COUNT_2M; ++i) {
        const auto e = group_reg.create();
        group_reg.emplace<Position>(e, 0.0f, 0.0f);
        group_reg.emplace<Velocity>(e, 1.0f, 2.0f);
      }

      bench_2m.run("EnTT (Owned Group)", [&] {
        group.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });
    }

    // 1B. 2,000,000 Entities: Realistic Swarm Physics (Euler + Bounds)
    {
      using EnTTRegistry2M = entt::basic_registry<std::uint64_t>;

      arche::World world;
      world.spawn_batch(COUNT_2M, Position{500.0f, 500.0f}, Velocity{10.0f, 15.0f});
      auto query = world.query<Position, Velocity>();

      bench_2m.title("2. 2M Entities: Realistic Swarm Physics (Euler + Bounds)")
          .run("arche-ecs (Chunked SoA)", [&] {
            query.for_each_chunk(
                [](uint32_t count, Position * __restrict__ pos, Velocity * __restrict__ vel) {
                  #pragma clang loop vectorize(enable)
                  for (uint32_t i = 0; i < count; ++i) {
                    pos[i].x += vel[i].vx * 0.016f;
                    pos[i].y += vel[i].vy * 0.016f;
                    if (pos[i].x < 0.0f || pos[i].x > 1920.0f) vel[i].vx = -vel[i].vx;
                    if (pos[i].y < 0.0f || pos[i].y > 1080.0f) vel[i].vy = -vel[i].vy;
                  }
                }
            );
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        ecs.entity().set<Position>({500.0f, 500.0f}).set<Velocity>({10.0f, 15.0f});
      }
      auto f_query = ecs.query<Position, Velocity>();

      bench_2m.run("Flecs (Archetype Table)", [&] {
        f_query.each([](Position &pos, Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
          if (pos.x < 0.0f || pos.x > 1920.0f) vel.vx = -vel.vx;
          if (pos.y < 0.0f || pos.y > 1080.0f) vel.vy = -vel.vy;
        });
      });

      EnTTRegistry2M registry;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        const auto e = registry.create();
        registry.emplace<Position>(e, 500.0f, 500.0f);
        registry.emplace<Velocity>(e, 10.0f, 15.0f);
      }
      auto view = registry.view<Position, Velocity>();

      bench_2m.run("EnTT (Standard View)", [&] {
        view.each([](Position &pos, Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
          if (pos.x < 0.0f || pos.x > 1920.0f) vel.vx = -vel.vx;
          if (pos.y < 0.0f || pos.y > 1080.0f) vel.vy = -vel.vy;
        });
      });
    }

    // 1C. 2,000,000 Entities: Multi-Threaded Scaling (8 Threads vs Single)
    {
      arche::World world;
      world.spawn_batch(COUNT_2M, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
      auto query = world.query<Position, const Velocity>();

      const uint32_t threads = std::max(2u, std::thread::hardware_concurrency());
      arche::ThreadPool pool(threads);

      bench_2m.title("3. 2M Entities: Multi-Threaded Chunk Parallelism")
          .run("arche-ecs (8-Thread query_parallel)", [&] {
            world.query_parallel<Position, const Velocity>(
                pool, [](uint32_t count, Position *pos, const Velocity *vel) {
                  for (uint32_t i = 0; i < count; ++i) {
                    pos[i].x += vel[i].vx * 0.016f;
                    pos[i].y += vel[i].vy * 0.016f;
                  }
                }
            );
          })
          .run("arche-ecs (Single-Thread baseline)", [&] {
            query.for_each_chunk(
                [](uint32_t count, Position *pos, const Velocity *vel) {
                  for (uint32_t i = 0; i < count; ++i) {
                    pos[i].x += vel[i].vx * 0.016f;
                    pos[i].y += vel[i].vy * 0.016f;
                  }
                }
            );
          });
    }
  }

  // ---------------------------------------------------------------------------
  // SECTION II: COMPLEX ENTITY SIMULATION — 8 COMPONENTS (500,000 ENTITIES)
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section II: Wide 8-Component Entity Simulation (500k)...\n\n";
  {
    ankerl::nanobench::Bench bench_wide;
    bench_wide.warmup(5).minEpochIterations(15);

    arche::World world;
    world.spawn_batch(
        COUNT_500K,
        Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, Acceleration{0.1f, 0.2f},
        Rotation{0.5f}, Scale{1.0f, 1.0f}, Mass{2.0f}, Radius{5.0f},
        BoidState{1, 10}
    );
    auto query = world.query<
        Position, Velocity, const Acceleration, Rotation, const Scale,
        const Mass, const Radius, BoidState
    >();

    bench_wide.title("4. Wide Entity Simulation (500k Entities: 8 Components)")
        .run("arche-ecs (Chunked SoA)", [&] {
          query.for_each_chunk(
              [](uint32_t count, Position *pos, Velocity *vel,
                 const Acceleration *acc, Rotation *rot, const Scale *s,
                 const Mass *m, const Radius *r, BoidState *state) {
                for (uint32_t i = 0; i < count; ++i) {
                  vel[i].vx += acc[i].ax * m[i].inv_mass * 0.016f;
                  vel[i].vy += acc[i].ay * m[i].inv_mass * 0.016f;
                  pos[i].x += vel[i].vx * s[i].sx * 0.016f;
                  pos[i].y += vel[i].vy * s[i].sy * 0.016f;
                  rot[i].angle += 0.01f;
                  if (pos[i].x < r[i].r) state[i].flags |= 2;
                }
              }
          );
        });

    flecs::world ecs;
    for (size_t i = 0; i < COUNT_500K; ++i) {
      ecs.entity()
          .set<Position>({0.0f, 0.0f})
          .set<Velocity>({1.0f, 2.0f})
          .set<Acceleration>({0.1f, 0.2f})
          .set<Rotation>({0.5f})
          .set<Scale>({1.0f, 1.0f})
          .set<Mass>({2.0f})
          .set<Radius>({5.0f})
          .set<BoidState>({1, 10});
    }
    auto f_query = ecs.query<
        Position, Velocity, const Acceleration, Rotation, const Scale,
        const Mass, const Radius, BoidState
    >();

    bench_wide.run("Flecs (Archetype Table)", [&] {
      f_query.each([](Position &pos, Velocity &vel, const Acceleration &acc,
                      Rotation &rot, const Scale &s, const Mass &m,
                      const Radius &r, BoidState &state) {
        vel.vx += acc.ax * m.inv_mass * 0.016f;
        vel.vy += acc.ay * m.inv_mass * 0.016f;
        pos.x += vel.vx * s.sx * 0.016f;
        pos.y += vel.vy * s.sy * 0.016f;
        rot.angle += 0.01f;
        if (pos.x < r.r) state.flags |= 2;
      });
    });

    entt::registry registry;
    for (size_t i = 0; i < COUNT_500K; ++i) {
      const auto e = registry.create();
      registry.emplace<Position>(e, 0.0f, 0.0f);
      registry.emplace<Velocity>(e, 1.0f, 2.0f);
      registry.emplace<Acceleration>(e, 0.1f, 0.2f);
      registry.emplace<Rotation>(e, 0.5f);
      registry.emplace<Scale>(e, 1.0f, 1.0f);
      registry.emplace<Mass>(e, 2.0f);
      registry.emplace<Radius>(e, 5.0f);
      registry.emplace<BoidState>(e, 1u, 10u);
    }
    auto view = registry.view<
        Position, Velocity, const Acceleration, Rotation, const Scale,
        const Mass, const Radius, BoidState
    >();

    bench_wide.run("EnTT (8 Sparse Pools)", [&] {
      view.each([](Position &pos, Velocity &vel, const Acceleration &acc,
                   Rotation &rot, const Scale &s, const Mass &m,
                   const Radius &r, BoidState &state) {
        vel.vx += acc.ax * m.inv_mass * 0.016f;
        vel.vy += acc.ay * m.inv_mass * 0.016f;
        pos.x += vel.vx * s.sx * 0.016f;
        pos.y += vel.vy * s.sy * 0.016f;
        rot.angle += 0.01f;
        if (pos.x < r.r) state.flags |= 2;
      });
    });
  }

  // ---------------------------------------------------------------------------
  // SECTION III: SPATIAL SORTING & CACHE LOCALITY (500,000 ENTITIES)
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section III: Spatial Sorting & Cache Locality (500k)...\n\n";
  {
    ankerl::nanobench::Bench bench_sort;
    bench_sort.warmup(2).minEpochIterations(5);

    // 5A. In-Place Entity Sorting Speed (500k Entities sorted by Position.x)
    bench_sort.title("5. In-Place Entity Sorting Speed (500k Entities)")
        .run("arche-ecs (sort in-place cycle chasing)", [&] {
          arche::World world;
          std::vector<Position> positions(COUNT_500K);
          std::mt19937 rng(1337);
          std::uniform_real_distribution<float> dist(0.0f, 10000.0f);
          for (size_t i = 0; i < COUNT_500K; ++i) {
            positions[i] = Position{dist(rng), dist(rng)};
          }
          std::vector<arche::Entity> ents(COUNT_500K);
          world.spawn_batch(
              COUNT_500K, std::span<arche::Entity>(ents), positions.data()
          );

          arche::Archetype *arch = world.get_record(ents[0]).archetype;
          world.sort<Position>(arch, [](const Position &a, const Position &b) {
            return a.x < b.x;
          });
        })
        .run("EnTT (registry.sort in-place dense pool)", [&] {
          entt::registry registry;
          std::mt19937 rng(1337);
          std::uniform_real_distribution<float> dist(0.0f, 10000.0f);
          for (size_t i = 0; i < COUNT_500K; ++i) {
            const auto e = registry.create();
            registry.emplace<Position>(e, dist(rng), dist(rng));
          }
          registry.sort<Position>([](const auto &l, const auto &r) {
            return l.x < r.x;
          });
        });

    // 5B. Spatial Cache Locality Speedup: Query Shuffled vs. Sorted
    {
      arche::World world;
      std::vector<Position> positions(COUNT_500K);
      std::vector<Velocity> velocities(COUNT_500K, Velocity{1.0f, 2.0f});
      std::mt19937 rng(42);
      std::uniform_real_distribution<float> dist(0.0f, 10000.0f);
      for (size_t i = 0; i < COUNT_500K; ++i) {
        positions[i] = Position{dist(rng), dist(rng)};
      }
      std::vector<arche::Entity> ents(COUNT_500K);
      world.spawn_batch(
          COUNT_500K, std::span<arche::Entity>(ents), positions.data(), velocities.data()
      );
      auto query = world.query<Position, const Velocity>();

      bench_sort.title("6. Spatial Locality Query Speedup (500k Entities)")
          .run("arche-ecs (Before Sort: Random Coordinates)", [&] {
            query.for_each_chunk([](uint32_t count, Position *p, const Velocity *v) {
              for (uint32_t i = 0; i < count; ++i) {
                p[i].x += v[i].vx * 0.016f;
                p[i].y += v[i].vy * 0.016f;
              }
            });
          });

      // Now sort entities in-place by X coordinate
      arche::Archetype *arch = world.get_record(ents[0]).archetype;
      world.sort<Position>(arch, [](const Position &a, const Position &b) {
        return a.x < b.x;
      });

      bench_sort.run("arche-ecs (After Sort: Spatially Monotonic)", [&] {
        query.for_each_chunk([](uint32_t count, Position *p, const Velocity *v) {
          for (uint32_t i = 0; i < count; ++i) {
            p[i].x += v[i].vx * 0.016f;
            p[i].y += v[i].vy * 0.016f;
          }
        });
      });
    }
  }

  // ---------------------------------------------------------------------------
  // SECTION IV: DEEP FRAGMENTATION (20 TABLES) & EXCLUSION FILTERS
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section IV: Deep Fragmentation (20 Archetypes) & Filters...\n\n";
  {
    ankerl::nanobench::Bench bench_frag;
    bench_frag.warmup(5).minEpochIterations(20);

    // 7A. 200,000 Entities across 20 Distinct Archetypes
    {
      constexpr size_t PER_TABLE = COUNT_200K / 20; // 10k entities per table
      arche::World world;
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag0{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag1{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag2{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag3{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag4{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag5{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag6{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag7{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag8{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag9{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag10{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag11{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag12{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag13{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag14{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag15{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag16{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag17{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag18{});
      world.spawn_batch(PER_TABLE, Position{}, Velocity{}, Tag19{});
      auto query = world.query<Position, const Velocity>();

      bench_frag.title("7. Deep Fragmentation (200k Entities across 20 Archetypes)")
          .run("arche-ecs (QueryCache)", [&] {
            query.for_each_chunk([](uint32_t count, Position *p, const Velocity *v) {
              for (uint32_t i = 0; i < count; ++i) {
                p[i].x += v[i].vx * 0.016f;
                p[i].y += v[i].vy * 0.016f;
              }
            });
          });

      flecs::world ecs;
      for (size_t i = 0; i < PER_TABLE; ++i) {
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag0>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag1>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag2>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag3>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag4>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag5>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag6>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag7>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag8>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag9>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag10>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag11>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag12>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag13>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag14>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag15>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag16>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag17>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag18>({});
        ecs.entity().set<Position>({}).set<Velocity>({}).set<Tag19>({});
      }
      auto f_query = ecs.query<Position, const Velocity>();

      bench_frag.run("Flecs (Archetype Table)", [&] {
        f_query.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });

      entt::registry registry;
      for (size_t i = 0; i < PER_TABLE; ++i) {
        auto e0 = registry.create(); registry.emplace<Position>(e0); registry.emplace<Velocity>(e0); registry.emplace<Tag0>(e0);
        auto e1 = registry.create(); registry.emplace<Position>(e1); registry.emplace<Velocity>(e1); registry.emplace<Tag1>(e1);
        auto e2 = registry.create(); registry.emplace<Position>(e2); registry.emplace<Velocity>(e2); registry.emplace<Tag2>(e2);
        auto e3 = registry.create(); registry.emplace<Position>(e3); registry.emplace<Velocity>(e3); registry.emplace<Tag3>(e3);
        auto e4 = registry.create(); registry.emplace<Position>(e4); registry.emplace<Velocity>(e4); registry.emplace<Tag4>(e4);
        auto e5 = registry.create(); registry.emplace<Position>(e5); registry.emplace<Velocity>(e5); registry.emplace<Tag5>(e5);
        auto e6 = registry.create(); registry.emplace<Position>(e6); registry.emplace<Velocity>(e6); registry.emplace<Tag6>(e6);
        auto e7 = registry.create(); registry.emplace<Position>(e7); registry.emplace<Velocity>(e7); registry.emplace<Tag7>(e7);
        auto e8 = registry.create(); registry.emplace<Position>(e8); registry.emplace<Velocity>(e8); registry.emplace<Tag8>(e8);
        auto e9 = registry.create(); registry.emplace<Position>(e9); registry.emplace<Velocity>(e9); registry.emplace<Tag9>(e9);
        auto e10 = registry.create(); registry.emplace<Position>(e10); registry.emplace<Velocity>(e10); registry.emplace<Tag10>(e10);
        auto e11 = registry.create(); registry.emplace<Position>(e11); registry.emplace<Velocity>(e11); registry.emplace<Tag11>(e11);
        auto e12 = registry.create(); registry.emplace<Position>(e12); registry.emplace<Velocity>(e12); registry.emplace<Tag12>(e12);
        auto e13 = registry.create(); registry.emplace<Position>(e13); registry.emplace<Velocity>(e13); registry.emplace<Tag13>(e13);
        auto e14 = registry.create(); registry.emplace<Position>(e14); registry.emplace<Velocity>(e14); registry.emplace<Tag14>(e14);
        auto e15 = registry.create(); registry.emplace<Position>(e15); registry.emplace<Velocity>(e15); registry.emplace<Tag15>(e15);
        auto e16 = registry.create(); registry.emplace<Position>(e16); registry.emplace<Velocity>(e16); registry.emplace<Tag16>(e16);
        auto e17 = registry.create(); registry.emplace<Position>(e17); registry.emplace<Velocity>(e17); registry.emplace<Tag17>(e17);
        auto e18 = registry.create(); registry.emplace<Position>(e18); registry.emplace<Velocity>(e18); registry.emplace<Tag18>(e18);
        auto e19 = registry.create(); registry.emplace<Position>(e19); registry.emplace<Velocity>(e19); registry.emplace<Tag19>(e19);
      }
      auto view = registry.view<Position, const Velocity>();

      bench_frag.run("EnTT (Standard View)", [&] {
        view.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });
    }

    // 7B. Exclusion Filtering: Without<FrozenTag> on 100k Entities
    {
      arche::World world;
      world.spawn_batch(COUNT_100K / 2, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
      world.spawn_batch(COUNT_100K / 2, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}, FrozenTag{});
      auto query = world.query<Position, const Velocity>(arche::Without<FrozenTag>{});

      bench_frag.title("8. Exclusion Filter Query (Without<FrozenTag>, 100k Entities)")
          .run("arche-ecs (Query Bitmask Exclusion)", [&] {
            query.for_each_chunk([](uint32_t count, Position *p, const Velocity *v) {
              for (uint32_t i = 0; i < count; ++i) {
                p[i].x += v[i].vx * 0.016f;
                p[i].y += v[i].vy * 0.016f;
              }
            });
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
      }
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f}).set<FrozenTag>({});
      }
      auto f_query = ecs.query_builder<Position, const Velocity>().without<FrozenTag>().build();

      bench_frag.run("Flecs (Table Query Filter)", [&] {
        f_query.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });

      entt::registry registry;
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        const auto e = registry.create();
        registry.emplace<Position>(e, 0.0f, 0.0f);
        registry.emplace<Velocity>(e, 1.0f, 2.0f);
      }
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        const auto e = registry.create();
        registry.emplace<Position>(e, 0.0f, 0.0f);
        registry.emplace<Velocity>(e, 1.0f, 2.0f);
        registry.emplace<FrozenTag>(e);
      }
      auto view = registry.view<Position, const Velocity>(entt::exclude<FrozenTag>);

      bench_frag.run("EnTT (View with Exclude)", [&] {
        view.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });
    }
  }

  // ---------------------------------------------------------------------------
  // SECTION V: LIFECYCLE, MUTATION & RANDOM ACCESS
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section V: Lifecycle, Mutation & Random Access...\n\n";
  {
    ankerl::nanobench::Bench bench_life;
    bench_life.warmup(5).minEpochIterations(25);

    // 8. Random Component Lookup (100k Shuffled Entity Handles)
    {
      std::mt19937 rng(42);

      arche::World world;
      std::vector<arche::Entity> arche_entities(COUNT_100K);
      for (size_t i = 0; i < COUNT_100K; ++i) {
        arche_entities[i] = world.spawn(Position{static_cast<float>(i), 0.0f});
      }
      std::vector<arche::Entity> shuffled_arche = arche_entities;
      std::shuffle(shuffled_arche.begin(), shuffled_arche.end(), rng);

      float sum_arche = 0.0f;
      bench_life.title("9. Random Access (100k Component Lookups)")
          .run("arche-ecs (SlotMap Indirection)", [&] {
            for (const auto &e : shuffled_arche) {
              sum_arche += world.get<Position>(e).x;
            }
            ankerl::nanobench::doNotOptimizeAway(sum_arche);
          });

      flecs::world ecs;
      std::vector<flecs::entity> flecs_entities(COUNT_100K);
      for (size_t i = 0; i < COUNT_100K; ++i) {
        flecs_entities[i] = ecs.entity().set<Position>({static_cast<float>(i), 0.0f});
      }
      std::vector<flecs::entity> shuffled_flecs = flecs_entities;
      std::shuffle(shuffled_flecs.begin(), shuffled_flecs.end(), rng);

      float sum_flecs = 0.0f;
      bench_life.run("Flecs (Entity Index)", [&] {
        for (const auto &e : shuffled_flecs) {
          sum_flecs += e.get<Position>()->x;
        }
        ankerl::nanobench::doNotOptimizeAway(sum_flecs);
      });

      entt::registry registry;
      std::vector<entt::entity> entt_entities(COUNT_100K);
      for (size_t i = 0; i < COUNT_100K; ++i) {
        entt_entities[i] = registry.create();
        registry.emplace<Position>(entt_entities[i], static_cast<float>(i), 0.0f);
      }
      std::vector<entt::entity> shuffled_entt = entt_entities;
      std::shuffle(shuffled_entt.begin(), shuffled_entt.end(), rng);

      float sum_entt = 0.0f;
      bench_life.run("EnTT (Direct Sparse Set Lookup)", [&] {
        for (const auto &e : shuffled_entt) {
          sum_entt += registry.get<Position>(e).x;
        }
        ankerl::nanobench::doNotOptimizeAway(sum_entt);
      });
    }

    // 9. Entity Creation: Bulk vs Single (20,000 Entities)
    {
      bench_life.title("10. Entity Creation (20k Entities: Pos + Vel)")
          .run("arche-ecs (Chunked Bulk Spawn)", [&] {
            arche::World world;
            world.spawn_batch(
                MUTATION_COUNT, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}
            );
          })
          .run("arche-ecs (Single spawn loop)", [&] {
            arche::World world;
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
            }
          })
          .run("EnTT (Registry Emplace loop)", [&] {
            entt::registry registry;
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              const auto e = registry.create();
              registry.emplace<Position>(e, 0.0f, 0.0f);
              registry.emplace<Velocity>(e, 1.0f, 2.0f);
            }
          })
          .run("Flecs (Table Insertion loop)", [&] {
            flecs::world ecs;
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
            }
          });
    }

    // 10. Structural Mutation: Add Component (20,000 Entities)
    {
      bench_life.title("11. Structural Mutation: Add Component (20k Entities)")
          .run("EnTT (Sparse Pool Append)", [&] {
            entt::registry registry;
            std::vector<entt::entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = registry.create();
              registry.emplace<Position>(entities[i], 0.0f, 0.0f);
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              registry.emplace<ExtraData>(entities[i]);
            }
          })
          .run("arche-ecs (Archetype Migration)", [&] {
            arche::World world;
            std::vector<arche::Entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = world.spawn(Position{0.0f, 0.0f});
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              world.add(entities[i], ExtraData{});
            }
          })
          .run("Flecs (Table Transition)", [&] {
            flecs::world ecs;
            std::vector<flecs::entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = ecs.entity().set<Position>({0.0f, 0.0f});
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i].set<ExtraData>({});
            }
          });
    }

    // 11. Entity Destruction (20,000 Entities Despawn)
    {
      bench_life.title("12. Entity Destruction (20k Entities Despawn)")
          .run("arche-ecs (Chunk Swap-Remove)", [&] {
            arche::World world;
            std::vector<arche::Entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              world.despawn(entities[i]);
            }
          })
          .run("EnTT (Sparse Pool Swap-Pop)", [&] {
            entt::registry registry;
            std::vector<entt::entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = registry.create();
              registry.emplace<Position>(entities[i], 0.0f, 0.0f);
              registry.emplace<Velocity>(entities[i], 1.0f, 2.0f);
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              registry.destroy(entities[i]);
            }
          })
          .run("Flecs (Table Swap-Remove)", [&] {
            flecs::world ecs;
            std::vector<flecs::entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i].destruct();
            }
          });
    }
  }

  std::cout << "\n==============================================================================\n";
  std::cout << "  Comprehensive 2M Comparative Benchmark Suite Completed Successfully!        \n";
  std::cout << "==============================================================================\n";
  return 0;
}
#else
#include <iostream>
int main() {
  std::cout << "Comparative benchmarks require -DARCHE_BUILD_COMPARISON_BENCHMARKS=ON\n";
  return 0;
}
#endif
