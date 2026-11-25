import arche;

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using arche::internal::Task;
using arche::internal::TaskGroup;
using arche::internal::ThreadPool;
using arche::internal::WorkStealingQueue;
} // namespace

// =============================================================================
// Unit Test 1: Single-Threaded LIFO Execution
// =============================================================================

void test_single_threaded_lifo() {
  std::cout << "Testing Single-Threaded Chase-Lev LIFO Ordering... ";

  WorkStealingQueue queue;
  constexpr int COUNT = 1000;

  struct Context {
    int id;
  };
  std::vector<Context> contexts(COUNT);
  for (int i = 0; i < COUNT; ++i) {
    contexts[i].id = i;
  }

  for (int i = 0; i < COUNT; ++i) {
    bool ok = queue.push(Task{
        .execute = [](void *data, uint32_t) {
          auto *c = static_cast<Context *>(data);
          (void)c;
        },
        .data = &contexts[i]
    });
    assert(ok && "Queue push must succeed within capacity");
  }

  assert(queue.size() == COUNT);
  assert(!queue.empty());

  // Pop in LIFO order (COUNT - 1 down to 0)
  for (int expected = COUNT - 1; expected >= 0; --expected) {
    auto task = queue.pop();
    assert(task.has_value());
    auto *c = static_cast<Context *>(task->data);
    assert(c->id == expected && "Pop must yield tasks in exact LIFO order");
  }

  assert(queue.empty());
  assert(!queue.pop().has_value());

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 2: Multi-Threaded Concurrent Stealing Parity (1 Producer, 4 Stealers)
// =============================================================================

void test_multithreaded_stealing_parity() {
  std::cout << "Testing Concurrent Multi-Stealer Parity (1 Producer, 4 Stealers)... ";

  WorkStealingQueue queue;
  constexpr size_t TOTAL_TASKS = 3000;

  struct Item {
    size_t id;
  };
  std::vector<Item> items(TOTAL_TASKS);
  for (size_t i = 0; i < TOTAL_TASKS; ++i) {
    items[i].id = i;
  }

  // Push all tasks into queue
  for (size_t i = 0; i < TOTAL_TASKS; ++i) {
    bool ok = queue.push(Task{
        .execute = nullptr,
        .data = &items[i]
    });
    assert(ok);
  }

  std::vector<std::atomic<bool>> executed(TOTAL_TASKS);
  for (size_t i = 0; i < TOTAL_TASKS; ++i) {
    executed[i].store(false, std::memory_order_relaxed);
  }

  std::atomic<size_t> completed{0};
  std::atomic<bool> stop_stealers{false};

  // Launch 4 stealer threads
  std::vector<std::thread> stealers;
  constexpr size_t NUM_STEALERS = 4;
  stealers.reserve(NUM_STEALERS);

  for (size_t s = 0; s < NUM_STEALERS; ++s) {
    stealers.emplace_back([&]() {
      while (!stop_stealers.load(std::memory_order_relaxed)) {
        if (auto task = queue.steal()) {
          auto *item = static_cast<Item *>(task->data);
          bool prev = executed[item->id].exchange(true, std::memory_order_relaxed);
          assert(!prev && "Duplicate task execution detected across stealers!");
          completed.fetch_add(1, std::memory_order_relaxed);
        } else {
          std::this_thread::yield();
        }
      }
      // Drain any remaining tasks on stop
      while (auto task = queue.steal()) {
        auto *item = static_cast<Item *>(task->data);
        bool prev = executed[item->id].exchange(true, std::memory_order_relaxed);
        assert(!prev && "Duplicate task execution detected across stealers!");
        completed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Wait until all tasks are stolen and executed
  while (completed.load(std::memory_order_relaxed) < TOTAL_TASKS) {
    std::this_thread::yield();
  }

  stop_stealers.store(true, std::memory_order_relaxed);
  for (auto &t : stealers) {
    t.join();
  }

  assert(completed.load() == TOTAL_TASKS);
  for (size_t i = 0; i < TOTAL_TASKS; ++i) {
    assert(executed[i].load() && "Every task must be executed exactly once!");
  }
  assert(queue.empty());

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 3: ThreadPool Elastic Work-Stealing with Dynamic Balancing
// =============================================================================

void test_thread_pool_work_stealing() {
  std::cout << "Testing ThreadPool Dynamic Work-Stealing... ";

  ThreadPool pool(4);
  constexpr size_t TASK_COUNT = 100;

  struct BatchItem {
    size_t id;
    std::atomic<bool> done{false};
    TaskGroup *group{nullptr};
  };
  std::vector<BatchItem> items(TASK_COUNT);
  TaskGroup group;
  group.pending.store(TASK_COUNT);

  for (size_t i = 0; i < TASK_COUNT; ++i) {
    items[i].id = i;
    items[i].group = &group;
  }

  // Submit all tasks targeting worker 0 (skewed load)
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    pool.schedule(
        Task{
            .execute = [](void *data, uint32_t) {
              auto *item = static_cast<BatchItem *>(data);
              item->done.store(true, std::memory_order_relaxed);
              item->group->pending.fetch_sub(1, std::memory_order_release);
            },
            .data = &items[i]
        },
        0 // All initially targeted to worker 0
    );
  }

  // Master thread waits; workers 1, 2, 3 must steal from worker 0
  pool.wait(group);

  for (size_t i = 0; i < TASK_COUNT; ++i) {
    assert(items[i].done.load());
  }

  std::cout << "PASSED\n";
}

// =============================================================================
// Unit Test 4: Nested parallel_for Execution (Zero Deadlock)
// =============================================================================

void test_nested_parallel_for_no_deadlock() {
  std::cout << "Testing Nested parallel_for (Zero Deadlock Invariant)... ";

  ThreadPool pool(4);
  std::atomic<uint32_t> outer_counter{0};
  std::atomic<uint32_t> inner_counter{0};

  // Outer parallel_for dispatches across workers
  pool.parallel_for([&](uint32_t outer_id) {
    outer_counter.fetch_add(1, std::memory_order_relaxed);

    // Inner parallel_for dispatched FROM WITHIN a worker thread!
    // In Phase 1 barrier models, this would cause an instant deadlock!
    if (outer_id == 0) {
      pool.parallel_for([&](uint32_t) {
        inner_counter.fetch_add(1, std::memory_order_relaxed);
      });
    }
  });

  assert(outer_counter.load() == pool.worker_count());
  assert(inner_counter.load() == pool.worker_count());

  std::cout << "PASSED\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
  std::cout << "====================================================\n";
  std::cout << "   Module 022: Work-Stealing & Chase-Lev Deque Tests \n";
  std::cout << "====================================================\n";

  test_single_threaded_lifo();
  test_multithreaded_stealing_parity();
  test_thread_pool_work_stealing();
  test_nested_parallel_for_no_deadlock();

  std::cout << "\nAll Work-Stealing tests passed successfully!\n";
  return 0;
}
