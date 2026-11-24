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
export import :component.detail.type_id_impl;
export import :hardware;
export import :math;
export import :gpu_layout;
export import :gpu_span;

// Layer 1: Memory & State Representation
export import :chunk;
export import :chunk.detail.impl;
export import :chunk_pool;
export import :chunk_pool.detail.impl;
export import :slot_map;
export import :entity.detail.slot_map_impl;
export import :entity_handle;
export import :resource;
export import :resource.detail.impl;

// Layer 2: Archetypes & Query Cache
export import :archetype;
export import :archetype.detail.impl;
export import :archetype_graph;
export import :archetype_graph.detail.impl;
export import :query_cache;

// Layer 3: Operations, Snapshot & Views
export import :mutation;
export import :mutation.detail.impl;
export import :snapshot;
export import :snapshot.detail.impl;
export import :command;
export import :command.detail.impl;
export import :component_access;
export import :component.detail.component_access_impl;
export import :entity_view;

// Layer 4: Execution Engine
export import :thread_pool;
export import :concurrency.detail.thread_pool_impl;
export import :query;
export import :query.detail.masks;
export import :query.detail.offsets;
export import :query.detail.impl;
export import :access_builder;
export import :system.detail.access_builder_impl;
export import :plugin;
export import :system.detail.plugin_impl;
export import :scheduler;
export import :system.detail.scheduler_impl;
export import :system_context;
export import :system.detail.system_context_impl;
export import :sort;
export import :sort.detail.impl;

// Layer 5: Domain Sub-Facades
export import :facade.archetype;
export import :facade.archetype_impl;
export import :facade.command;
export import :facade.command_impl;
export import :facade.entity;
export import :facade.entity_impl;
export import :facade.gpu;
export import :facade.gpu_impl;
export import :facade.resource;
export import :facade.resource_impl;
export import :facade.snapshot;
export import :facade.snapshot_impl;
export import :facade.system;
export import :facade.system_impl;

// Layer 6: World Coordinator & Assembly
export import :world;
export import :world.detail.impl;
