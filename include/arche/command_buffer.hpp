#pragma once

#include "arche/entity.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace arche {

class World;

// =============================================================================
// Command Type Identifiers
// =============================================================================

/**
 * @enum CommandType
 * @brief Identifies the type of deferred mutation command stored in the buffer.
 */
enum class CommandType : uint8_t {
  /// Reserved for deferred entity creation.
  CreateEntity,

  /// Destroys an entity and frees its slot.
  DestroyEntity,

  /// Attaches a new component to an entity.
  AddComponent,

  /// Strips an existing component from an entity.
  RemoveComponent
};

// =============================================================================
// Deferred Command Buffer
// =============================================================================

/**
 * @class CommandBuffer
 * @brief Records entity changes during loops so they can be safely applied
 * later.
 *
 * @details
 * ### Why We Need a Command Buffer
 * When a system is looping through entities (for example, checking health to
 * remove dead enemies), modifying an entity immediately causes problems:
 * - Removing an entity or adding a component moves data around in memory
 *   (Swap-and-Pop).
 * - Changing memory during a loop disrupts the iteration and can lead to bugs.
 *
 * A CommandBuffer solves this:
 * 1. Inside your loop, you record actions (like `destroy_entity` or
 * `add_component`).
 * 2. All commands are written into a single continuous stream of bytes.
 * 3. Once your loops finish, you call `playback(world)` to apply all changes
 * safely in the order they were recorded.
 *
 * ### Why Use a Byte Stream?
 * Instead of allocating individual objects on the heap for every command (which
 * is slow and scatters memory), all commands and their component data are
 * packed tightly into one `std::vector<uint8_t>`. This makes recording fast and
 * avoids heap allocation spikes during gameplay.
 */
class CommandBuffer {

private:
  // --- Member Data -----------------------------------------------------------

  /// Continuous byte storage for recorded commands.
  std::vector<uint8_t> buffer_;

public:
  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Creates a command buffer with an initial pre-allocated capacity.
   *
   * @param initial_capacity Initial byte capacity reserved in the buffer
   * (default 64 KB).
   */
  explicit CommandBuffer(size_t initial_capacity = 65536) {
    buffer_.reserve(initial_capacity);
  }

  /**
   * @brief Cleans up and clears the command buffer.
   */
  ~CommandBuffer() { clear(); }

  // Non-copyable, movable
  CommandBuffer(const CommandBuffer &) = delete;
  CommandBuffer &operator=(const CommandBuffer &) = delete;
  CommandBuffer(CommandBuffer &&) noexcept = default;
  CommandBuffer &operator=(CommandBuffer &&) noexcept = default;

  // --- Command Recording -----------------------------------------------------

  /**
   * @brief Queues the destruction of an entity.
   *
   * @param entity Handle of the entity to destroy during playback.
   */
  inline void destroy_entity(Entity entity) {
    write_pod(CommandType::DestroyEntity);
    write_pod(entity);
  }

  /**
   * @brief Queues adding a component to an entity.
   *
   * @tparam T Component type (must be trivially copyable plain data).
   * @param entity Target entity to receive the component.
   * @param component Component data to attach during playback.
   */
  template <typename T> inline void add_component(Entity entity, T component) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "Deferred add_component requires trivially copyable data types"
    );
    write_pod(CommandType::AddComponent);
    write_pod(entity);
    write_pod(type_id<T>());
    write_pod(static_cast<uint32_t>(sizeof(T)));
    write_bytes(&component, sizeof(T));
  }

  /**
   * @brief Queues removing a component from an entity.
   *
   * @tparam T Component type to strip from the entity.
   * @param entity Target entity losing the component.
   */
  template <typename T> inline void remove_component(Entity entity) {
    write_pod(CommandType::RemoveComponent);
    write_pod(entity);
    write_pod(type_id<T>());
  }

  // --- Playback & Buffer Management ------------------------------------------

  /// Executes all recorded commands against the World in order, then clears the
  /// buffer.
  void playback(World &world);

  /**
   * @brief Clears all queued commands, keeping allocated capacity for reuse.
   */
  inline void clear() noexcept { buffer_.clear(); }

  /**
   * @brief Checks if there are any commands currently queued.
   */
  [[nodiscard]] inline bool empty() const noexcept { return buffer_.empty(); }

  /**
   * @brief Returns the total size of recorded commands in bytes.
   */
  [[nodiscard]] inline size_t size_bytes() const noexcept {
    return buffer_.size();
  }

private:
  // --- Internal Byte Writing Helpers -----------------------------------------

  /**
   * @brief Appends raw bytes to the end of the command buffer.
   *
   * @param data Pointer to the bytes to copy.
   * @param size Number of bytes to copy.
   */
  inline void write_bytes(const void *data, size_t size) {
    const auto *ptr = static_cast<const uint8_t *>(data);
    buffer_.insert(buffer_.end(), ptr, ptr + size);
  }

  /**
   * @brief Appends a plain data value directly into the byte stream.
   *
   * @tparam T Trivially copyable type.
   * @param value The value to append.
   */
  template <typename T> inline void write_pod(const T &value) {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "write_pod requires trivially copyable data types"
    );
    write_bytes(&value, sizeof(T));
  }
};

} // namespace arche
