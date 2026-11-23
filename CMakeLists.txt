cmake_minimum_required(VERSION 3.20)
project(arche-ecs CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -fno-omit-frame-pointer")

add_compile_options(-Wall -Wextra -Wpedantic -fdiagnostics-absolute-paths)

# ==============================================================================
# Architecture-as-Code: Modular Staged Header Trees (Physical Boundary Enforcement)
# ==============================================================================
# Stages only permitted subsystem directories into a target's private shadow root.
# Preserves canonical include syntax (#include "<subsystem>/<header>.hpp")
# while physically preventing the compiler from discovering unauthorized folders.
function(stage_target_headers target_name)
    cmake_parse_arguments(STAGE "" "" "INTERNAL;PUBLIC" ${ARGN})
    set(shadow_root "${CMAKE_CURRENT_BINARY_DIR}/staged_headers/${target_name}")

    # 1. Stage internal subsystems from src/<subsystem>
    foreach(sub ${STAGE_INTERNAL})
        set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/${sub}")
        set(dest_dir "${shadow_root}/${sub}")
        if(EXISTS "${src_dir}" AND NOT EXISTS "${dest_dir}")
            file(MAKE_DIRECTORY "${shadow_root}")
            file(CREATE_LINK "${src_dir}" "${dest_dir}" SYMBOLIC COPY_ON_ERROR)
        endif()
    endforeach()

    # 2. Stage public API components from include/arche/<component>
    foreach(pub ${STAGE_PUBLIC})
        set(src_path "${CMAKE_CURRENT_SOURCE_DIR}/include/arche/${pub}")
        set(dest_path "${shadow_root}/arche/${pub}")
        if(EXISTS "${src_path}" AND NOT EXISTS "${dest_path}")
            file(MAKE_DIRECTORY "${shadow_root}/arche")
            file(CREATE_LINK "${src_path}" "${dest_path}" SYMBOLIC COPY_ON_ERROR)
        endif()
    endforeach()

    # Assign shadow root as include directory
    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${target_name} INTERFACE "${shadow_root}")
    else()
        target_include_directories(${target_name} PRIVATE "${shadow_root}")
    endif()
endfunction()

# --- Layer 0: Primitives, Hardware Intrinsics & Core Component Layout ---------
add_library(arche_types INTERFACE)
stage_target_headers(arche_types PUBLIC types)

add_library(arche_hardware INTERFACE)
stage_target_headers(arche_hardware INTERNAL hardware PUBLIC types)
target_link_libraries(arche_hardware INTERFACE arche_types)

add_library(arche_core_component INTERFACE)
stage_target_headers(arche_core_component INTERNAL component hardware PUBLIC types)
target_link_libraries(arche_core_component INTERFACE arche_hardware arche_types)

add_library(arche_core INTERFACE)
target_link_libraries(arche_core INTERFACE arche_core_component arche_hardware arche_types)

# --- Layer 1: Memory Chunks & Generational Sparse Maps ------------------------
add_library(arche_chunk OBJECT src/chunk/chunk.cpp)
stage_target_headers(arche_chunk 
    INTERNAL chunk component hardware 
    PUBLIC types
)
target_link_libraries(arche_chunk PUBLIC arche_core)

add_library(arche_sparse_entity INTERFACE)
stage_target_headers(arche_sparse_entity 
    INTERNAL entity component hardware 
    PUBLIC types
)
target_link_libraries(arche_sparse_entity INTERFACE arche_core)

add_library(arche_public_entities INTERFACE)
stage_target_headers(arche_public_entities 
    PUBLIC types entity.hpp resource.hpp
)
target_link_libraries(arche_public_entities INTERFACE arche_types)

# --- Layer 2: Archetypes, Tables & Transition Graph ---------------------------
add_library(arche_query_cache INTERFACE)
stage_target_headers(arche_query_cache 
    INTERNAL query component 
    PUBLIC types
)
target_link_libraries(arche_query_cache INTERFACE arche_core)

add_library(arche_archetype OBJECT src/archetype/component_migration.cpp)
stage_target_headers(arche_archetype 
    INTERNAL archetype chunk entity component hardware query
    PUBLIC types
)
target_link_libraries(arche_archetype PUBLIC 
    arche_chunk 
    arche_sparse_entity 
    arche_query_cache
)

# --- Layer 3: Lifecycle, Deferred Commands & In-Place Component Access ---------
add_library(arche_entity_lifecycle INTERFACE)
stage_target_headers(arche_entity_lifecycle 
    INTERNAL entity archetype chunk component hardware 
    PUBLIC types
)
target_link_libraries(arche_entity_lifecycle INTERFACE arche_archetype)

