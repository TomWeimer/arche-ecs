#pragma once

// IWYU pragma: begin_exports

// Core Types & Identifiers
#include "arche/entity.hpp"
#include "arche/resource.hpp"
#include "component/column_layout.hpp"
#include "component/component_mask.hpp"
#include "component/type_id.hpp"
#include "entity/entity_record.hpp"
#include "hardware/hardware.hpp"
#include "persistence/snapshot.hpp"

// Memory Layout & Storage
#include "archetype/archetype.hpp"
#include "chunk/chunk.hpp"
#include "entity/slot_map.hpp"
#include "hardware/gpu_layout.hpp"

// Queries, Filters & Iteration
#include "arche/query.hpp"
#include "query/filter.hpp"
#include "spatial/sort.hpp"

// Concurrency & Commands
#include "arche/command.hpp"
#include "concurrency/thread_pool.hpp"

// Central World Facade
#include "arche/world.hpp"
#include "world/world_facades_impl.hpp"

// Scheduling, Access & Plugins
#include "system/system_context.hpp"
#include "system/access.hpp"
#include "system/plugin.hpp"
#include "system/scheduler.hpp"

// IWYU pragma: end_exports

namespace arche {

/**
 * @namespace arche
 * @brief Standalone, zero-dependency, high-performance C++20 archetype ECS
 * library.
 */

} // namespace arche
