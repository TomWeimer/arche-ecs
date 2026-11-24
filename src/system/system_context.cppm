module;

#include <cstdint>
#include <utility>

export module arche:system_context;

import :command;
import :thread_pool;
import :resource;
import :query;

export namespace arche {

// =============================================================================
// SystemContext (Execution Environment Injected into Systems)
// =============================================================================

/**
 * @struct SystemContext
 * @brief Dynamic execution scope and dependency access provider passed to each system node.
 *
 * Encapsulates thread-local CommandBuffer injection, global resource access,
 * cached query instantiation, version inspection, and parallel intra-query dispatch.
 */
struct SystemContext {
  using CacheResolver = QueryCache *(*)(
      void *ctx,
      ComponentMask inc,
      ComponentMask exc,
      ComponentMask any
  );

  /// Reference to this worker thread's deferred mutation command buffer.
  CommandBuffer &cmd;

  /**
   * @brief Constructs an active SystemContext instance.
   */
  SystemContext(
      CommandBuffer &c,
      ResourceContainer &res,
      void *cache_ctx,
      CacheResolver get_cache,
      uint32_t ver,
      uint32_t prev_ver = 0,
      ThreadPool *p = nullptr,
      ParallelCommandBuffer *pc = nullptr
  ) noexcept;

  /**
   * @brief Reads an immutable world resource of type `Res`.
   *
   * @tparam Res Resource type to read.
   * @return Const reference to the resource instance.
   * @pre The resource must have been registered in the World.
   */
  template <typename Res>
  [[nodiscard]] const Res &read_resource() const;

  /**
   * @brief Retrieves a mutable reference to a world resource of type `Res`.
   *
   * @tparam Res Resource type to mutate.
   * @return Mutable reference to the resource instance.
   * @pre The resource must have been registered in the World.
   */
  template <typename Res>
  [[nodiscard]] Res &write_resource();

  /**
   * @brief Checks if a world resource of type `Res` is registered.
   */
  template <typename Res>
  [[nodiscard]] bool has_resource() const noexcept;

  /**
   * @brief Creates a typed Query matching specified component terms and filters.
   *
   * @tparam Components Required component types.
   * @tparam FilterArgs Variadic filter constraints (`With<T>`, `Without<T>`, `Mutated<T>`, etc.).
   * @param filters Query filter instances.
   * @return Initialized Query configured with the scheduler's execution context.
   */
  template <QueryTerm... Components, QueryFilter... FilterArgs>
  [[nodiscard]] Query<Components...> query(FilterArgs &&...filters) const;

  /**
   * @brief Current monotonically increasing world simulation tick sequence.
   */
  [[nodiscard]] uint32_t version() const noexcept;

  /**
   * @brief Previous simulation tick sequence prior to the current frame.
   */
  [[nodiscard]] uint32_t previous_version() const noexcept;

  /**
   * @brief Ergonomic alias for `version()`.
   */
  [[nodiscard]] uint32_t tick() const noexcept;

  /**
   * @brief Ergonomic alias for `version()`.
   */
  [[nodiscard]] uint32_t current_tick() const noexcept;

private:
  ResourceContainer *resources_{nullptr};
  void *cache_ctx_{nullptr};
  CacheResolver get_cache_fn_{nullptr};
  uint32_t version_{0};
  uint32_t previous_version_{0};
  ThreadPool *pool_{nullptr};
  ParallelCommandBuffer *parallel_cmd_{nullptr};
};

} // namespace arche
