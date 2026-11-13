#pragma once

namespace arche::test {

struct Position {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct Velocity {
  float vx{0.0f};
  float vy{0.0f};
  float vz{0.0f};
};

struct TagBurning {
  float intensity{1.0f};
};

struct BoidTag {};

} // namespace arche::test