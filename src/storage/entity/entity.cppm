module;

export module arche:entity_handle;

// =============================================================================
// Entity Subsystem Re-Exports
// =============================================================================

/**
 * @file entity.cppm
 * @brief Public interface umbrella partition for the entity subsystem.
 *
 * Re-exports the 64-bit Entity handle primitive and the generational SlotMap
 * routing table.
 */

export import :entity;
export import :slot_map;
