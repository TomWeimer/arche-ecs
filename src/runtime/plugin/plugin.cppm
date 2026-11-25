module;

#include <cassert>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module arche:plugin;

import :access;
import :system_types;
import :scheduler;

export namespace arche {

class World;

// =============================================================================
// Plugin Registration Context
// =============================================================================

/**
 * @class PluginContext
 * @brief Scoped builder passed to plugins during installation to register systems.
 *
 * Enforces that systems can only be registered through autonomous plugins.
 */
class PluginContext {
public:
  explicit PluginContext(internal::Scheduler &scheduler) noexcept
      : scheduler_(&scheduler) {}

  /// Adds a system function to the execution schedule with data access declarations.
  PluginContext &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  ) {
    assert(scheduler_ != nullptr);
    scheduler_->add_system(std::move(name), access, std::move(fn), scale);
    return *this;
  }

  /// Adds a scheduled sorting stage to the execution schedule targeting matching archetypes.
  template <typename Component, typename... Filters, typename Predicate, typename Sorter>
  PluginContext &add_sort_stage(
      std::string name,
      Predicate &&condition,
      Sorter &&sorter
  ) {
    assert(scheduler_ != nullptr);
    scheduler_->template add_sort_stage<Component, Filters...>(
        std::move(name),
        std::forward<Predicate>(condition),
        std::forward<Sorter>(sorter)
    );
    return *this;
  }

private:
  internal::Scheduler *scheduler_{nullptr};
};

// =============================================================================
// Plugin Lifecycle Concept
// =============================================================================

/**
 * @concept Plugin
 * @brief Autonomous module contract initializing resources, systems, and
 * components.
 */
template <typename P>
concept Plugin = requires(
    P plugin,
    World &world,
    PluginContext &ctx
) {
  { plugin.build(world, ctx) } -> std::same_as<void>;
  { P::name() } -> std::convertible_to<std::string_view>;
};

// =============================================================================
// Plugin Management & Registry
// =============================================================================

/**
 * @class PluginRegistry
 * @brief Tracks installed engine plugins to enforce single-installation
 * invariants.
 */
class PluginRegistry {
private:
  std::vector<std::string_view> installed_;

public:
  PluginRegistry() = default;
  ~PluginRegistry() = default;

  /**
   * @brief Records a plugin by name.
   *
   * @param name Unique string identifier of the plugin.
   * @throws std::runtime_error If a plugin with the given name was already
   * registered.
   */
  void register_plugin(std::string_view name);

  /**
   * @brief Checks if a plugin with the given name has already been registered.
   */
  [[nodiscard]] bool is_installed(std::string_view name) const noexcept;

  /**
   * @brief Returns a span of all installed plugin names.
   */
  [[nodiscard]] const std::vector<std::string_view> &installed() const noexcept;
};

} // namespace arche
