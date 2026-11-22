# Guide: Persistence & Binary Snapshots

> **Status:** Documentation Skeleton (To be expanded)

## 1. Full-World Binary Serialization
- Direct memory serialization of active chunks and `SlotMap` generation tables.
- Zero per-entity serialization overhead.

## 2. Saving State
```cpp
// To memory buffer
size_t required_bytes = world.snapshots.size_bytes();
std::vector<uint8_t> buffer(required_bytes);
world.snapshots.save(buffer.data(), buffer.size());

// Direct to file
world.snapshots.save_to_file("saves/world_checkpoint.bin");
```

## 3. Restoring State
```cpp
world.snapshots.load_from_file("saves/world_checkpoint.bin");
```
