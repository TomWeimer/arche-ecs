#pragma once

#include "arche/concurrency/thread_pool.hpp"

namespace arche {

class World;
class CommandBuffer;
class ParallelCommandBuffer;

struct ChunkDispatcher {
  template <typename... Components, typename WorkerFn>
  static void dispatch_parallel_chunks(World &world, ThreadPool &pool, WorkerFn &&worker);

  template <typename... Components, typename Func>
  static void query_parallel(World &world, ThreadPool &pool, Func &&func);

  template <typename... Components, typename Func>
  static void query_parallel(World &world, ThreadPool &pool, ParallelCommandBuffer &cmd_buffers, Func &&func);
};

} // namespace arche
