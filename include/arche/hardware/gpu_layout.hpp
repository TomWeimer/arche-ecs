#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace arche {

// =============================================================================
// std430 Layout Validation Traits & Concepts
// =============================================================================

/**
 * @struct Std430Traits
 * @brief Compile-time traits validating memory layout compatibility with GPU
 * std430 layout specifications (Vulkan SSBOs, Metal, DirectX).
 *
 * @details
 * Under the std430 packing specification for Shader Storage Buffer Objects
 * (SSBOs):
 * 1. Base alignment of scalars equals their byte size (e.g., float = 4 bytes).
 * 2. Two-component vectors (vec2) align to 2 * base alignment (8 bytes).
 * 3. Three- and four-component vectors (vec3, vec4) align to 4 * base alignment
 * (16 bytes).
 * 4. Array strides match element base alignment, but array elements of vec3 are
 * padded to 16 bytes.
 *
 * Hazard Note:
 * A standard C++ struct `{ float x, y, z; }` has a size of 12 bytes and 4-byte
 * alignment. If streamed into an SSBO expecting `vec3 data[]`, GPU shaders will
 * read 16-byte strides, leading to corrupted memory reads. `Std430Traits`
 * detects this condition at compile time.
 *
 * @tparam T Component type being validated for GPU streaming.
 */
template <typename T> struct Std430Traits {
  /// @brief Size of the component in bytes.
  static constexpr size_t size = sizeof(T);

  /// @brief Memory alignment of the component in bytes.
  static constexpr size_t alignment = alignof(T);

  /// @brief Validates that the struct's alignment is a power of two.
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);

  /// @brief Validates that size is an exact multiple of alignment (no trailing
  /// partial alignment padding).
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;

  /// @brief Explicit check: reject 12-byte structs (unpadded vec3 hazard)
  /// unless explicitly 16-byte aligned.
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

/**
 * @concept GpuStd430Compatible
 * @brief Concept constraining component types to trivially copyable,
 * std430-aligned structs.
 *
 * Enforces zero-overhead memcpy and DMA streaming safety directly into mapped
 * GPU buffers or GPU device memory.
 *
 * @tparam T Component type to evaluate.
 */
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

// =============================================================================
// Contiguous Component View (Approach B - Zero-Copy Binding)
// =============================================================================

/**
 * @class ComponentSpan
 * @brief Strongly-typed, non-owning view of a contiguous component column
 * within a single chunk.
 *
 * @details
 * Enables zero-copy GPU integration (Approach B):
 * - On Apple Silicon unified memory or Vulkan Buffer Device Address (BDA)
 * architectures, each chunk's column pointer can be bound directly to graphics
 * pipelines without copies.
 * - Guarantees contiguous cache-friendly access over [0, count_) elements.
 *
 * @tparam T Component type viewed in the chunk column.
 */
template <typename T> class ComponentSpan {
public:
  /**
   * @brief Constructs a component span over a contiguous memory buffer.
   *
   * @param data Pointer to the start of the contiguous component column.
   * @param count Number of active entities/components in the chunk.
   */
  inline ComponentSpan(const T *data, uint32_t count) noexcept
      : data_(data), count_(count) {}

  /**
   * @brief Returns a pointer to the contiguous underlying data array.
   */
  [[nodiscard]] inline const T *data() const noexcept { return data_; }

  /**
   * @brief Returns the number of active components in this chunk column.
   */
  [[nodiscard]] inline uint32_t count() const noexcept { return count_; }

  /**
   * @brief Returns the total byte size occupied by the active components in
   * this chunk.
   */
  [[nodiscard]] inline size_t size_bytes() const noexcept {
    return static_cast<size_t>(count_) * sizeof(T);
  }

  /**
   * @brief Returns the stride (in bytes) between consecutive elements.
   */
  [[nodiscard]] inline size_t stride() const noexcept { return sizeof(T); }

  /**
   * @brief Checks whether the span contains zero elements.
   */
  [[nodiscard]] inline bool empty() const noexcept { return count_ == 0; }

  /**
   * @brief Subscript access to a component by its intra-chunk index.
   *
   * @param index Local index within the chunk [0, count_).
   * @return Const reference to the component at `index`.
   */
  [[nodiscard]] inline const T &operator[](size_t index) const noexcept {
    assert(index < count_ && "ComponentSpan: index out of bounds");
    return data_[index];
  }

  /**
   * @brief Converts the view to a standard C++20 std::span.
   */
  [[nodiscard]] inline std::span<const T> as_span() const noexcept {
    return std::span<const T>(data_, count_);
  }

private:
  const T *data_{nullptr};
  uint32_t count_{0};
};

// =============================================================================
// Type-Erased GPU Buffer View (Approach A - Universal Streaming)
// =============================================================================

/**
 * @struct GpuBufferView
 * @brief Type-erased descriptor of a component memory buffer for graphics
 * pipelines.
 *
 * @details
 * Facilitates graphics backend abstraction (Vulkan, Metal, DirectX 12, WebGPU):
 * Encapsulates raw memory pointer, total byte payload, item count, and
 * alignment without requiring the graphics backend to know the compile-time C++
 * ECS component type.
 */
struct GpuBufferView {
  /// @brief Raw pointer to the start of the buffer memory.
  const void *data{nullptr};

  /// @brief Total active size in bytes (`count * element_size`).
  size_t byte_size{0};

  /// @brief Total number of elements stored.
  uint32_t count{0};

  /// @brief Size of each individual element in bytes.
  uint32_t element_size{0};

  /// @brief Required memory alignment in bytes.
  uint32_t alignment{0};
};

// =============================================================================
// Vulkan-Compatible Indirect Draw Structures
// =============================================================================

/**
 * @struct DrawIndirectCommand
 * @brief Binary layout identical to VkDrawIndirectCommand for GPU-driven
 * multi-draw.
 */
struct DrawIndirectCommand {
  uint32_t vertex_count{0}; ///< Number of vertices to draw (chunk entity count)
  uint32_t instance_count{1}; ///< Number of instances (1)
  uint32_t first_vertex{0};   ///< Starting vertex offset in mapped VRAM buffer
  uint32_t first_instance{0}; ///< First instance ID (0)
};

static_assert(
    sizeof(DrawIndirectCommand) == 16,
    "DrawIndirectCommand must be exactly 16 bytes (VkDrawIndirectCommand "
    "compatible)"
);

} // namespace arche
