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

# --- Test Target --------------------------------------------------------------
add_executable(arche_tests
    tests/entity_tests.cpp
)

target_link_libraries(arche_tests PRIVATE
    arche::arche
)

# Rule to run tests via CTest and via direct target
enable_testing()
add_test(NAME arche_tests COMMAND arche_tests)

add_custom_target(run_tests
    COMMAND arche_tests
    DEPENDS arche_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Executing arche_tests..."
)

# --- Clean Rules --------------------------------------------------------------
# Note: 'clean' is built into CMake (cmake --build build --target clean)
# 'fclean' removes object files as well as the compiled library and test binaries
add_custom_target(fclean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E rm -f
        $<TARGET_FILE:arche>
        $<TARGET_FILE:arche_tests>
    COMMENT "Full clean: removing intermediate objects and binaries..."
)