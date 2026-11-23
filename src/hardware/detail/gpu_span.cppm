module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

export module arche:hardware.detail.gpu_span;

export namespace arche {

template <typename T> struct Std430Traits {
  static constexpr size_t size = sizeof(T);
  static constexpr size_t alignment = alignof(T);
  static constexpr bool is_power_of_two_aligned = (alignment != 0) &&
      ((alignment & (alignment - 1)) == 0);
  static constexpr bool is_size_multiple_of_alignment = (size % alignment) == 0;
  static constexpr bool is_safe_std430 = is_power_of_two_aligned &&
      is_size_multiple_of_alignment && (size != 12 || alignment == 16);
};

template <typename T>
concept GpuStd430Compatible = std::is_trivially_copyable_v<T> &&
    Std430Traits<T>::is_safe_std430;

template <typename T> class ComponentSpan {
public:
  inline ComponentSpan(const T *data, uint32_t count) noexcept
      : data_(data), count_(count) {}

  [[nodiscard]] inline const T *data() const noexcept { return data_; }
  [[nodiscard]] inline uint32_t count() const noexcept { return count_; }
  [[nodiscard]] inline size_t size_bytes() const noexcept {
    return static_cast<size_t>(count_) * sizeof(T);
  }
  [[nodiscard]] inline size_t stride() const noexcept { return sizeof(T); }
  [[nodiscard]] inline bool empty() const noexcept { return count_ == 0; }

  [[nodiscard]] inline const T &operator[](size_t index) const noexcept {
    assert(index < count_ && "ComponentSpan: index out of bounds");
    return data_[index];
  }

  [[nodiscard]] inline std::span<const T> as_span() const noexcept {
    return std::span<const T>(data_, count_);
  }

private:
  const T *data_{nullptr};
  uint32_t count_{0};
};

struct GpuBufferView {
  const void *data{nullptr};
  size_t byte_size{0};
  uint32_t count{0};
  uint32_t element_size{0};
  uint32_t alignment{0};
};

struct alignas(16) DrawIndirectCommand {
  uint32_t vertex_count{0};
  uint32_t instance_count{1};
  uint32_t first_vertex{0};
  uint32_t first_instance{0};
};

static_assert(
    sizeof(DrawIndirectCommand) == 16,
    "DrawIndirectCommand must be exactly 16 bytes (VkDrawIndirectCommand "
    "compatible)"
);

} // namespace arche
