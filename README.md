# Arche ECS

A standalone, high-performance C++20 archetype-based Entity Component System (ECS) library.

## Design Principles & Invariants

- **Flat Data over Abstraction:** No virtual methods, no abstract classes, and no polymorphic interfaces. Components are Plain Old Data (POD) structs arranged linearly in memory for optimal cache efficiency.
- **Zero External Dependencies:** Built solely on standard C++20 and CPU intrinsics.
- **Zero Simulation Logic:** Pure ECS primitive engine without domain-specific game or simulation code.

## Requirements

- **C++ Compiler:** Modern C++20 compiler (`Clang 13+`, `GCC 11+`, `AppleClang 13+`, or `MSVC 2019+`)
- **Build System:** `CMake 3.20+`

## Project Layout

```text
arche-ecs/
├── CMakeLists.txt          # CMake configuration and targets
├── include/
│   └── arche/
│       └── arche.hpp       # Public library API (Entity, World)
├── src/
│   └── arche.cpp           # Library implementation
└── tests/
    └── test_main.cpp       # Unit test runner
```

## Quick Start

### 1. Configure the project
```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

### 2. Build the static library
```bash
cmake --build build --target arche
```
Produces `build/libarche.a`.

### 3. Run unit tests
```bash
cmake --build build --target run_tests
```
Alternatively, with `ctest`:
```bash
ctest --test-dir build --output-on-failure
```

### 4. Cleaning
```bash
# Clean intermediate object files
cmake --build build --target clean

# Full clean: remove object files, static library, and test executables
cmake --build build --target fclean
```

*(If you prefer `make`, you can also run `make -C build <target>` e.g. `make -C build arche`)*.

## Integration

To integrate `arche-ecs` into your CMake project:

### Using `FetchContent`
```cmake
include(FetchContent)

FetchContent_Declare(
    arche
    GIT_REPOSITORY https://github.com/<your-username>/arche-ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(arche)

target_link_libraries(your_target PRIVATE arche::arche)
```

### Using `add_subdirectory`
```cmake
add_subdirectory(path/to/arche-ecs)
target_link_libraries(your_target PRIVATE arche::arche)
```

## License

This project is licensed under the [MIT License](LICENSE).
