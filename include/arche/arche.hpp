#pragma once

// Core Types & Identifiers
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/hardware.hpp"
#include "arche/math_utils.hpp"
#include "arche/type_id.hpp"
#include "arche/type_registry.hpp"

// Memory Layout & Storage
#include "arche/archetype.hpp"
#include "arche/archetype_graph.hpp"
#include "arche/chunk.hpp"
#include "arche/chunk_pool.hpp"
#include "arche/gpu_reflection.hpp"
#include "arche/slot_map.hpp"

// Queries, Filters & Iteration
#include "arche/query.hpp"
#include "arche/query_cache.hpp"
#include "arche/query_filter.hpp"
#include "arche/query_iterator.hpp"

// Concurrency & Commands
#include "arche/command_buffer.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/thread_pool.hpp"

// Central World Facade
#include "arche/world.hpp"

namespace arche {

/**
 * @namespace arche
 * @brief Standalone, zero-dependency, high-performance C++20 archetype ECS library.
 */

} // namespace arche

