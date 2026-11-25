module;

#include <cassert>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:query;

export import :entity;
export import :archetype;
export import :chunk;
export import :filters;
export import :thread_pool;
export import :command;
export import :query.detail.filter;
export import :query.detail.query_iterator;
export import :query.detail.masks;
export import :query.detail.offsets;

export namespace arche {

// =============================================================================
// Query Callback Concepts
// =============================================================================

/**
 * @concept ChunkCallback
 * @brief Validates single-threaded chunk callbacks: void(uint32_t count,
 * Components*...).
 */
template <typename F, typename... Components>
concept ChunkCallback = std::
    is_invocable_v<F, uint32_t, column_pointer_type_t<Components>...>;

/**
 * @concept PaddedChunkCallback
 * @brief Validates SIMD-padded chunk callbacks: void(uint32_t active, uint32_t
 * padded, Components*...).
 */
template <typename F, typename... Components>
concept PaddedChunkCallback = std::
    is_invocable_v<F, uint32_t, uint32_t, column_pointer_type_t<Components>...>;

/**
 * @concept ParallelChunkCallback
 * @brief Validates multi-threaded chunk callbacks with optional CommandBuffer
 * injection:
 * - Mode A (Pure compute): void(uint32_t count, Components*...)
 * - Mode B (Deferred mutations): void(uint32_t count, CommandBuffer&,
 * Components*...)
 */
template <typename F, typename... Components>
concept ParallelChunkCallback =
    std::is_invocable_v<F, uint32_t, column_pointer_type_t<Components>...> ||
    std::is_invocable_v<
        F,
        uint32_t,
        CommandBuffer &,
        column_pointer_type_t<Components>...>;

// =============================================================================
// Query Execution Context
// =============================================================================

/**
 * @struct QueryExecutionContext
 * @brief Concurrency and execution environment injected into a Query.
 *
 * Provides worker thread dispatching, thread-local deferred command buffers,
 * and temporal simulation versioning (e.g. injected by SystemContext or World).
 */
struct QueryExecutionContext {
  /// Worker thread pool for parallel intra-query execution.
  internal::ThreadPool *pool{nullptr};

  /// Multi-buffered command buffer for lock-free parallel mutations.
  internal::ParallelCommandBuffer *parallel_cmd{nullptr};

  /// Current world or system tick used for auto-defaulting change detection.
  uint32_t current_version{0};

