module;

export module arche;

// =============================================================================
// Arche ECS - C++20 Primary Module Interface Unit
// =============================================================================

// --- Public Library Surface (in namespace arche) -----------------------------
export import :types;
export import :entity;
export import :access;
export import :filters;
export import :type_id;
export import :gpu_span;
export import :entity_view;
export import :command;
export import :command.detail.impl;
export import :query;
export import :access_builder;
export import :plugin;
export import :system_types;
export import :system_context;
export import :domain.archetype;
export import :domain.command;
export import :domain.entity;
export import :domain.gpu;
export import :domain.resource;
export import :domain.snapshot;
export import :domain.system;
export import :world;

// --- Internal Engine Subsystems (in namespace arche::internal) ----------------
export import :hardware;
export import :math;
export import :gpu_layout;
export import :component_mask;
export import :component.detail.column_layout;
export import :component.detail.type_id_impl;
export import :chunk;
export import :chunk.detail.chunk_header;
export import :chunk.detail.impl;
export import :chunk_pool;
export import :chunk_pool.detail.impl;
export import :slot_map;
export import :entity.detail.entity_record;
export import :entity.detail.slot_map_impl;
export import :entity_handle;
export import :resource;
export import :resource.detail.impl;
export import :archetype;
export import :archetype.detail.impl;
export import :archetype_graph;
export import :archetype_graph.detail.impl;
export import :query_cache;
export import :mutation;
export import :mutation.detail.impl;
export import :snapshot;
export import :snapshot.detail.header;
export import :snapshot.detail.impl;
export import :thread_pool;
export import :concurrency.detail.thread_pool_impl;
export import :scheduler;
export import :system.detail.scheduler_impl;
export import :sort;
export import :sort.detail.impl;
export import :sort.detail.permute;
export import :query.detail.masks;
export import :query.detail.offsets;
export import :query.detail.impl;
export import :query.detail.query_iterator;
export import :query.detail.filter;
export import :system.detail.access_builder_impl;
export import :system.detail.plugin_impl;
export import :system.detail.system_context_impl;
export import :domain.archetype_impl;
export import :domain.command_impl;
export import :domain.entity_impl;
export import :domain.gpu_impl;
export import :domain.resource_impl;
export import :domain.snapshot_impl;
export import :domain.system_impl;
export import :world.detail.impl;
