module;

#include <cassert>

export module arche:domain.command_impl;

import :domain.command;
import :command;

export namespace arche {

using namespace internal;

// =============================================================================
// CommandFacade Method Implementations
// =============================================================================

void CommandFacade::commit() {
  assert(parallel_cmd_ != nullptr);
  if (commit_fn_) {
    commit_fn_(*parallel_cmd_, ctx_);
  }
  parallel_cmd_->clear();
}

void CommandFacade::clear() {
  assert(parallel_cmd_ != nullptr);
  parallel_cmd_->clear();
}

} // namespace arche
