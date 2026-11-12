#include "arche/command_buffer.hpp"
#include "arche/world.hpp"

#include <cassert>
#include <cstring>

namespace arche {

// =============================================================================
// Deferred Command Playback Implementation
// =============================================================================

/**
 * @brief Executes all recorded commands against the World in order, then clears
 * the buffer.
 *
 * @details
 * ### Execution Steps:
 * 1. Sequentially scans recorded commands from the continuous byte stream.
 * 2. Unpacks command arguments using safe byte copies (`read_value`) to prevent
 *    unaligned memory access faults.
 * 3. Dispatches actions:
 *    - `DestroyEntity`: Destroys the entity and frees its slot.
 *    - `AddComponent`: Migrates the entity to include the new component
 * payload.
 *    - `RemoveComponent`: Migrates the entity to exclude the removed component.
 * 4. Clears recorded command bytes while preserving reserved vector capacity.
 *
 * @param world Central World instance to apply the recorded modifications to.
 */
void CommandBuffer::playback(World &world) {
  size_t offset = 0;

  // Reads a plain data value and advances the byte stream offset safely
  auto read_value = [&]<typename T>(T &out) {
    std::memcpy(&out, &buffer_[offset], sizeof(T));
    offset += sizeof(T);
  };

  while (offset < buffer_.size()) {
    CommandType cmd_type{};
    read_value(cmd_type);

    switch (cmd_type) {
    case CommandType::DestroyEntity: {
      Entity entity{};
      read_value(entity);
      world.destroy_entity(entity);
      break;
    }
    case CommandType::AddComponent: {
      Entity entity{};
      TypeID tid{};
      uint32_t size{};
      read_value(entity);
      read_value(tid);
      read_value(size);

      const void *data = &buffer_[offset];
      offset += size;

      world.apply_command_add_component(entity, tid, data);
      break;
    }
    case CommandType::RemoveComponent: {
      Entity entity{};
      TypeID tid{};
      read_value(entity);
      read_value(tid);

      world.apply_command_remove_component(entity, tid);
      break;
    }
    default:
      assert(false && "Corrupted CommandBuffer byte stream encountered!");
      return;
    }
  }
  clear();
}

} // namespace arche
