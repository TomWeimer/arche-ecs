import arche;

#include <cassert>
#include <iostream>

namespace {
using arche::INVALID_RESOURCE_ID;
using arche::resource_id;
using arche::internal::ResourceContainer;
using arche::ResourceID;
} // namespace

// =============================================================================
// Test Resource Types
// =============================================================================

struct SimulationClock {
  float dt{0.016f};
  uint64_t frame{0};
};

struct GravityConfig {
  float x{0.0f};
  float y{-9.81f};
  float z{0.0f};
};

struct SpatialHashSettings {
  float cell_size{10.0f};
  int table_size{1024};
};

struct UnusedResource {
  int dummy{0};
};

// Tracked resource to verify non-virtual destructor invocations and leak
// freedom
struct TrackedResource {
  int value{0};
  inline static int live_instances = 0;

  explicit TrackedResource(int val = 0) : value(val) { ++live_instances; }
  ~TrackedResource() { --live_instances; }

  TrackedResource(const TrackedResource &) = delete;
  TrackedResource &operator=(const TrackedResource &) = delete;
  TrackedResource(TrackedResource &&other) noexcept : value(other.value) {
    ++live_instances;
  }
  TrackedResource &operator=(TrackedResource &&other) noexcept {
    value = other.value;
    return *this;
  }
};

// =============================================================================
// Unit Test 1: Monotonic ResourceID Generation & cv-decay normalization
// =============================================================================

/**
 * @brief Verifies that resource_id<T>() generates sequential monotonic IDs
 * and normalizes const/reference cv-qualifiers via std::decay_t.
 */
