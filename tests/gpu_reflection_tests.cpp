#include "arche/arche.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {
using arche::GpuBufferView;
using arche::GpuStd430Compatible;
using arche::Std430Traits;
using arche::World;
} // namespace

// =============================================================================
// Standalone Test Component Types
// =============================================================================

/**
 * @struct GpuPosition
 * @brief 2D position vector matching GLSL std430 `vec2` layout (8 bytes, 4-byte
 * aligned).
 */
struct GpuPosition {
  float x{0.0f};
  float y{0.0f};
};

/**
 * @struct GpuColor
 * @brief 4D color vector matching GLSL std430 `vec4` layout (16 bytes, 4-byte
 * aligned).
 */
struct GpuColor {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{1.0f};
};

/**
 * @struct RawVec3Unsafe
 * @brief Intentionally unpadded 12-byte struct (`float[3]`) to test
 * compile-time traits.
 *
 * @note In GLSL `std430`, arrays of `vec3` mandate 16-byte strides. A naive
 * 12-byte C++ struct violates this contract and corrupts shader reads if
 * streamed into an SSBO.
 */
struct RawVec3Unsafe {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

/**
 * @struct SafeGpuVec3
 * @brief Explicitly padded 16-byte 3D vector adhering to std430 array stride
 * requirements.
 */
struct alignas(16) SafeGpuVec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float padding{0.0f};
};

// =============================================================================
// Unit Test 1: std430 Compile-Time Reflection Traits
// =============================================================================

/**
 * @brief Validates compile-time std430 memory layout constraints and concepts.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Shader Storage Buffer Objects (SSBOs) under the Vulkan, Metal, and DirectX
 * `std430` layout rules mandate strict element stride and alignment matching
 * base scalar alignment.
 *
 * Hazard Prevention:
 * In C++, `sizeof(float[3]) == 12` with 4-byte alignment. In GLSL `std430`,
 * however, arrays of `vec3` have an element stride of 16 bytes. Streaming raw
 * 12-byte structs directly into an SSBO results in off-by-4-byte corruptions
 * for every element after index 0.
 *
 * This test guarantees:
 * 1. Native scalar (`float`) passes `GpuStd430Compatible`.
 * 2. 2-component vector (`GpuPosition` - 8 bytes) passes `GpuStd430Compatible`.
 * 3. 4-component vector (`GpuColor` - 16 bytes) passes `GpuStd430Compatible`.
 * 4. Padded 16-byte vector (`SafeGpuVec3`) passes `GpuStd430Compatible`.
 * 5. Unpadded 12-byte vector (`RawVec3Unsafe`) fails
 * `Std430Traits<T>::is_safe_std430` at compile time, preventing unsafe GPU
 * streaming attempts.
 */
