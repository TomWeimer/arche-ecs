#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <vector>
#include <windows.h>
#endif

namespace arche {

// =============================================================================
// Hardware Topology & Platform-Adaptive Memory Utilities
// =============================================================================

/**
 * @struct Hardware
 * @brief System hardware topology detection and platform-adaptive memory
 * utilities.
 *
 * @details
 * ### Architectural Rationale:
 * In high-throughput archetype ECS architectures, cache line alignment and
 * operating system virtual memory page geometry dictate performance ceilings:
 * - **False Sharing & Split Cache Lines:**
 *   Standard x86-64 hardware uses 64-byte Level 1 cache lines. However, Apple
 * Silicon (M1/M2/M3/M4) performance cores utilize **128-byte** cache lines.
 * Hardcoding 64-byte alignments on ARM64 macOS incurs severe cache thrashing,
 * split cache-line penalties, and false sharing across core boundaries.
 * - **Virtual Memory Page Boundaries & TLB Performance:**
 *   Operating system virtual memory page sizes vary across platforms:
 *   - Linux / Windows x86-64: Typically **4,096 bytes (4 KB)**.
 *   - macOS Apple Silicon (ARM64): Strictly **16,384 bytes (16 KB)**.
 *   - Enterprise AArch64 Linux: Up to **65,536 bytes (64 KB)**.
 * - **Chunk Sizing Guarantee:**
 *   `optimal_chunk_size()` rounds memory allocations up to the nearest integer
 * multiple of the host OS page size (targeting ~64 KB), ensuring that chunk
 * buffers map cleanly to Translation Lookaside Buffer (TLB) entries without
 * fragmentation.
 */
struct Hardware {
  // --- Hardware Topology Queries ---------------------------------------------

  /**
   * @brief Returns the host CPU Level 1 data cache line size in bytes (64 or
   * 128).
   *
   * @details
   * Queried dynamically once at startup and cached in a thread-safe static
   * local.
   * - Apple Silicon (ARM64 Darwin): 128 bytes.
   * - x86-64 (Intel / AMD): 64 bytes.
   *
   * @return L1 data cache line size in bytes.
   */
  [[nodiscard]] inline static size_t cache_line_size() noexcept {
    static const size_t size = detect_cache_line_size();
    return size;
  }

  /**
   * @brief Returns the host operating system virtual memory page size in bytes.
   *
   * @details
   * Queried dynamically once at startup and cached in a thread-safe static
   * local.
   * - macOS ARM64: 16,384 bytes (16 KB).
   * - Linux / Windows x86-64: 4,096 bytes (4 KB).
   *
   * @return OS page size in bytes.
   */
  [[nodiscard]] inline static size_t page_size() noexcept {
    static const size_t size = detect_page_size();
    return size;
  }

  // --- Dynamic Sizing Math ---------------------------------------------------

  /**
   * @brief Computes the optimal chunk byte size as an exact multiple of the OS
   * page size.
   *
   * @details
   * Evaluates the ceiling multiple formula:
   *   `OptimalSize = ⌈ max(target, page) / page ⌉ × page`
   *
   * Guarantees zero allocator fragmentation and single-mapping TLB alignment.
   *
   * @param target_size Baseline desired chunk capacity in bytes (defaults to
   * 65,536 / 64 KB).
   * @return Aligned byte size that is an exact integer multiple of
   * `page_size()`.
   */
  [[nodiscard]] inline static size_t optimal_chunk_size(
      size_t target_size = 65536
  ) noexcept {
    const size_t page = page_size();
    if (target_size <= page) {
      return page;
    }
    return ((target_size + page - 1) / page) * page;
  }

  // --- Aligned Virtual Memory Allocation -------------------------------------

  /**
   * @brief Allocates heap memory strictly aligned to the specified byte
   * boundary.
   *
   * @details
   * Enforces that `alignment` is at least `sizeof(void*)` and delegates to the
   * platform aligned allocation primitive.
   *
   * @param bytes Number of bytes to allocate.
   * @param alignment Alignment boundary in bytes (e.g., 64, 128, 4096).
   * @return Pointer to aligned buffer, or nullptr if allocation failed or bytes
   * == 0.
   */
  [[nodiscard]] inline static void *allocate_aligned(
      size_t bytes,
      size_t alignment
  ) noexcept {
    if (bytes == 0) {
      return nullptr;
    }
    alignment = std::max(alignment, sizeof(void *));
    return os_allocate_aligned(bytes, alignment);
  }

