#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace arche {

// =============================================================================
// Std430 Layout Validation
// =============================================================================

/**
 * @struct Std430Traits
 * @brief Compile-time validation traits for GLSL/Vulkan std430 memory layout
 * rules.
 *
 * Inspects component alignment, power-of-two constraints, and structure packing
 * to prevent GPU bus padding misalignment hazards (e.g. unpadded 12-byte vec3).
 */
template <typename T> struct Std430Traits {
  /// Size of the component in bytes.
  static constexpr size_t size = sizeof(T);

  /// Memory alignment of the component in bytes.
  static constexpr size_t alignment = alignof(T);

  /// Validates that the struct's alignment is a power of two.
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);

  /// Validates that size is an exact multiple of alignment.
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;

  /// Explicit check: rejects 12-byte structs unless explicitly 16-byte aligned.
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

/**
 * @concept GpuStd430Compatible
 * @brief Concept validating that type T is trivially copyable and satisfies
 * std430 rules.
 */
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

// =============================================================================
// ComponentSpan: Contiguous Component Column Slice
// =============================================================================

/**
 * @class ComponentSpan
 * @brief Lightweight, non-owning view over a contiguous column of components in
 * a chunk.
 *
 * Provides direct pointer and span access to cache-aligned component memory,
 * suitable for zero-copy GPU uploads (glBufferSubData, Vulkan staging buffers).
 *
 * ```cpp
 * // Example:
 * for (const auto &span : world.gpu.view_chunks<Position>()) {
 *     upload_to_vbo(vbo_id, span.data(), span.size_bytes());
 * }
 * ```
 */
template <typename T> class ComponentSpan {
public:
  /// Constructs a component span over a contiguous memory buffer.
  inline ComponentSpan(const T *data, uint32_t count) noexcept
      : data_(data), count_(count) {}

  /// Returns a pointer to the contiguous underlying data array.
  [[nodiscard]] inline const T *data() const noexcept { return data_; }

  /// Returns the number of active components in this chunk column.
  [[nodiscard]] inline uint32_t count() const noexcept { return count_; }

  /// Returns the total byte size occupied by active components in this chunk.
  [[nodiscard]] inline size_t size_bytes() const noexcept {
    return static_cast<size_t>(count_) * sizeof(T);
  }

  /// Returns the stride (in bytes) between consecutive elements.
  [[nodiscard]] inline size_t stride() const noexcept { return sizeof(T); }

  /// Checks whether the span contains zero elements.
  [[nodiscard]] inline bool empty() const noexcept { return count_ == 0; }

  /// Subscript access to a component by its intra-chunk index.
  [[nodiscard]] inline const T &operator[](size_t index) const noexcept {
    assert(index < count_ && "ComponentSpan: index out of bounds");
    return data_[index];
  }

  /// Converts the view to a standard C++20 std::span.
  [[nodiscard]] inline std::span<const T> as_span() const noexcept {
    return std::span<const T>(data_, count_);
  }

private:
  const T *data_{nullptr};
  uint32_t count_{0};
};

// =============================================================================
// GpuBufferView: Type-Erased GPU Buffer Descriptor
// =============================================================================

/**
 * @struct GpuBufferView
 * @brief Type-erased descriptor of a contiguous component buffer for graphics
 * backends.
 *
 * Encapsulates raw memory pointers, item count, byte size, element stride, and
 * alignment without exposing compile-time C++ component types to graphics
 * pipelines.
 */
struct GpuBufferView {
  /// Raw pointer to the start of the buffer memory.
  const void *data{nullptr};

  /// Total active size in bytes (`count * element_size`).
  size_t byte_size{0};

  /// Total number of elements stored.
  uint32_t count{0};

  /// Size of each individual element in bytes.
  uint32_t element_size{0};

  /// Required memory alignment in bytes.
  uint32_t alignment{0};
};

// =============================================================================
// DrawIndirectCommand: Vulkan-Compatible Indirect Multi-Draw
// =============================================================================

/**
 * @struct DrawIndirectCommand
 * @brief 16-byte structure binary-compatible with VkDrawIndirectCommand.
 *
 * Emitted by GPU batch culling and instanced drawing pipelines.
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
