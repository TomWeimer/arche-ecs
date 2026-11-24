module;

export module arche:facade.command;

import :command;

export namespace arche {

// =============================================================================
// Commands Domain Sub-Facade
// =============================================================================

/**
 * @class CommandFacade
 * @brief Domain sub-facade for managing world-level deferred command buffers.
 *
 * Accessible via `world.commands`.
 */
class CommandFacade {
public:
  constexpr CommandFacade() noexcept = default;
  explicit constexpr CommandFacade(
      ParallelCommandBuffer &parallel_cmd,
      void (*commit_fn)(ParallelCommandBuffer &cmd, void *ctx) = nullptr,
      void *ctx = nullptr
  ) noexcept
      : parallel_cmd_(&parallel_cmd), commit_fn_(commit_fn), ctx_(ctx) {}

  /// Commits all pending deferred commands recorded in the world's parallel command buffer.
  void commit();

  /// Clears all pending deferred commands without executing them.
  void clear();

private:
  ParallelCommandBuffer *parallel_cmd_{nullptr};
  void (*commit_fn_)(ParallelCommandBuffer &cmd, void *ctx){nullptr};
  void *ctx_{nullptr};
};

} // namespace arche
