import arche;

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using arche::Access;
using arche::Plugin;
using arche::Scheduler;
using arche::SystemContext;
using arche::SystemScale;
using arche::ThreadPool;
using arche::World;
} // namespace

// =============================================================================
// Test Component & Resource Types
// =============================================================================

struct Position {
  float x{0.0f}, y{0.0f}, z{0.0f};
};

struct Velocity {
  float vx{0.0f}, vy{0.0f}, vz{0.0f};
};

struct SimulationClock {
  float total_time{0.0f};
  uint64_t frame_count{0};
};

struct PhysicsSettings {
  float gravity_y{-9.81f};
};

// =============================================================================
// Plugins for Testing
// =============================================================================

// 1. Valid Stateless Plugin
struct ValidClockPlugin {
  static constexpr std::string_view name() noexcept {
    return "ValidClockPlugin";
  }

  void build(World &world, Scheduler &scheduler) const {
    world.resources.insert<SimulationClock>();

    scheduler.add_system(
        "ClockTick",
        Access{}.writes_resource<SimulationClock>().build(),
        [](SystemContext &ctx) {
          auto &clock = ctx.write_resource<SimulationClock>();
          clock.total_time += 0.016f;
          clock.frame_count++;
        },
        SystemScale::Narrow
    );
  }
};

// 2. Valid Stateful Plugin with Configuration
struct ConfigurablePhysicsPlugin {
  float custom_gravity{-9.81f};

  static constexpr std::string_view name() noexcept {
    return "ConfigurablePhysicsPlugin";
  }

  void build(World &world, Scheduler &scheduler) const {
    world.resources.insert<PhysicsSettings>(custom_gravity);

    scheduler.add_system(
        "ApplyGravity",
        Access{}.reads_resource<PhysicsSettings>().writes<Velocity>().build(),
        [](SystemContext &ctx) {
          const auto &settings = ctx.read_resource<PhysicsSettings>();
          (void)settings;
        },
        SystemScale::Wide
    );
  }
};

// 3. Second Distinct Plugin
struct RenderPlugin {
  static constexpr std::string_view name() noexcept { return "RenderPlugin"; }

  void build(World &world, Scheduler &scheduler) const {
    (void)world;
    scheduler.add_system(
        "RenderSystem",
        Access{}.reads<Position>().build(),
        [](SystemContext &) {},
        SystemScale::Narrow
    );
  }
};

// 4. Invalid Candidate: Missing name()
struct MissingNamePlugin {
  void build(World &, Scheduler &) const {}
};

// 5. Invalid Candidate: Invalid build() signature
struct InvalidBuildPlugin {
  static constexpr std::string_view name() noexcept {
    return "InvalidBuildPlugin";
  }
  int build(World &, Scheduler &) const { return 42; }
};

// =============================================================================
// Unit Test 1: Static Concept Verification (static_assert)
// =============================================================================

