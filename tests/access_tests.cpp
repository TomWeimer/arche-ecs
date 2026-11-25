import arche;

#include <cassert>
#include <iostream>

namespace {
using arche::Access;
using arche::AccessBuilder;
using arche::AccessDescriptor;
using arche::resource_id;
using arche::component_id;
} // namespace

// =============================================================================
// Test Component Types
// =============================================================================

struct Position {
  float x, y, z;
};

struct Velocity {
  float vx, vy, vz;
};

struct Acceleration {
  float ax, ay, az;
};

struct Mass {
  float value;
};

struct Health {
  int current;
  int max;
};

// =============================================================================
// Test Resource Types
// =============================================================================

struct TimeStep {
  float dt{0.016f};
};

struct Gravity {
  float gx{0.0f}, gy{-9.81f}, gz{0.0f};
};

struct CollisionConfig {
  float restitution{0.8f};
  int iterations{4};
};

// =============================================================================
// Unit Test 1: Empty Descriptors & Default State
// =============================================================================

/**
 * @brief Verifies that default-constructed AccessDescriptor has all bits
 * cleared and correctly reports is_empty().
 */
void test_empty_descriptors() {
  std::cout << "Testing Empty Descriptors & Default State... ";

  AccessDescriptor default_desc{};
  assert(default_desc.is_empty());
  assert(default_desc.read_components.empty());
  assert(default_desc.write_components.empty());
  assert(default_desc.read_resources.empty());
  assert(default_desc.write_resources.empty());

  // Empty builder produces empty descriptor
  AccessDescriptor built_empty = AccessBuilder{}.build();
  assert(built_empty.is_empty());

  // Non-empty descriptor reports is_empty() == false
  AccessDescriptor read_only = AccessBuilder{}.reads<Position>().build();
  assert(!read_only.is_empty());

  AccessDescriptor write_only = AccessBuilder{}.writes<Velocity>().build();
  assert(!write_only.is_empty());

  AccessDescriptor
      res_read = AccessBuilder{}.reads_resource<TimeStep>().build();
  assert(!res_read.is_empty());

  AccessDescriptor
      res_write = AccessBuilder{}.writes_resource<Gravity>().build();
  assert(!res_write.is_empty());

  // Two empty descriptors never conflict
  assert(!default_desc.conflicts_with(built_empty));
  assert(!built_empty.conflicts_with(default_desc));

  // An empty descriptor never conflicts with an active descriptor
  assert(!default_desc.conflicts_with(read_only));
  assert(!read_only.conflicts_with(default_desc));
  assert(!default_desc.conflicts_with(write_only));
  assert(!write_only.conflicts_with(default_desc));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Component Conflict Matrix (RAR, RAW, WAR, WAW)
// =============================================================================

/**
 * @brief Exhaustively validates the 4 fundamental concurrent access cases for
 * entity components.
 * - RAR (Read-After-Read): SAFE (no conflict)
 * - RAW (Read-After-Write): CONFLICT
 * - WAR (Write-After-Read): CONFLICT
 * - WAW (Write-After-Write): CONFLICT
 */
void test_component_conflict_matrix() {
  std::cout << "Testing Component Conflict Matrix (RAR, RAW, WAR, WAW)... ";

  // System A: reads Position
  AccessDescriptor sys_a_read = AccessBuilder{}.reads<Position>().build();

  // System B: reads Position
  AccessDescriptor sys_b_read = AccessBuilder{}.reads<Position>().build();

  // System C: writes Position
  AccessDescriptor sys_c_write = AccessBuilder{}.writes<Position>().build();

  // System D: reads Velocity (completely disjoint)
  AccessDescriptor sys_d_disjoint = AccessBuilder{}
                                        .reads<Velocity>()
                                        .writes<Acceleration>()
                                        .build();

  // 1. RAR: Read-After-Read -> SAFE (both read Position simultaneously)
  assert(!sys_a_read.conflicts_with(sys_b_read));
  assert(!sys_b_read.conflicts_with(sys_a_read));

  // 2. RAW: System C writes Position, System A reads Position -> CONFLICT
  assert(sys_c_write.conflicts_with(sys_a_read));
  assert(sys_a_read.conflicts_with(sys_c_write));

  // 3. WAR: System A reads Position, System C writes Position -> CONFLICT
  assert(sys_a_read.conflicts_with(sys_c_write));
  assert(sys_c_write.conflicts_with(sys_a_read));

  // 4. WAW: System C writes Position, System E also writes Position -> CONFLICT
  AccessDescriptor sys_e_write = AccessBuilder{}.writes<Position>().build();
  assert(sys_c_write.conflicts_with(sys_e_write));
  assert(sys_e_write.conflicts_with(sys_c_write));

  // 5. Disjoint components -> SAFE
  assert(!sys_a_read.conflicts_with(sys_d_disjoint));
  assert(!sys_d_disjoint.conflicts_with(sys_a_read));
  assert(!sys_c_write.conflicts_with(sys_d_disjoint));
  assert(!sys_d_disjoint.conflicts_with(sys_c_write));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Resource Conflict Matrix (RAR, RAW, WAR, WAW)
// =============================================================================

/**
 * @brief Validates concurrent access conflict detection for World Resources.
 */
void test_resource_conflict_matrix() {
  std::cout << "Testing Resource Conflict Matrix (RAR, RAW, WAR, WAW)... ";

  // System A: reads Gravity
  AccessDescriptor
      sys_a_read = AccessBuilder{}.reads_resource<Gravity>().build();

  // System B: reads Gravity
  AccessDescriptor
      sys_b_read = AccessBuilder{}.reads_resource<Gravity>().build();

  // System C: writes Gravity
  AccessDescriptor
      sys_c_write = AccessBuilder{}.writes_resource<Gravity>().build();

  // System D: disjoint resource TimeStep
  AccessDescriptor sys_d_disjoint = AccessBuilder{}
                                        .reads_resource<TimeStep>()
                                        .writes_resource<CollisionConfig>()
                                        .build();

  // 1. RAR: SAFE
  assert(!sys_a_read.conflicts_with(sys_b_read));
  assert(!sys_b_read.conflicts_with(sys_a_read));

  // 2. RAW: CONFLICT
  assert(sys_c_write.conflicts_with(sys_a_read));
  assert(sys_a_read.conflicts_with(sys_c_write));

  // 3. WAR: CONFLICT
  assert(sys_a_read.conflicts_with(sys_c_write));
  assert(sys_c_write.conflicts_with(sys_a_read));

  // 4. WAW: CONFLICT
  AccessDescriptor
      sys_e_write = AccessBuilder{}.writes_resource<Gravity>().build();
  assert(sys_c_write.conflicts_with(sys_e_write));
  assert(sys_e_write.conflicts_with(sys_c_write));

  // 5. Disjoint resources -> SAFE
  assert(!sys_a_read.conflicts_with(sys_d_disjoint));
  assert(!sys_d_disjoint.conflicts_with(sys_a_read));
  assert(!sys_c_write.conflicts_with(sys_d_disjoint));
  assert(!sys_d_disjoint.conflicts_with(sys_c_write));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Cross-Domain Isolation (Components vs Resources)
// =============================================================================

/**
 * @brief Verifies that component bitmasks and resource bitmasks operate in
 * completely disjoint domains even if numeric IDs overlap.
 */
void test_cross_domain_isolation() {
  std::cout << "Testing Cross-Domain Isolation (Components vs Resources)... ";

  // System 1 only modifies component Position
  AccessDescriptor sys1 = AccessBuilder{}.writes<Position>().build();

  // System 2 only reads or writes resource TimeStep
  AccessDescriptor sys2 = AccessBuilder{}
                              .reads_resource<TimeStep>()
                              .writes_resource<Gravity>()
                              .build();

  // Even if component_id<Position>() == resource_id<TimeStep>(), they must not
  // conflict!
  assert(!sys1.conflicts_with(sys2));
  assert(!sys2.conflicts_with(sys1));

  // Mixed system with component and resource overlap
  AccessDescriptor sys3 = AccessBuilder{}
                              .reads<Position>()
                              .writes_resource<TimeStep>()
                              .build();

  // sys1 writes Position, sys3 reads Position -> conflict on component
  assert(sys1.conflicts_with(sys3));
  assert(sys3.conflicts_with(sys1));

  // sys2 reads TimeStep, sys3 writes TimeStep -> conflict on resource
  assert(sys2.conflicts_with(sys3));
  assert(sys3.conflicts_with(sys2));

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Variadic Pack Registration & Bit Verification
// =============================================================================

/**
 * @brief Verifies that multi-type variadic template methods set every
 * corresponding bit in the 128-bit SIMD bitmasks.
 */
void test_variadic_pack_registration() {
  std::cout << "Testing Variadic Pack Registration & Bit Verification... ";

  AccessDescriptor desc = AccessBuilder{}
                              .reads<Position, Velocity, Mass>()
                              .writes<Acceleration, Health>()
                              .reads_resource<TimeStep, Gravity>()
                              .writes_resource<CollisionConfig>()
                              .build();

  // Check read components
  assert(desc.read_components.test(component_id<Position>()));
  assert(desc.read_components.test(component_id<Velocity>()));
  assert(desc.read_components.test(component_id<Mass>()));
  assert(!desc.read_components.test(component_id<Acceleration>()));
  assert(!desc.read_components.test(component_id<Health>()));
  assert(desc.read_components.count() == 3);

  // Check write components
  assert(desc.write_components.test(component_id<Acceleration>()));
  assert(desc.write_components.test(component_id<Health>()));
  assert(!desc.write_components.test(component_id<Position>()));
  assert(desc.write_components.count() == 2);

  // Check read resources
  assert(desc.read_resources.test(resource_id<TimeStep>()));
  assert(desc.read_resources.test(resource_id<Gravity>()));
  assert(!desc.read_resources.test(resource_id<CollisionConfig>()));
  assert(desc.read_resources.count() == 2);

  // Check write resources
  assert(desc.write_resources.test(resource_id<CollisionConfig>()));
  assert(!desc.write_resources.test(resource_id<TimeStep>()));
  assert(desc.write_resources.count() == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 6: Fluent Builder Chaining Order & Type Alias
// =============================================================================

/**
 * @brief Verifies that builder method chaining order is commutative and that
 * the `Access` type alias is completely interchangeable with `AccessBuilder`.
 */
void test_fluent_builder_chaining_and_alias() {
  std::cout << "Testing Fluent Builder Chaining Order & Type Alias... ";

  // Order A
  AccessDescriptor descA = Access{}
                               .reads<Position>()
                               .writes<Velocity>()
                               .reads_resource<Gravity>()
                               .writes_resource<TimeStep>()
                               .build();

  // Order B (inverted chaining sequence)
  AccessDescriptor descB = AccessBuilder{}
                               .writes_resource<TimeStep>()
                               .reads_resource<Gravity>()
                               .writes<Velocity>()
                               .reads<Position>()
                               .build();

  assert(descA.read_components == descB.read_components);
  assert(descA.write_components == descB.write_components);
  assert(descA.read_resources == descB.read_resources);
  assert(descA.write_resources == descB.write_resources);

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "   Module 020: AccessDescriptor & Conflict Tests    \n";
  std::cout << "====================================================\n";

  test_empty_descriptors();
  test_component_conflict_matrix();
  test_resource_conflict_matrix();
  test_cross_domain_isolation();
  test_variadic_pack_registration();
  test_fluent_builder_chaining_and_alias();

  std::cout << "\nAll AccessDescriptor unit tests passed successfully!\n";
  return 0;
}
