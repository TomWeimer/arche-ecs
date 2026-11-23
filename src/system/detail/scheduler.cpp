module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

module arche;

namespace arche {

namespace {

inline void merge_access(
    AccessDescriptor &dst,
    const AccessDescriptor &src
) noexcept {
  dst.read_components = dst.read_components | src.read_components;
  dst.write_components = dst.write_components | src.write_components;
  dst.read_resources = dst.read_resources | src.read_resources;
  dst.write_resources = dst.write_resources | src.write_resources;
}

} // namespace

// =============================================================================
// Scheduler: DAG Partitioning Pipeline
// =============================================================================

void Scheduler::partition_batches() {
  const size_t n = systems_.size();
  std::vector<bool> scheduled(n, false);
  size_t scheduled_count = 0;

  while (scheduled_count < n) {
    Batch current_batch;
    AccessDescriptor batch_access{};

    for (size_t i = 0; i < n; ++i) {
      if (scheduled[i]) {
        continue;
      }

      // Wide systems get dedicated exclusive-pool batches
      if (systems_[i].scale == SystemScale::Wide) {
        if (current_batch.system_indices.empty()) {
          current_batch.system_indices.push_back(i);
          current_batch.is_wide = true;
          current_batch.access = systems_[i].access;
          scheduled[i] = true;
          scheduled_count++;
        }
        break;
      }

      // Check conflict against everything already admitted into current_batch
      if (!batch_access.conflicts_with(systems_[i].access)) {
        current_batch.system_indices.push_back(i);
        merge_access(batch_access, systems_[i].access);

        scheduled[i] = true;
        scheduled_count++;
      }
    }

    if (!current_batch.system_indices.empty()) {
      if (!current_batch.is_wide) {
        current_batch.access = batch_access;
      }
      batches_.push_back(std::move(current_batch));
    }
  }
}

} // namespace arche
