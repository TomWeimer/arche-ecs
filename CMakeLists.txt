cmake_minimum_required(VERSION 3.20)
project(arche-ecs CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -fno-omit-frame-pointer")

add_compile_options(-Wall -Wextra -Wpedantic)

# --- Library Target -----------------------------------------------------------
add_library(arche STATIC
    src/arche.cpp
    src/chunk.cpp
    src/world.cpp
    src/command_buffer.cpp
)

target_include_directories(arche PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Alias so consumers can link arche::arche
add_library(arche::arche ALIAS arche)

# --- Test Targets -------------------------------------------------------------
enable_testing()

# Module 001 Tests: Entity & Generational SlotMap
add_executable(entity_tests tests/entity_tests.cpp)
target_link_libraries(entity_tests PRIVATE arche::arche)
add_test(NAME entity_tests COMMAND entity_tests)

# Module 002 Tests: Type Registry & Component Mask
add_executable(type_tests tests/type_tests.cpp)
target_link_libraries(type_tests PRIVATE arche::arche)
add_test(NAME type_tests COMMAND type_tests)

# Module 003 Tests: Archetype & Transition Graph
add_executable(archetype_tests tests/archetype_tests.cpp)
target_link_libraries(archetype_tests PRIVATE arche::arche)
add_test(NAME archetype_tests COMMAND archetype_tests)

# Module 004 Tests: Hardware & Chunk Storage
add_executable(chunk_storage_tests tests/chunk_storage_tests.cpp)
target_link_libraries(chunk_storage_tests PRIVATE arche::arche)
add_test(NAME chunk_storage_tests COMMAND chunk_storage_tests)

# Module 005 Tests: Structural Mutations & Command Buffer
add_executable(structural_mutation_tests tests/structural_mutation_tests.cpp)
target_link_libraries(structural_mutation_tests PRIVATE arche::arche)
add_test(NAME structural_mutation_tests COMMAND structural_mutation_tests)

# Module 006 Tests: Query System & SIMD Iteration
add_executable(query_tests tests/query_tests.cpp)
target_link_libraries(query_tests PRIVATE arche::arche)
add_test(NAME query_tests COMMAND query_tests)

# Module 007 Tests: GPU std430 Reflection & Buffer Views
add_executable(gpu_reflection_tests tests/gpu_reflection_tests.cpp)
target_link_libraries(gpu_reflection_tests PRIVATE arche::arche)
add_test(NAME gpu_reflection_tests COMMAND gpu_reflection_tests)

# Module 008: All-in-One Invariant Test Suite
add_executable(arche_tests tests/arche_tests.cpp)
target_link_libraries(arche_tests PRIVATE arche::arche)
add_test(NAME arche_tests COMMAND arche_tests)

# Module 008: Cycle-Accurate Hardware Performance Benchmarks
add_executable(arche_benchmarks benchmarks/arche_benchmarks.cpp)
target_link_libraries(arche_benchmarks PRIVATE arche::arche)
add_test(NAME arche_benchmarks COMMAND arche_benchmarks)

# Module 009 Tests: 64-Bit Generational Handles & Instant Snapshots
add_executable(handles_and_snapshots_tests tests/handles_and_snapshots_tests.cpp)
target_link_libraries(handles_and_snapshots_tests PRIVATE arche::arche)
add_test(NAME handles_and_snapshots_tests COMMAND handles_and_snapshots_tests)

# Module 010 Tests: Bulk Entity Lifecycle
add_executable(batch_lifecycle_tests tests/batch_lifecycle_tests.cpp)
target_link_libraries(batch_lifecycle_tests PRIVATE arche::arche)
add_test(NAME batch_lifecycle_tests COMMAND batch_lifecycle_tests)

# Module 011 Tests: SIMD Tail-Padding, Prefetching & Hardware Allocators
add_executable(simd_and_hardware_tests tests/simd_and_hardware_tests.cpp)
target_link_libraries(simd_and_hardware_tests PRIVATE arche::arche)
add_test(NAME simd_and_hardware_tests COMMAND simd_and_hardware_tests)

# Custom Target: run all unit tests
add_custom_target(run_tests
    COMMAND entity_tests
    COMMAND type_tests
    COMMAND archetype_tests
    COMMAND chunk_storage_tests
    COMMAND structural_mutation_tests
    COMMAND query_tests
    COMMAND gpu_reflection_tests
    COMMAND arche_tests
    COMMAND handles_and_snapshots_tests
    COMMAND batch_lifecycle_tests
    COMMAND simd_and_hardware_tests
    DEPENDS entity_tests type_tests archetype_tests chunk_storage_tests structural_mutation_tests query_tests gpu_reflection_tests arche_tests handles_and_snapshots_tests batch_lifecycle_tests simd_and_hardware_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Executing all unit tests..."
)

# --- Clean Rules --------------------------------------------------------------
# 'fclean' removes object files as well as the compiled library and test binaries
add_custom_target(fclean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E rm -f
        $<TARGET_FILE:arche>
        $<TARGET_FILE:entity_tests>
        $<TARGET_FILE:type_tests>
        $<TARGET_FILE:archetype_tests>
        $<TARGET_FILE:chunk_storage_tests>
        $<TARGET_FILE:structural_mutation_tests>
        $<TARGET_FILE:query_tests>
        $<TARGET_FILE:gpu_reflection_tests>
        $<TARGET_FILE:arche_tests>
        $<TARGET_FILE:arche_benchmarks>
        $<TARGET_FILE:handles_and_snapshots_tests>
        $<TARGET_FILE:batch_lifecycle_tests>
        $<TARGET_FILE:simd_and_hardware_tests>
    COMMENT "Full clean: removing intermediate objects and binaries..."
)
