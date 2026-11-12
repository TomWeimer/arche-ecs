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
)

target_include_directories(arche PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Alias so consumers can link arche::arche
add_library(arche::arche ALIAS arche)

# --- Test Targets -------------------------------------------------------------
enable_testing()

# Module 001 Tests: Entity & Generational SlotMap
add_executable(arche_tests tests/entity_tests.cpp)
target_link_libraries(arche_tests PRIVATE arche::arche)
add_test(NAME entity_tests COMMAND arche_tests)

# Module 002 Tests: Type Registry & Component Mask
add_executable(type_tests tests/type_tests.cpp)
target_link_libraries(type_tests PRIVATE arche::arche)
add_test(NAME type_tests COMMAND type_tests)

# Module 003 Tests: Archetype & Transition Graph
add_executable(archetype_tests tests/archetype_tests.cpp)
target_link_libraries(archetype_tests PRIVATE arche::arche)
add_test(NAME archetype_tests COMMAND archetype_tests)

# Custom Target: run all unit tests
add_custom_target(run_tests
    COMMAND arche_tests
    COMMAND type_tests
    COMMAND archetype_tests
    DEPENDS arche_tests type_tests archetype_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Executing all unit tests..."
)

# --- Clean Rules --------------------------------------------------------------
# 'fclean' removes object files as well as the compiled library and test binaries
add_custom_target(fclean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E rm -f
        $<TARGET_FILE:arche>
        $<TARGET_FILE:arche_tests>
        $<TARGET_FILE:type_tests>
        $<TARGET_FILE:archetype_tests>
    COMMENT "Full clean: removing intermediate objects and binaries..."
)