void test_sequential_resource_id_generation() {
  std::cout << "Testing ResourceID Monotonic Allocation & Normalization... ";

  const ResourceID id1 = resource_id<SimulationClock>();
  const ResourceID id2 = resource_id<GravityConfig>();
  const ResourceID id3 = resource_id<SpatialHashSettings>();

  // IDs must be distinct sequential integers
  assert(id1 != INVALID_RESOURCE_ID);
  assert(id2 != INVALID_RESOURCE_ID);
  assert(id3 != INVALID_RESOURCE_ID);
  assert(id1 != id2);
  assert(id2 != id3);
  assert(id1 != id3);

  // Calling resource_id<T>() repeatedly must yield the exact same cached ID
  assert(resource_id<SimulationClock>() == id1);
  assert(resource_id<GravityConfig>() == id2);

  // cv-qualifiers and references must decay to the same ResourceID
  assert(resource_id<const SimulationClock>() == id1);
  assert(resource_id<SimulationClock &>() == id1);
  assert(resource_id<const SimulationClock &>() == id1);
  assert(resource_id<volatile SimulationClock>() == id1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: In-Place Insertion, Mutation & Const Retrieval
// =============================================================================

/**
 * @brief Verifies that resources can be constructed in-place and accessed
 * mutably or immutably via const correctness.
 */
void test_resource_insertion_and_retrieval() {
  std::cout << "Testing Resource Insertion, Mutation & Const Retrieval... ";

  ResourceContainer resources;

  assert(!resources.has<SimulationClock>());
  assert(!resources.has<GravityConfig>());

  // In-place construction with arguments
  SimulationClock &clock = resources.insert<SimulationClock>(0.008f, 100ULL);
  assert(resources.has<SimulationClock>());
  assert(clock.dt == 0.008f);
  assert(clock.frame == 100ULL);

  // Mutable get and in-place modification
  SimulationClock &clock_ref = resources.get<SimulationClock>();
  assert(&clock == &clock_ref);
  clock_ref.frame = 101ULL;
  assert(resources.get<SimulationClock>().frame == 101ULL);

  // Insert second independent resource
  resources.insert<GravityConfig>(0.0f, -19.62f, 0.0f);
  assert(resources.has<GravityConfig>());
  assert(resources.get<GravityConfig>().y == -19.62f);

  // Const retrieval test
  const ResourceContainer &const_res = resources;
  assert(const_res.has<SimulationClock>());
  const auto &const_clock = const_res.get<SimulationClock>();
  assert(const_clock.frame == 101ULL);
  assert(const_res.get<GravityConfig>().y == -19.62f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Resource Re-Initialization / Overwrite
// =============================================================================

/**
 * @brief Verifies that calling insert<T>() on an already registered resource
 * cleanly replaces it and destructs the prior instance.
 */
void test_resource_reinitialization() {
  std::cout << "Testing Resource Re-Initialization & Replacement... ";

  TrackedResource::live_instances = 0;
  {
    ResourceContainer resources;

    resources.insert<TrackedResource>(42);
    assert(TrackedResource::live_instances == 1);
    assert(resources.get<TrackedResource>().value == 42);

    // Overwrite with new value: must destruct old and construct new
    resources.insert<TrackedResource>(99);
    assert(TrackedResource::live_instances == 1);
    assert(resources.get<TrackedResource>().value == 99);
  }
  // Container destroyed: must destruct the remaining instance
  assert(TrackedResource::live_instances == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Resource Removal & Exception Safety
// =============================================================================

/**
 * @brief Verifies that remove<T>() frees resources, calls destructors,
 * and causes subsequent get<T>() calls to throw std::runtime_error.
 */
void test_resource_removal_and_exceptions() {
  std::cout << "Testing Resource Removal & Missing Exceptions... ";

  TrackedResource::live_instances = 0;
  ResourceContainer resources;

  // Removing an uninserted resource must safely return false
  assert(!resources.remove<SimulationClock>());
  assert(!resources.remove<TrackedResource>());

  // Insert and then remove
  resources.insert<TrackedResource>(500);
  assert(TrackedResource::live_instances == 1);
  assert(resources.has<TrackedResource>());

  const bool removed = resources.remove<TrackedResource>();
  assert(removed);
  assert(!resources.has<TrackedResource>());
  assert(TrackedResource::live_instances == 0);

  // Calling remove again returns false
  assert(!resources.remove<TrackedResource>());

  // Accessing removed resource must throw std::runtime_error
  bool threw = false;
  try {
    [[maybe_unused]] auto &val = resources.get<TrackedResource>();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  // Accessing never-inserted resource must also throw
  threw = false;
  try {
    [[maybe_unused]] auto &val = resources.get<UnusedResource>();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Container Clear & Destructor Cleanup
// =============================================================================

/**
 * @brief Verifies that clear() releases all resources and invokes destructors.
 */
void test_resource_clear_lifecycle() {
  std::cout << "Testing ResourceContainer Clear & Destructor Lifecycle... ";

  TrackedResource::live_instances = 0;
  {
    ResourceContainer resources;

    resources.insert<SimulationClock>(0.016f, 1);
    resources.insert<GravityConfig>(0.0f, -9.81f, 0.0f);
    resources.insert<TrackedResource>(10);

    assert(resources.has<SimulationClock>());
    assert(resources.has<GravityConfig>());
    assert(resources.has<TrackedResource>());
    assert(TrackedResource::live_instances == 1);

    resources.clear();

    assert(!resources.has<SimulationClock>());
    assert(!resources.has<GravityConfig>());
    assert(!resources.has<TrackedResource>());
    assert(TrackedResource::live_instances == 0);

    // Can insert again after clear
    resources.insert<TrackedResource>(20);
    assert(TrackedResource::live_instances == 1);
    assert(resources.get<TrackedResource>().value == 20);
  }
  assert(TrackedResource::live_instances == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: Move Construction & Move Assignment
// =============================================================================

/**
 * @brief Verifies that ResourceContainer supports move semantics with
 * clean transfer of ownership and zero memory leaks.
 */
void test_resource_move_semantics() {
  std::cout << "Testing ResourceContainer Move Semantics... ";

  TrackedResource::live_instances = 0;
  {
    ResourceContainer src;
    src.insert<SimulationClock>(0.033f, 42);
    src.insert<TrackedResource>(77);

    assert(src.has<SimulationClock>());
    assert(src.has<TrackedResource>());
    assert(TrackedResource::live_instances == 1);

    // 1. Move Construction
    ResourceContainer dst(std::move(src));

    assert(dst.has<SimulationClock>());
    assert(dst.has<TrackedResource>());
    assert(dst.get<SimulationClock>().frame == 42);
    assert(dst.get<TrackedResource>().value == 77);
    assert(TrackedResource::live_instances == 1);

    // 2. Move Assignment
    ResourceContainer dst2;
    dst2.insert<GravityConfig>(1.0f, 2.0f, 3.0f);

    dst2 = std::move(dst);

    assert(dst2.has<SimulationClock>());
    assert(dst2.has<TrackedResource>());
    assert(!dst2.has<GravityConfig>());
    assert(dst2.get<SimulationClock>().frame == 42);
    assert(dst2.get<TrackedResource>().value == 77);
    assert(TrackedResource::live_instances == 1);
  }
  // All resources must be cleanly destroyed
  assert(TrackedResource::live_instances == 0);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 7: World Resource Facade Integration
// =============================================================================

/**
 * @brief Verifies that arche::World correctly embeds ResourceContainer and
 * exposes clean facade methods (insert_resource, resource, has_resource,
 * remove_resource).
 */
void test_world_resource_facade() {
  std::cout << "Testing World Resource Facade Integration... ";

  arche::World world;

  assert(!world.resources.has<SimulationClock>());
  assert(!world.resources.has<GravityConfig>());

  // In-place construction on World
  world.resources.insert<SimulationClock>(0.008f, 500ULL);
  world.resources.insert<GravityConfig>(0.0f, -9.81f, 0.0f);

  assert(world.resources.has<SimulationClock>());
  assert(world.resources.has<GravityConfig>());

  // Mutable access through World
  auto &clock = world.resources.get<SimulationClock>();
  assert(clock.dt == 0.008f);
  assert(clock.frame == 500ULL);
  clock.frame = 501ULL;
  assert(world.resources.get<SimulationClock>().frame == 501ULL);

  // Const access through const World&
  const arche::World &cworld = world;
  assert(cworld.resources.has<GravityConfig>());
  assert(cworld.resources.get<GravityConfig>().y == -9.81f);

  // Const and mutable resource checks
  assert(world.resources.has<SimulationClock>());
  assert(cworld.resources.has<GravityConfig>());

  // Removal through World
  assert(world.resources.remove<SimulationClock>());
  assert(!world.resources.has<SimulationClock>());
  assert(!world.resources.remove<SimulationClock>());

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "   Module 019: ResourceContainer & ComponentID Tests      \n";
  std::cout << "====================================================\n";

  test_sequential_resource_id_generation();
  test_resource_insertion_and_retrieval();
  test_resource_reinitialization();
  test_resource_removal_and_exceptions();
  test_resource_clear_lifecycle();
  test_resource_move_semantics();
  test_world_resource_facade();

  std::cout << "\nAll resource container unit tests passed successfully!\n";
  return 0;
}
