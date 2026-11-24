module;

#include <cstddef>
#include <span>

export module arche:gpu_span;

export namespace arche {

// =============================================================================
// ComponentSpan: Zero-Copy Contiguous Chunk Column View
// =============================================================================

/**
 * @class ComponentSpan
 * @brief Zero-copy, non-owning contiguous view over an archetype chunk
 * component column.
 *
 * Inherits standard C++20 `std::span<const T>` semantics with ergonomics
 * helpers
 * (`count()`, `stride()`, `size_bytes()`).
 *
 * Enables direct pointer extraction from contiguous chunk columns for zero-copy
 * memory uploads to graphics staging buffers (Vulkan, Metal, DirectX, WebGPU).
 *
 * @tparam T Component type stored in the chunk column.
 */
template <typename T> class ComponentSpan : public std::span<const T> {
public:
  using std::span<const T>::span;

  constexpr ComponentSpan(const T *data, size_t count) noexcept
      : std::span<const T>(data, count) {}

  /// Returns the number of active component elements in this chunk column.
  [[nodiscard]] constexpr size_t count() const noexcept { return this->size(); }

  /// Returns the byte stride between contiguous elements (sizeof(T)).
  [[nodiscard]] constexpr size_t stride() const noexcept { return sizeof(T); }

  /// Converts this view explicitly to a standard C++20 std::span.
  [[nodiscard]] constexpr std::span<const T> as_span() const noexcept {
    return *this;
  }
};

} // namespace arche