  /**
   * @brief Releases aligned memory previously allocated by
   * `allocate_aligned()`.
   *
   * @param ptr Pointer to aligned memory buffer (safe to pass nullptr).
   */
  inline static void free_aligned(void *ptr) noexcept {
    if (ptr) {
      os_free_aligned(ptr);
    }
  }

private:
  // --- Platform Memory Primitives --------------------------------------------

  /**
   * @brief Platform-specific aligned heap allocation primitive.
   */
  [[nodiscard]] inline static void *os_allocate_aligned(
      size_t bytes,
      size_t alignment
  ) noexcept {
#if defined(_WIN32)
    return _aligned_malloc(bytes, alignment);
#else
    void *ptr = nullptr;
    if (posix_memalign(&ptr, alignment, bytes) != 0) {
      return nullptr;
    }
    return ptr;
#endif
  }

  /**
   * @brief Platform-specific aligned heap deallocation primitive.
   */
  inline static void os_free_aligned(void *ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    ::free(ptr);
#endif
  }

  // --- Platform Cache Line Queries -------------------------------------------

#if defined(__APPLE__)
  /**
   * @brief Queries Darwin kernel sysctl for Level 1 data cache line size.
   */
  [[nodiscard]] inline static size_t query_apple_cache_line_size() noexcept {
    size_t line_size = 0;
    size_t size_len = sizeof(line_size);
    if (sysctlbyname("hw.cachelinesize", &line_size, &size_len, nullptr, 0) ==
            0 &&
        line_size > 0) {
      return line_size;
    }
#if defined(__aarch64__)
    return 128; // Apple Silicon M-series performance cores default to 128 bytes
#else
    return 64; // Intel Mac default
#endif
  }
#elif defined(__linux__)
  /**
   * @brief Queries Linux sysconf for Level 1 data cache line size.
   */
  [[nodiscard]] inline static size_t query_linux_cache_line_size() noexcept {
    const long line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return (line_size > 0) ? static_cast<size_t>(line_size) : 64;
  }
#elif defined(_WIN32)
  /**
   * @brief Queries Windows logical processor information for L1 data cache line
   * size.
   */
  [[nodiscard]] inline static size_t query_windows_cache_line_size() noexcept {
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(nullptr, &buffer_size);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
          buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)
      );
      if (GetLogicalProcessorInformation(buffer.data(), &buffer_size)) {
        for (const auto &info : buffer) {
          if (info.Relationship == RelationCache && info.Cache.Level == 1 &&
              (info.Cache.Type == CacheData ||
               info.Cache.Type == CacheUnified)) {
            return info.Cache.LineSize;
          }
        }
      }
    }
    return 64;
  }
#endif

  /**
   * @brief Dispatches host CPU Level 1 cache line query with fallback.
   */
  [[nodiscard]] inline static size_t detect_cache_line_size() noexcept {
#if defined(__APPLE__)
    return query_apple_cache_line_size();
#elif defined(__linux__)
    return query_linux_cache_line_size();
#elif defined(_WIN32)
    return query_windows_cache_line_size();
#else
    return 64; // Portable default fallback
#endif
  }

  // --- Platform Page Size Queries --------------------------------------------

#if defined(_WIN32)
  /**
   * @brief Queries Windows system information for virtual memory page size.
   */
  [[nodiscard]] inline static size_t query_windows_page_size() noexcept {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return static_cast<size_t>(sys_info.dwPageSize);
  }
#elif defined(__APPLE__) || defined(__linux__)
  /**
   * @brief Queries POSIX sysconf for virtual memory page size.
   */
  [[nodiscard]] inline static size_t query_posix_page_size() noexcept {
    const long page = sysconf(_SC_PAGESIZE);
    return (page > 0) ? static_cast<size_t>(page) : 4096;
  }
#endif

  /**
   * @brief Dispatches host operating system virtual page query with fallback.
   */
  [[nodiscard]] inline static size_t detect_page_size() noexcept {
#if defined(_WIN32)
    return query_windows_page_size();
#elif defined(__APPLE__) || defined(__linux__)
    return query_posix_page_size();
#else
    return 4096; // Portable default fallback
#endif
  }
};

} // namespace arche
