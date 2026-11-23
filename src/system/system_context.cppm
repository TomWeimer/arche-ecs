module;

#include <cstdint>
#include <tuple>
#include <utility>

export module arche:system_context;

import :command;
import :thread_pool;

export namespace arche {

class World;

struct SystemContext {
  World &world;
  CommandBuffer &cmd;
  ThreadPool *pool{nullptr};
  ParallelCommandBuffer *parallel_cmd{nullptr};

  SystemContext(World &w, CommandBuffer &c) noexcept
      : world(w), cmd(c), pool(nullptr), parallel_cmd(nullptr) {}

  SystemContext(
      World &w,
      CommandBuffer &c,
      ThreadPool &p,
      ParallelCommandBuffer &pc
  ) noexcept
      : world(w), cmd(c), pool(&p), parallel_cmd(&pc) {}

  template <typename... Components, typename Func>
  void query_parallel(Func &&func);

  template <typename Func> inline void parallel_for(Func &&func) {
    if (pool != nullptr) {
      pool->parallel_for(std::forward<Func>(func));
    } else {
      func(0);
    }
  }

  [[nodiscard]] uint32_t version() const noexcept;
  [[nodiscard]] uint32_t tick() const noexcept;
  [[nodiscard]] uint32_t current_tick() const noexcept;
};

} // namespace arche
