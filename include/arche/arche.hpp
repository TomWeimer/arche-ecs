#pragma once

// IWYU pragma: begin_exports

// Core Types & Identifiers
#include "arche/hardware/hardware.hpp"
#include "arche/component/component_mask.hpp"
#include "arche/entity/entity.hpp"
#include "arche/entity/entity_record.hpp"
#include "arche/entity/entity_view.hpp"
#include "arche/component/column_layout.hpp"
#include "arche/resource/resource.hpp"
#include "arche/persistence/snapshot.hpp"
#include "arche/component/type_id.hpp"

// Memory Layout & Storage
#include "arche/archetype/archetype.hpp"
#include "arche/hardware/gpu_layout.hpp"
#include "arche/chunk/chunk.hpp"
#include "arche/entity/slot_map.hpp"

// Queries, Filters & Iteration
#include "arche/query/filter.hpp"
#include "arche/query/query.hpp"
#include "arche/spatial/sort.hpp"

// Concurrency & Commands
#include "arche/command/command_buffer.hpp"
#include "arche/system/system_context.hpp"
#include "arche/concurrency/thread_pool.hpp"

// Central World Facade
#include "arche/world.hpp"
#include "arche/world_facades.hpp"
#include "arche/world_facades_impl.hpp"

// Scheduling, Access & Plugins
#include "arche/system/access.hpp"
#include "arche/system/plugin.hpp"
#include "arche/system/scheduler.hpp"

// IWYU pragma: end_exports

namespace arche {

/**
 * @namespace arche
 * @brief Standalone, zero-dependency, high-performance C++20 archetype ECS
 * library.
 */

} // namespace arche
