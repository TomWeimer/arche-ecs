import arche;

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using arche::Access;
using arche::Plugin;
using arche::PluginContext;
using arche::SystemContext;
using arche::SystemScale;
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

  void build(World &world, PluginContext &ctx) const {
    world.resources.insert<SimulationClock>();

    ctx.add_system(
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

  void build(World &world, PluginContext &ctx) const {
    world.resources.insert<PhysicsSettings>(custom_gravity);

    ctx.add_system(
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

  void build(World &world, PluginContext &ctx) const {
    (void)world;
    ctx.add_system(
        "RenderSystem",
        Access{}.reads<Position>().build(),
        [](SystemContext &) {},
        SystemScale::Narrow
    );
  }
};

// 4. Invalid Candidate: Missing name()
struct MissingNamePlugin {
  void build(World &, PluginContext &) const {}
};

// 5. Invalid Candidate: Invalid build() signature
struct InvalidBuildPlugin {
  static constexpr std::string_view name() noexcept {
    return "InvalidBuildPlugin";
  }
  int build(World &, PluginContext &) const { return 42; }
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

  // Install via type template parameter
  world.add_plugin<ValidClockPlugin>();

  // Resource was initialized
  assert(world.resources.has<SimulationClock>());
  assert(world.resources.get<SimulationClock>().frame_count == 0);

  // Compile and run a frame
  world.compile();
  world.run();

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

  // Install configured instance
  world.add_plugin(
      ConfigurablePhysicsPlugin{.custom_gravity = -19.62f}
  );

  // Resource was inserted with custom parameter
  assert(world.resources.has<PhysicsSettings>());
  assert(world.resources.get<PhysicsSettings>().gravity_y == -19.62f);

  world.compile();
  world.run();

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Plugin Fluent Method Chaining on World
// =============================================================================

void test_plugin_fluent_chaining() {
  std::cout << "Testing Plugin Fluent Method Chaining on World... ";

  World world;
  world.add_plugin<ValidClockPlugin>()
       .add_plugin(ConfigurablePhysicsPlugin{.custom_gravity = -25.0f});

  assert(world.resources.has<SimulationClock>());
  assert(world.resources.get<PhysicsSettings>().gravity_y == -25.0f);

  world.compile();
  world.run();

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
  world.add_plugin<ValidClockPlugin>();

  // Second installation of identical plugin must throw std::runtime_error
  bool threw = false;
  try {
    world.add_plugin<ValidClockPlugin>();
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  assert(threw);

  // Second installation of stateful plugin with duplicate name throws
  world.add_plugin(ConfigurablePhysicsPlugin{});
  threw = false;
  try {
    world.add_plugin(
        ConfigurablePhysicsPlugin{.custom_gravity = -5.0f}
    );
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  assert(threw);

  // Installing a different plugin succeeds
  world.add_plugin<RenderPlugin>();

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: World Simulation Tick Advancement via run_frame
// =============================================================================

void test_world_run_frame_advances_tick() {
  std::cout << "Testing World Simulation Tick Advancement via run... ";

  World world;

  world.add_plugin<ValidClockPlugin>();
  world.compile();

  const uint32_t initial_version = world.version();
  assert(initial_version >= 1);

  world.run();
  assert(world.version() == initial_version + 1);

  world.run();
  assert(world.version() == initial_version + 2);

  assert(world.resources.get<SimulationClock>().frame_count == 2);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 7: Individual SystemFacade Lookup, Enable/Disable, run_now, show_access
// =============================================================================

void test_system_facade_inspection_and_control() {
  std::cout << "Testing Individual SystemFacade (Lookup, Enable/Disable, run_now, show_access)... ";

  World world;
  world.add_plugin<ValidClockPlugin>();
  world.add_plugin(ConfigurablePhysicsPlugin{.custom_gravity = -9.81f});
  world.compile();

  // 1. Non-existent system lookup returns nullopt
  auto missing = world.system("NonExistentSystem");
  assert(!missing.has_value());

  // 2. Valid system lookup
  auto sys_opt = world.system("ClockTick");
  assert(sys_opt.has_value());
  auto &sys = *sys_opt;

  // 3. System is enabled by default
  assert(sys.is_enabled());

  // 4. show_access returns formatted description
  std::string access_info = sys.show_access();
  assert(!access_info.empty());
  assert(access_info.find("ClockTick") != std::string::npos);

  // 5. Test run_now() executes system directly
  assert(world.resources.get<SimulationClock>().frame_count == 0);
  sys.run_now();
  assert(world.resources.get<SimulationClock>().frame_count == 1);

  // 6. Test disable() prevents execution during world.run()
  sys.disable();
  assert(!sys.is_enabled());

  world.run();
  // ClockTick was disabled, so frame_count remains 1
  assert(world.resources.get<SimulationClock>().frame_count == 1);

  // 7. Test enable() restores execution during world.run()
  sys.enable();
  assert(sys.is_enabled());

  world.run();
  // ClockTick was enabled, so frame_count increments to 2
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
  test_plugin_fluent_chaining();
  test_duplicate_plugin_prevention();
  test_world_run_frame_advances_tick();
  test_system_facade_inspection_and_control();

  std::cout << "\nAll Plugin unit tests passed successfully!\n";
  return 0;
}
