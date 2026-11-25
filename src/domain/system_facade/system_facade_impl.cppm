module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

export module arche:domain.system_impl;

import :domain.system;
import :scheduler;

export namespace arche {

using namespace internal;

// =============================================================================
// SystemFacade Method Implementations
// =============================================================================

bool SystemFacade::is_enabled() const noexcept {
  assert(scheduler_ != nullptr);
  return scheduler_->is_system_enabled(system_index_);
}

void SystemFacade::enable() noexcept {
  assert(scheduler_ != nullptr);
  scheduler_->set_system_enabled(system_index_, true);
}

void SystemFacade::disable() noexcept {
  assert(scheduler_ != nullptr);
  scheduler_->set_system_enabled(system_index_, false);
}

void SystemFacade::run_now() {
  assert(scheduler_ != nullptr && get_target_ != nullptr);
  scheduler_->run_system(system_index_, get_target_(world_ptr_));
}

std::string SystemFacade::show_access() const {
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

} // namespace arche
