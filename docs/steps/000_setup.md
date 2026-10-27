# Step 0: Engine Skeleton & Linkage Target

## Objective
Establish a clean, standalone C++20 static library target (`arche`) that compiles with symbols, produces no warnings, and exports an empty public header surface.

## Invariants to Protect
- Library target name: `arche` (aliased as `arche::arche`).
- Zero dependencies outside C++20 standard library.
- Compiles cleanly under `-O3 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic`.

## Concrete Deliverables
1. `CMakeLists.txt`: Defines `arche` as a `STATIC` library target and exports `include/` via `$<BUILD_INTERFACE:...>`.
2. `include/arche/arche.hpp`: Minimal header with include guard/`#pragma once` and `namespace arche` containing a placeholder `World` struct.
3. `src/arche.cpp`: Compiles `arche.hpp` to ensure self-contained headers.

## Verification Checklist

- [ ] Directory layout matches:

      arche-ecs/
      ├── CMakeLists.txt
      ├── include/arche/arche.hpp
      └── src/arche.cpp
- [ ] Command exits with 0:
      cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
      cmake --build build --target arche
- [ ] `build/libarche.a` (or platform equivalent) is produced.