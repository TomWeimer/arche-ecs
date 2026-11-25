module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

export module arche:command;

export import :entity;
export import :component_id;

export namespace arche {

// =============================================================================
// Command Operation Codes
// =============================================================================

/**
 * @enum CommandType
 * @brief Identifies deferred structural mutation opcodes in serialized CommandBuffer bytecode.
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
 * @brief Lock-free, linear bytecode recording buffer for deferred structural world mutations.
 *
 * Records entity creations, destructions, and component additions/removals into
 * a contiguous memory byte stream. Pure passive storage container with zero
 * execution/replay dependencies.
 */
class CommandBuffer {
private:
  std::vector<uint8_t> buffer_;

public:
  /**
   * @brief Constructs a CommandBuffer with reserved byte capacity.
   * @param initial_capacity Baseline memory reservation in bytes (default: 64 KB).
   */
  explicit CommandBuffer(size_t initial_capacity = 65536);

  ~CommandBuffer();

  CommandBuffer(const CommandBuffer &) = delete;
  CommandBuffer &operator=(const CommandBuffer &) = delete;
  CommandBuffer(CommandBuffer &&) noexcept = default;
  CommandBuffer &operator=(CommandBuffer &&) noexcept = default;

  // --- Deferred Mutation Recording -------------------------------------------

  /**
   * @brief Defers the creation of a new entity with the specified initial components.
   */
  template <typename... Comps>
  void spawn(Comps... components);

  /**
   * @brief Defers the destruction of an entity.
   */
  void despawn(Entity entity);

  /**
   * @brief Defers batch destruction of entities.
   */
  void despawn(std::span<const Entity> entities);

  /**
   * @brief Defers attaching a component to an entity.
   */
  template <typename T>
  void add(Entity entity, T component);

  /**
   * @brief Defers removing a component from an entity.
   */
  template <typename T>
  void remove(Entity entity);

  // --- Buffer Inspection & Direct Memory Access ------------------------------

  void clear() noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] size_t size_bytes() const noexcept;
  [[nodiscard]] const uint8_t *data() const noexcept;
  [[nodiscard]] std::span<const uint8_t> bytecode() const noexcept;

  // --- Bytecode Serialization Helpers ----------------------------------------

  template <typename... Args>
  void write(const Args &...args);

  void write_bytes(const void *data, size_t size);

  template <typename T>
  [[nodiscard]] T read(size_t &offset) const noexcept;

  [[nodiscard]] const void *read_bytes(size_t &offset, size_t size) const noexcept;
};

// =============================================================================
// CommandBuffer Inlined Implementations
// =============================================================================

inline CommandBuffer::CommandBuffer(size_t initial_capacity) {
  buffer_.reserve(initial_capacity);
}

inline CommandBuffer::~CommandBuffer() { clear(); }

inline void CommandBuffer::clear() noexcept { buffer_.clear(); }

inline bool CommandBuffer::empty() const noexcept { return buffer_.empty(); }

inline size_t CommandBuffer::size_bytes() const noexcept {
  return buffer_.size();
}

inline const uint8_t *CommandBuffer::data() const noexcept {
  return buffer_.data();
}

inline std::span<const uint8_t> CommandBuffer::bytecode() const noexcept {
  return buffer_;
}

template <typename... Comps>
inline void CommandBuffer::spawn(Comps... components) {
  static_assert(
      (std::is_trivially_copyable_v<std::decay_t<Comps>> && ...),
      "Deferred spawn requires trivially copyable POD components"
  );

  const uint32_t comp_count = static_cast<uint32_t>(sizeof...(Comps));
  write(CommandType::CreateEntity, comp_count);
  if constexpr (sizeof...(Comps) > 0) {
    (
        [&] {
          using Comp = std::decay_t<Comps>;
          write(
              component_id<Comp>(),
              static_cast<uint32_t>(sizeof(Comp)),
              static_cast<uint32_t>(alignof(Comp))
          );
          write_bytes(&components, sizeof(Comp));
        }(),
        ...);
  }
}

inline void CommandBuffer::despawn(Entity entity) {
  write(CommandType::DestroyEntity, entity);
}

inline void CommandBuffer::despawn(std::span<const Entity> entities) {
  if (entities.empty()) {
    return;
  }
  const uint32_t count = static_cast<uint32_t>(entities.size());
  write(CommandType::DestroyEntities, count);
  write_bytes(entities.data(), count * sizeof(Entity));
}

template <typename T>
inline void CommandBuffer::add(Entity entity, T component) {
  using Comp = std::decay_t<T>;
  static_assert(
      std::is_trivially_copyable_v<Comp>,
      "Deferred add requires trivially copyable POD component"
  );
  const uint32_t size = static_cast<uint32_t>(sizeof(Comp));
  const uint32_t align = static_cast<uint32_t>(alignof(Comp));
  write(CommandType::AddComponent, entity, component_id<Comp>(), size, align);
  write_bytes(&component, size);
}

template <typename T>
inline void CommandBuffer::remove(Entity entity) {
  using Comp = std::decay_t<T>;
  write(CommandType::RemoveComponent, entity, component_id<Comp>());
}

template <typename... Args>
inline void CommandBuffer::write(const Args &...args) {
  auto write_one = [this](const auto &val) {
    using T = std::decay_t<decltype(val)>;
    static_assert(
        std::is_trivially_copyable_v<T>,
        "write requires trivially copyable POD types"
    );
    write_bytes(&val, sizeof(T));
  };
  (write_one(args), ...);
}

inline void CommandBuffer::write_bytes(const void *data, size_t size) {
  const auto *ptr = static_cast<const uint8_t *>(data);
  buffer_.insert(buffer_.end(), ptr, ptr + size);
}

template <typename T>
inline T CommandBuffer::read(size_t &offset) const noexcept {
  using Decayed = std::decay_t<T>;
  static_assert(
      std::is_trivially_copyable_v<Decayed>,
      "read requires trivially copyable POD types"
  );
  Decayed val{};
  std::memcpy(&val, &buffer_[offset], sizeof(Decayed));
  offset += sizeof(Decayed);
  return val;
}

inline const void *CommandBuffer::read_bytes(
    size_t &offset,
    size_t size
) const noexcept {
  const void *ptr = &buffer_[offset];
  offset += size;
  return ptr;
}

// =============================================================================
// ParallelCommandBuffer: Multi-Threaded Command Queue
// =============================================================================

namespace internal {

/**
 * @class ParallelCommandBuffer
 * @brief Thread-indexed aggregation of per-worker CommandBuffers.
 */
class ParallelCommandBuffer {
private:
  std::vector<CommandBuffer> buffers_;

public:
  explicit ParallelCommandBuffer(uint32_t thread_count);
  ~ParallelCommandBuffer() = default;

  ParallelCommandBuffer(const ParallelCommandBuffer &) = delete;
  ParallelCommandBuffer &operator=(const ParallelCommandBuffer &) = delete;
  ParallelCommandBuffer(ParallelCommandBuffer &&) noexcept = default;
  ParallelCommandBuffer &operator=(ParallelCommandBuffer &&) noexcept = default;

  [[nodiscard]] uint32_t thread_count() const noexcept;
  [[nodiscard]] CommandBuffer &get(uint32_t thread_id) noexcept;
  [[nodiscard]] std::span<CommandBuffer> buffers() noexcept;
  [[nodiscard]] std::span<const CommandBuffer> buffers() const noexcept;
  void clear() noexcept;
};

} // namespace internal

} // namespace arche
