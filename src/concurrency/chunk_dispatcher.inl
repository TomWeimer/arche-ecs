// #pragma once

// #include "concurrency/chunk_dispatcher.hpp"
// #include "arche/command.hpp"
// #include "arche/query.hpp"
// #include "arche/world.hpp"

// #include <array>
// #include <atomic>
// #include <tuple>
// #include <utility>
// #include <vector>

// namespace arche {

// template <typename... Components, typename WorkerFn>
// inline void ChunkDispatcher::dispatch_parallel_chunks(
//     World &world,
//     ThreadPool &pool,
//     WorkerFn &&worker
// ) {
//   auto q = world.query<Components...>();

//   struct ChunkTask {
//     Chunk *chunk{nullptr};
//     std::array<uint32_t, sizeof...(Components)> offsets;
//   };

//   std::vector<ChunkTask> tasks;
//   q.for_each_active_chunk([&](Chunk *chunk, Archetype *arch) {
//     tasks.push_back(
//         {chunk,
//          {Query<Components...>::template resolve_column_offset<Components>(
//              arch
//          )...}}
//     );
//   });

//   if (tasks.empty()) {
//     return;
//   }

//   const size_t total_tasks = tasks.size();
//   std::atomic<size_t> next_task{0};

//   pool.parallel_for([&](uint32_t thread_id) {
//     while (true) {
//       const size_t idx = next_task.fetch_add(1, std::memory_order_relaxed);
//       if (idx >= total_tasks) {
//         break;
//       }
//       worker(thread_id, tasks[idx].chunk, tasks[idx].offsets);
//     }
//   });
// }

// template <typename... Components, typename Func>
// inline void ChunkDispatcher::query_parallel(World &world, ThreadPool &pool,
// Func &&func) {
//   auto unpack_ptrs =
//       []<size_t... Is>(
//           Chunk *chunk, const auto &offsets, std::index_sequence<Is...>
//       ) {
//         return std::make_tuple(
//             Query<Components...>::template get_column_ptr<Components>(
//                 chunk, offsets[Is]
//             )...
//         );
//       };

//   dispatch_parallel_chunks<Components...>(
//       world, pool, [&](uint32_t, Chunk *chunk, const auto &offsets) {
//         std::apply(
//             [&](auto &&...ptrs) { func(chunk->count(), ptrs...); },
//             unpack_ptrs(
//                 chunk, offsets, std::index_sequence_for<Components...>{}
//             )
//         );
//       }
//   );
// }

// template <typename... Components, typename Func>
// inline void ChunkDispatcher::query_parallel(
//     World &world,
//     ThreadPool &pool,
//     ParallelCommandBuffer &cmd_buffers,
//     Func &&func
// ) {
//   auto unpack_ptrs =
//       []<size_t... Is>(
//           Chunk *chunk, const auto &offsets, std::index_sequence<Is...>
//       ) {
//         return std::make_tuple(
//             Query<Components...>::template get_column_ptr<Components>(
//                 chunk, offsets[Is]
//             )...
//         );
//       };

//   dispatch_parallel_chunks<Components...>(
//       world, pool, [&](uint32_t thread_id, Chunk *chunk, const auto &offsets)
//       {
//         CommandBuffer &thread_cmd = cmd_buffers.get(thread_id);
//         std::apply(
//             [&](auto &&...ptrs) { func(chunk->count(), thread_cmd, ptrs...);
//             }, unpack_ptrs(
//                 chunk, offsets, std::index_sequence_for<Components...>{}
//             )
//         );
//       }
//   );
// }

// // Query delegation methods
// template <typename... Components>
// template <typename Func>
// inline void Query<Components...>::each_parallel(ThreadPool &pool, Func
// &&func) {
//   ChunkDispatcher::query_parallel<Components...>(world_, pool,
//   std::forward<Func>(func));
// }

// template <typename... Components>
// template <typename Func>
// inline void Query<Components...>::each_parallel(
//     ThreadPool &pool,
//     ParallelCommandBuffer &cmd_buffers,
//     Func &&func
// ) {
//   ChunkDispatcher::query_parallel<Components...>(world_, pool, cmd_buffers,
//   std::forward<Func>(func));
// }

// } // namespace arche