void test_static_concept_verification() {
  std::cout << "Testing Static Concept Verification (static_assert)... ";

  // Valid plugins satisfy the concept
  static_assert(
      Plugin<ValidClockPlugin>, "ValidClockPlugin must satisfy Plugin concept"
  );
  static_assert(
      Plugin<ConfigurablePhysicsPlugin>,
      "ConfigurablePhysicsPlugin must satisfy Plugin concept"
  );
  static_assert(
      Plugin<RenderPlugin>, "RenderPlugin must satisfy Plugin concept"
  );

  // Invalid plugins fail the concept
  static_assert(
      !Plugin<MissingNamePlugin>,
      "MissingNamePlugin must NOT satisfy Plugin concept"
  );
  static_assert(
      !Plugin<InvalidBuildPlugin>,
      "InvalidBuildPlugin must NOT satisfy Plugin concept"
  );
  static_assert(!Plugin<int>, "int must NOT satisfy Plugin concept");

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Stateless Plugin Installation
// =============================================================================

void test_stateless_plugin_installation() {
  std::cout << "Testing Stateless Plugin Installation... ";

  World world;
  ThreadPool pool(2);

  // Install via type template parameter
  world.systems.add_plugin<ValidClockPlugin>();

  // Resource was initialized
  assert(world.resources.has<SimulationClock>());
  assert(world.resources.get<SimulationClock>().frame_count == 0);

  // Compile and run a frame
  world.systems.compile();
  world.systems.run(pool);

  assert(world.resources.get<SimulationClock>().frame_count == 1);
  assert(world.resources.get<SimulationClock>().total_time > 0.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Stateful Plugin Installation
// =============================================================================

void test_stateful_plugin_installation() {
  std::cout << "Testing Stateful Plugin Installation with Config... ";

  World world;
  ThreadPool pool(2);

  // Install configured instance
  world.systems.add_plugin(
      ConfigurablePhysicsPlugin{.custom_gravity = -19.62f}
  );

  // Resource was inserted with custom parameter
  assert(world.resources.has<PhysicsSettings>());
  assert(world.resources.get<PhysicsSettings>().gravity_y == -19.62f);

  world.systems.compile();
  world.systems.run(pool);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: External Standalone Scheduler Overloads
// =============================================================================

void test_external_scheduler_overloads() {
  std::cout << "Testing External Standalone Scheduler Overloads... ";

  World world;
  ThreadPool pool(2);
  Scheduler ext_scheduler;

  // 1. Stateless plugin targeting external scheduler
  world.systems.add_plugin<ValidClockPlugin>(ext_scheduler);
  assert(world.resources.has<SimulationClock>());

  // 2. Stateful plugin targeting external scheduler
  world.systems.add_plugin(
      ConfigurablePhysicsPlugin{.custom_gravity = -25.0f}, ext_scheduler
  );
  assert(world.resources.get<PhysicsSettings>().gravity_y == -25.0f);

  // Run via external scheduler
  ext_scheduler.compile(world);
  ext_scheduler.run(world, pool);

  assert(world.resources.get<SimulationClock>().frame_count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Duplicate Plugin Prevention Exception
// =============================================================================

void test_duplicate_plugin_prevention() {
  std::cout << "Testing Duplicate Plugin Prevention Exception... ";

  World world;

  // First installation succeeds
  world.systems.add_plugin<ValidClockPlugin>();

  // Second installation of identical plugin must throw std::runtime_error
  bool threw = false;
  try {
    world.systems.add_plugin<ValidClockPlugin>();
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  assert(threw);

  // Second installation of stateful plugin with duplicate name throws
  world.systems.add_plugin(ConfigurablePhysicsPlugin{});
  threw = false;
  try {
    world.systems.add_plugin(
        ConfigurablePhysicsPlugin{.custom_gravity = -5.0f}
    );
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  assert(threw);

  // Installing a different plugin succeeds
  world.systems.add_plugin<RenderPlugin>();

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: World Simulation Tick Advancement via run_frame
// =============================================================================

void test_world_run_frame_advances_tick() {
  std::cout << "Testing World Simulation Tick Advancement via run_frame... ";

  World world;
  ThreadPool pool(2);

  world.systems.add_plugin<ValidClockPlugin>();
  world.systems.compile();

  const uint32_t initial_version = world.version();
  assert(initial_version >= 1);

  world.systems.run(pool);
  assert(world.version() == initial_version + 1);

  world.systems.run(pool);
  assert(world.version() == initial_version + 2);

  assert(world.resources.get<SimulationClock>().frame_count == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "      Module 022: Plugin Architecture Tests         \n";
  std::cout << "====================================================\n";

  test_static_concept_verification();
  test_stateless_plugin_installation();
  test_stateful_plugin_installation();
  test_external_scheduler_overloads();
  test_duplicate_plugin_prevention();
  test_world_run_frame_advances_tick();

  std::cout << "\nAll Plugin unit tests passed successfully!\n";
  return 0;
}
