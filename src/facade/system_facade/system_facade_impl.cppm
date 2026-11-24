module;

#include <cassert>
#include <string>
#include <type_traits>
#include <utility>

export module arche:facade.system_impl;

import :facade.system;
import :scheduler;
import :plugin;
import :thread_pool;

export namespace arche {

// =============================================================================
// SystemFacade Method Implementations
// =============================================================================

void SystemFacade::run(ThreadPool &pool) {
  assert(scheduler_ != nullptr && get_target_ != nullptr);
  scheduler_->run(get_target_(world_ptr_), pool);
}

void SystemFacade::run() {
  assert(scheduler_ != nullptr && get_target_ != nullptr);
  scheduler_->run(get_target_(world_ptr_));
}

void SystemFacade::compile() {
  assert(scheduler_ != nullptr && get_target_ != nullptr);
  scheduler_->compile(get_target_(world_ptr_));
}

SystemFacade &SystemFacade::add_system(
    std::string name,
    AccessDescriptor access,
    SystemFunction fn,
    SystemScale scale
) {
  assert(scheduler_ != nullptr);
  scheduler_->add_system(std::move(name), access, std::move(fn), scale);
  return *this;
}

Scheduler &SystemFacade::scheduler() noexcept {
  assert(scheduler_ != nullptr);
  return *scheduler_;
}

const Scheduler &SystemFacade::scheduler() const noexcept {
  assert(scheduler_ != nullptr);
  return *scheduler_;
}

template <typename P>
inline void SystemFacade::register_plugin_name() {
  assert(plugins_ != nullptr);
  plugins_->register_plugin(P::name());
}

} // namespace arche
