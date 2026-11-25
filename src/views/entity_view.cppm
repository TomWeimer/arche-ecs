module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

export module arche:entity_view;

import :entity;
import :component_id;
import :chunk;
import :slot_map;
import :archetype;
import :mutation;

export namespace arche {

// =============================================================================
// Entity Table View (Global Population & Table Queries)
// =============================================================================

/**
 * @class EntityTableView
 * @brief Domain view for global entity table queries and slot map introspection.
 *
 * Accessible via `world.entities`.
 */
class EntityTableView {
public:
  constexpr EntityTableView() noexcept = default;
  explicit constexpr EntityTableView(internal::SlotMap &slot_map) noexcept
      : slot_map_(&slot_map) {}

  /// Returns true if the entity handle is alive and matches its current generation.
  [[nodiscard]] bool contains(Entity entity) const noexcept {
    assert(slot_map_ != nullptr);
    return slot_map_->is_alive(entity);
  }

  /// Returns the total number of living entities across all archetypes.
  [[nodiscard]] size_t count() const noexcept {
    assert(slot_map_ != nullptr);
    return slot_map_->size();
  }

  /// Returns the allocated capacity of the internal slot map.
  [[nodiscard]] size_t capacity() const noexcept {
    assert(slot_map_ != nullptr);
    return slot_map_->capacity();
  }

  /// Returns true if there are zero active entities in the simulation.
  [[nodiscard]] bool is_empty() const noexcept {
    assert(slot_map_ != nullptr);
    return slot_map_->size() == 0;
  }

  /// Resolves an entity handle from a slot index, if alive.
  [[nodiscard]] std::optional<Entity> resolve(uint32_t index) const noexcept {
    assert(slot_map_ != nullptr);
    return slot_map_->resolve(index);
  }

private:
  internal::SlotMap *slot_map_{nullptr};
};

using EntityFacade = EntityTableView;

// =============================================================================
// Entity Handle Views (Const & Mutable)
// =============================================================================

/**
 * @class EntityRef
 * @brief Read-only lightweight view bound to a specific Entity handle and SlotMap.
 *
 * Provides safe, const-qualified component inspection and liveness queries without
 * upward dependencies on World.
 */
class EntityRef {
public:
  constexpr EntityRef() noexcept = default;
  constexpr EntityRef(const internal::SlotMap *slot_map, Entity entity) noexcept
      : slot_map_(slot_map), entity_(entity) {}

  /// Returns the underlying 64-bit Entity identifier.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the raw Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity handle is currently alive.
  [[nodiscard]] bool is_alive() const noexcept {
    return slot_map_ != nullptr && slot_map_->is_alive(entity_);
  }

  /// Returns true if the entity possesses component T.
  template <typename T>
  [[nodiscard]] bool has() const noexcept {
    assert(slot_map_ != nullptr && "EntityRef operation called on null SlotMap!");
    const auto *rec = slot_map_->find_record(entity_);
    return rec && rec->archetype && rec->archetype->has_component(component_id<T>());
  }

  /// Fast read-only component retrieval. Never marks chunk dirty.
  template <typename T>
  [[nodiscard]] const T &get() const {
    assert(slot_map_ != nullptr && "EntityRef operation called on null SlotMap!");
    const auto *rec = slot_map_->find_record(entity_);
    assert(rec && rec->archetype && rec->chunk && "Invalid entity record or null chunk!");
    const auto *layout = rec->archetype->column_layout_for(component_id<T>());
    assert(layout != nullptr && "Component not present in archetype!");
    return *(rec->chunk->typed_slice<T>(layout->chunk_byte_offset) + rec->row);
  }

private:
  const internal::SlotMap *slot_map_{nullptr};
  Entity entity_{null_entity};
};

/**
 * @class EntityMut
 * @brief Mutable lightweight view bound to an Entity handle and MutationContext.
 *
 * Enables chained mutation workflows: reading, writing, adding components,
 * removing components, and despawning the entity directly through the handle.
 */
class EntityMut {
public:
  constexpr EntityMut(internal::MutationContext ctx, Entity entity) noexcept
      : ctx_(ctx), entity_(entity) {}

  /// Implicit conversion to a read-only EntityRef view.
  [[nodiscard]] operator EntityRef() const noexcept {
    return EntityRef(&ctx_.slot_map, entity_);
  }

  /// Returns the underlying 64-bit Entity identifier.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the raw Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity handle is alive.
  [[nodiscard]] bool is_alive() const noexcept {
    return ctx_.slot_map.is_alive(entity_);
  }

  /// Checks if the entity possesses component T.
  template <typename T>
  [[nodiscard]] bool has() const noexcept {
    const auto *rec = ctx_.slot_map.find_record(entity_);
    return rec && rec->archetype && rec->archetype->has_component(component_id<T>());
  }

  /// Read-only component inspection.
  template <typename T>
  [[nodiscard]] const T &get() const {
    const auto *rec = ctx_.slot_map.find_record(entity_);
    assert(rec && rec->archetype && rec->chunk && "Invalid entity record or null chunk!");
    const auto *layout = rec->archetype->column_layout_for(component_id<T>());
    assert(layout != nullptr && "Component not present in archetype!");
    return *(rec->chunk->typed_slice<T>(layout->chunk_byte_offset) + rec->row);
  }

  /// Mutable component access. Automatically advances the chunk's dirty tick.
  template <typename T>
  [[nodiscard]] T &get_mut() {
    const auto *rec = ctx_.slot_map.find_record(entity_);
    assert(rec && rec->archetype && rec->chunk && "Invalid entity record or null chunk!");
    const int16_t col = rec->archetype->column_index(component_id<T>());
    assert(col >= 0 && "Component not present in archetype!");
    const auto *layout = rec->archetype->column_layout_for(component_id<T>());
    assert(layout != nullptr);
    rec->chunk->set_column_version(static_cast<uint16_t>(col), ctx_.current_version);
    return *(rec->chunk->typed_slice<T>(layout->chunk_byte_offset) + rec->row);
  }

  /// Dynamically attaches component T to the entity.
  template <typename T>
  void add(T &&component) {
    internal::Mutation::add_component(ctx_, entity_, std::forward<T>(component));
  }

  /// Dynamically removes component T from the entity.
  template <typename T>
  void remove() {
    internal::Mutation::remove_component<T>(ctx_, entity_);
  }

  /// Despawns the entity, recycling its slot and updating the chunk via Swap-and-Pop.
  bool despawn() {
    return internal::Mutation::destroy_entity(ctx_, entity_);
  }

private:
  internal::MutationContext ctx_;
  Entity entity_{null_entity};
};

} // namespace arche