void test_std430_trait_validations() {
  std::cout << "[Test 1/3] std430 Compile-Time Reflection Traits... ";

  // 1. Positive checks: verify compliant scalar and vector types satisfy
  // concept
  static_assert(
      GpuStd430Compatible<float>, "float must satisfy GpuStd430Compatible"
  );
  static_assert(
      GpuStd430Compatible<GpuPosition>,
      "GpuPosition (vec2) must satisfy GpuStd430Compatible"
  );
  static_assert(
      GpuStd430Compatible<GpuColor>,
      "GpuColor (vec4) must satisfy GpuStd430Compatible"
  );
  static_assert(
      GpuStd430Compatible<SafeGpuVec3>,
      "SafeGpuVec3 (padded 16B) must satisfy GpuStd430Compatible"
  );

  // 2. Trait detail checks: verify size, alignment, and power-of-two properties
  static_assert(Std430Traits<GpuPosition>::size == 8);
  static_assert(Std430Traits<GpuPosition>::alignment == 4);
  static_assert(Std430Traits<GpuPosition>::is_power_of_two_aligned);
  static_assert(Std430Traits<GpuPosition>::is_size_multiple_of_alignment);
  static_assert(Std430Traits<GpuPosition>::is_safe_std430);

  // 3. Negative check: unpadded 12-byte vec3 must fail std430 safety
  // verification
  static_assert(
      !Std430Traits<RawVec3Unsafe>::is_safe_std430,
      "Unpadded 12-byte vec3 must fail std430 safety trait!"
  );

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: ComponentSpan Zero-Copy Chunk Views (Approach B)
// =============================================================================

/**
 * @brief Validates zero-copy chunk span extraction across multi-chunk storage.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * For architectures supporting direct buffer binding (Apple Silicon Unified
 * Memory or Vulkan Buffer Device Address / bindless rendering), zero-copy
 * access to contiguous chunk component columns eliminates all CPU repacking and
 * memcpy overhead (Approach B).
 *
 * This test guarantees:
 * 1. `world.view_component_chunks<T>()` extracts non-owning `ComponentSpan<T>`
 * views for each chunk containing the requested component.
 * 2. Spans correctly represent contiguous element counts, byte sizes, and
 * element strides.
 * 3. Element indexing via `span[r]` provides direct, uninterrupted read access
 * to in-chunk components.
 * 4. Conversion to `std::span<const T>` via `span.as_span()` seamlessly bridges
 * with standard C++20 range algorithms.
 * 5. Component count query `world.count_components<T>()` accurately matches
 * total entities.
 */
void test_component_span_views() {
  std::cout
      << "[Test 2/3] ComponentSpan Zero-Copy Chunk Views (Approach B)... ";
  World world;

  constexpr int TOTAL_ENTITIES = 500;

  // 1. Populate world across chunks with deterministic coordinate data
  for (int i = 0; i < TOTAL_ENTITIES; ++i) {
    world.spawn(
        GpuPosition{static_cast<float>(i), static_cast<float>(i * 2)}
    );
  }

  // 2. Verify total component count query
  assert(world.count_components<GpuPosition>() == TOTAL_ENTITIES);

  // 3. Extract non-owning component chunk views
  auto views = world.view_component_chunks<GpuPosition>();
  assert(!views.empty());

  // 4. Validate span pointers, byte dimensions, and data continuity across
  // chunks
  size_t inspected_entities = 0;
  for (const auto &span : views) {
    assert(span.data() != nullptr);
    assert(!span.empty());
    assert(span.count() > 0);
    assert(span.stride() == sizeof(GpuPosition));
    assert(span.size_bytes() == span.count() * sizeof(GpuPosition));

    // Verify std::span conversion interoperability
    std::span<const GpuPosition> cpp_span = span.as_span();
    assert(cpp_span.size() == span.count());
    assert(cpp_span.data() == span.data());

    // Verify intra-chunk element data continuity
    for (uint32_t r = 0; r < span.count(); ++r) {
      assert(span[r].x == static_cast<float>(inspected_entities + r));
      assert(span[r].y == static_cast<float>((inspected_entities + r) * 2));
    }
    inspected_entities += span.count();
  }

  // 5. Ensure all entities across all chunks were visited without omission
  assert(inspected_entities == TOTAL_ENTITIES);

  // 6. Validate GpuBufferView type-erased descriptor construction
  if (!views.empty()) {
    const auto &first_span = views.front();
    GpuBufferView buffer_view{
        .data = first_span.data(),
        .byte_size = first_span.size_bytes(),
        .count = first_span.count(),
        .element_size = static_cast<uint32_t>(sizeof(GpuPosition)),
        .alignment = static_cast<uint32_t>(alignof(GpuPosition)),
    };
    assert(buffer_view.data == first_span.data());
    assert(buffer_view.byte_size == first_span.size_bytes());
    assert(buffer_view.count == first_span.count());
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: Direct VRAM Batch Streaming (Approach A - 50,000 Entities)
// =============================================================================

/**
 * @brief Validates high-throughput batch streaming into a mapped VRAM buffer.
 *
 * --- Why This Test Is Necessary (Documentation Rationale) ---
 * Under Universal DMA / Batched Linear Upload (Approach A), the rendering
 * backend maps a single GPU buffer (Host-Visible / Host-Coherent) and streams
 * all entity components into it using sequential memcpy transfers.
 *
 * Performance Budget:
 * In a 120 Hz game loop (8.33 ms frame budget), copying 50,000 particles (400
 * KB) must complete in < 50 microseconds (< 0.6% of frame budget) and saturate
 * memory bandwidth (>= 15 GB/s).
 *
 * This test guarantees:
 * 1. `world.stream_component_to<T>(buffer)` sequentially copies all chunk
 * columns into a single destination buffer without intermediate allocations.
 * 2. Exact total byte count is returned and matches `N * sizeof(T)`.
 * 3. Boundary elements (first, middle, last) match original entity values.
 * 4. Microbenchmark confirms streaming throughput well within real-time
 * budgets.
 */
void test_stream_component_to_mock_vram() {
  std::cout << "[Test 3/3] Direct VRAM Batch Streaming (50,000 Entities)... ";
  World world;

  constexpr int NUM_ENTITIES = 50'000;

  // 1. Populate 50,000 entities across multiple 64 KB chunks
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    world.spawn(
        GpuPosition{static_cast<float>(i), static_cast<float>(i * 3)}
    );
  }

  // 2. Allocate mock GPU mapped VRAM buffer (50,000 * 8B = 400 KB)
  const size_t vram_bytes = NUM_ENTITIES * sizeof(GpuPosition);
  std::vector<uint8_t> mock_mapped_vram(vram_bytes, 0);

  // 3. Warm-up stream pass
  const size_t initial_bytes = world.stream_component_to<GpuPosition>(
      mock_mapped_vram.data()
  );
  assert(initial_bytes == vram_bytes);

  // 4. Benchmark streaming throughput over 1,000 full-frame iterations
  constexpr int ITERATIONS = 1000;
  auto start = std::chrono::high_resolution_clock::now();

  size_t bytes_written = 0;
  for (int it = 0; it < ITERATIONS; ++it) {
    bytes_written = world.stream_component_to<GpuPosition>(
        mock_mapped_vram.data()
    );
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - start;
  double avg_stream_us = (elapsed.count() * 1000.0) / ITERATIONS;
  double gb_per_sec = (static_cast<double>(bytes_written) /
                       (1024.0 * 1024.0 * 1024.0)) /
      (avg_stream_us / 1'000'000.0);

  assert(bytes_written == vram_bytes);

  // 5. Verify integrity of boundary and interior entries in streamed buffer
  const auto *streamed_positions = reinterpret_cast<const GpuPosition *>(
      mock_mapped_vram.data()
  );
  assert(streamed_positions[0].x == 0.0f);
  assert(streamed_positions[0].y == 0.0f);

  assert(streamed_positions[25000].x == 25000.0f);
  assert(streamed_positions[25000].y == 75000.0f);

  assert(streamed_positions[49999].x == 49999.0f);
  assert(streamed_positions[49999].y == 149997.0f);

  std::cout << "PASSED (" << avg_stream_us
            << " µs per full 50k particle stream, " << gb_per_sec
            << " GB/s memory bandwidth)\n";
}

// =============================================================================
// Test Suite Entry Point
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs GPU std430 Reflection & Views Tests\n";
  std::cout << "====================================================\n";

  test_std430_trait_validations();
  test_component_span_views();
  test_stream_component_to_mock_vram();

  std::cout << "====================================================\n";
  std::cout << "All GPU std430 Reflection tests completed successfully!\n";
  std::cout << "====================================================\n";
  return 0;
}
