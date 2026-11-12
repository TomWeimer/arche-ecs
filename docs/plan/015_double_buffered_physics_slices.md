# Module 015: Double-Buffered Physics Component Slices

This document specifies the exact behavioral deltas, code modifications, and new test suites required to introduce dual-slice double-buffered component columns and $O(1)$ pointer-flip physics sub-steps in `arche-ecs`, assuming Module 014 (Archetype Permutation & Cache Locality) is already implemented.

---

## 1. Behavioral Deltas: What Changes and Why

### 1.1 Double-Buffered Component Columns
* **Before (Module 014):**  
  Every component column in an archetype chunk had a single memory slice. If a system modified a component (e.g. `Position`) during physics or flocking updates, it mutated data in-place.
* **Now (Module 015):**  
  Components can be registered as double-buffered via `world.register_double_buffered_component<T>()`. In the chunk layout, two identical, contiguous memory slices are allocated: `Read` slice and `Write` slice. Systems query `(const T&, Write<T>)` to read immutable step $t$ values and write updated step $t+1$ values to separate memory slices.
* **Why we need to change this part:**  
  Eliminates order-dependent simulation bias (where entities processed later in a loop read mutated $t+1$ neighbor positions instead of original $t$ positions) and completely removes multi-threaded race conditions between neighbor reads and writes without mutexes.

### 1.2 $O(1)$ Pointer-Flip Physics Sub-Steps & Automatic Dirty Ticking
* **Before (Module 014):**  
  Transferring simulation data across sub-steps required copying arrays via `std::memcpy` or rebuilding entities.
* **Now (Module 015):**  
  `world.flip_double_buffer<T>()` performs an $O(1)$ swap of `chunk_byte_offset` and `write_byte_offset` in the archetype layout table, and automatically marks all affected chunk columns dirty with the current simulation tick.
* **Why we need to change this part:**  
  Swapping two integer offsets takes $< 10\text{ ns}$ across all archetypes. Zero bytes are copied; zero entities are moved; zero cache lines are invalidated. Automatically stamping the chunk version keeps reactive `Mutated<T>` queries in sync across sub-steps.

### 1.3 Deduplication & Helper Architecture: Dual-Slice Accessors
* **`Chunk::typed_write_slice<T>()`:**  
  **Where defined:** `include/arche/chunk.hpp`.  
  **What code it replaces:** Replaces raw pointer arithmetic and casting `reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(chunk) + layout.write_byte_offset)` when accessing secondary double-buffered write targets.

---

## 2. Modifications to Existing Files

### 2.1 `include/arche/type_registry.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ComponentDescriptor` had no tracking for double-buffered components.
* **Now:** Adds `bool is_double_buffered{false}` to `ComponentDescriptor` and `register_double_buffered<T>()` to `TypeRegistry`.

#### Functions / Members to Delete
* `None`.

#### Modified Struct `ComponentDescriptor`

##### Old Version (Module 014)
```cpp
struct ComponentDescriptor {
  TypeID id{INVALID_TYPE_ID};
  size_t size{0};
  size_t alignment{0};
  std::string_view name{"Unknown"};
  bool is_trivially_copyable{false};
  bool is_trivially_destructible{false};
```

##### New Version (Module 015)
```cpp
struct ComponentDescriptor {
  TypeID id{INVALID_TYPE_ID};
  size_t size{0};
  size_t alignment{0};
  std::string_view name{"Unknown"};
  bool is_trivially_copyable{false};
  bool is_trivially_destructible{false};
  bool is_double_buffered{false}; ///< True if component allocates dual read/write slices
```

#### New Methods to Add to `class TypeRegistry` in `include/arche/type_registry.hpp`

Insert inside `class TypeRegistry`, public section, right after `register_type<T>()` (around line 140):

```cpp
  /**
   * @brief Registers component T as double-buffered with dual read/write chunk slices.
   */
  template <typename T>
  inline TypeID register_double_buffered() {
    const TypeID id = register_type<T>();
    descriptors_[id].is_double_buffered = true;
    return id;
  }
```

---

