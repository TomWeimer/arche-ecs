import arche;

#if __has_include(<nanobench.h>) && __has_include(<entt/entt.hpp>) && __has_include(<flecs.h>)
#include <entt/entt.hpp>
#include <flecs.h>
#include <nanobench.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <random>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

// =============================================================================
// Memory & Heap Allocation Tracking Telemetry
// =============================================================================

static std::atomic<bool> g_track_allocations{false};
static std::atomic<size_t> g_alloc_count{0};
static std::atomic<size_t> g_alloc_bytes{0};

void *operator new(std::size_t size) {
  if (g_track_allocations.load(std::memory_order_relaxed)) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
  }
  void *ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void *operator new[](std::size_t size) {
  if (g_track_allocations.load(std::memory_order_relaxed)) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
  }
  void *ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void operator delete(void *ptr) noexcept {
  std::free(ptr);
}

void operator delete[](void *ptr) noexcept {
  std::free(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept {
  std::free(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept {
  std::free(ptr);
}

static ecs_os_api_malloc_t g_orig_flecs_malloc = nullptr;
static ecs_os_api_calloc_t g_orig_flecs_calloc = nullptr;
static ecs_os_api_realloc_t g_orig_flecs_realloc = nullptr;

static void *flecs_malloc_hook(ecs_size_t size) {
  if (g_track_allocations.load(std::memory_order_relaxed)) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(static_cast<size_t>(size), std::memory_order_relaxed);
  }
  return g_orig_flecs_malloc(size);
}

static void *flecs_calloc_hook(ecs_size_t size) {
  if (g_track_allocations.load(std::memory_order_relaxed)) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(static_cast<size_t>(size), std::memory_order_relaxed);
  }
  return g_orig_flecs_calloc(size);
}

static void *flecs_realloc_hook(void *ptr, ecs_size_t size) {
  if (g_track_allocations.load(std::memory_order_relaxed)) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(static_cast<size_t>(size), std::memory_order_relaxed);
  }
  return g_orig_flecs_realloc(ptr, size);
}

inline void init_flecs_alloc_hooks() {
  if (!g_orig_flecs_malloc) {
    g_orig_flecs_malloc = ecs_os_api.malloc_;
    g_orig_flecs_calloc = ecs_os_api.calloc_;
    g_orig_flecs_realloc = ecs_os_api.realloc_;
    ecs_os_api.malloc_ = flecs_malloc_hook;
    ecs_os_api.calloc_ = flecs_calloc_hook;
    ecs_os_api.realloc_ = flecs_realloc_hook;
  }
}

inline size_t get_current_rss_bytes() {
#if defined(__APPLE__)
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(
          mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count
      ) == KERN_SUCCESS) {
    return info.resident_size;
  }
  return 0;
#elif defined(__linux__)
  long rss = 0L;
  FILE *fp = fopen("/proc/self/statm", "r");
  if (fp) {
    if (fscanf(fp, "%*s%ld", &rss) == 1) {
      fclose(fp);
      return static_cast<size_t>(rss) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
    }
    fclose(fp);
  }
  return 0;
#else
  return 0;
#endif
}

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

struct Health {
  float hp{100.0f};
};

struct Damage {
  float amount{0.0f};
};

struct SpatialGridCell {
  int32_t cell_x{0};
  int32_t cell_y{0};
};

struct TimeStepResource {
  float dt{0.016f};
  uint64_t frame{0};
};

struct AudioQueueResource {
  uint32_t sound_events{0};
};

// 20 distinct tag components for deep fragmentation testing
struct Tag0 {
  uint32_t id{0};
};
struct Tag1 {
  uint32_t id{1};
};
struct Tag2 {
  uint32_t id{2};
};
struct Tag3 {
  uint32_t id{3};
};
struct Tag4 {
  uint32_t id{4};
};
struct Tag5 {
  uint32_t id{5};
};
struct Tag6 {
  uint32_t id{6};
};
struct Tag7 {
  uint32_t id{7};
};
struct Tag8 {
  uint32_t id{8};
};
struct Tag9 {
  uint32_t id{9};
};
struct Tag10 {
  uint32_t id{10};
};
struct Tag11 {
  uint32_t id{11};
};
struct Tag12 {
  uint32_t id{12};
};
struct Tag13 {
  uint32_t id{13};
};
struct Tag14 {
  uint32_t id{14};
};
struct Tag15 {
  uint32_t id{15};
};
struct Tag16 {
  uint32_t id{16};
};
struct Tag17 {
  uint32_t id{17};
};
struct Tag18 {
  uint32_t id{18};
};
struct Tag19 {
  uint32_t id{19};
};

struct ExtraData {
  float values[4]{1.0f, 2.0f, 3.0f, 4.0f};
};

struct FullFrameSimulationPlugin {
  static constexpr std::string_view name() noexcept {
    return "FullFrameSimulationPlugin";
  }

  void build(arche::World &world, arche::PluginContext &ctx) const {
    world.resources.insert<TimeStepResource>(0.016f, 0ULL);
    world.resources.insert<AudioQueueResource>(0U);

    // 1. TimeStepSystem (Narrow)
    ctx.add_system(
        "TimeStepSystem",
        arche::Access{}.writes_resource<TimeStepResource>().build(),
        [](arche::SystemContext &ctx) {
          auto &ts = ctx.write_resource<TimeStepResource>();
          ts.frame++;
          ts.dt = 0.016f;
        },
        arche::SystemScale::Narrow
    );

    // 2. InputPollingSystem (Narrow)
    ctx.add_system(
        "InputPollingSystem",
        arche::Access{}.reads_resource<TimeStepResource>().build(),
        [](arche::SystemContext &ctx) {
          const auto &ts = ctx.read_resource<TimeStepResource>();
          ankerl::nanobench::doNotOptimizeAway(ts.frame);
        },
        arche::SystemScale::Narrow
    );

    // 3. AudioSyncSystem (Narrow)
    ctx.add_system(
        "AudioSyncSystem",
        arche::Access{}
            .reads_resource<TimeStepResource>()
            .writes_resource<AudioQueueResource>()
            .build(),
        [](arche::SystemContext &ctx) {
          auto &audio = ctx.write_resource<AudioQueueResource>();
          audio.sound_events = 0;
        },
        arche::SystemScale::Narrow
    );

    // 4. StatsLoggingSystem (Narrow)
    ctx.add_system(
        "StatsLoggingSystem",
        arche::Access{}.reads_resource<TimeStepResource>().build(),
        [](arche::SystemContext &ctx) {
          const auto &ts = ctx.read_resource<TimeStepResource>();
          ankerl::nanobench::doNotOptimizeAway(ts.frame);
        },
        arche::SystemScale::Narrow
    );

    // 5. GravitySystem (Wide)
    ctx.add_system(
        "GravitySystem",
        arche::Access{}
            .reads_resource<TimeStepResource>()
            .reads<Acceleration>()
            .writes<Velocity>()
            .build(),
        [](arche::SystemContext &ctx) {
          const auto &ts = ctx.read_resource<TimeStepResource>();
          const float dt = ts.dt;
          ctx.query<Velocity, const Acceleration>().each_parallel(
              [dt](uint32_t count, Velocity *vel, const Acceleration *acc) {
                for (uint32_t i = 0; i < count; ++i) {
                  vel[i].vx += acc[i].ax * dt;
                  vel[i].vy += acc[i].ay * dt;
                }
              }
          );
        },
        arche::SystemScale::Wide
    );

    // 6. MovementSystem (Wide)
    ctx.add_system(
        "MovementSystem",
        arche::Access{}
            .reads_resource<TimeStepResource>()
            .reads<Velocity>()
            .writes<Position>()
            .build(),
        [](arche::SystemContext &ctx) {
          const auto &ts = ctx.read_resource<TimeStepResource>();
          const float dt = ts.dt;
          ctx.query<Position, const Velocity>().each_parallel(
              [dt](uint32_t count, Position *pos, const Velocity *vel) {
                for (uint32_t i = 0; i < count; ++i) {
                  pos[i].x += vel[i].vx * dt;
                  pos[i].y += vel[i].vy * dt;
                }
              }
          );
        },
        arche::SystemScale::Wide
    );

    // 7. SpatialHashingSystem (Wide)
    ctx.add_system(
        "SpatialHashingSystem",
        arche::Access{}.reads<Position>().writes<SpatialGridCell>().build(),
        [](arche::SystemContext &ctx) {
          ctx.query<SpatialGridCell, const Position>().each_parallel(
              [](uint32_t count, SpatialGridCell *cell, const Position *pos) {
                for (uint32_t i = 0; i < count; ++i) {
                  cell[i].cell_x = static_cast<int32_t>(pos[i].x) / 64;
                  cell[i].cell_y = static_cast<int32_t>(pos[i].y) / 64;
                }
              }
          );
        },
        arche::SystemScale::Wide
    );

    // 8. DamageSystem (Wide)
    ctx.add_system(
        "DamageSystem",
        arche::Access{}.reads<Damage>().writes<Health>().build(),
        [](arche::SystemContext &ctx) {
          ctx.query<Health, const Damage>().each_parallel(
              [](uint32_t count, Health *hp, const Damage *dmg) {
                for (uint32_t i = 0; i < count; ++i) {
                  hp[i].hp -= dmg[i].amount;
                }
              }
          );
        },
        arche::SystemScale::Wide
    );

    // 9. HealthCleanupSystem (Narrow, deferred despawn + respawn)
    ctx.add_system(
        "HealthCleanupSystem",
        arche::Access{}.reads<Health>().build(),
        [](arche::SystemContext &ctx) {
          ctx.query<arche::Entity, const Health>().each_chunk(
              [&](uint32_t count,
                  arche::Entity *entities,
                  const Health *healths) {
                for (uint32_t i = 0; i < count; ++i) {
                  if (healths[i].hp <= 0.0f) {
                    ctx.cmd.despawn(entities[i]);
                    ctx.cmd.spawn(
                        Position{0.0f, 0.0f},
                        Velocity{1.0f, 1.0f},
                        Acceleration{0.0f, -9.81f},
                        SpatialGridCell{0, 0},
                        Health{100.0f},
                        Damage{100.0f}
                    );
                  }
                }
              }
          );
        },
        arche::SystemScale::Narrow
    );

    // 10. RenderPrepSystem (Narrow)
    ctx.add_system(
        "RenderPrepSystem",
        arche::Access{}.reads<Position>().reads<Health>().build(),
        [](arche::SystemContext &ctx) {
          float total_pos = 0.0f;
          ctx.query<const Position, const Health>().each_chunk(
              [&](uint32_t count, const Position *pos, const Health *hp) {
                for (uint32_t i = 0; i < count; ++i) {
                  if (hp[i].hp > 0.0f) {
                    total_pos += pos[i].x;
                  }
                }
              }
          );
          ankerl::nanobench::doNotOptimizeAway(total_pos);
        },
        arche::SystemScale::Narrow
    );
  }
};

