#pragma once

#include "arche/entity/entity.hpp"
#include "arche/hardware/gpu_layout.hpp"
#include "arche/persistence/snapshot.hpp"
#include "arche/resource/resource.hpp"
#include "arche/system/plugin.hpp"
#include "arche/system/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arche {

class World;
class Archetype;
struct Chunk;
class ThreadPool;
class Scheduler;

// =============================================================================
// Entities Domain Sub-Facade
// =============================================================================

/**
 * @class EntityFacade
 * @brief Domain sub-facade for global entity table queries and slot map introspection.
 */
class EntityFacade {
public:
  explicit constexpr EntityFacade(World *world) noexcept : world_(world) {}

  /// Returns true if the entity handle is alive and matches its current generation.
  [[nodiscard]] bool contains(Entity entity) const noexcept;

  /// Returns the total number of living entities across all archetypes.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the allocated capacity of the internal slot map.
  [[nodiscard]] size_t capacity() const noexcept;

  /// Returns true if zero entities currently exist in the simulation.
  [[nodiscard]] bool is_empty() const noexcept;

  /// Reconstructs a valid Entity handle from a 32-bit slot index, or std::nullopt if dead/empty.
  [[nodiscard]] std::optional<Entity> resolve(uint32_t index) const noexcept;

private:
  World *world_{nullptr};
};

// =============================================================================
// Snapshots Domain Sub-Facade
// =============================================================================

/**
 * @class SnapshotFacade
 * @brief Domain sub-facade for full-world binary serialization and persistence.
 */
class SnapshotFacade {
public:
  explicit constexpr SnapshotFacade(World *world) noexcept : world_(world) {}

  /// Serializes all archetypes, chunk columns, and active entities into a binary destination buffer.
  size_t save(void *destination_buffer, size_t max_bytes);

  /// Restores complete simulation state from a serialized snapshot buffer.
  void load(const void *snapshot_buffer, size_t bytes);

  /// Saves the complete simulation state directly to a file on disk.
  bool save_to_file(std::string_view file_path);

  /// Restores the complete simulation state directly from a file on disk.
  bool load_from_file(std::string_view file_path);

  /// Returns the total buffer size in bytes required to store a snapshot of the current state.
  [[nodiscard]] size_t size_bytes() const noexcept;

  /// Returns the snapshot header containing magic numbers, version, and component descriptors.
  [[nodiscard]] SnapshotHeader header() const noexcept;

private:
  World *world_{nullptr};
};

// =============================================================================
// Resources Domain Sub-Facade
// =============================================================================

/**
 * @class ResourceFacade
 * @brief Domain sub-facade for managing global singleton resources independent of entities.
 */
class ResourceFacade {
public:
  explicit constexpr ResourceFacade(World *world) noexcept : world_(world) {}

  /// Inserts or replaces a global singleton resource of type T.
  template <typename T, typename... Args>
  inline T &insert(Args &&...args);

  /// Retrieves a mutable reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] inline T &get();

  /// Retrieves a read-only reference to the singleton resource of type T.
  template <typename T>
  [[nodiscard]] inline const T &get() const;

  /// Checks whether a singleton resource of type T exists.
  template <typename T>
  [[nodiscard]] inline bool has() const noexcept;

  /// Checks whether a singleton resource exists by its runtime ResourceID.
  [[nodiscard]] inline bool has(ResourceID id) const noexcept;

  /// Removes the singleton resource of type T, returning true if it was present.
  template <typename T>
  inline bool remove();

private:
  World *world_{nullptr};
};

// =============================================================================
// Archetype & Storage Introspection Sub-Facade
// =============================================================================

/**
 * @class ArchetypeFacade
 * @brief Domain sub-facade for low-level archetype graph metrics and chunk memory layout.
 */
class ArchetypeFacade {
public:
  explicit constexpr ArchetypeFacade(World *world) noexcept : world_(world) {}

  /// Returns the total number of unique component archetypes registered in the graph.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the mutable list of allocated memory chunks for the specified archetype.
  [[nodiscard]] std::vector<Chunk *> &chunks(Archetype *arch);

  /// Returns a read-only span of memory chunks for the specified archetype.
  [[nodiscard]] std::span<Chunk *const> chunks(const Archetype *arch) const;

  /// Returns the number of entities across all archetypes containing component T.
  template <typename T>
  [[nodiscard]] inline uint32_t count_with() const noexcept;

private:
  World *world_{nullptr};
};

// =============================================================================
// Systems & Scheduler Sub-Facade
// =============================================================================

/**
 * @class SystemFacade
 * @brief Domain sub-facade for registering execution stages, systems, and plugins.
 */
class SystemFacade {
public:
  explicit constexpr SystemFacade(World *world) noexcept : world_(world) {}

  /// Executes all compiled scheduler stages in parallel using the provided ThreadPool.
  void run(ThreadPool &pool);

  /// Executes all compiled scheduler stages sequentially on the current thread.
  void run();

  /// Compiles system stages, resolving parallel dependencies and access descriptors.
  void compile();

  /// Adds an individual system function with its data access declaration.
  SystemFacade &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  );

  /// Registers a plugin name identifier for tracking.
  template <typename P>
  void register_plugin_name();

  /// Direct mutable reference to the underlying stage Scheduler.
  [[nodiscard]] Scheduler &scheduler() noexcept;

  /// Direct read-only reference to the underlying stage Scheduler.
  [[nodiscard]] const Scheduler &scheduler() const noexcept;

  /// Installs an autonomous Plugin struct with lifecycle initialization.
  template <Plugin P>
  World &add_plugin();

  template <Plugin P>
  World &add_plugin(P &&plugin);

  template <Plugin P>
  World &add_plugin(Scheduler &ext_scheduler);

  template <Plugin P>
  World &add_plugin(P &&plugin, Scheduler &ext_scheduler);

private:
  World *world_{nullptr};
};

// =============================================================================
// GPU Streaming & Reflection Sub-Facade
// =============================================================================

/**
 * @class GpuFacade
 * @brief Domain sub-facade for contiguous component chunk reflection and GPU streaming.
 */
class GpuFacade {
public:
  explicit constexpr GpuFacade(World *world) noexcept : world_(world) {}

  /// Returns spans of contiguous component column slices across all chunks for GPU upload.
  template <typename T>
  [[nodiscard]] inline std::vector<ComponentSpan<T>> view_chunks();

  /// Streams all component columns of type T directly into a destination memory buffer.
  template <typename T>
  inline size_t stream_to(void *destination_buffer);

private:
  World *world_{nullptr};
};

} // namespace arche
