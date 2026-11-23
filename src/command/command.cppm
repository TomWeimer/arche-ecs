module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module arche:command;

import :entity;
import :type_id;

export namespace arche {

class World;

enum class CommandType : uint8_t {
  CreateEntity,
  DestroyEntity,
  AddComponent,
  RemoveComponent,
  DestroyEntities
};

class CommandBuffer {
public:
  using SpawnFn = void (*)(World &, const uint8_t *);
  using RegisterTypeFn = void (*)(World &);

private:
  std::vector<uint8_t> buffer_;

public:
  explicit CommandBuffer(size_t initial_capacity = 65536) {
    buffer_.reserve(initial_capacity);
  }

  ~CommandBuffer() { clear(); }

  CommandBuffer(const CommandBuffer &) = delete;
  CommandBuffer &operator=(const CommandBuffer &) = delete;
  CommandBuffer(CommandBuffer &&) noexcept = default;
  CommandBuffer &operator=(CommandBuffer &&) noexcept = default;

  template <typename... Comps> inline void spawn(Comps... components) {
    static_assert(
        (std::is_trivially_copyable_v<std::decay_t<Comps>> && ...),
        "Deferred spawn requires trivially copyable POD components"
    );
    write_pod(CommandType::CreateEntity);
    write_pod<SpawnFn>(&spawn_entity_helper<Comps...>);
    write_entity_payload(std::move(components)...);
  }

  inline void despawn(Entity entity) {
    write_pod(CommandType::DestroyEntity);
    write_pod(entity);
  }

  inline void despawn(std::span<const Entity> entities) {
    if (entities.empty())
      return;
    write_pod(CommandType::DestroyEntities);
    write_pod(static_cast<uint32_t>(entities.size()));
    write_bytes(entities.data(), entities.size() * sizeof(Entity));
  }

  template <typename T> inline void add(Entity entity, T component) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "Deferred add requires trivially copyable data types"
    );
    write_pod(CommandType::AddComponent);
    write_pod(entity);
    write_pod(type_id<T>());
    write_pod<RegisterTypeFn>(&register_type_helper<T>);
    write_component_payload(component);
  }

  template <typename T> inline void remove(Entity entity) {
    write_pod(CommandType::RemoveComponent);
    write_pod(entity);
    write_pod(type_id<T>());
  }

  void commit(World &world);

  inline void clear() noexcept { buffer_.clear(); }
  [[nodiscard]] inline bool empty() const noexcept { return buffer_.empty(); }
  [[nodiscard]] inline size_t size_bytes() const noexcept {
    return buffer_.size();
  }
  [[nodiscard]] inline const uint8_t *data() const noexcept {
    return buffer_.data();
  }

private:
  template <typename... Comps>
  static void spawn_entity_helper(World &world, const uint8_t *payload);

  template <typename T> static void register_type_helper(World &world);

  template <typename... Comps>
  inline void write_entity_payload(Comps &&...components) {
    using TupleType = std::tuple<std::decay_t<Comps>...>;
    if constexpr (sizeof...(Comps) > 0) {
      const uint32_t size = static_cast<uint32_t>(sizeof(TupleType));
      write_pod(size);
      TupleType tuple{std::forward<Comps>(components)...};
      write_bytes(&tuple, size);
    } else {
      write_pod(uint32_t{0});
    }
  }

  template <typename T>
  inline void write_component_payload(const T &component) {
    write_pod(static_cast<uint32_t>(sizeof(T)));
    write_bytes(&component, sizeof(T));
  }

  inline void write_bytes(const void *data, size_t size) {
    const auto *ptr = static_cast<const uint8_t *>(data);
    buffer_.insert(buffer_.end(), ptr, ptr + size);
  }

  template <typename T> inline void write_pod(const T &value) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "write_pod requires trivially copyable data types"
    );
    write_bytes(&value, sizeof(T));
  }
};

class ParallelCommandBuffer {
private:
  std::vector<CommandBuffer> buffers_;

public:
  explicit ParallelCommandBuffer(uint32_t thread_count = 1)
      : buffers_(thread_count) {}

  ~ParallelCommandBuffer() = default;

  [[nodiscard]] inline uint32_t thread_count() const noexcept {
    return static_cast<uint32_t>(buffers_.size());
  }

  [[nodiscard]] inline CommandBuffer &get(uint32_t thread_id) noexcept {
    assert(thread_id < buffers_.size() && "Worker thread ID out of range");
    return buffers_[thread_id];
  }

  inline void commit(World &world) {
    for (auto &buf : buffers_) {
      buf.commit(world);
      buf.clear();
    }
  }

  inline void clear() noexcept {
    for (auto &buf : buffers_) {
      buf.clear();
    }
  }
};

} // namespace arche
