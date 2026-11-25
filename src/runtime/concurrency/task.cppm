module;

#include <cstdint>

export module arche:task;

export namespace arche::internal {

/**
 * @struct Task
 * @brief Zero-overhead Plain Old Data (POD) task executable across worker threads.
 *
 * Conforms to Layer 0/4 invariants: zero virtual methods, zero heap allocation,
 * encapsulates a stateless function pointer and a type-erased context pointer.
 */
struct Task {
  using Function = void (*)(void *data, uint32_t thread_id);

  Function execute{nullptr};
  void *data{nullptr};

  void run(uint32_t thread_id) const noexcept {
    if (execute) {
      execute(data, thread_id);
    }
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return execute != nullptr;
  }
};

} // namespace arche::internal