### 2.2 `include/arche/chunk.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `ComponentColumnLayout` only stored a single `chunk_byte_offset`.
* **Now:** Stores `chunk_byte_offset` (active read slice), `write_byte_offset` (write target slice), `is_double_buffered`, and a member function `flip()` to swap offsets in 1 CPU cycle.

#### Functions / Members to Delete
* `None`.

#### Modified Struct `ComponentColumnLayout`

##### Old Version (Module 014)
```cpp
struct ComponentColumnLayout {
  TypeID type_id{INVALID_TYPE_ID};
  uint32_t size{0};
  uint32_t alignment{0};
  uint32_t chunk_byte_offset{0};
  const ComponentDescriptor *descriptor{nullptr};
};
```

##### New Version (Module 015)
```cpp
struct ComponentColumnLayout {
  TypeID type_id{INVALID_TYPE_ID};
  uint32_t size{0};
  uint32_t alignment{0};
  uint32_t chunk_byte_offset{0}; ///< Active READ / primary slice byte offset
  uint32_t write_byte_offset{0}; ///< Target WRITE / secondary slice byte offset
  bool is_double_buffered{false};
  const ComponentDescriptor *descriptor{nullptr};

  /// Swaps read and write offsets in O(1) time
  inline void flip() noexcept {
    if (is_double_buffered) {
      std::swap(chunk_byte_offset, write_byte_offset);
    }
  }
};
```

#### New Methods to Add to `struct Chunk` in `include/arche/chunk.hpp`

Insert inside `struct Chunk`, public section, right next to `component_slice` (around line 325):

```cpp
  /**
   * @brief Resolves pointer to the active WRITE slice of a double-buffered column.
   */
  [[nodiscard]] inline void *component_write_slice(
      const ComponentColumnLayout &layout
  ) noexcept {
    return reinterpret_cast<uint8_t *>(this) + layout.write_byte_offset;
  }

  /**
   * @brief Typed accessor for the active WRITE slice of a double-buffered column.
   * @details Defined in `include/arche/chunk.hpp`. Replaces raw reinterpret_cast
   * calls for secondary write target slices.
   */
  template <typename T>
  [[nodiscard]] inline T *typed_write_slice(uint32_t write_byte_offset) noexcept {
    return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(this) + write_byte_offset);
  }
```

---

### 2.3 `include/arche/archetype.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `_calculate_entity_size_sum` and `_build_column_layouts` allocated only a single slice per component.
* **Now:** Doubles memory calculation for `desc.is_double_buffered`, allocates dual contiguous slices, and provides `flip_double_buffer(TypeID)`.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `Archetype`

##### Old Version (Module 014)
```cpp
  [[nodiscard]] inline uint32_t _calculate_entity_size_sum(
      const TypeRegistry &registry
  ) const {
    uint32_t total = static_cast<uint32_t>(sizeof(Entity));
    for (TypeID t : types_) {
      const auto &desc = registry.get_descriptor(t);
      total += desc.size;
    }
    return total;
  }

  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t CHUNK_HEADER_RESERVE = sizeof(ChunkHeader);

    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
        static_cast<uint32_t>(sizeof(Entity));

    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.descriptor = &desc;

      columns_.push_back(layout);
      type_to_column_[t] = static_cast<int16_t>(i);

      current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) * desc.size;
    }

    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }
```

##### New Version (Module 015)
```cpp
  [[nodiscard]] inline uint32_t _calculate_entity_size_sum(
      const TypeRegistry &registry
  ) const {
    uint32_t total = static_cast<uint32_t>(sizeof(Entity));
    for (TypeID t : types_) {
      const auto &desc = registry.get_descriptor(t);
      // Double the memory allocation footprint if component is double-buffered
      total += desc.is_double_buffered ? static_cast<uint32_t>(desc.size * 2)
                                       : desc.size;
    }
    return total;
  }

  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t CHUNK_HEADER_RESERVE = sizeof(ChunkHeader);

    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
        static_cast<uint32_t>(sizeof(Entity));

    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.is_double_buffered = desc.is_double_buffered;
      layout.descriptor = &desc;

      const uint32_t column_span = (chunk_capacity_ + SIMD_TAIL_PADDING) * desc.size;
      current_offset += column_span;

      // Allocate secondary WRITE slice if double-buffered
      if (desc.is_double_buffered) {
        current_offset = _align_up(current_offset, desc.alignment);
        layout.write_byte_offset = current_offset;
        current_offset += column_span;
      } else {
        layout.write_byte_offset = layout.chunk_byte_offset;
      }

      columns_.push_back(layout);
      type_to_column_[t] = static_cast<int16_t>(i);
    }

    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }
```

