module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

export module arche:chunk_pool;

import :hardware;

export namespace arche {

class ChunkPool {
public:
  using ChunkAllocFn = void *(*)(size_t bytes, size_t alignment);
  using ChunkFreeFn = void (*)(void *ptr, size_t bytes);

private:
  size_t chunk_size_{65536};
  size_t total_allocated_{0};
  std::vector<void *> free_list_;
  ChunkAllocFn alloc_fn_{nullptr};
  ChunkFreeFn free_fn_{nullptr};

public:
  explicit ChunkPool(
      size_t chunk_size = 65536,
      size_t initial_reserve = 0,
      ChunkAllocFn custom_alloc = nullptr,
      ChunkFreeFn custom_free = nullptr
  )
      : chunk_size_(Hardware::optimal_chunk_size(chunk_size)),
        alloc_fn_(custom_alloc), free_fn_(custom_free) {
    if (initial_reserve > 0) {
      reserve(initial_reserve);
    }
  }

  ~ChunkPool() { purge(); }

  ChunkPool(const ChunkPool &) = delete;
  ChunkPool &operator=(const ChunkPool &) = delete;

  ChunkPool(ChunkPool &&other) noexcept
      : chunk_size_(other.chunk_size_),
        total_allocated_(other.total_allocated_),
        free_list_(std::move(other.free_list_)), alloc_fn_(other.alloc_fn_),
        free_fn_(other.free_fn_) {
    other.total_allocated_ = 0;
    other.alloc_fn_ = nullptr;
    other.free_fn_ = nullptr;
  }

  ChunkPool &operator=(ChunkPool &&other) noexcept {
    if (this != &other) {
      purge();
      chunk_size_ = other.chunk_size_;
      total_allocated_ = other.total_allocated_;
      free_list_ = std::move(other.free_list_);
      alloc_fn_ = other.alloc_fn_;
      free_fn_ = other.free_fn_;

      other.total_allocated_ = 0;
      other.alloc_fn_ = nullptr;
      other.free_fn_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] void *acquire() {
    if (!free_list_.empty()) {
      void *ptr = free_list_.back();
      free_list_.pop_back();
      return ptr;
    }
    void *ptr = allocate_block();
    total_allocated_++;
    return ptr;
  }

  void release(void *ptr) {
    if (ptr == nullptr)
      return;
    free_list_.push_back(ptr);
  }

  [[nodiscard]] void *acquire_buffer() { return acquire(); }
  void release_buffer(void *ptr) noexcept { release(ptr); }
  void clear() noexcept { purge(); }

  void reserve(size_t count) {
    free_list_.reserve(free_list_.size() + count);
    for (size_t i = 0; i < count; ++i) {
      free_list_.push_back(allocate_block());
      total_allocated_++;
    }
  }

  void purge() {
    for (void *ptr : free_list_) {
      deallocate_block(ptr);
    }
    free_list_.clear();
    total_allocated_ = 0;
  }

  [[nodiscard]] size_t chunk_size() const noexcept { return chunk_size_; }
  [[nodiscard]] size_t available() const noexcept { return free_list_.size(); }
  [[nodiscard]] size_t free_count() const noexcept { return free_list_.size(); }
  [[nodiscard]] size_t allocated() const noexcept { return total_allocated_; }
  [[nodiscard]] size_t total_allocated() const noexcept {
    return total_allocated_;
  }
  [[nodiscard]] size_t in_use() const noexcept {
    return total_allocated_ - free_list_.size();
  }

  inline void set_allocator(
      ChunkAllocFn alloc_fn,
      ChunkFreeFn free_fn
  ) noexcept {
    alloc_fn_ = alloc_fn;
    free_fn_ = free_fn;
  }

private:
  [[nodiscard]] void *allocate_block() {
    const size_t alignment = std::max<size_t>(Hardware::cache_line_size(), 64);
    if (alloc_fn_) {
      return alloc_fn_(chunk_size_, alignment);
    }
    void *ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(chunk_size_, alignment);
#else
    if (posix_memalign(&ptr, alignment, chunk_size_) != 0) {
      ptr = nullptr;
    }
#endif
    return ptr;
  }

  void deallocate_block(void *ptr) {
    if (ptr == nullptr)
      return;
    if (free_fn_) {
      free_fn_(ptr, chunk_size_);
      return;
    }
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }
};

} // namespace arche