add_library(arche_command OBJECT
    src/command/command_buffer.cpp
    src/command/command_playback.cpp
)
stage_target_headers(arche_command 
    INTERNAL command archetype chunk entity component hardware 
    PUBLIC types command.hpp
)
target_link_libraries(arche_command PUBLIC 
    arche_archetype 
    arche_entity_lifecycle
)

add_library(arche_component_access INTERFACE)
stage_target_headers(arche_component_access 
    INTERNAL component archetype chunk 
    PUBLIC types
)
target_link_libraries(arche_component_access INTERFACE arche_archetype)

# --- Layer 4: Execution Engine (Query, DAG Scheduler, Spatial Permutation) ----
add_library(arche_concurrency INTERFACE)
stage_target_headers(arche_concurrency 
    INTERNAL concurrency hardware 
    PUBLIC types
)
target_link_libraries(arche_concurrency INTERFACE arche_core)

add_library(arche_query INTERFACE)
stage_target_headers(arche_query 
    INTERNAL query archetype chunk component hardware concurrency 
    PUBLIC types query.hpp
)
target_link_libraries(arche_query INTERFACE 
    arche_archetype 
    arche_query_cache 
    arche_concurrency
)

add_library(arche_system OBJECT src/system/scheduler.cpp)
stage_target_headers(arche_system 
    INTERNAL system command query concurrency archetype chunk entity component hardware 
    PUBLIC types
)
target_link_libraries(arche_system PUBLIC 
    arche_command 
    arche_query 
    arche_concurrency
)

add_library(arche_spatial OBJECT src/spatial/sort.cpp)
stage_target_headers(arche_spatial 
    INTERNAL spatial query archetype chunk component hardware 
    PUBLIC types
)
target_link_libraries(arche_spatial PUBLIC 
    arche_query 
    arche_archetype
)

