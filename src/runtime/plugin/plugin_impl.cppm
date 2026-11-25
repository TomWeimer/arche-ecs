module;

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module arche:plugin.detail.plugin_impl;

import :plugin;

export namespace arche {

// =============================================================================
// PluginRegistry Implementation
// =============================================================================

void PluginRegistry::register_plugin(std::string_view name) {
  for (const auto &installed_name : installed_) {
    if (installed_name == name) {
      throw std::runtime_error(
          "arche::Plugin: Plugin '" + std::string(name) + "' is already installed."
      );
    }
  }
  installed_.emplace_back(name);
}

[[nodiscard]] bool PluginRegistry::is_installed(
    std::string_view name
) const noexcept {
  for (const auto &installed_name : installed_) {
    if (installed_name == name) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] const std::vector<std::string_view> &
PluginRegistry::installed() const noexcept {
  return installed_;
}

} // namespace arche