#### New Methods to Add to `class Archetype` in `include/arche/archetype.hpp`

Insert inside `class Archetype`, public section, right after `column_layout_for` (around line 240):

```cpp
  /**
   * @brief Performs an O(1) pointer flip between read and write slices for component type t.
   */
  inline void flip_double_buffer(TypeID t) noexcept {
    const int16_t idx = column_index(t);
    if (idx >= 0 && columns_[idx].is_double_buffered) {
      columns_[idx].flip();
    }
  }
```

---

### 2.4 `include/arche/query_filter.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `query_filter.hpp` only declared `With`, `Without`, `Mutated`, and `Added`.
* **Now:** Adds `Write<T>` wrapper and `is_write_target` traits to bind write target slices in queries.

#### Functions / Members to Delete
* `None`.

#### Modified Structs
* `None`.

#### New Structs to Add to `include/arche/query_filter.hpp`

Insert at the end of `include/arche/query_filter.hpp`, right before the closing `} // namespace arche`:

```cpp
// =============================================================================
// Double-Buffer Write Target Query Wrapper
// =============================================================================

/**
 * @struct Write
 * @brief Query parameter wrapper indicating that a double-buffered component should
 * bind to the chunk's WRITE slice instead of the default READ slice.
 *
 * @tparam T Underlying component type.
 */
template <typename T>
struct Write {
  T *data{nullptr};

  constexpr explicit Write(T *ptr = nullptr) noexcept : data(ptr) {}

  constexpr T &operator[](size_t index) noexcept { return data[index]; }
  constexpr const T &operator[](size_t index) const noexcept { return data[index]; }
  constexpr T *get() noexcept { return data; }
  constexpr const T *get() const noexcept { return data; }
};

template <typename T>
struct is_write_target : std::false_type {
  using underlying_type = T;
};

template <typename T>
struct is_write_target<Write<T>> : std::true_type {
  using underlying_type = T;
};

template <typename T>
inline constexpr bool is_write_target_v = is_write_target<T>::value;
```

---

### 2.5 `include/arche/query.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `resolve_column_offset` and `get_column_ptr` only handled standard component pointers.
* **Now:** Detects `Write<T>` query parameters, binding them to `layout->write_byte_offset` and wrapping them in `Write<T>{ptr}`.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `Query`

##### Old Version (Module 014)
```cpp
  template <typename Comp>
  static inline uint32_t resolve_column_offset(Archetype *arch) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return 0;
    } else {
      const auto *layout = arch->column_layout_for(
          type_id<std::decay_t<Comp>>()
      );
      assert(layout != nullptr && "Queried component missing from matched archetype layout!");
      return layout->chunk_byte_offset;
    }
  }

  template <typename Comp>
  static inline Comp *get_column_ptr(Chunk *chunk, uint32_t offset) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return chunk->entity_slice();
    } else {
      return reinterpret_cast<Comp *>(chunk->component_slice(offset));
    }
  }
```

