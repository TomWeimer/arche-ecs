module;

#include <cstddef>

export module arche:math;

export namespace arche::math {

// =============================================================================
// Memory & SIMD Alignment Primitives
// =============================================================================

/**
 * @brief Rounds an integer or offset up to the nearest multiple of `alignment`.
 *
 * @tparam T Integer or pointer arithmetic value type.
 * @param val Value to be aligned.
 * @param alignment Required alignment boundary (must be a power of two).
 * @return Value rounded up to alignment boundary.
 */
template <typename T>
[[nodiscard]] constexpr T align_up(T val, size_t alignment) noexcept {
  return static_cast<T>((val + alignment - 1) & ~(alignment - 1));
}

/**
 * @brief Rounds an entity count up to the nearest SIMD vector width boundary.
 *
 * Used for tail-padding chunk memory allocations and query loops to enable
 * branchless vectorized SIMD loops (default Width = 16 elements / 512-bit
 * AVX-512).
 *
 * @tparam Width Vector lane count (defaults to 16).
 * @tparam T Integer value type.
 * @param val Number of active entities.
 * @return Value rounded up to a multiple of Width.
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_up_simd(T val) noexcept {
  return static_cast<T>((val + Width - 1) & ~(Width - 1));
}

/**
 * @brief Rounds an entity count down to the nearest SIMD vector width boundary.
 *
 * Used to identify the vectorized loop boundary before executing tail
 * iterations.
 *
 * @tparam Width Vector lane count (defaults to 16).
 * @tparam T Integer value type.
 * @param val Number of active entities.
 * @return Value rounded down to a multiple of Width.
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_down_simd(T val) noexcept {
  return static_cast<T>((val / Width) * Width);
}

/**
 * @brief Integer division with ceiling rounding (ceil(val / divisor)).
 *
 * Computes ceil(val / divisor) without floating-point conversions.
 *
 * @tparam T Integer value type.
 * @param val Numerator.
 * @param divisor Denominator.
 * @return Smallest integer greater than or equal to val / divisor.
 */
template <typename T>
[[nodiscard]] constexpr T div_ceil(T val, T divisor) noexcept {
  return static_cast<T>((val + divisor - 1) / divisor);
}

} // namespace arche::math
