module;

#include <cassert>
#include <string>
#include <utility>

export module arche:domain.system_impl;

import :domain.system;
import :scheduler;
import :thread_pool;

export namespace arche {

using namespace internal;

// =============================================================================
// SystemFacade Method Implementations
// =============================================================================

void SystemFacade::run() {
  assert(scheduler_ != nullptr && get_target_ != nullptr);
  if (pool_ != nullptr) {
    scheduler_->run(get_target_(world_ptr_), *pool_);
  } else {
    scheduler_->run(get_target_(world_ptr_));
  }
}

void SystemFacade::run_sequential() {
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

} // namespace arche
