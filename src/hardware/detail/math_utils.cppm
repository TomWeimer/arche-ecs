module;

#include <cstddef>

export module arche:hardware.detail.math_utils;

export namespace arche::math {

template <typename T>
[[nodiscard]] constexpr T align_up(T val, size_t alignment) noexcept {
  return static_cast<T>((val + alignment - 1) & ~(alignment - 1));
}

template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_up_simd(T val) noexcept {
  return static_cast<T>((val + Width - 1) & ~(Width - 1));
}

template <size_t Width = 16, typename T>
[[nodiscard]] constexpr T round_down_simd(T val) noexcept {
  return static_cast<T>((val / Width) * Width);
}

template <typename T>
[[nodiscard]] constexpr T div_ceil(T val, T divisor) noexcept {
  return static_cast<T>((val + divisor - 1) / divisor);
}

} // namespace arche::math