# --- Layer 5: World Coordinator & Assembly Facades ----------------------------
add_library(arche_world OBJECT
    src/world/world.cpp
    src/world/world_facades.cpp
)
# Assembly coordinator can access the complete project tree
target_include_directories(arche_world 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(arche_world PUBLIC
    arche_system
    arche_spatial
    arche_command
    arche_query
    arche_component_access
    arche_entity_lifecycle
    arche_archetype
    arche_chunk
    arche_public_entities
)

# --- Layer 6: Master Unified Library Archive & Alias --------------------------
add_library(arche STATIC
    $<TARGET_OBJECTS:arche_chunk>
    $<TARGET_OBJECTS:arche_archetype>
    $<TARGET_OBJECTS:arche_command>
    $<TARGET_OBJECTS:arche_system>
    $<TARGET_OBJECTS:arche_spatial>
    $<TARGET_OBJECTS:arche_world>
)
target_link_libraries(arche PUBLIC arche_world)
target_include_directories(arche PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
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

# Module 012 Tests: Chunk Dirty Ticking & Change Detection
add_executable(dirty_ticks_tests tests/dirty_ticks_tests.cpp)
target_link_libraries(dirty_ticks_tests PRIVATE arche::arche)
add_test(NAME dirty_ticks_tests COMMAND dirty_ticks_tests)

# Module 013 Tests: Reactive Flat Query Caching
add_executable(query_caching_tests tests/query_caching_tests.cpp)
target_link_libraries(query_caching_tests PRIVATE arche::arche)
add_test(NAME query_caching_tests COMMAND query_caching_tests)

# Module 014 Tests: In-Place Archetype Permutation & Cache Locality
add_executable(archetype_permutation_tests tests/archetype_permutation_tests.cpp)
target_link_libraries(archetype_permutation_tests PRIVATE arche::arche)
add_test(NAME archetype_permutation_tests COMMAND archetype_permutation_tests)

# Module 015 Tests: Double-Buffered Physics Component Slices
add_executable(double_buffered_tests tests/double_buffered_tests.cpp)
target_link_libraries(double_buffered_tests PRIVATE arche::arche)
add_test(NAME double_buffered_tests COMMAND double_buffered_tests)

# Module 016 Tests: Chunk Multithreading & Ring Buffers
add_executable(multithreaded_query_tests tests/multithreaded_query_tests.cpp)
target_link_libraries(multithreaded_query_tests PRIVATE arche::arche)
add_test(NAME multithreaded_query_tests COMMAND multithreaded_query_tests)

# Module 017 Tests: 1M+ Scale Validation & Profiling Harness
add_executable(stage2_validation_tests tests/stage2_validation_tests.cpp)
target_link_libraries(stage2_validation_tests PRIVATE arche::arche)
add_test(NAME stage2_validation_tests COMMAND stage2_validation_tests)

# Module 018 Tests: Clean Public API & Facade
add_executable(public_api_tests tests/public_api_tests.cpp)
target_link_libraries(public_api_tests PRIVATE arche::arche)
add_test(NAME public_api_tests COMMAND public_api_tests)

# Module 019 Tests: ResourceContainer & Type-Safe World Singletons
add_executable(resource_tests tests/resource_tests.cpp)
target_link_libraries(resource_tests PRIVATE arche::arche)
add_test(NAME resource_tests COMMAND resource_tests)

# Module 020 Tests: Declarative Access Descriptors & Conflict Detection
add_executable(access_tests tests/access_tests.cpp)
target_link_libraries(access_tests PRIVATE arche::arche)
add_test(NAME access_tests COMMAND access_tests)

# Module 021 Tests: Scheduler DAG & Parallel Execution
add_executable(scheduler_tests tests/scheduler_tests.cpp)
target_link_libraries(scheduler_tests PRIVATE arche::arche)
add_test(NAME scheduler_tests COMMAND scheduler_tests)

# Module 022 Tests: Plugin Concept & World Integration
add_executable(plugin_tests tests/plugin_tests.cpp)
target_link_libraries(plugin_tests PRIVATE arche::arche)
add_test(NAME plugin_tests COMMAND plugin_tests)

# Module 023 Tests: Modular World Sub-Facades (Snapshots, Resources, Archetypes, GPU)
add_executable(world_facade_tests tests/world_facade_tests.cpp)
target_link_libraries(world_facade_tests PRIVATE arche::arche)
add_test(NAME world_facade_tests COMMAND world_facade_tests)

# Architecture-as-Code (AaC) Boundary & Dependency Flow Verification
find_package(Python3 REQUIRED)
add_test(
    NAME architecture_verification
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_architecture.py
)

# Module 018 Example: Colony 120 Hz Swarm Simulation Loop
add_executable(colony_simulation_loop examples/colony_simulation_loop.cpp)
target_link_libraries(colony_simulation_loop PRIVATE arche::arche)

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
    COMMAND dirty_ticks_tests
    COMMAND query_caching_tests
    COMMAND archetype_permutation_tests
    COMMAND double_buffered_tests
    COMMAND multithreaded_query_tests
    COMMAND stage2_validation_tests
    COMMAND public_api_tests
    COMMAND resource_tests
    COMMAND access_tests
    COMMAND scheduler_tests
    COMMAND plugin_tests
    COMMAND world_facade_tests
    DEPENDS entity_tests type_tests archetype_tests chunk_storage_tests structural_mutation_tests query_tests gpu_reflection_tests arche_tests handles_and_snapshots_tests batch_lifecycle_tests simd_and_hardware_tests dirty_ticks_tests query_caching_tests archetype_permutation_tests double_buffered_tests multithreaded_query_tests stage2_validation_tests public_api_tests resource_tests access_tests scheduler_tests plugin_tests world_facade_tests
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
        $<TARGET_FILE:dirty_ticks_tests>
        $<TARGET_FILE:query_caching_tests>
        $<TARGET_FILE:archetype_permutation_tests>
        $<TARGET_FILE:double_buffered_tests>
        $<TARGET_FILE:multithreaded_query_tests>
        $<TARGET_FILE:stage2_validation_tests>
        $<TARGET_FILE:public_api_tests>
        $<TARGET_FILE:resource_tests>
        $<TARGET_FILE:access_tests>
        $<TARGET_FILE:scheduler_tests>
        $<TARGET_FILE:plugin_tests>
        $<TARGET_FILE:world_facade_tests>
        $<TARGET_FILE:colony_simulation_loop>
    COMMENT "Full clean: removing intermediate objects and binaries..."
)

# ==============================================================================
# Optional External Comparison Benchmarks (OFF by default)
# ==============================================================================
option(ARCHE_BUILD_COMPARISON_BENCHMARKS "Build comparative ECS benchmarks against other libraries" OFF)

if(ARCHE_BUILD_COMPARISON_BENCHMARKS)
    message(STATUS "Configuring external comparative benchmarks...")
    include(FetchContent)

    # 1. Fetch lightweight nanobench
    FetchContent_Declare(
        nanobench
        GIT_REPOSITORY https://github.com/martinus/nanobench.git
        GIT_TAG        v4.3.11
    )
    FetchContent_MakeAvailable(nanobench)

    # 2. Fetch EnTT for side-by-side comparison
    FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG        v3.13.2
    )
    FetchContent_MakeAvailable(entt)

    # 3. Fetch Flecs for side-by-side comparison
    set(FLECS_STATIC ON CACHE BOOL "" FORCE)
    set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
    set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
    set(FLECS_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        flecs
        GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
        GIT_TAG        v4.0.4
    )
    FetchContent_MakeAvailable(flecs)

    add_executable(comparative_ecs_benchmarks benchmarks/comparative_benchmarks.cpp)
    target_compile_options(comparative_ecs_benchmarks PRIVATE -Wno-deprecated-literal-operator)
    target_link_libraries(comparative_ecs_benchmarks PRIVATE arche::arche nanobench EnTT::EnTT flecs_static)
endif()

