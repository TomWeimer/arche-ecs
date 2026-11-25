module;

#include <concepts>
#include <cstdint>
#include <string_view>
#include <vector>

export module arche:plugin;

export namespace arche {

class World;

namespace internal {
class Scheduler;
}

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
    internal::Scheduler &scheduler
) {
  { plugin.build(world, scheduler) } -> std::same_as<void>;
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