  /// Previous simulation tick before recent advancement (used for SINCE_LAST_FRAME).
  uint32_t previous_version{0};
};

// =============================================================================
// Query: Type-Safe, SIMD-Aligned Entity Traversal
// =============================================================================

/**
 * @class Query
 * @brief Type-safe, SIMD-aligned query view over matching archetype chunks.
 *
 * Lightweight, zero-allocation streaming views constructed via
 * `world.query<...>()` or `ctx.query<...>()`. Archetypes are cached globally in
 * the World.
 *
 * ### Quick Traversal Guide
 * - **Per-Entity Loop:**    `for (auto [pos, vel] : query) { pos.x += vel.vx;
 * }`
 * - **Contiguous Chunks:**  `query.each_chunk([](uint32_t count, Position *pos,
 * ...) { ... });`
 * - **SIMD-Padded Chunks:** `query.each_chunk_padded([](uint32_t active,
 * uint32_t padded, Position *pos, ...) { ... });`
 * - **Multi-Threaded:**     `query.each_parallel([](uint32_t count,
 * [CommandBuffer &cmd,] Position *pos, ...) { ... });`
 *
 *
 * ### Query Construction
 *
 * Depending of the intent of the query you may want different behaviors to
 * define the behavior of a query you need to define the data terms and the
 * query filters.
 *
 *  ```cpp
 *  // Examples:
 *  world.query<Position,  Without<Dead>>()
 *  ctx.query<const Position, const Velocity
 * // The Data terms must come first and then the filters
 * // ctx.query<Data Terms, ..., Filters, ...>()
 *  ```
 *
 * ### Data Terms
 * - `T` → Fetches mutable `T&`
 * - `const T` → Fetches read-only `const T&`
 * - `Opt<T>` or `const T*` → Fetches readonly `const T*` if present, or nullptr
 * if absent.
 * - `OptMut<T>` or `T*` → Fetches `T*` mutable if present, or nullptr
 * if absent.
 * - `Has<T>` → Yields a boolean (`bool`) indicating whether an
 * entity possesses component T, without fetching or binding any component data.
 * ### Query Filters
 * - `With<T...>`: Require components without fetching them.
 * - `Without<T...>`: Exclude archetypes containing specified components.
 * - `Or<With<A>, With<B>>`: Match archetypes containing any specified
 * component.
 * - `Mutated<T>` / `Added`: Skip untouched chunks since specified simulation
 * version.
 */
template <QueryTerm... Components> class Query {
public:
  template <QueryTerm... Comps> friend class QueryIterator;

  using Iterator = QueryIterator<Components...>;

  // --- Constructors ----------------------------------------------------------

  /// Constructs a standalone single-threaded Query view over the given
  /// QueryCache.
  template <QueryFilter... Filters>
  explicit Query(const internal::QueryCache &cache, Filters... filters);

  /// Constructs an execution-ready Query view with injected concurrency
  /// context.
  template <QueryFilter... Filters>
  Query(
      const internal::QueryCache &cache,
      QueryExecutionContext exec_ctx,
      Filters... filters
  );

  // --- Introspection ---------------------------------------------------------

  /// Returns true if no matching archetypes contain live entities (O(1)
  /// short-circuit).
  ///
  /// ```cpp
  /// // Example:
  /// if (world.query<Position, Without<Dead>>().empty()) {
  ///     // All matched entities are dead or despawned
  /// }
  /// ```
  [[nodiscard]] bool empty() const noexcept;

  /// Returns a zero-copy span over all archetype tables matching this query's
  /// bitmasks.
  [[nodiscard]] std::span<internal::Archetype *const>
  matching_archetypes() const noexcept;

  // --- C++20 Range Iteration -------------------------------------------------

  /// Standard C++20 range iteration support: for (auto [pos, vel] : query)
  [[nodiscard]] Iterator begin();
  [[nodiscard]] Iterator end();
  [[nodiscard]] Iterator iter() { return begin(); }

  // --- SIMD & Batch Execution ------------------------------------------------

  /// @brief Contiguous chunk batch loop for automatic compiler SIMD
  /// vectorization.
  ///
  /// Invokes the provided callback once per 64 KB chunk with direct typed array
  /// pointers.
  ///
  /// ### Inferred Callback Signature
  /// ```cpp
  /// [](uint32_t count, Components*...)
  ///
  /// // Example for Query<Position, const Velocity>:
  /// query.each_chunk([](uint32_t count, Position *pos, const Velocity *vel) {
  ///     for (uint32_t i = 0; i < count; ++i) {
  ///         pos[i].x += vel[i].vx;
  ///     }
  /// });
  /// ```
  /// @param func Invocable receiving active entity count and component column
  /// arrays.
  template <ChunkCallback<Components...> Func> void each_chunk(Func &&func);

  /// @brief SIMD tail-padded chunk batch loop with zero epilogue loops.
  ///
  /// Rounds active chunk entity counts up to SIMD lane boundaries (16 elements)
  /// using safe capacity padding in 64 KB chunks, allowing unconditional vector
  /// loads.
  ///
  /// ### Inferred Callback Signature
  /// ```cpp
  /// [](uint32_t active, uint32_t padded, Components*...)
  ///
  /// // Example for Query<Position, const Velocity>:
  /// query.each_chunk_padded([](uint32_t active, uint32_t padded, Position
  /// *pos, const Velocity *vel) {
  ///     for (uint32_t i = 0; i < padded; i += 16) {
  ///         // Safe to process 16 elements at a time with AVX-512 / NEON
  ///         vector loads
  ///     }
  /// });
  /// ```
  /// @param func Invocable receiving active count, padded count (aligned to
  /// 16), and column arrays.
  template <PaddedChunkCallback<Components...> Func>
  void each_chunk_padded(Func &&func);

  /// @brief Multi-threaded chunk loop with compile-time inferred CommandBuffer
  /// injection.
  ///
  /// Distributes 64 KB chunk tasks across worker threads. Arche ECS detects at
  /// compile-time whether the callback accepts a thread-local `CommandBuffer&`
  /// for deferred mutations:
  ///
  /// ### Inferred Callback Signatures
  /// - **Mode A (Pure Math / Zero Allocations):**
  ///   ```cpp
  ///   [](uint32_t count, Components*...)
  ///   ```
  /// - **Mode B (Deferred Structural Mutations):**
  ///   ```cpp
  ///   [](uint32_t count, CommandBuffer &cmd, Components*...)
  ///   ```
  ///
  /// ### Example:
  /// ```cpp
  /// query.each_parallel([](uint32_t count, CommandBuffer &cmd, Position *pos,
  /// const Health *hp) {
  ///     for (uint32_t i = 0; i < count; ++i) {
  ///         if (hp[i].current <= 0) cmd.despawn(entity);
  ///     }
  /// });
  /// ```
  /// @param func Invocable matching either Mode A or Mode B signature.
  template <ParallelChunkCallback<Components...> Func>
  void each_parallel(Func &&func);

private:
  const internal::QueryCache *cache_{nullptr};
  internal::ThreadPool *pool_{nullptr};
  internal::ParallelCommandBuffer *parallel_cmd_{nullptr};
  detail::query::MutationFilterList mutation_filters_{};
  uint32_t added_since_version_{0};
  bool has_added_filter_{false};
};

} // namespace arche