int main() {
  std::cout << "==============================================================="
               "===============\n";
  std::cout << "  Comprehensive ECS Benchmark: arche-ecs vs. Flecs vs. EnTT "
               "(2M Scale Edition)\n";
  std::cout << "==============================================================="
               "===============\n\n";

  constexpr size_t COUNT_2M = 2'000'000;
  constexpr size_t COUNT_500K = 500'000;
  constexpr size_t COUNT_200K = 200'000;
  constexpr size_t COUNT_100K = 100'000;
  constexpr size_t MUTATION_COUNT = 20'000;

  // ---------------------------------------------------------------------------
  // SECTION I: EXTREME SCALE — 2,000,000 (2M) ENTITIES
  // ---------------------------------------------------------------------------
  std::cout << ">>> Running Section I: 2,000,000 (2M) Entity Benchmarks...\n\n";
  {
    ankerl::nanobench::Bench bench_2m;
    bench_2m.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(250)
    );

    // 1A. 2,000,000 Entities: Simple Streaming (Pos += Vel * dt)
    {
      using EnTTRegistry2M = entt::basic_registry<std::uint64_t>;

      arche::World world;
      world.spawn(
          {.count = COUNT_2M}, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}
      );
      auto query = world.query<Position, const Velocity>();

      bench_2m.title("1. 2M Entities: Simple Streaming (Pos + Vel)")
          .run("arche-ecs (Chunked SoA batch)", [&] {
            query.each_chunk([](uint32_t count, Position *pos, const Velocity *vel) {
              for (uint32_t i = 0; i < count; ++i) {
                pos[i].x += vel[i].vx * 0.016f;
                pos[i].y += vel[i].vy * 0.016f;
              }
            });
          })
          .run("arche-ecs (range-for idiomatic)", [&] {
            for (auto [pos, vel] : query) {
              pos.x += vel.vx * 0.016f;
              pos.y += vel.vy * 0.016f;
            }
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
      }
      auto f_query = ecs.query<Position, const Velocity>();

      bench_2m.run("Flecs (Table iter batch)", [&] {
        f_query.run([](flecs::iter &it) {
          while (it.next()) {
            auto pos = it.field<Position>(0);
            auto vel = it.field<const Velocity>(1);
            for (size_t i = 0; i < it.count(); ++i) {
              pos[i].x += vel[i].vx * 0.016f;
              pos[i].y += vel[i].vy * 0.016f;
            }
          }
        });
      });

      bench_2m.run("Flecs (each idiomatic)", [&] {
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
      world.spawn(
          {.count = COUNT_2M}, Position{500.0f, 500.0f}, Velocity{10.0f, 15.0f}
      );
      auto query = world.query<Position, Velocity>();

      bench_2m.title("2. 2M Entities: Realistic Swarm Physics (Euler + Bounds)")
          .run("arche-ecs (Chunked SoA batch)", [&] {
            query.each_chunk([](uint32_t count, Position *pos, Velocity *vel) {
              for (uint32_t i = 0; i < count; ++i) {
                pos[i].x += vel[i].vx * 0.016f;
                pos[i].y += vel[i].vy * 0.016f;
                if (pos[i].x < 0.0f || pos[i].x > 1920.0f)
                  vel[i].vx = -vel[i].vx;
                if (pos[i].y < 0.0f || pos[i].y > 1080.0f)
                  vel[i].vy = -vel[i].vy;
              }
            });
          })
          .run("arche-ecs (range-for idiomatic)", [&] {
            for (auto [pos, vel] : query) {
              pos.x += vel.vx * 0.016f;
              pos.y += vel.vy * 0.016f;
              if (pos.x < 0.0f || pos.x > 1920.0f)
                vel.vx = -vel.vx;
              if (pos.y < 0.0f || pos.y > 1080.0f)
                vel.vy = -vel.vy;
            }
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        ecs.entity()
            .set<Position>({500.0f, 500.0f})
            .set<Velocity>({10.0f, 15.0f});
      }
      auto f_query = ecs.query<Position, Velocity>();

      bench_2m.run("Flecs (Table iter batch)", [&] {
        f_query.run([](flecs::iter &it) {
          while (it.next()) {
            auto pos = it.field<Position>(0);
            auto vel = it.field<Velocity>(1);
            for (size_t i = 0; i < it.count(); ++i) {
              pos[i].x += vel[i].vx * 0.016f;
              pos[i].y += vel[i].vy * 0.016f;
              if (pos[i].x < 0.0f || pos[i].x > 1920.0f)
                vel[i].vx = -vel[i].vx;
              if (pos[i].y < 0.0f || pos[i].y > 1080.0f)
                vel[i].vy = -vel[i].vy;
            }
          }
        });
      });

      bench_2m.run("Flecs (each idiomatic)", [&] {
        f_query.each([](Position &pos, Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
          if (pos.x < 0.0f || pos.x > 1920.0f)
            vel.vx = -vel.vx;
          if (pos.y < 0.0f || pos.y > 1080.0f)
            vel.vy = -vel.vy;
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
          if (pos.x < 0.0f || pos.x > 1920.0f)
            vel.vx = -vel.vx;
          if (pos.y < 0.0f || pos.y > 1080.0f)
            vel.vy = -vel.vy;
        });
      });
    }

    // 1C. 2,000,000 Entities: Multi-Threaded Scaling (1, 2, 4, 8 Threads)
    {
      arche::World world;
      world.spawn(
          {.count = COUNT_2M}, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}
      );

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_2M; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
      }
      ecs.system<Position, const Velocity>()
          .multi_threaded()
          .each([](Position &pos, const Velocity &vel) {
            pos.x += vel.vx * 0.016f;
            pos.y += vel.vy * 0.016f;
          });

      bench_2m.title("3. 2M Entities: Multi-Threaded Scaling (1, 2, 4, 8 Threads)");

      const std::vector<uint32_t> thread_counts = {1, 2, 4, 8};

      for (uint32_t t : thread_counts) {
        world.set_threads(t);
        bench_2m.run("arche-ecs (" + std::to_string(t) + "-Thread query_parallel)", [&] {
          world.query<Position, const Velocity>().each_parallel(
              [](uint32_t count, Position *pos, const Velocity *vel) {
                for (uint32_t i = 0; i < count; ++i) {
                  pos[i].x += vel[i].vx * 0.016f;
                  pos[i].y += vel[i].vy * 0.016f;
                }
              }
          );
        });

        ecs.set_threads(static_cast<int32_t>(t));
        ecs.progress(); // Warmup stages
        bench_2m.run("Flecs (" + std::to_string(t) + "-Thread multi_threaded)", [&] {
          ecs.progress();
        });
      }
    }
  }

  // ---------------------------------------------------------------------------
  // SECTION II: COMPLEX ENTITY SIMULATION — 8 COMPONENTS (500,000 ENTITIES)
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section II: Wide 8-Component Entity Simulation "
               "(500k)...\n\n";
  {
    ankerl::nanobench::Bench bench_wide;
    bench_wide.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(250)
    );

    arche::World world;
    world.spawn(
        {.count = COUNT_500K},
        Position{0.0f, 0.0f},
        Velocity{1.0f, 2.0f},
        Acceleration{0.1f, 0.2f},
        Rotation{0.5f},
        Scale{1.0f, 1.0f},
        Mass{2.0f},
        Radius{5.0f},
        BoidState{1, 10}
    );
    auto query = world.query<
        Position,
        Velocity,
        const Acceleration,
        Rotation,
        const Scale,
        const Mass,
        const Radius,
        BoidState>();

    bench_wide.title("4. Wide Entity Simulation (500k Entities: 8 Components)")
        .run("arche-ecs (Chunked SoA)", [&] {
          query.each_chunk([](uint32_t count,
                              Position *pos,
                              Velocity *vel,
                              const Acceleration *acc,
                              Rotation *rot,
                              const Scale *s,
                              const Mass *m,
                              const Radius *r,
                              BoidState *state) {
            for (uint32_t i = 0; i < count; ++i) {
              vel[i].vx += acc[i].ax * m[i].inv_mass * 0.016f;
              vel[i].vy += acc[i].ay * m[i].inv_mass * 0.016f;
              pos[i].x += vel[i].vx * s[i].sx * 0.016f;
              pos[i].y += vel[i].vy * s[i].sy * 0.016f;
              rot[i].angle += 0.01f;
              if (pos[i].x < r[i].r)
                state[i].flags |= 2;
            }
          });
        })
        .run("arche-ecs (range-for idiomatic)", [&] {
          for (auto [pos, vel, acc, rot, s, m, r, state] : query) {
            vel.vx += acc.ax * m.inv_mass * 0.016f;
            vel.vy += acc.ay * m.inv_mass * 0.016f;
            pos.x += vel.vx * s.sx * 0.016f;
            pos.y += vel.vy * s.sy * 0.016f;
            rot.angle += 0.01f;
            if (pos.x < r.r)
              state.flags |= 2;
          }
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
        Position,
        Velocity,
        const Acceleration,
        Rotation,
        const Scale,
        const Mass,
        const Radius,
        BoidState>();

    bench_wide.run("Flecs (Table iter batch)", [&] {
      f_query.run([](flecs::iter &it) {
        while (it.next()) {
          auto pos = it.field<Position>(0);
          auto vel = it.field<Velocity>(1);
          auto acc = it.field<const Acceleration>(2);
          auto rot = it.field<Rotation>(3);
          auto s = it.field<const Scale>(4);
          auto m = it.field<const Mass>(5);
          auto r = it.field<const Radius>(6);
          auto state = it.field<BoidState>(7);
          for (size_t i = 0; i < it.count(); ++i) {
            vel[i].vx += acc[i].ax * m[i].inv_mass * 0.016f;
            vel[i].vy += acc[i].ay * m[i].inv_mass * 0.016f;
            pos[i].x += vel[i].vx * s[i].sx * 0.016f;
            pos[i].y += vel[i].vy * s[i].sy * 0.016f;
            rot[i].angle += 0.01f;
            if (pos[i].x < r[i].r)
              state[i].flags |= 2;
          }
        }
      });
    });

    bench_wide.run("Flecs (each idiomatic)", [&] {
      f_query.each([](Position &pos,
                      Velocity &vel,
                      const Acceleration &acc,
                      Rotation &rot,
                      const Scale &s,
                      const Mass &m,
                      const Radius &r,
                      BoidState &state) {
        vel.vx += acc.ax * m.inv_mass * 0.016f;
        vel.vy += acc.ay * m.inv_mass * 0.016f;
        pos.x += vel.vx * s.sx * 0.016f;
        pos.y += vel.vy * s.sy * 0.016f;
        rot.angle += 0.01f;
        if (pos.x < r.r)
          state.flags |= 2;
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
        Position,
        Velocity,
        const Acceleration,
        Rotation,
        const Scale,
        const Mass,
        const Radius,
        BoidState>();

    bench_wide.run("EnTT (8 Sparse Pools)", [&] {
      view.each([](Position &pos,
                   Velocity &vel,
                   const Acceleration &acc,
                   Rotation &rot,
                   const Scale &s,
                   const Mass &m,
                   const Radius &r,
                   BoidState &state) {
        vel.vx += acc.ax * m.inv_mass * 0.016f;
        vel.vy += acc.ay * m.inv_mass * 0.016f;
        pos.x += vel.vx * s.sx * 0.016f;
        pos.y += vel.vy * s.sy * 0.016f;
        rot.angle += 0.01f;
        if (pos.x < r.r)
          state.flags |= 2;
      });
    });
  }

  // ---------------------------------------------------------------------------
  // SECTION III: SPATIAL SORTING & CACHE LOCALITY (500,000 ENTITIES)
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section III: Spatial Sorting & Cache Locality "
               "(500k)...\n\n";

  struct PositionSorterPlugin {
    static std::string_view name() { return "PositionSorterPlugin"; }
    void build(arche::World &, arche::PluginContext &ctx) {
      ctx.add_sort_stage<Position>(
          "PositionSort",
          [](const arche::SystemContext &) { return true; },
          [](arche::ColumnView<Position> columns, std::vector<uint32_t> &out_perm) {
            struct Item {
              float x;
              uint32_t idx;
            };
            std::vector<Item> items;
            items.reserve(columns.total_count());
            uint32_t gid = 0;
            for (std::span<const Position> span : columns) {
              for (const auto &p : span) {
                items.push_back({p.x, gid++});
              }
            }
            std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
              return a.x < b.x;
            });
            out_perm.resize(items.size());
            for (size_t i = 0; i < items.size(); ++i) {
              out_perm[i] = items[i].idx;
            }
          }
      );
    }
  };

  {
    ankerl::nanobench::Bench bench_sort;
    bench_sort.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(250)
    );

    // 5A. In-Place Entity Sorting Speed (500k Entities sorted by Position.x)
    bench_sort.title("5. In-Place Entity Sorting Speed (500k Entities)")
        .run(
            "arche-ecs (sort in-place cycle chasing)",
            [&] {
              arche::World world;
              std::vector<Position> positions(COUNT_500K);
              std::mt19937 rng(1337);
              std::uniform_real_distribution<float> dist(0.0f, 10000.0f);
              for (size_t i = 0; i < COUNT_500K; ++i) {
                positions[i] = Position{dist(rng), dist(rng)};
              }
              std::vector<arche::Entity> ents(COUNT_500K);
              world.spawn(
                  {.count = COUNT_500K},
                  std::span<arche::Entity>(ents),
                  positions.data()
              );

              world.add_plugin<PositionSorterPlugin>();
              world.run();
            }
        )
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
      world.spawn(
          {.count = COUNT_500K},
          std::span<arche::Entity>(ents),
          positions.data(),
          velocities.data()
      );
      auto query = world.query<Position, const Velocity>();

      bench_sort.title("6. Spatial Locality Query Speedup (500k Entities)")
          .run("arche-ecs (Before Sort: Random Coordinates)", [&] {
            query.each_chunk(
                [](uint32_t count, Position *p, const Velocity *v) {
                  for (uint32_t i = 0; i < count; ++i) {
                    p[i].x += v[i].vx * 0.016f;
                    p[i].y += v[i].vy * 0.016f;
                  }
                }
            );
          });

      // Now sort entities in-place by X coordinate
      world.add_plugin<PositionSorterPlugin>();
      world.run();

      bench_sort.run("arche-ecs (After Sort: Spatially Monotonic)", [&] {
        query.each_chunk([](uint32_t count, Position *p, const Velocity *v) {
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
  std::cout << "\n>>> Running Section IV: Deep Fragmentation (20 Archetypes) & "
               "Filters...\n\n";
  {
    ankerl::nanobench::Bench bench_frag;
    bench_frag.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(250)
    );

    // 7A. 200,000 Entities across 20 Distinct Archetypes
    {
      constexpr size_t PER_TABLE = COUNT_200K / 20; // 10k entities per table
      arche::World world;
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag0{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag1{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag2{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag3{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag4{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag5{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag6{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag7{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag8{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag9{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag10{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag11{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag12{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag13{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag14{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag15{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag16{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag17{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag18{});
      world.spawn({.count = PER_TABLE}, Position{}, Velocity{}, Tag19{});
      auto query = world.query<Position, const Velocity>();

      bench_frag
          .title("7. Deep Fragmentation (200k Entities across 20 Archetypes)")
          .run("arche-ecs (QueryCache)", [&] {
            query.each_chunk(
                [](uint32_t count, Position *p, const Velocity *v) {
                  for (uint32_t i = 0; i < count; ++i) {
                    p[i].x += v[i].vx * 0.016f;
                    p[i].y += v[i].vy * 0.016f;
                  }
                }
            );
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

      bench_frag.run("Flecs (Table iter batch)", [&] {
        f_query.run([](flecs::iter &it) {
          while (it.next()) {
            auto pos = it.field<Position>(0);
            auto vel = it.field<const Velocity>(1);
            for (size_t i = 0; i < it.count(); ++i) {
              pos[i].x += vel[i].vx * 0.016f;
              pos[i].y += vel[i].vy * 0.016f;
            }
          }
        });
      });

      bench_frag.run("Flecs (each idiomatic)", [&] {
        f_query.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
      });

      entt::registry registry;
      for (size_t i = 0; i < PER_TABLE; ++i) {
        auto e0 = registry.create();
        registry.emplace<Position>(e0);
        registry.emplace<Velocity>(e0);
        registry.emplace<Tag0>(e0);
        auto e1 = registry.create();
        registry.emplace<Position>(e1);
        registry.emplace<Velocity>(e1);
        registry.emplace<Tag1>(e1);
        auto e2 = registry.create();
        registry.emplace<Position>(e2);
        registry.emplace<Velocity>(e2);
        registry.emplace<Tag2>(e2);
        auto e3 = registry.create();
        registry.emplace<Position>(e3);
        registry.emplace<Velocity>(e3);
        registry.emplace<Tag3>(e3);
        auto e4 = registry.create();
        registry.emplace<Position>(e4);
        registry.emplace<Velocity>(e4);
        registry.emplace<Tag4>(e4);
        auto e5 = registry.create();
        registry.emplace<Position>(e5);
        registry.emplace<Velocity>(e5);
        registry.emplace<Tag5>(e5);
        auto e6 = registry.create();
        registry.emplace<Position>(e6);
        registry.emplace<Velocity>(e6);
        registry.emplace<Tag6>(e6);
        auto e7 = registry.create();
        registry.emplace<Position>(e7);
        registry.emplace<Velocity>(e7);
        registry.emplace<Tag7>(e7);
        auto e8 = registry.create();
        registry.emplace<Position>(e8);
        registry.emplace<Velocity>(e8);
        registry.emplace<Tag8>(e8);
        auto e9 = registry.create();
        registry.emplace<Position>(e9);
        registry.emplace<Velocity>(e9);
        registry.emplace<Tag9>(e9);
        auto e10 = registry.create();
        registry.emplace<Position>(e10);
        registry.emplace<Velocity>(e10);
        registry.emplace<Tag10>(e10);
        auto e11 = registry.create();
        registry.emplace<Position>(e11);
        registry.emplace<Velocity>(e11);
        registry.emplace<Tag11>(e11);
        auto e12 = registry.create();
        registry.emplace<Position>(e12);
        registry.emplace<Velocity>(e12);
        registry.emplace<Tag12>(e12);
        auto e13 = registry.create();
        registry.emplace<Position>(e13);
        registry.emplace<Velocity>(e13);
        registry.emplace<Tag13>(e13);
        auto e14 = registry.create();
        registry.emplace<Position>(e14);
        registry.emplace<Velocity>(e14);
        registry.emplace<Tag14>(e14);
        auto e15 = registry.create();
        registry.emplace<Position>(e15);
        registry.emplace<Velocity>(e15);
        registry.emplace<Tag15>(e15);
        auto e16 = registry.create();
        registry.emplace<Position>(e16);
        registry.emplace<Velocity>(e16);
        registry.emplace<Tag16>(e16);
        auto e17 = registry.create();
        registry.emplace<Position>(e17);
        registry.emplace<Velocity>(e17);
        registry.emplace<Tag17>(e17);
        auto e18 = registry.create();
        registry.emplace<Position>(e18);
        registry.emplace<Velocity>(e18);
        registry.emplace<Tag18>(e18);
        auto e19 = registry.create();
        registry.emplace<Position>(e19);
        registry.emplace<Velocity>(e19);
        registry.emplace<Tag19>(e19);
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
      world.spawn(
          {.count = COUNT_100K / 2}, Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}
      );
      world.spawn(
          {.count = COUNT_100K / 2},
          Position{0.0f, 0.0f},
          Velocity{1.0f, 2.0f},
          FrozenTag{}
      );
      auto query = world.query<Position, const Velocity>(
          arche::Without<FrozenTag>{}
      );

      bench_frag
          .title(
              "8. Exclusion Filter Query (Without<FrozenTag>, 100k Entities)"
          )
          .run("arche-ecs (Query Bitmask Exclusion)", [&] {
            query.each_chunk(
                [](uint32_t count, Position *p, const Velocity *v) {
                  for (uint32_t i = 0; i < count; ++i) {
                    p[i].x += v[i].vx * 0.016f;
                    p[i].y += v[i].vy * 0.016f;
                  }
                }
            );
          });

      flecs::world ecs;
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 2.0f});
      }
      for (size_t i = 0; i < COUNT_100K / 2; ++i) {
        ecs.entity()
            .set<Position>({0.0f, 0.0f})
            .set<Velocity>({1.0f, 2.0f})
            .set<FrozenTag>({});
      }
      auto f_query = ecs.query_builder<Position, const Velocity>()
                         .without<FrozenTag>()
                         .build();

      bench_frag.run("Flecs (Table iter batch)", [&] {
        f_query.run([](flecs::iter &it) {
          while (it.next()) {
            auto pos = it.field<Position>(0);
            auto vel = it.field<const Velocity>(1);
            for (size_t i = 0; i < it.count(); ++i) {
              pos[i].x += vel[i].vx * 0.016f;
              pos[i].y += vel[i].vy * 0.016f;
            }
          }
        });
      });

      bench_frag.run("Flecs (each idiomatic)", [&] {
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
      auto view = registry.view<Position, const Velocity>(
          entt::exclude<FrozenTag>
      );

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
  std::cout
      << "\n>>> Running Section V: Lifecycle, Mutation & Random Access...\n\n";
  {
    ankerl::nanobench::Bench bench_life;
    bench_life.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(250)
    );

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
        flecs_entities[i] = ecs.entity().set<Position>(
            {static_cast<float>(i), 0.0f}
        );
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
        registry.emplace<Position>(
            entt_entities[i], static_cast<float>(i), 0.0f
        );
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
          .run(
              "arche-ecs (Chunked Bulk Spawn)",
              [&] {
                arche::World world;
                world.spawn(
                    {.count = MUTATION_COUNT},
                    Position{0.0f, 0.0f},
                    Velocity{1.0f, 2.0f}
                );
              }
          )
          .run(
              "arche-ecs (Single spawn loop)",
              [&] {
                arche::World world;
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  world.spawn(Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f});
                }
              }
          )
          .run(
              "EnTT (Registry Emplace loop)",
              [&] {
                entt::registry registry;
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  const auto e = registry.create();
                  registry.emplace<Position>(e, 0.0f, 0.0f);
                  registry.emplace<Velocity>(e, 1.0f, 2.0f);
                }
              }
          )
          .run("Flecs (Table Insertion loop)", [&] {
            flecs::world ecs;
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              ecs.entity()
                  .set<Position>({0.0f, 0.0f})
                  .set<Velocity>({1.0f, 2.0f});
            }
          });
    }

    // 10. Structural Mutation: Add Component (20,000 Entities)
    {
      bench_life.title("11. Structural Mutation: Add Component (20k Entities)")
          .run(
              "EnTT (Sparse Pool Append)",
              [&] {
                entt::registry registry;
                std::vector<entt::entity> entities(MUTATION_COUNT);
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  entities[i] = registry.create();
                  registry.emplace<Position>(entities[i], 0.0f, 0.0f);
                }
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  registry.emplace<ExtraData>(entities[i]);
                }
              }
          )
          .run(
              "arche-ecs (Archetype Migration)",
              [&] {
                arche::World world;
                std::vector<arche::Entity> entities(MUTATION_COUNT);
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  entities[i] = world.spawn(Position{0.0f, 0.0f});
                }
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  world.add(entities[i], ExtraData{});
                }
              }
          )
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
          .run(
              "arche-ecs (Chunk Swap-Remove)",
              [&] {
                arche::World world;
                std::vector<arche::Entity> entities(MUTATION_COUNT);
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  entities[i] = world.spawn(
                      Position{0.0f, 0.0f}, Velocity{1.0f, 2.0f}
                  );
                }
                for (size_t i = 0; i < MUTATION_COUNT; ++i) {
                  world.despawn(entities[i]);
                }
              }
          )
          .run(
              "EnTT (Sparse Pool Swap-Pop)",
              [&] {
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
              }
          )
          .run("Flecs (Table Swap-Remove)", [&] {
            flecs::world ecs;
            std::vector<flecs::entity> entities(MUTATION_COUNT);
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i] = ecs.entity()
                                .set<Position>({0.0f, 0.0f})
                                .set<Velocity>({1.0f, 2.0f});
            }
            for (size_t i = 0; i < MUTATION_COUNT; ++i) {
              entities[i].destruct();
            }
          });
    }
  }

  // ---------------------------------------------------------------------------
  // SECTION VI: FULL-FRAME 10-SYSTEM GAME LOOP SIMULATION
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section VI: Full-Frame 10-System Game Loop Simulation (100k Entities)...\n\n";
  {
    ankerl::nanobench::Bench bench_frame;
    bench_frame.warmup(2).minEpochIterations(5).maxEpochTime(
        std::chrono::milliseconds(250)
    );

    constexpr size_t GAME_LOOP_ENTITIES = 100'000;
    constexpr size_t LETHAL_COUNT = 100;
    const size_t ALIVE_COUNT = GAME_LOOP_ENTITIES - LETHAL_COUNT;

    // 1. Setup arche-ecs 8 Threads
    arche::World arche_8t;
    arche_8t.set_threads(8);
    arche_8t.spawn(
        {.count = ALIVE_COUNT},
        Position{0.0f, 0.0f},
        Velocity{1.0f, 1.0f},
        Acceleration{0.0f, -9.81f},
        SpatialGridCell{0, 0},
        Health{100.0f},
        Damage{0.0f}
    );
    arche_8t.spawn(
        {.count = LETHAL_COUNT},
        Position{0.0f, 0.0f},
        Velocity{1.0f, 1.0f},
        Acceleration{0.0f, -9.81f},
        SpatialGridCell{0, 0},
        Health{100.0f},
        Damage{100.0f}
    );
    arche_8t.add_plugin<FullFrameSimulationPlugin>();

    // 2. Setup arche-ecs 1 Thread
    arche::World arche_1t;
    arche_1t.set_threads(1);
    arche_1t.spawn(
        {.count = ALIVE_COUNT},
        Position{0.0f, 0.0f},
        Velocity{1.0f, 1.0f},
        Acceleration{0.0f, -9.81f},
        SpatialGridCell{0, 0},
        Health{100.0f},
        Damage{0.0f}
    );
    arche_1t.spawn(
        {.count = LETHAL_COUNT},
        Position{0.0f, 0.0f},
        Velocity{1.0f, 1.0f},
        Acceleration{0.0f, -9.81f},
        SpatialGridCell{0, 0},
        Health{100.0f},
        Damage{100.0f}
    );
    arche_1t.add_plugin<FullFrameSimulationPlugin>();

    // 3. Setup Flecs 8 Threads
    flecs::world flecs_8t;
    flecs_8t.set_threads(8);
    flecs_8t.set<TimeStepResource>({0.016f, 0});
    flecs_8t.set<AudioQueueResource>({0});

    for (size_t i = 0; i < ALIVE_COUNT; ++i) {
      flecs_8t.entity()
          .set<Position>({0.0f, 0.0f})
          .set<Velocity>({1.0f, 1.0f})
          .set<Acceleration>({0.0f, -9.81f})
          .set<SpatialGridCell>({0, 0})
          .set<Health>({100.0f})
          .set<Damage>({0.0f});
    }
    for (size_t i = 0; i < LETHAL_COUNT; ++i) {
      flecs_8t.entity()
          .set<Position>({0.0f, 0.0f})
          .set<Velocity>({1.0f, 1.0f})
          .set<Acceleration>({0.0f, -9.81f})
          .set<SpatialGridCell>({0, 0})
          .set<Health>({100.0f})
          .set<Damage>({100.0f});
    }

    flecs_8t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          auto *ts = it.world().get_mut<TimeStepResource>();
          if (ts) {
            ts->frame++;
            ts->dt = 0.016f;
          }
        });
    flecs_8t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          const auto *ts = it.world().get<TimeStepResource>();
          if (ts) {
            ankerl::nanobench::doNotOptimizeAway(ts->frame);
          }
        });
    flecs_8t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          auto *audio = it.world().get_mut<AudioQueueResource>();
          if (audio) {
            audio->sound_events = 0;
          }
        });
    flecs_8t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          const auto *ts = it.world().get<TimeStepResource>();
          if (ts) {
            ankerl::nanobench::doNotOptimizeAway(ts->frame);
          }
        });
    flecs_8t.system<Velocity, const Acceleration>("GravitySystem")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .each([](Velocity &vel, const Acceleration &acc) {
          vel.vx += acc.ax * 0.016f;
          vel.vy += acc.ay * 0.016f;
        });
    flecs_8t.system<Position, const Velocity>("MovementSystem")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
    flecs_8t.system<SpatialGridCell, const Position>("SpatialHashingSystem")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .each([](SpatialGridCell &cell, const Position &pos) {
          cell.cell_x = static_cast<int32_t>(pos.x) / 64;
          cell.cell_y = static_cast<int32_t>(pos.y) / 64;
        });
    flecs_8t.system<Health, const Damage>("DamageSystem")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .each([](Health &hp, const Damage &dmg) {
          hp.hp -= dmg.amount;
        });
    flecs_8t.system<Health>("HealthCleanupSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::iter &it, size_t i, Health &hp) {
          if (hp.hp <= 0.0f) {
            it.entity(i).destruct();
            it.world().entity()
                .set<Position>({0.0f, 0.0f})
                .set<Velocity>({1.0f, 1.0f})
                .set<Acceleration>({0.0f, -9.81f})
                .set<SpatialGridCell>({0, 0})
                .set<Health>({100.0f})
                .set<Damage>({100.0f});
          }
        });
    flecs_8t.system<const Position, const Health>("RenderPrepSystem")
        .kind(flecs::OnStore)
        .multi_threaded()
        .each([](const Position &pos, const Health &hp) {
          if (hp.hp > 0.0f) {
            ankerl::nanobench::doNotOptimizeAway(pos.x);
          }
        });

    // 4. Setup Flecs 1 Thread
    flecs::world flecs_1t;
    flecs_1t.set_threads(1);
    flecs_1t.set<TimeStepResource>({0.016f, 0});
    flecs_1t.set<AudioQueueResource>({0});

    for (size_t i = 0; i < ALIVE_COUNT; ++i) {
      flecs_1t.entity()
          .set<Position>({0.0f, 0.0f})
          .set<Velocity>({1.0f, 1.0f})
          .set<Acceleration>({0.0f, -9.81f})
          .set<SpatialGridCell>({0, 0})
          .set<Health>({100.0f})
          .set<Damage>({0.0f});
    }
    for (size_t i = 0; i < LETHAL_COUNT; ++i) {
      flecs_1t.entity()
          .set<Position>({0.0f, 0.0f})
          .set<Velocity>({1.0f, 1.0f})
          .set<Acceleration>({0.0f, -9.81f})
          .set<SpatialGridCell>({0, 0})
          .set<Health>({100.0f})
          .set<Damage>({100.0f});
    }

    flecs_1t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          auto *ts = it.world().get_mut<TimeStepResource>();
          if (ts) {
            ts->frame++;
            ts->dt = 0.016f;
          }
        });
    flecs_1t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          const auto *ts = it.world().get<TimeStepResource>();
          if (ts) {
            ankerl::nanobench::doNotOptimizeAway(ts->frame);
          }
        });
    flecs_1t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          auto *audio = it.world().get_mut<AudioQueueResource>();
          if (audio) {
            audio->sound_events = 0;
          }
        });
    flecs_1t.system<>()
        .kind(flecs::PreUpdate)
        .run([](flecs::iter &it) {
          const auto *ts = it.world().get<TimeStepResource>();
          if (ts) {
            ankerl::nanobench::doNotOptimizeAway(ts->frame);
          }
        });
    flecs_1t.system<Velocity, const Acceleration>("GravitySystem")
        .kind(flecs::OnUpdate)
        .each([](Velocity &vel, const Acceleration &acc) {
          vel.vx += acc.ax * 0.016f;
          vel.vy += acc.ay * 0.016f;
        });
    flecs_1t.system<Position, const Velocity>("MovementSystem")
        .kind(flecs::OnUpdate)
        .each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
          pos.y += vel.vy * 0.016f;
        });
    flecs_1t.system<SpatialGridCell, const Position>("SpatialHashingSystem")
        .kind(flecs::OnUpdate)
        .each([](SpatialGridCell &cell, const Position &pos) {
          cell.cell_x = static_cast<int32_t>(pos.x) / 64;
          cell.cell_y = static_cast<int32_t>(pos.y) / 64;
        });
    flecs_1t.system<Health, const Damage>("DamageSystem")
        .kind(flecs::OnUpdate)
        .each([](Health &hp, const Damage &dmg) {
          hp.hp -= dmg.amount;
        });
    flecs_1t.system<Health>("HealthCleanupSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::iter &it, size_t i, Health &hp) {
          if (hp.hp <= 0.0f) {
            it.entity(i).destruct();
            it.world().entity()
                .set<Position>({0.0f, 0.0f})
                .set<Velocity>({1.0f, 1.0f})
                .set<Acceleration>({0.0f, -9.81f})
                .set<SpatialGridCell>({0, 0})
                .set<Health>({100.0f})
                .set<Damage>({100.0f});
          }
        });
    flecs_1t.system<const Position, const Health>("RenderPrepSystem")
        .kind(flecs::OnStore)
        .each([](const Position &pos, const Health &hp) {
          if (hp.hp > 0.0f) {
            ankerl::nanobench::doNotOptimizeAway(pos.x);
          }
        });

    // 5. Setup EnTT Sequential
    entt::registry entt_reg;
    for (size_t i = 0; i < ALIVE_COUNT; ++i) {
      const auto e = entt_reg.create();
      entt_reg.emplace<Position>(e, 0.0f, 0.0f);
      entt_reg.emplace<Velocity>(e, 1.0f, 1.0f);
      entt_reg.emplace<Acceleration>(e, 0.0f, -9.81f);
      entt_reg.emplace<SpatialGridCell>(e, 0, 0);
      entt_reg.emplace<Health>(e, 100.0f);
      entt_reg.emplace<Damage>(e, 0.0f);
    }
    for (size_t i = 0; i < LETHAL_COUNT; ++i) {
      const auto e = entt_reg.create();
      entt_reg.emplace<Position>(e, 0.0f, 0.0f);
      entt_reg.emplace<Velocity>(e, 1.0f, 1.0f);
      entt_reg.emplace<Acceleration>(e, 0.0f, -9.81f);
      entt_reg.emplace<SpatialGridCell>(e, 0, 0);
      entt_reg.emplace<Health>(e, 100.0f);
      entt_reg.emplace<Damage>(e, 100.0f);
    }
    TimeStepResource entt_ts{0.016f, 0};
    AudioQueueResource entt_audio{0};
    std::vector<entt::entity> entt_to_despawn;
    entt_to_despawn.reserve(LETHAL_COUNT * 2);

    bench_frame.title("13. Full-Frame 10-System Game Loop (100k Entities)")
        .run("arche-ecs (Scheduler 8 Threads)", [&] {
          arche_8t.run();
        })
        .run("arche-ecs (Scheduler 1 Thread)", [&] {
          arche_1t.run();
        })
        .run("Flecs (Pipeline 8 Threads)", [&] {
          flecs_8t.progress();
        })
        .run("Flecs (Pipeline 1 Thread)", [&] {
          flecs_1t.progress();
        })
        .run("EnTT (Sequential 1 Thread)", [&] {
          // 1. TimeStep
          entt_ts.frame++;
          entt_ts.dt = 0.016f;
          // 2. Input
          ankerl::nanobench::doNotOptimizeAway(entt_ts.frame);
          // 3. Audio
          entt_audio.sound_events = 0;
          // 4. Stats
          ankerl::nanobench::doNotOptimizeAway(entt_ts.frame);
          // 5. Gravity
          {
            auto v = entt_reg.view<Velocity, const Acceleration>();
            v.each([&](Velocity &vel, const Acceleration &acc) {
              vel.vx += acc.ax * entt_ts.dt;
              vel.vy += acc.ay * entt_ts.dt;
            });
          }
          // 6. Movement
          {
            auto v = entt_reg.view<Position, const Velocity>();
            v.each([&](Position &pos, const Velocity &vel) {
              pos.x += vel.vx * entt_ts.dt;
              pos.y += vel.vy * entt_ts.dt;
            });
          }
          // 7. SpatialHashing
          {
            auto v = entt_reg.view<SpatialGridCell, const Position>();
            v.each([](SpatialGridCell &cell, const Position &pos) {
              cell.cell_x = static_cast<int32_t>(pos.x) / 64;
              cell.cell_y = static_cast<int32_t>(pos.y) / 64;
            });
          }
          // 8. Damage
          {
            auto v = entt_reg.view<Health, const Damage>();
            v.each([](Health &hp, const Damage &dmg) {
              hp.hp -= dmg.amount;
            });
          }
          // 9. HealthCleanup (deferred despawn + spawn)
          {
            entt_to_despawn.clear();
            auto v = entt_reg.view<Health>();
            v.each([&](entt::entity e, Health &hp) {
              if (hp.hp <= 0.0f) {
                entt_to_despawn.push_back(e);
              }
            });
            for (auto e : entt_to_despawn) {
              entt_reg.destroy(e);
              auto fresh = entt_reg.create();
              entt_reg.emplace<Position>(fresh, 0.0f, 0.0f);
              entt_reg.emplace<Velocity>(fresh, 1.0f, 1.0f);
              entt_reg.emplace<Acceleration>(fresh, 0.0f, -9.81f);
              entt_reg.emplace<SpatialGridCell>(fresh, 0, 0);
              entt_reg.emplace<Health>(fresh, 100.0f);
              entt_reg.emplace<Damage>(fresh, 100.0f);
            }
          }
          // 10. RenderPrep
          {
            float total_pos = 0.0f;
            auto v = entt_reg.view<const Position, const Health>();
            v.each([&](const Position &pos, const Health &hp) {
              if (hp.hp > 0.0f) {
                total_pos += pos.x;
              }
            });
            ankerl::nanobench::doNotOptimizeAway(total_pos);
          }
        });
  }

  // ---------------------------------------------------------------------------
  // SECTION VII: MEMORY FOOTPRINT & HEAP ALLOCATION TELEMETRY
  // ---------------------------------------------------------------------------
  std::cout << "\n>>> Running Section VII: Memory Footprint & Steady-State Allocation Telemetry...\n\n";
  {
    init_flecs_alloc_hooks();

    // 14. Steady-State Zero-Allocation Audit (50 Simulation Frames, 100k Entities)
    std::cout << "--- 14. Steady-State Heap Allocation Audit (50 Frames, 100k Entities) ---\n";
    {
      // A. arche-ecs Steady-State
      size_t arche_allocs = 0;
      size_t arche_bytes = 0;
      {
        arche::World world;
        world.spawn(
            {.count = 100'000}, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
        );
        auto query = world.query<Position, const Velocity>();

        // Warmup frame
        for (auto [pos, vel] : query) {
          pos.x += vel.vx * 0.016f;
        }

        // Measure 50 steady-state frames
        g_alloc_count = 0;
        g_alloc_bytes = 0;
        g_track_allocations = true;
        for (int frame = 0; frame < 50; ++frame) {
          query.each_chunk([](uint32_t count, Position *pos, const Velocity *vel) {
            for (uint32_t i = 0; i < count; ++i) {
              pos[i].x += vel[i].vx * 0.016f;
            }
          });
        }
        g_track_allocations = false;
        arche_allocs = g_alloc_count.load();
        arche_bytes = g_alloc_bytes.load();
      }

      // B. Flecs Steady-State
      size_t flecs_allocs = 0;
      size_t flecs_bytes = 0;
      {
        flecs::world ecs;
        for (size_t i = 0; i < 100'000; ++i) {
          ecs.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 1.0f});
        }
        auto query = ecs.query<Position, const Velocity>();

        // Warmup frame
        query.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
        });

        // Measure 50 steady-state frames
        g_alloc_count = 0;
        g_alloc_bytes = 0;
        g_track_allocations = true;
        for (int frame = 0; frame < 50; ++frame) {
          query.run([](flecs::iter &it) {
            while (it.next()) {
              auto pos = it.field<Position>(0);
              auto vel = it.field<Velocity>(1);
              for (size_t i = 0; i < it.count(); ++i) {
                pos[i].x += vel[i].vx * 0.016f;
              }
            }
          });
        }
        g_track_allocations = false;
        flecs_allocs = g_alloc_count.load();
        flecs_bytes = g_alloc_bytes.load();
      }

      // C. EnTT Steady-State
      size_t entt_allocs = 0;
      size_t entt_bytes = 0;
      {
        entt::registry registry;
        for (size_t i = 0; i < 100'000; ++i) {
          const auto e = registry.create();
          registry.emplace<Position>(e, 0.0f, 0.0f);
          registry.emplace<Velocity>(e, 1.0f, 1.0f);
        }
        auto view = registry.view<Position, const Velocity>();

        // Warmup frame
        view.each([](Position &pos, const Velocity &vel) {
          pos.x += vel.vx * 0.016f;
        });

        // Measure 50 steady-state frames
        g_alloc_count = 0;
        g_alloc_bytes = 0;
        g_track_allocations = true;
        for (int frame = 0; frame < 50; ++frame) {
          view.each([](Position &pos, const Velocity &vel) {
            pos.x += vel.vx * 0.016f;
          });
        }
        g_track_allocations = false;
        entt_allocs = g_alloc_count.load();
        entt_bytes = g_alloc_bytes.load();
      }

      std::cout << "  " << std::left << std::setw(30) << "arche-ecs (Chunked SoA)"
                << " | Allocations: " << std::setw(3) << arche_allocs
                << " | Total Bytes: " << std::setw(6) << arche_bytes << " B (Zero-Allocation Guarantee: "
                << (arche_allocs == 0 ? "PASSED" : "FAILED") << ")\n";
      std::cout << "  " << std::left << std::setw(30) << "Flecs (Table Batch)"
                << " | Allocations: " << std::setw(3) << flecs_allocs
                << " | Total Bytes: " << std::setw(6) << flecs_bytes << " B (Zero-Allocation Guarantee: "
                << (flecs_allocs == 0 ? "PASSED" : "FAILED") << ")\n";
      std::cout << "  " << std::left << std::setw(30) << "EnTT (Standard View)"
                << " | Allocations: " << std::setw(3) << entt_allocs
                << " | Total Bytes: " << std::setw(6) << entt_bytes << " B (Zero-Allocation Guarantee: "
                << (entt_allocs == 0 ? "PASSED" : "FAILED") << ")\n";
    }

    // 15. Memory Footprint & Overhead per Entity (1,000,000 Entities: Pos + Vel)
    std::cout << "\n--- 15. Memory Footprint & Overhead per Entity (1,000,000 Entities) ---\n";
    std::cout << "  Component Data Payload: 16.00 MB (16 bytes/entity: Position + Velocity)\n\n";
    {
      constexpr size_t ENTITY_COUNT = 1'000'000;
      constexpr double PAYLOAD_BYTES_PER_ENTITY = 16.0;

      // arche-ecs
      size_t arche_heap_bytes = 0;
      {
        g_alloc_bytes = 0;
        g_track_allocations = true;
        auto world = std::make_unique<arche::World>();
        world->spawn(
            {.count = ENTITY_COUNT}, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
        );
        g_track_allocations = false;
        arche_heap_bytes = g_alloc_bytes.load();
        ankerl::nanobench::doNotOptimizeAway(world->entities.count());
      }

      // Flecs
      size_t flecs_heap_bytes = 0;
      {
        g_alloc_bytes = 0;
        g_track_allocations = true;
        auto ecs = std::make_unique<flecs::world>();
        for (size_t i = 0; i < ENTITY_COUNT; ++i) {
          ecs->entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 1.0f});
        }
        g_track_allocations = false;
        flecs_heap_bytes = g_alloc_bytes.load();
        ankerl::nanobench::doNotOptimizeAway(ecs->count<Position>());
      }

      // EnTT
      size_t entt_heap_bytes = 0;
      {
        g_alloc_bytes = 0;
        g_track_allocations = true;
        auto registry = std::make_unique<entt::registry>();
        for (size_t i = 0; i < ENTITY_COUNT; ++i) {
          const auto e = registry->create();
          registry->emplace<Position>(e, 0.0f, 0.0f);
          registry->emplace<Velocity>(e, 1.0f, 1.0f);
        }
        g_track_allocations = false;
        entt_heap_bytes = g_alloc_bytes.load();
        ankerl::nanobench::doNotOptimizeAway(registry->view<Position>().size());
      }

      auto print_mem_row = [](const char *name, size_t alloc_bytes, size_t count, double payload_per_entity) {
        const double total_mb = static_cast<double>(alloc_bytes) / (1024.0 * 1024.0);
        const double bytes_per_entity = static_cast<double>(alloc_bytes) / static_cast<double>(count);
        const double overhead_bytes = bytes_per_entity - payload_per_entity;
        const double packing_efficiency = (payload_per_entity / bytes_per_entity) * 100.0;

        std::cout << "  " << std::left << std::setw(28) << name << " | Allocated: "
                  << std::right << std::setw(7) << std::fixed << std::setprecision(2) << total_mb << " MB"
                  << " | " << std::setw(5) << std::setprecision(1) << bytes_per_entity << " B/entity"
                  << " | Overhead: +" << std::setw(4) << std::setprecision(1) << overhead_bytes << " B"
                  << " | Efficiency: " << std::setw(5) << std::setprecision(1) << packing_efficiency << "%\n";
      };

      std::cout << "  " << std::string(88, '-') << "\n";
      print_mem_row("arche-ecs (64KB Chunk Pages)", arche_heap_bytes, ENTITY_COUNT, PAYLOAD_BYTES_PER_ENTITY);
      print_mem_row("Flecs (Table Blocks)", flecs_heap_bytes, ENTITY_COUNT, PAYLOAD_BYTES_PER_ENTITY);
      print_mem_row("EnTT (Dense Sparse Pools)", entt_heap_bytes, ENTITY_COUNT, PAYLOAD_BYTES_PER_ENTITY);
      std::cout << "  " << std::string(88, '-') << "\n";
    }
  }

  // ===========================================================================
  // SECTION VIII: Sparse Activity & Change Detection (500k Entities, 5% Active)
  // ===========================================================================
  std::cout << "\n>>> Running Section VIII: Sparse Activity & Change Detection (500k Entities, 5% Active)...\n";
  {
    constexpr size_t TOTAL_ENTITIES = 500'000;
    constexpr size_t ACTIVE_PERCENT = 5; // 5% active = 25,000 entities
    constexpr size_t ACTIVE_ENTITIES = (TOTAL_ENTITIES * ACTIVE_PERCENT) / 100;

    // Benchmark 16: Naive Full Scan vs Change Detection (Mutated<T> / ChangedSince)
    // Setup 500,000 entities in both engines.
    // Modify only the first 25,000 entities.
    // arche-ecs uses Mutated<Position>{last_tick} to skip 95% of chunk pages in O(1).
    // Flecs uses query or filter (table-level / changed column tracking) or iter.changed().
    // EnTT scans dense pool / tests changed flag.

    arche::World arche_world;
    std::vector<arche::Entity> arche_ents = arche_world.spawn(
        {.count = TOTAL_ENTITIES}, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
    );
    // Warmup frame to establish baseline tick
    arche_world.run(); // tick = 2
    const uint32_t baseline_tick = arche_world.version();

    // Mutate 5% of entities (entities 0 to ACTIVE_ENTITIES-1)
    for (size_t i = 0; i < ACTIVE_ENTITIES; ++i) {
      arche_world.get_mut<Position>(arche_ents[i]).x += 1.0f;
    }

    // Flecs setup
    flecs::world flecs_world;
    std::vector<flecs::entity> flecs_ents;
    flecs_ents.reserve(TOTAL_ENTITIES);
    for (size_t i = 0; i < TOTAL_ENTITIES; ++i) {
      flecs_ents.push_back(
          flecs_world.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 1.0f})
      );
    }
    // Modify 5% in Flecs
    for (size_t i = 0; i < ACTIVE_ENTITIES; ++i) {
      auto *p = flecs_ents[i].get_mut<Position>();
      p->x += 1.0f;
      flecs_ents[i].modified<Position>();
    }

    // EnTT setup with explicit dirty/active flag component for fair comparison
    struct ActiveFlag {
      uint8_t dummy{1};
    };
    entt::registry entt_reg;
    std::vector<entt::entity> entt_ents;
    entt_ents.reserve(TOTAL_ENTITIES);
    for (size_t i = 0; i < TOTAL_ENTITIES; ++i) {
      const auto e = entt_reg.create();
      entt_reg.emplace<Position>(e, 0.0f, 0.0f);
      entt_reg.emplace<Velocity>(e, 1.0f, 1.0f);
      entt_ents.push_back(e);
    }
    for (size_t i = 0; i < ACTIVE_ENTITIES; ++i) {
      entt_reg.emplace<ActiveFlag>(entt_ents[i]);
      entt_reg.get<Position>(entt_ents[i]).x += 1.0f;
    }

    ankerl::nanobench::Bench bench_sparse;
    bench_sparse.warmup(1).minEpochIterations(5).maxEpochTime(
        std::chrono::milliseconds(150)
    );

    bench_sparse.title("16. Sparse Activity (500k Entities, 5% Mutated)")
        .run("arche-ecs (Mutated<Position> chunk skipping)", [&] {
          size_t processed = 0;
          arche_world.query<Position, const Velocity>(arche::Mutated<Position>{baseline_tick})
              .each_chunk([&](uint32_t count, Position *p, const Velocity *v) {
                for (uint32_t i = 0; i < count; ++i) {
                  p[i].x += v[i].vx * 0.016f;
                  p[i].y += v[i].vy * 0.016f;
                }
                processed += count;
              });
          ankerl::nanobench::doNotOptimizeAway(processed);
        })
        .run("arche-ecs (Full Scan baseline without filter)", [&] {
          size_t processed = 0;
          arche_world.query<Position, const Velocity>()
              .each_chunk([&](uint32_t count, Position *p, const Velocity *v) {
                for (uint32_t i = 0; i < count; ++i) {
                  p[i].x += v[i].vx * 0.016f;
                  p[i].y += v[i].vy * 0.016f;
                }
                processed += count;
              });
          ankerl::nanobench::doNotOptimizeAway(processed);
        })
        .run("Flecs (Filter iter.changed() table skipping)", [&] {
          size_t processed = 0;
          auto q = flecs_world.query<Position, const Velocity>();
          q.each([&](flecs::iter &it, size_t, Position &p, const Velocity &v) {
            if (it.changed()) {
              p.x += v.vx * 0.016f;
              p.y += v.vy * 0.016f;
              processed++;
            }
          });
          ankerl::nanobench::doNotOptimizeAway(processed);
        })
        .run("Flecs (Full Scan baseline without filter)", [&] {
          size_t processed = 0;
          auto q = flecs_world.query<Position, const Velocity>();
          q.each([&](Position &p, const Velocity &v) {
            p.x += v.vx * 0.016f;
            p.y += v.vy * 0.016f;
            processed++;
          });
          ankerl::nanobench::doNotOptimizeAway(processed);
        })
        .run("EnTT (ActiveFlag Sparse View 5%)", [&] {
          size_t processed = 0;
          auto view = entt_reg.view<Position, const Velocity, const ActiveFlag>();
          view.each([&](Position &p, const Velocity &v, const ActiveFlag &) {
            p.x += v.vx * 0.016f;
            p.y += v.vy * 0.016f;
            processed++;
          });
          ankerl::nanobench::doNotOptimizeAway(processed);
        })
        .run("EnTT (Full Scan baseline 500k)", [&] {
          size_t processed = 0;
          auto view = entt_reg.view<Position, const Velocity>();
          view.each([&](Position &p, const Velocity &v) {
            p.x += v.vx * 0.016f;
            p.y += v.vy * 0.016f;
            processed++;
          });
          ankerl::nanobench::doNotOptimizeAway(processed);
        });
  }

  // ===========================================================================
  // SECTION IX: Dynamic Archetype Churn (50k Entities Buff/Tag Cycling)
  // ===========================================================================
  std::cout << "\n>>> Running Section IX: Dynamic Archetype Churn (50k Entities Buff Cycling)...\n";
  {
    constexpr size_t CHURN_ENTITIES = 50'000;
    struct SpeedBuff {
      float multiplier{2.0f};
    };
    struct ShieldBuff {
      float hp{100.0f};
    };

    // Benchmark 17: Add & Remove Component Churn per Frame (50,000 entities)
    // Measures archetype table migration / graph transitions vs sparse set insertion/deletion.
    arche::World arche_world;
    std::vector<arche::Entity> arche_ents = arche_world.spawn(
        {.count = CHURN_ENTITIES}, Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f}
    );

    flecs::world flecs_world;
    std::vector<flecs::entity> flecs_ents;
    flecs_ents.reserve(CHURN_ENTITIES);
    for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
      flecs_ents.push_back(
          flecs_world.entity().set<Position>({0.0f, 0.0f}).set<Velocity>({1.0f, 1.0f})
      );
    }

    entt::registry entt_reg;
    std::vector<entt::entity> entt_ents;
    entt_ents.reserve(CHURN_ENTITIES);
    for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
      const auto e = entt_reg.create();
      entt_reg.emplace<Position>(e, 0.0f, 0.0f);
      entt_reg.emplace<Velocity>(e, 1.0f, 1.0f);
      entt_ents.push_back(e);
    }

    ankerl::nanobench::Bench bench_churn;
    bench_churn.warmup(1).minEpochIterations(3).maxEpochTime(
        std::chrono::milliseconds(200)
    );

    bench_churn.title("17. Dynamic Churn: Add & Remove Buff (50k Entities)")
        .run("arche-ecs (Add + Remove Archetype Graph)", [&] {
          // Add SpeedBuff to 50k entities (moves to Pos+Vel+SpeedBuff archetype)
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            arche_world.add(arche_ents[i], SpeedBuff{2.5f});
          }
          // Remove SpeedBuff (moves back to Pos+Vel archetype)
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            arche_world.remove<SpeedBuff>(arche_ents[i]);
          }
        })
        .run("Flecs (Add + Remove Table Transition)", [&] {
          // Add SpeedBuff to 50k entities
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            flecs_ents[i].set<SpeedBuff>({2.5f});
          }
          // Remove SpeedBuff
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            flecs_ents[i].remove<SpeedBuff>();
          }
        })
        .run("EnTT (Emplace + Erase Sparse Set)", [&] {
          // Emplace SpeedBuff to 50k entities
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            entt_reg.emplace<SpeedBuff>(entt_ents[i], 2.5f);
          }
          // Erase SpeedBuff
          for (size_t i = 0; i < CHURN_ENTITIES; ++i) {
            entt_reg.erase<SpeedBuff>(entt_ents[i]);
          }
        });

    // Benchmark 18: Multi-Archetype Alternating Churn (50k Entities cycling across 4 archetypes)
    bench_churn.title("18. Multi-Archetype Alternating Churn (50k Entities)")
        .run("arche-ecs (4-Way Cycle)", [&] {
          // Even entities get SpeedBuff, odd get ShieldBuff
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            arche_world.add(arche_ents[i], SpeedBuff{1.5f});
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            arche_world.add(arche_ents[i], ShieldBuff{50.0f});
          }
          // Clean up
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            arche_world.remove<SpeedBuff>(arche_ents[i]);
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            arche_world.remove<ShieldBuff>(arche_ents[i]);
          }
        })
        .run("Flecs (4-Way Cycle)", [&] {
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            flecs_ents[i].set<SpeedBuff>({1.5f});
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            flecs_ents[i].set<ShieldBuff>({50.0f});
          }
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            flecs_ents[i].remove<SpeedBuff>();
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            flecs_ents[i].remove<ShieldBuff>();
          }
        })
        .run("EnTT (4-Way Cycle)", [&] {
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            entt_reg.emplace<SpeedBuff>(entt_ents[i], 1.5f);
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            entt_reg.emplace<ShieldBuff>(entt_ents[i], 50.0f);
          }
          for (size_t i = 0; i < CHURN_ENTITIES; i += 2) {
            entt_reg.erase<SpeedBuff>(entt_ents[i]);
          }
          for (size_t i = 1; i < CHURN_ENTITIES; i += 2) {
            entt_reg.erase<ShieldBuff>(entt_ents[i]);
          }
        });
  }

  std::cout << "\n============================================================="
               "=================\n";
  std::cout << "  Comprehensive 2M Comparative Benchmark Suite Completed "
               "Successfully!        \n";
  std::cout << "==============================================================="
               "===============\n";
  return 0;
}
#else
#include <iostream>
int main() {
  std::cout << "Comparative benchmarks require "
               "-DARCHE_BUILD_COMPARISON_BENCHMARKS=ON\n";
  return 0;
}
#endif
