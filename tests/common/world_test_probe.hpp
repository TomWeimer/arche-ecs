#pragma once

#include <cstdint>
#include <span>

import arche;

namespace arche::test {

/**
 * @struct WorldTestProbe
 * @brief White-box testing probe granting unit tests access to internal world state.
 *
 * Dedicated strictly to internal unit tests. Never compiled into public library artifacts.
 */
struct WorldTestProbe {
  static void set_version(World &w, uint32_t v) noexcept {
    internal::WorldAccess::set_version(w, v);
  }

  static void commit(World &w, CommandBuffer &cmd) {
    internal::WorldAccess::commit(w, cmd);
  }

  static void commit_commands(World &w) {
    internal::WorldAccess::commit_commands(w);
  }

  static void permute(
      World &w,
      internal::Archetype *arch,
      std::span<const uint32_t> indices
  ) {
    arche::permute(w, arch, indices);
  }

  static internal::SlotMap &slot_map(World &w) noexcept {
    return internal::WorldAccess::slot_map(w);
  }

  static const internal::SlotMap &slot_map(const World &w) noexcept {
    return internal::WorldAccess::slot_map(w);
  }

  static internal::ArchetypeGraph &graph(World &w) noexcept {
    return internal::WorldAccess::graph(w);
  }

  static const internal::ArchetypeGraph &graph(const World &w) noexcept {
    return internal::WorldAccess::graph(w);
  }

  static internal::SystemExecutionTarget execution_target(World &w) noexcept {
    return internal::WorldAccess::system_execution_target(w);
  }

  static SystemContext create_system_context(
      World &w,
      CommandBuffer &cmd,
      internal::ParallelCommandBuffer *pcmd = nullptr
  ) {
    return internal::WorldAccess::create_system_context(w, cmd, pcmd);
  }
};

} // namespace arche::test
