#pragma once

// IWYU pragma: begin_exports

// Core Types & Identifiers
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/hardware.hpp"
#include "arche/type_id.hpp"
#include "arche/type_registry.hpp"

// Memory Layout & Storage
#include "arche/archetype.hpp"
#include "arche/chunk.hpp"
#include "arche/gpu_reflection.hpp"

// Queries, Filters & Iteration
#include "arche/query.hpp"
#include "arche/query_filter.hpp"

// Concurrency & Commands
#include "arche/command_buffer.hpp"
#include "arche/parallel_command_buffer.hpp"
#include "arche/thread_pool.hpp"

// Central World Facade
#include "arche/world.hpp"

// IWYU pragma: end_exports

namespace arche {

/**
 * @namespace arche
 * @brief Standalone, zero-dependency, high-performance C++20 archetype ECS library.
 */

} // namespace arche

