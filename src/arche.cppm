module;

export module arche;

// =============================================================================
// Arche ECS - C++20 Primary Module Interface Unit
// =============================================================================

// Layer 0: Foundation Types, Hardware & Descriptors
export import :types;
export import :entity;
export import :access;
export import :filters;
export import :component_mask;
export import :type_id;
export import :hardware;
export import :gpu_layout;

// Layer 1: Memory & State Representation
export import :chunk;
export import :chunk_pool;
export import :slot_map;
export import :entity_handle;
export import :resource;
export import :snapshot;

// Layer 2: Archetypes & Query Cache
export import :archetype;
export import :archetype_graph;
export import :query_cache;

// Layer 3: Operations & Views
export import :command;
export import :component_access;
export import :entity_view;

// Layer 4: Execution Engine
export import :thread_pool;
export import :query;
export import :query.detail.impl;
export import :access_builder;
export import :plugin;
export import :scheduler;
export import :system_context;
export import :sort;

// Layer 5: Contracts & World Coordinator
export import :contracts.archetype;
export import :contracts.entity;
export import :contracts.gpu;
export import :contracts.resource;
export import :contracts.snapshot;
export import :contracts.system;
export import :world;
export import :world.detail.access_impl;
export import :world.detail.impl;