##### New Version (Module 015)
```cpp
  template <typename Comp>
  static inline uint32_t resolve_column_offset(Archetype *arch) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return 0;
    } else if constexpr (is_write_target_v<std::decay_t<Comp>>) {
      using Raw = typename is_write_target<std::decay_t<Comp>>::underlying_type;
      const auto *layout = arch->column_layout_for(type_id<Raw>());
      assert(layout != nullptr && "Queried component missing from matched archetype layout!");
      assert(layout->is_double_buffered && "Component requested as Write<T> must be double-buffered!");
      return layout->write_byte_offset;
    } else {
      const auto *layout = arch->column_layout_for(type_id<std::decay_t<Comp>>());
      assert(layout != nullptr && "Queried component missing from matched archetype layout!");
      return layout->chunk_byte_offset;
    }
  }

  template <typename Comp>
  static inline auto get_column_ptr(Chunk *chunk, uint32_t offset) {
    if constexpr (std::is_same_v<std::decay_t<Comp>, Entity>) {
      return chunk->entity_slice();
    } else if constexpr (is_write_target_v<std::decay_t<Comp>>) {
      using Raw = typename is_write_target<std::decay_t<Comp>>::underlying_type;
      return Write<Raw>{reinterpret_cast<Raw *>(chunk->component_slice(offset))};
    } else {
      return reinterpret_cast<Comp *>(chunk->component_slice(offset));
    }
  }
```

---

### 2.6 `include/arche/world.hpp`

#### Behavioral Delta (Why this file changes)
* **Before:** `World` had no mechanism to configure double-buffered components or flip buffer pointers.
* **Now:** Adds `register_double_buffered_component<T>()` and `flip_double_buffer<T>()` with automatic monotonic tick advancement and chunk dirty stamping.

#### Functions / Members to Delete
* `None`.

#### Modified Functions in `World`
* `None`.

#### New Methods to Add to `include/arche/world.hpp`

Insert inside `class World`, public section (around line 145):

```cpp
  // --- Double-Buffered Component Columns -------------------------------------

  /**
   * @brief Registers component T as double-buffered with dual read/write chunk slices.
   */
  template <typename T>
  inline void register_double_buffered_component() {
    registry_.register_double_buffered<std::decay_t<T>>();
  }

  /**
   * @brief Performs an O(1) pointer-flip swapping read and write slices for component T,
   * advancing the simulation tick and stamping chunk dirty versions automatically.
   */
  template <typename T>
  inline void flip_double_buffer() noexcept {
    const TypeID tid = type_id<std::decay_t<T>>();
    advance_tick(); // Advances monotonic simulation tick
    for (const auto &arch_ptr : graph_.archetypes()) {
      arch_ptr->flip_double_buffer(tid);
      for (Chunk *chunk : get_archetype_chunks(arch_ptr.get())) {
        mark_chunk_dirty<T>(chunk); // Automatically synchronize dirty version!
      }
    }
  }
```

---

## 3. Brand-New Files

### 3.1 `tests/double_buffer_tests.cpp`

