module;

#include <cstdint>
#include <string>
#include <vector>

export module arche:world.detail.world_access;

import :component_mask;
import :query_cache;
import :chunk;
import :chunk_pool;
import :slot_map;
import :archetype_graph;
import :type_id;
import :resource;
import :scheduler;

export namespace arche {
class World;
class Archetype;
struct Chunk;
struct TypeRegistry;
class ResourceContainer;
class Scheduler;
class ChunkPool;
class ArchetypeGraph;
} // namespace arche

export namespace arche::internal {

struct WorldStorage;

struct WorldAccess {
  static WorldStorage &storage(World &w) noexcept;
  static const WorldStorage &storage(const World &w) noexcept;

  static SlotMap &slot_map(World &w) noexcept;
  static const SlotMap &slot_map(const World &w) noexcept;

  static ChunkPool &chunk_pool(World &w) noexcept;

  static ArchetypeGraph &graph(World &w) noexcept;
  static const ArchetypeGraph &graph(const World &w) noexcept;

  static TypeRegistry &registry(World &w) noexcept;
  static const TypeRegistry &registry(const World &w) noexcept;

  static ResourceContainer &resources(World &w) noexcept;
  static const ResourceContainer &resources(const World &w) noexcept;

  static Scheduler &scheduler(World &w) noexcept;
  static const Scheduler &scheduler(const World &w) noexcept;

  static std::vector<std::string> &installed_plugins(World &w) noexcept;

  static uint32_t &version(World &w) noexcept;
  static uint32_t version(const World &w) noexcept;
  static uint32_t advance_version(World &w) noexcept;

  static uint32_t &chunk_size(World &w) noexcept;
  static uint32_t chunk_size(const World &w) noexcept;

  static QueryCache *query_cache(
      World &w,
      const ComponentMask &include,
      const ComponentMask &exclude
  );
};

} // namespace arche::internal
