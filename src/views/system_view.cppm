module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

export module arche:system_view;

import :scheduler;

export namespace arche {

// =============================================================================
// Individual System Domain View
// =============================================================================

/**
 * @class SystemView
 * @brief Domain view providing introspection and control over a single registered system.
 *
 * Accessible via `world.system("system_name")`.
 */
class SystemView {
public:
  constexpr SystemView() noexcept = default;
  explicit constexpr SystemView(
      internal::Scheduler &scheduler,
      size_t system_index,
      const internal::SystemExecutionTarget *target = nullptr
  ) noexcept
      : scheduler_(&scheduler),
        system_index_(system_index),
        target_(target) {}

  /// Returns whether this system is active in the scheduler.
  [[nodiscard]] bool is_enabled() const noexcept {
    assert(scheduler_ != nullptr);
    return scheduler_->is_system_enabled(system_index_);
  }

  /// Enables this system for subsequent world.run() iterations.
  void enable() noexcept {
    assert(scheduler_ != nullptr);
    scheduler_->set_system_enabled(system_index_, true);
  }

  /// Disables this system (skipped during world.run()).
  void disable() noexcept {
    assert(scheduler_ != nullptr);
    scheduler_->set_system_enabled(system_index_, false);
  }

  /// Explicitly runs this individual system immediately outside the main DAG.
  void run_now() {
    assert(scheduler_ != nullptr);
    assert(target_ != nullptr && "Cannot run_now() without execution target!");
    scheduler_->run_system(system_index_, *target_);
  }

  /// Returns a formatted string displaying the data access of the system.
  [[nodiscard]] std::string show_access() const {
    assert(scheduler_ != nullptr);
    const auto &node = scheduler_->get_system(system_index_);
    std::ostringstream ss;
    ss << "System '" << node.name << "' Access:\n";
    ss << "  Read Components:  [";
    bool first = true;
    for (uint32_t i = 0; i < 128; ++i) {
      if (node.access.read_components.test(i)) {
        if (!first) ss << ", ";
        ss << i;
        first = false;
      }
    }
    ss << "]\n  Write Components: [";
    first = true;
    for (uint32_t i = 0; i < 128; ++i) {
      if (node.access.write_components.test(i)) {
        if (!first) ss << ", ";
        ss << i;
        first = false;
      }
    }
    ss << "]\n  Read Resources:   [";
    first = true;
    for (uint32_t i = 0; i < 64; ++i) {
      if (node.access.read_resources.test(i)) {
        if (!first) ss << ", ";
        ss << i;
        first = false;
      }
    }
    ss << "]\n  Write Resources:  [";
    first = true;
    for (uint32_t i = 0; i < 64; ++i) {
      if (node.access.write_resources.test(i)) {
        if (!first) ss << ", ";
        ss << i;
        first = false;
      }
    }
    ss << "]";
    return ss.str();
  }

private:
  internal::Scheduler *scheduler_{nullptr};
  size_t system_index_{0};
  const internal::SystemExecutionTarget *target_{nullptr};
};

using SystemFacade = SystemView;

} // namespace arche
