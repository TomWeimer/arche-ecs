module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

export module arche:scheduler;

import :types.detail.system_types;
import :access;
import :thread_pool;
import :command;

export namespace arche {

class World;

/**
 * @class Scheduler
 * @brief Declarative dependency graph compiler and lock-free parallel runner.
 */
class Scheduler {
public:
  struct SystemNode {
    std::string name;
    AccessDescriptor access;
    SystemFunction fn{nullptr};
    SystemScale scale{SystemScale::Narrow};
  };

  struct Batch {
    std::vector<size_t> system_indices;
    AccessDescriptor access{};
    bool is_wide{false};
  };

private:
  std::vector<SystemNode> systems_;
  std::vector<Batch> batches_;
  bool dirty_{true};

  void validate_system_resources(World &world);
  void partition_batches();
  void execute_batch(
      const Batch &batch,
      World &world,
      ThreadPool &pool,
      ParallelCommandBuffer &parallel_cmd
  );

public:
  Scheduler() = default;
  ~Scheduler() = default;
  Scheduler(const Scheduler &) = default;
  Scheduler &operator=(const Scheduler &) = default;
  Scheduler(Scheduler &&) noexcept = default;
  Scheduler &operator=(Scheduler &&) noexcept = default;

  Scheduler &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  ) {
    systems_.push_back(
        SystemNode{std::move(name), access, std::move(fn), scale}
    );
    dirty_ = true;
    return *this;
  }

  void compile(World &world);
  void run(World &world, ThreadPool &pool);
  void run(World &world);
};

} // namespace arche
