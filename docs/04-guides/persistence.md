# Guide: Persistence & Binary Snapshots

> **Status:** Documentation Skeleton (To be expanded)

## 1. Full-World Binary Serialization
- Blit-level serialization of active chunk memory pages and `SlotMap` generation tables.
- Zero per-entity serialization overhead.

## 2. 64-Byte Snapshot Header
- `SnapshotHeader`: Cache-aligned 64-byte descriptor prefixing every saved stream.
- Validates magic bytes (`'ARCH'`), format version, simulation sequence tick, and active counts.
- Rejects corrupt or incompatible save files before allocating memory.

## 3. Saving State
```cpp
// To memory buffer
size_t required_bytes = world.snapshots.size_bytes();
std::vector<uint8_t> buffer(required_bytes);
world.snapshots.save(buffer.data(), buffer.size());

// Direct to file
world.snapshots.save_to_file("saves/world_checkpoint.bin");
```

## 4. Restoring State
```cpp
// Load from file and instantly restore simulation state
world.snapshots.load_from_file("saves/world_checkpoint.bin");
```
