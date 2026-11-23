module;

#include <cassert>

export module arche:entity_view;

import :entity;

export namespace arche {

class World;

class EntityRef {
public:
  constexpr EntityRef() noexcept = default;
  constexpr EntityRef(const World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }
  [[nodiscard]] bool is_alive() const noexcept;
  template <typename T> [[nodiscard]] bool has() const noexcept;
  template <typename T> [[nodiscard]] const T &get() const;

private:
  const World *world_{nullptr};
  Entity entity_{null_entity};
};

class EntityMut {
public:
  constexpr EntityMut() noexcept = default;
  constexpr EntityMut(World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  [[nodiscard]] operator EntityRef() const noexcept {
    return EntityRef(world_, entity_);
  }

  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }
  [[nodiscard]] bool is_alive() const noexcept;
  template <typename T> [[nodiscard]] bool has() const noexcept;
  template <typename T> [[nodiscard]] const T &get() const;
  template <typename T> [[nodiscard]] T &get_mut();
  template <typename T> void add(T &&component);
  template <typename T> void remove();
  bool despawn();

private:
  World *world_{nullptr};
  Entity entity_{null_entity};
};

} // namespace arche