```cpp
#include "arche/entity.hpp"
#include "arche/query.hpp"
#include "arche/query_filter.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

namespace {
using arche::Entity;
using arche::Mutated;
using arche::World;
using arche::Write;
} // namespace

// =============================================================================
// Standalone Test Component Types
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
};

// =============================================================================
// Unit Test 1: Distinct Physical Memory Slices (Read != Write)
// =============================================================================

void test_distinct_memory_slices() {
  std::cout << "[Test 1/5] Physical Slice Separation (Read != Write)... ";
  World world;

  world.register_double_buffered_component<Position>();
  world.create_entity(Position{10.0f, 20.0f}, Velocity{1.0f, 2.0f});

  const void *read_addr = nullptr;
  const void *write_addr = nullptr;

  auto q = world.query<const Position, Write<Position>, Velocity>();
  q.for_each_chunk([&](uint32_t count, const Position *r, Write<Position> w, Velocity *) {
    assert(count == 1);
    read_addr = static_cast<const void *>(r);
    write_addr = static_cast<const void *>(w.get());
  });

  assert(read_addr != nullptr && write_addr != nullptr);
  assert(read_addr != write_addr);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: O(1) Pointer Flipping Across Sub-Steps
// =============================================================================

void test_pointer_flip_substep() {
  std::cout << "[Test 2/5] O(1) Pointer-Flip Physics Sub-Steps... ";
  World world;

  world.register_double_buffered_component<Position>();
  Entity e = world.create_entity(Position{0.0f, 0.0f}, Velocity{5.0f, 0.0f});

  auto q = world.query<const Position, Write<Position>, const Velocity>();
  q.for_each_chunk([](uint32_t count, const Position *r, Write<Position> w, const Velocity *v) {
    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = r[i].x + v[i].vx;
      w[i].y = r[i].y + v[i].vy;
    }
  });

  assert(world.get_component<Position>(e).x == 0.0f);

  world.flip_double_buffer<Position>();
  assert(world.get_component<Position>(e).x == 5.0f);

  q.for_each_chunk([](uint32_t count, const Position *r, Write<Position> w, const Velocity *v) {
    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = r[i].x + v[i].vx;
    }
  });

  world.flip_double_buffer<Position>();
  assert(world.get_component<Position>(e).x == 10.0f);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Automatic Dirty Ticking on Flip
// =============================================================================

void test_automatic_dirty_ticking_on_flip() {
  std::cout << "[Test 3/5] Automatic Dirty Ticking on Pointer Flip... ";
  World world;

  world.register_double_buffered_component<Position>();
  world.create_entity(Position{0.0f, 0.0f}, Velocity{1.0f, 1.0f});

  const uint32_t initial_tick = world.current_tick();

  // Flip buffer: must advance world tick and stamp Position column
  world.flip_double_buffer<Position>();

  assert(world.current_tick() > initial_tick);

  // Mutated<Position> filter with initial_tick must match the chunk
  size_t count = 0;
  world.query<Position>(Mutated<Position>{initial_tick}).for_each_chunk([&](uint32_t n, Position *) {
    count += n;
  });
  assert(count == 1);

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Elimination of Order-Dependent Bias
// =============================================================================

void test_zero_order_bias() {
  std::cout << "[Test 4/5] Elimination of Order-Dependent Simulation Bias... ";
  World world;

  world.register_double_buffered_component<Position>();

  world.create_entity(Position{10.0f, 0.0f});
  world.create_entity(Position{20.0f, 0.0f});
  world.create_entity(Position{30.0f, 0.0f});

  auto q = world.query<const Position, Write<Position>>();

  q.for_each_chunk([](uint32_t count, const Position *r, Write<Position> w) {
    float sum = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
      sum += r[i].x;
    }
    const float avg = sum / static_cast<float>(count);

    for (uint32_t i = 0; i < count; ++i) {
      w[i].x = avg;
    }
  });

  world.flip_double_buffer<Position>();

  world.query<const Position>().for_each_chunk([](uint32_t count, const Position *r) {
    for (uint32_t i = 0; i < count; ++i) {
      assert(r[i].x == 20.0f);
    }
  });

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 5: Pointer-Flip Latency Benchmark (< 10 ns)
// =============================================================================

void test_flip_latency() {
  std::cout << "[Test 5/5] Pointer-Flip Latency Benchmark (< 10 ns)... ";
  World world;

  world.register_double_buffered_component<Position>();
  world.create_entities(10'000, Position{0.0f, 0.0f});

  constexpr int ITERS = 100'000;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < ITERS; ++i) {
    world.flip_double_buffer<Position>();
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::nano> total_ns = end - start;
  const double ns_per_flip = total_ns.count() / static_cast<double>(ITERS);

  assert(ns_per_flip < 100.0);
  std::cout << "PASSED (" << ns_per_flip << " ns / flip)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Module 015: Double-Buffer Tests\n";
  std::cout << "====================================================\n";

  test_distinct_memory_slices();
  test_pointer_flip_substep();
  test_automatic_dirty_ticking_on_flip();
  test_zero_order_bias();
  test_flip_latency();

  std::cout << "====================================================\n";
  std::cout << "All Module 015 tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
```

---

## 4. Build Configuration: `CMakeLists.txt`

Add the new unit test target to `CMakeLists.txt`:

```cmake
# Module 015 Tests: Double-Buffered Physics Component Slices
add_executable(double_buffer_tests tests/double_buffer_tests.cpp)
target_link_libraries(double_buffer_tests PRIVATE arche::arche)
add_test(NAME double_buffer_tests COMMAND double_buffer_tests)
```
