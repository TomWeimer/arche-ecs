#include "arche/arche.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

// =============================================================================
// Consumer Component Types (Zero Library Context)
// =============================================================================

struct Position {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
  float vz{0.0f};
};

struct BoidState {
  float hunger{0.0f};
  uint32_t cluster_id{0};
};

struct MortonKey {
  uint32_t code{0};
};

// =============================================================================
// 120 Hz Simulation Harness
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "Running arche-ecs Clean API Colony Simulation Loop\n";
  std::cout << "====================================================\n\n";

  arche::World world;
  arche::ThreadPool pool(std::thread::hardware_concurrency());
  arche::ParallelCommandBuffer cmd_buffers(pool.worker_count());

  // 1. Configure Double-Buffered Physics
  world.enable_double_buffer<Position>();

  // 2. High-Throughput Bulk Spawning (100,000 Entities)
  constexpr size_t BOID_COUNT = 100'000;
  std::cout << "Spawning " << BOID_COUNT << " boid entities in bulk...\n";

  world.spawn_batch(
      BOID_COUNT,
      Position{0.0f, 0.0f, 0.0f},
      Velocity{1.0f, 0.5f, 0.0f},
      BoidState{0.0f, 1},
      MortonKey{0}
  );

  std::cout << "Living entities: " << world.entity_count() << "\n\n";

  // Mock VRAM buffer for triple-buffered GPU streaming
  constexpr size_t SLOT_BYTES = BOID_COUNT * sizeof(Position);
  std::vector<uint8_t> mock_vram(SLOT_BYTES * 3, 0);

  // 3. Execute 120 simulation frames @ 120 Hz
  const auto start = std::chrono::high_resolution_clock::now();

  for (uint32_t frame = 0; frame < 120; ++frame) {
    // A. Advance simulation tick
    world.tick();

    // B. Concurrent Physics / Flocking (Write<Position> secondary buffer isolation)
    world.query_parallel<const Position, const Velocity, BoidState, arche::Write<Position>>(
        pool,
        cmd_buffers,
        [](uint32_t count,
           arche::CommandBuffer &,
           const Position *pos,
           const Velocity *vel,
           BoidState *state,
           arche::Write<Position> next_pos) {
          for (uint32_t i = 0; i < count; ++i) {
            next_pos[i].x = pos[i].x + vel[i].vx * 0.00833f;
            next_pos[i].y = pos[i].y + vel[i].vy * 0.00833f;
            next_pos[i].z = pos[i].z + vel[i].vz * 0.00833f;

            state[i].hunger += 0.01f;
          }
        }
    );

    // C. Replay deferred mutations sequentially on main thread
    world.commit(cmd_buffers);

    // D. O(1) Double-buffer flip (swaps slice offsets and auto-stamps versions)
    world.flip<Position>();

    // E. Triple-buffered GPU ring streaming
    const uint32_t ring_slot = frame % 3;
    world.stream_gpu<Position>(ring_slot, mock_vram.data(), SLOT_BYTES);
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start
  ).count();

  std::cout << "Simulated 120 frames in " << (elapsed / 1000.0f) << " ms ("
            << (elapsed / 120.0f) << " us/frame)\n";
  std::cout << "All operations executed successfully with clean API!\n";
  return 0;
}
