module;

#include <cassert>
#include <cstdint>
#include <utility>

export module arche:system.detail.system_context_impl;

import :system_context;
import :query;

export namespace arche {

// =============================================================================
// SystemContext Implementation
// =============================================================================

SystemContext::SystemContext(
    CommandBuffer &c,
    ResourceContainer &res,
    void *cache_ctx,
    CacheResolver get_cache,
    uint32_t ver,
    uint32_t prev_ver,
    ThreadPool *p,
    ParallelCommandBuffer *pc
) noexcept
    : cmd(c),
      resources_(&res),
      cache_ctx_(cache_ctx),
      get_cache_fn_(get_cache),
      version_(ver),
      previous_version_(prev_ver),
      pool_(p),
      parallel_cmd_(pc) {}

template <typename Res>
[[nodiscard]] inline const Res &SystemContext::read_resource() const {
  assert(resources_ != nullptr);
  return resources_->get<Res>();
}

template <typename Res>
[[nodiscard]] inline Res &SystemContext::write_resource() {
  assert(resources_ != nullptr);
  return resources_->get<Res>();
}

template <typename Res>
[[nodiscard]] inline bool SystemContext::has_resource() const noexcept {
  return resources_ != nullptr && resources_->has<Res>();
}

template <QueryTerm... Components, QueryFilter... FilterArgs>
[[nodiscard]] inline Query<Components...> SystemContext::query(
    FilterArgs &&...filters
) const {
  auto [include_mask, exclude_mask, any_mask] =
      detail::query::compute_query_masks<Components...>(filters...);

  assert(get_cache_fn_ != nullptr);
  QueryCache *cache =
      get_cache_fn_(cache_ctx_, include_mask, exclude_mask, any_mask);
  assert(cache != nullptr);

  return Query<Components...>(
      *cache,
      QueryExecutionContext{pool_, parallel_cmd_, version_, previous_version_},
      std::forward<FilterArgs>(filters)...
  );
}

[[nodiscard]] uint32_t SystemContext::version() const noexcept {
  return version_;
}

[[nodiscard]] uint32_t SystemContext::previous_version() const noexcept {
  return previous_version_;
}

[[nodiscard]] uint32_t SystemContext::tick() const noexcept {
  return version();
}

[[nodiscard]] uint32_t SystemContext::current_tick() const noexcept {
  return version();
}

} // namespace arche
