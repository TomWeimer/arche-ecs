module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module arche:command;

export import :entity;
export import :component_id;
export import :slot_map;
export import :chunk_pool;
export import :archetype;
export import :archetype_graph;
export import :mutation;

export namespace arche {

// =============================================================================
// Command Operation Codes
// =============================================================================

/**
 * @enum CommandType
 * @brief Identifies deferred structural mutation opcodes in serialized
 * CommandBuffer bytecode.
 */
enum class CommandType : uint8_t {
  CreateEntity,
  DestroyEntity,
  AddComponent,
  RemoveComponent,
  DestroyEntities
};

// =============================================================================
// CommandBuffer: Deferred Linear Bytecode Recorder
// =============================================================================

/**
 * @class CommandBuffer
 * @brief Lock-free, linear bytecode recording buffer for deferred structural
 * world mutations.
 *
 * Records entity creations, destructions, and component additions/removals into
 * a contiguous memory byte stream. Enables safe, highly parallel query loops
 * where structural alterations must be deferred to prevent data races and
 * archetype invalidation during iteration.
 *
 * ### Thread Safety:
 * - Single-threaded recording: Each worker thread records into its own
 * dedicated `CommandBuffer`.
 * - Multi-threaded recording: Use `ParallelCommandBuffer` to automatically
 * allocate per-thread buffers.
 * - Single-threaded playback: Playback via `commit(ctx)` is strictly sequential
 * and executed at system batch barriers or frame boundaries.
 *
 * ### Example:
 * ```cpp
 * CommandBuffer cmd;
 * cmd.spawn(Position{1.0f, 2.0f}, Velocity{0.0f, 1.0f});
 * cmd.despawn(entity_id);
 * cmd.add(entity_id, Frozen{true});
 * cmd.commit(world.command_context()); // Or cmd.commit(world);
 * ```
 */
class CommandBuffer {
public:
  /// Function pointer type for type-erased component tuple deserialization and
  /// entity creation.
  using SpawnFn = void (*)(internal::MutationContext &, const uint8_t *);

  /// Function pointer type for deferred component type descriptor registration.
  using RegisterTypeFn = void (*)(internal::ComponentRegistry &);

private:
  std::vector<uint8_t> buffer_;

public:
  /**
   * @brief Constructs a CommandBuffer with reserved byte capacity.
   * @param initial_capacity Baseline memory reservation in bytes (default: 64
   * KB).
   */
  explicit CommandBuffer(size_t initial_capacity = 65536);

  ~CommandBuffer();

  CommandBuffer(const CommandBuffer &) = delete;
  CommandBuffer &operator=(const CommandBuffer &) = delete;
  CommandBuffer(CommandBuffer &&) noexcept = default;
  CommandBuffer &operator=(CommandBuffer &&) noexcept = default;

  // --- Deferred Mutation Recording -------------------------------------------

  /**
   * @brief Defers the creation of a new entity with the specified initial
   * components.
   *
   * Serializes the POD component values into the internal byte stream alongside
   * a type-erased deserializer function pointer. Type registration and
   * archetype resolution occur on `commit()`.
   *
   * @tparam Comps Component types to attach to the created entity (must be
   * trivially copyable).
   * @param components Values of the initial components.
   */
  template <typename... Comps> void spawn(Comps... components);

  /**
   * @brief Defers the destruction of an entity.
   *
   * Records a destruction opcode and entity handle. On commit, the entity's
   * components are swapped-and-popped and its SlotMap generational record is
   * freed.
   *
   * @param entity Target entity handle to destroy.
   */
  void despawn(Entity entity);

  /**
   * @brief Defers the batch destruction of multiple entities.
   *
   * Records a batch destruction opcode with an array of entity handles. During
   * commit, relocations are batched and sorted per chunk in descending row
   * order for maximum cache efficiency.
   *
   * @param entities Contiguous span of entity handles to destroy.
   */
  void despawn(std::span<const Entity> entities);

  /**
   * @brief Defers attaching a component to an existing entity.
   *
   * On commit, moves the entity from its current archetype to the target
   * archetype with component T added, preserving all existing component values.
   *
   * @tparam T Component type to attach (must be trivially copyable).
   * @param entity Target entity handle.
   * @param component Component instance data.
   */
  template <typename T> void add(Entity entity, T component);

  /**
   * @brief Defers removing a component from an existing entity.
   *
   * On commit, moves the entity from its current archetype to the target
   * archetype with component T removed, preserving all other remaining
   * components.
   *
   * @tparam T Component type to remove.
   * @param entity Target entity handle.
   */
  template <typename T> void remove(Entity entity);

  // --- Playback & Execution --------------------------------------------------

  /**
   * @brief Flushes and executes all recorded commands in exact FIFO order into
   * the ECS storage.
   *
   * Dispatches opcodes, allocates entities, moves entities between archetypes,
   * and clears the buffer.
   *
   * @param ctx Execution context view holding references to SlotMap, ChunkPool,
   * ArchetypeGraph, and ComponentRegistry.
   */
  void commit(internal::MutationContext ctx);

  // --- Buffer Inspection & Reset ---------------------------------------------

  /// Clears the bytecode buffer without deallocating underlying reserved memory
  /// capacity.
  void clear() noexcept;

  /// Returns true if no deferred commands are currently queued in the buffer.
  [[nodiscard]] bool empty() const noexcept;

  /// Returns the current serialized size of the command bytecode stream in
  /// bytes.
  [[nodiscard]] size_t size_bytes() const noexcept;

  /// Returns a direct pointer to the raw bytecode data.
  [[nodiscard]] const uint8_t *data() const noexcept;

private:
  template <typename... Args> void write(const Args &...args);
  void write_bytes(const void *data, size_t size);

  template <typename T>
  [[nodiscard]] T read(size_t &offset) const noexcept;
  [[nodiscard]] const void *read_bytes(size_t &offset, size_t size) const noexcept;
};

// =============================================================================
// ParallelCommandBuffer: Sharded Thread-Local Command Aggregator
// =============================================================================

namespace internal {

/**
 * @class ParallelCommandBuffer
 * @brief Thread-indexed bank of CommandBuffers for lock-free parallel
 * recording.
 *
 * Allocates a distinct `CommandBuffer` for each worker thread, eliminating
 * atomic contention and mutex locks during parallel query iterations. All
 * buffers are committed sequentially during synchronization barriers.
 */
class ParallelCommandBuffer {
private:
  std::vector<CommandBuffer> buffers_;

public:
  /**
   * @brief Constructs a ParallelCommandBuffer with a dedicated buffer for each
   * worker thread.
   * @param thread_count Number of worker threads (default: 1).
   */
  explicit ParallelCommandBuffer(uint32_t thread_count = 1);

  ~ParallelCommandBuffer() = default;

  /// Returns the number of sharded worker thread command buffers.
  [[nodiscard]] uint32_t thread_count() const noexcept;

  /// Retrieves the dedicated CommandBuffer for a specific worker thread index.
  [[nodiscard]] CommandBuffer &get(uint32_t thread_id) noexcept;

  /// Commits all thread-local command buffers sequentially into the ECS
  /// storage.
  void commit(internal::MutationContext ctx);

  /// Clears all thread-local command buffers.
  void clear() noexcept;
};

} // namespace internal

} // namespace arche
