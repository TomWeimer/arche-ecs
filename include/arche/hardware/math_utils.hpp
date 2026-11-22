#pragma once

#include <cstddef>

namespace arche::math {

/**
 * @brief Aligns a value up to the next multiple of alignment.
 */
template <typename T>
[[nodiscard]] constexpr T align_up(T val, size_t alignment) noexcept {
  return static_cast<T>((val + alignment - 1) & ~(alignment - 1));
}

/**
 * @brief Rounds up a value to the next multiple of SIMD vector width (default
 * 16 elements).
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_up_simd(T val) noexcept {
  return static_cast<T>((val + Width - 1) & ~(Width - 1));
}

/**
 * @brief Rounds down a value to the nearest multiple of SIMD vector width.
 */
template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_down_simd(T val) noexcept {
  return static_cast<T>((val / Width) * Width);
}

/**
 * @brief Integer ceiling division (val / divisor rounded up).
 */
template <typename T>
[[nodiscard]] constexpr T div_ceil(T val, T divisor) noexcept {
  return static_cast<T>((val + divisor - 1) / divisor);
}

} // namespace arche::math