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

export module arche:command.detail.impl;

import :command;
import :mutation;

export namespace arche {

using namespace internal;

// =============================================================================
// ParallelCommandBuffer Implementations
// =============================================================================

namespace internal {

ParallelCommandBuffer::ParallelCommandBuffer(uint32_t thread_count)
    : buffers_(thread_count) {}

uint32_t ParallelCommandBuffer::thread_count() const noexcept {
  return static_cast<uint32_t>(buffers_.size());
}

CommandBuffer &ParallelCommandBuffer::get(uint32_t thread_id) noexcept {
  assert(thread_id < buffers_.size() && "Worker thread ID out of range");
  return buffers_[thread_id];
}

void ParallelCommandBuffer::commit(MutationContext ctx) {
  for (auto &buf : buffers_) {
    buf.commit(ctx);
  }
}

void ParallelCommandBuffer::clear() noexcept {
  for (auto &buf : buffers_) {
    buf.clear();
  }
}

} // namespace internal

// =============================================================================
// CommandBuffer Lifecycle & Buffer Operations
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

// =============================================================================
// CommandBuffer Recording Implementations
// =============================================================================

// --- Spawn -------------------------------------------------------------------

template <typename... Comps>
inline void CommandBuffer::spawn(Comps... components) {
  // 1. Validate trivially copyable requirements for POD components.
  static_assert(
      (std::is_trivially_copyable_v<std::decay_t<Comps>> && ...),
      "Deferred spawn requires trivially copyable POD components"
  );

  // 2. Serialize packed component tuple payload into bytecode stream.
  using TupleType = std::tuple<std::decay_t<Comps>...>;
  if constexpr (sizeof...(Comps) > 0) {
    const uint32_t size = static_cast<uint32_t>(sizeof(TupleType));
    write(
        CommandType::CreateEntity,
        &Mutation::spawn_payload<Comps...>,
        size
    );
    TupleType tuple{std::forward<Comps>(components)...};
    write_bytes(&tuple, size);
  } else {
    write(
        CommandType::CreateEntity,
        &Mutation::spawn_payload<Comps...>,
        uint32_t{0}
    );
  }
}

// --- Despawn -----------------------------------------------------------------

inline void CommandBuffer::despawn(Entity entity) {
  // Write single-entity destruction opcode and target entity handle in one
  // statement.
  write(CommandType::DestroyEntity, entity);
}

inline void CommandBuffer::despawn(std::span<const Entity> entities) {
  // 1. Skip redundant operations for empty entity spans.
  if (entities.empty()) {
    return;
  }

  // 2. Write batch destruction opcode, count header, and contiguous handle
  // array.
  write(CommandType::DestroyEntities, static_cast<uint32_t>(entities.size()));
  write_bytes(entities.data(), entities.size() * sizeof(Entity));
}

// --- Add - Component ---------------------------------------------------------

template <typename T>
inline void CommandBuffer::add(Entity entity, T component) {
  // 1. Enforce POD trivial copyability for safe bytecode blitting.
  static_assert(
      std::is_trivially_copyable_v<T>,
      "Deferred add requires trivially copyable data types"
  );

  // 2. Atomically write opcode, target entity, TypeID, type registrar, and
  // component size.
  write(
      CommandType::AddComponent,
      entity,
      type_id<T>(),
      RegisterTypeFn{[](TypeRegistry &reg) { reg.register_type<T>(); }},
      static_cast<uint32_t>(sizeof(T))
  );

  // 3. Append component data bytes.
  write_bytes(&component, sizeof(T));
}

// --- Remove - Component ------------------------------------------------------

template <typename T> inline void CommandBuffer::remove(Entity entity) {
  // 1. Write component removal opcode, entity handle, and TypeID in one
  // statement.
  write(CommandType::RemoveComponent, entity, type_id<T>());
}

// =============================================================================
// CommandBuffer Internal Serialization Helpers
// =============================================================================

template <typename... Args>
inline void CommandBuffer::write(const Args &...args) {
  // 1. Fold over variadic arguments, enforcing trivial copyability per
  // argument.
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
  // 1. Append contiguous byte range into internal buffer.
  const auto *ptr = static_cast<const uint8_t *>(data);
  buffer_.insert(buffer_.end(), ptr, ptr + size);
}

template <typename T>
inline T CommandBuffer::read(size_t &offset) const noexcept {
  // 1. Enforce trivial copyability on deserialized value type.
  using Decayed = std::decay_t<T>;
  static_assert(
      std::is_trivially_copyable_v<Decayed>,
      "read requires trivially copyable POD types"
  );

  // 2. Bitwise blit value out of buffer and advance cursor offset.
  Decayed val{};
  std::memcpy(&val, &buffer_[offset], sizeof(Decayed));
  offset += sizeof(Decayed);
  return val;
}

inline const void *CommandBuffer::read_bytes(
    size_t &offset,
    size_t size
) const noexcept {
  // 1. Obtain pointer to contiguous bytes and advance cursor offset.
  const void *ptr = &buffer_[offset];
  offset += size;
  return ptr;
}

// =============================================================================
// CommandBuffer Execution & Commit
// =============================================================================

inline void CommandBuffer::commit(MutationContext ctx) {
  // 1. Initialize bytecode reading cursor.
  size_t offset = 0;

  // 2. Sequential FIFO opcode execution loop.
  while (offset < buffer_.size()) {
    switch (read<CommandType>(offset)) {
    case CommandType::CreateEntity: {
      const auto spawn_fn = read<SpawnFn>(offset);
      const auto size = read<uint32_t>(offset);
      const auto *payload = static_cast<const uint8_t *>(
          read_bytes(offset, size)
      );
      spawn_fn(ctx, payload);
      break;
    }
    case CommandType::DestroyEntity: {
      Mutation::destroy_entity(ctx, read<Entity>(offset));
      break;
    }
    case CommandType::DestroyEntities: {
      const auto count = read<uint32_t>(offset);
      const auto *ents = static_cast<const Entity *>(
          read_bytes(offset, count * sizeof(Entity))
      );
      Mutation::destroy_entities(ctx, {ents, count});
      break;
    }
    case CommandType::AddComponent: {
      const auto entity = read<Entity>(offset);
      const auto tid = read<TypeID>(offset);
      const auto reg_fn = read<RegisterTypeFn>(offset);
      const auto size = read<uint32_t>(offset);
      const void *data = read_bytes(offset, size);
      if (reg_fn) {
        reg_fn(ctx.registry);
      }
      Mutation::add_component(ctx, entity, tid, data);
      break;
    }
    case CommandType::RemoveComponent: {
      const auto entity = read<Entity>(offset);
      const auto tid = read<TypeID>(offset);
      Mutation::remove_component(ctx, entity, tid);
      break;
    }
    default:
      assert(false && "Corrupted CommandBuffer bytecode encountered!");
      return;
    }
  }

  // 3. Reset buffer after successful playback.
  clear();
}

} // namespace arche
