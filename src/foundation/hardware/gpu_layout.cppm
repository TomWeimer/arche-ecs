module;

#include <cstddef>
#include <type_traits>

export module arche:gpu_layout;

export namespace arche::internal {

// =============================================================================
// Std430 Layout Validation Traits
// =============================================================================

/**
 * @struct Std430Traits
 * @brief Compile-time validation of memory alignment and struct packing under
 * GPU std430 rules.
 *
 * Std430 layout rules (Vulkan, OpenGL SSBO, DirectX DXIL):
 * - Types must be aligned to a power of two.
 * - Array element stride must equal alignment.
 * - Struct size must be a multiple of its alignment.
 * - Rejects unpadded 12-byte vec3 types (which require 16-byte alignment in
 * standard GPU buffers).
 *
 * @tparam T Candidate component type to validate for GPU buffer compatibility.
 */
template <typename T> struct Std430Traits {
  static constexpr size_t size = sizeof(T);
  static constexpr size_t alignment = alignof(T);
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

/**
 * @concept GpuStd430Compatible
 * @brief Constrains component types to trivially copyable structs satisfying
 * std430 GPU alignment.
 *
 * Prevents runtime bus faults or shader misalignment artifacts when mapping ECS
 * chunk memory directly into graphics vertex or storage buffers.
 */
template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

} // namespace arche
