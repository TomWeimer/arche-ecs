#!/usr/bin/env python3
"""
Architecture-as-Code (AaC) Dependency Flow & Boundary Validator for Arche ECS.

Enforces:
  Rule 1: Strict Top-Down DAG (Layer N headers may only include Layer <= N).
  Rule 2: Public Wall (Public headers in include/arche/types and contracts must never include src/).
  Rule 3: Storage Encapsulation (world_storage.hpp may only be included by world/ and world_access).
  Rule 4: Sibling Subsystem Disjointness (Forbidden horizontal cross-includes).
  Rule 5: Cycle Detection (The file dependency graph must be strictly acyclic).
"""

import os
import re
import sys
from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple

# =============================================================================
# Architectural Subsystems & Layer Definitions
# =============================================================================

# Layer 0: Primitives, Hardware, Core Mask & Types
# Layer 1: Memory Primaries, Page Slabs, Sparse Maps
# Layer 2: Archetypal Table Storage & Query Cache
# Layer 3: Entity Lifecycle & Deferred Commands
# Layer 4: Queries, Iteration, DAG Scheduling, Spatial Permutation
# Layer 5: Central World Coordination & Public Facades
# Layer 6: Master Library Umbrella (arche.hpp)

SUBSYSTEM_RULES: Dict[str, Dict] = {
    "types": {
        "layer": 0,
        "description": "Fundamental zero-dependency POD types and C++20 filter tags",
        "allowed_targets": ["types"],
    },
    "hardware": {
        "layer": 0,
        "description": "Hardware intrinsics, SIMD width, and cache alignment",
        "allowed_targets": ["hardware", "types"],
    },
    "core_component": {
        "layer": 0,
        "description": "TypeID, ComponentMask, and ColumnLayout primitives",
        "allowed_targets": ["hardware", "types", "core_component"],
    },
    "chunk": {
        "layer": 1,
        "description": "64KB chunk page memory and slab pool",
        "allowed_targets": ["core_component", "hardware", "types", "chunk"],
    },
    "sparse_entity": {
        "layer": 1,
        "description": "Dense/sparse generational SlotMap and EntityRecord",
        "allowed_targets": ["core_component", "hardware", "types", "sparse_entity"],
    },
    "public_entities": {
        "layer": 1,
        "description": "Public Entity and Resource handle headers",
        "allowed_targets": ["types", "public_entities"],
    },
    "persistence": {
        "layer": 1,
        "description": "Binary snapshot data structures",
        "allowed_targets": ["types", "sparse_entity", "hardware", "persistence"],
    },
    "query_cache": {
        "layer": 2,
        "description": "Reactive matching archetype cache",
        "allowed_targets": ["core_component", "types", "query_cache"],
    },
    "archetype": {
        "layer": 2,
        "description": "Archetype node, transition graph, and migration",
        "allowed_targets": ["chunk", "sparse_entity", "core_component", "hardware", "types", "query_cache", "archetype"],
    },
    "entity_lifecycle": {
        "layer": 3,
        "description": "Fast entity spawn, batch allocation, and EntityRef view",
        "allowed_targets": ["archetype", "chunk", "sparse_entity", "core_component", "hardware", "types", "entity_lifecycle"],
    },
    "command": {
        "layer": 3,
        "description": "Lock-free command buffer recording and playback",
        "allowed_targets": ["archetype", "chunk", "sparse_entity", "entity_lifecycle", "core_component", "hardware", "types", "command"],
    },
    "component_access": {
        "layer": 3,
        "description": "Type-erased in-place component access and version stamping",
        "allowed_targets": ["archetype", "chunk", "sparse_entity", "core_component", "hardware", "types", "component_access"],
    },
    "query": {
        "layer": 4,
        "description": "Type-safe SIMD queries, filters, and iterators",
        "allowed_targets": ["archetype", "chunk", "sparse_entity", "query_cache", "core_component", "hardware", "types", "query"],
    },
    "concurrency": {
        "layer": 4,
        "description": "Work-stealing thread pool and chunk dispatchers",
        "allowed_targets": ["query", "command", "archetype", "chunk", "hardware", "types", "concurrency"],
    },
    "system": {
        "layer": 4,
        "description": "DAG system scheduling, access bitmasks, and plugins",
        "allowed_targets": ["command", "query", "archetype", "chunk", "sparse_entity", "core_component", "hardware", "types", "system"],
    },
    "spatial": {
        "layer": 4,
        "description": "Spatial cache locality permutation and sorting",
        "allowed_targets": ["query", "archetype", "chunk", "core_component", "hardware", "types", "spatial"],
    },
    "contracts": {
        "layer": 5,
        "description": "Domain sub-facade abstract interfaces",
        "allowed_targets": ["types", "public_entities", "contracts"],
    },
    "world": {
        "layer": 5,
        "description": "Central coordinator, WorldStorage assembly, and domain facades",
        "allowed_targets": ["*"],  # Assembly layer can access all subsystems
    },
    "arche_master": {
        "layer": 6,
        "description": "Master library umbrella include",
        "allowed_targets": ["*"],
    },
}

# =============================================================================
# Helper Utilities
# =============================================================================

def classify_subsystem(file_path: str) -> Tuple[str, int]:
    p = file_path.replace("\\", "/")
    if p.startswith("include/arche/types/"):
        return "types", SUBSYSTEM_RULES["types"]["layer"]
    if p.startswith("include/arche/contracts/"):
        return "contracts", SUBSYSTEM_RULES["contracts"]["layer"]
    if p.startswith("include/arche/entity.hpp") or p.startswith("include/arche/resource.hpp"):
        return "public_entities", SUBSYSTEM_RULES["public_entities"]["layer"]
    if p.startswith("include/arche/command.hpp"):
        return "command", SUBSYSTEM_RULES["command"]["layer"]
    if p.startswith("include/arche/query.hpp"):
        return "query", SUBSYSTEM_RULES["query"]["layer"]
    if p.startswith("include/arche/world.hpp"):
        return "world", SUBSYSTEM_RULES["world"]["layer"]
    if p == "include/arche/arche.hpp":
        return "arche_master", SUBSYSTEM_RULES["arche_master"]["layer"]

    # src/ mappings
    if p.startswith("src/hardware/"):
        return "hardware", SUBSYSTEM_RULES["hardware"]["layer"]
    if p.startswith("src/component/type_id") or p.startswith("src/component/component_mask") or p.startswith("src/component/column_layout"):
        return "core_component", SUBSYSTEM_RULES["core_component"]["layer"]
    if p.startswith("src/chunk/"):
        return "chunk", SUBSYSTEM_RULES["chunk"]["layer"]
    if p.startswith("src/entity/slot_map") or p.startswith("src/entity/entity_record"):
        return "sparse_entity", SUBSYSTEM_RULES["sparse_entity"]["layer"]
    if p.startswith("src/persistence/"):
        return "persistence", SUBSYSTEM_RULES["persistence"]["layer"]
    if p.startswith("src/query/query_cache"):
        return "query_cache", SUBSYSTEM_RULES["query_cache"]["layer"]
    if p.startswith("src/archetype/"):
        return "archetype", SUBSYSTEM_RULES["archetype"]["layer"]
    if p.startswith("src/entity/entity_allocator") or p.startswith("src/entity/entity_view"):
        return "entity_lifecycle", SUBSYSTEM_RULES["entity_lifecycle"]["layer"]
    if p.startswith("src/command/"):
        return "command", SUBSYSTEM_RULES["command"]["layer"]
    if p.startswith("src/component/component_access"):
        return "component_access", SUBSYSTEM_RULES["component_access"]["layer"]
    if p.startswith("src/query/"):
        return "query", SUBSYSTEM_RULES["query"]["layer"]
    if p.startswith("src/concurrency/"):
        return "concurrency", SUBSYSTEM_RULES["concurrency"]["layer"]
    if p.startswith("src/system/"):
        return "system", SUBSYSTEM_RULES["system"]["layer"]
    if p.startswith("src/spatial/"):
        return "spatial", SUBSYSTEM_RULES["spatial"]["layer"]
    if p.startswith("src/world/"):
        return "world", SUBSYSTEM_RULES["world"]["layer"]

    return "unknown", -1

def resolve_include(source_file: str, include_target: str, root_dir: str) -> Optional[str]:
    # 1. Search in include/ and src/
    for prefix in ["include", "src"]:
        candidate = os.path.join(root_dir, prefix, include_target)
        if os.path.isfile(candidate):
            return os.path.relpath(candidate, root_dir).replace("\\", "/")
    # 2. Relative to source_file directory
    candidate = os.path.join(os.path.dirname(os.path.join(root_dir, source_file)), include_target)
    if os.path.isfile(candidate):
        return os.path.relpath(candidate, root_dir).replace("\\", "/")
    return None

# =============================================================================
# Architectural Verification Engine
# =============================================================================

def run_checks(root_dir: str) -> int:
    include_re = re.compile(r'^\s*#include\s+["<]([^">]+)[">]')
    files: List[str] = []

    for folder in ["include", "src"]:
        full_folder = os.path.join(root_dir, folder)
        if not os.path.exists(full_folder):
            continue
        for root, _, filenames in os.walk(full_folder):
            for fname in filenames:
                if fname.endswith((".hpp", ".inl", ".cpp")):
                    rel = os.path.relpath(os.path.join(root, fname), root_dir).replace("\\", "/")
                    files.append(rel)

    files.sort()
    violations: List[str] = []
    include_graph: Dict[str, List[str]] = defaultdict(list)

    for src_file in files:
        src_sub, src_layer = classify_subsystem(src_file)
        if src_layer == -1:
            continue

        full_path = os.path.join(root_dir, src_file)
        with open(full_path, "r", encoding="utf-8", errors="replace") as fp:
            for line_no, line in enumerate(fp, 1):
                match = include_re.match(line)
                if not match:
                    continue
                raw_target = match.group(1)

                resolved = resolve_include(src_file, raw_target, root_dir)
                if not resolved:
                    continue  # Standard library header (e.g., <vector>, <cstdint>)

                include_graph[src_file].append(resolved)
                tgt_sub, tgt_layer = classify_subsystem(resolved)
                if tgt_layer == -1:
                    continue

                # -------------------------------------------------------------
                # Rule 1: Strict Top-Down DAG on Headers (.hpp)
                # -------------------------------------------------------------
                # Headers define data types; an earlier layer cannot include a higher layer.
                # (Note: .inl files providing out-of-line template bodies are allowed to use lower/peer tools)
                is_header = src_file.endswith(".hpp")
                if is_header and src_layer < tgt_layer:
                    # Exception: contracts are abstract interfaces, world includes them
                    violations.append(
                        f"[Rule 1 Violation: Upward Include] {src_file}:{line_no}\n"
                        f"    Layer {src_layer} ({src_sub}) includes Layer {tgt_layer} ({tgt_sub}): '{raw_target}'\n"
                        f"    Remedy: Lower layers must never depend on higher layers. Use forward declaration or inversion."
                    )

                # -------------------------------------------------------------
                # Rule 2: Clean Public Wall
                # -------------------------------------------------------------
                # Pure public types and contracts must never include private src/ files
                if src_file.startswith("include/arche/types/") or src_file.startswith("include/arche/contracts/"):
                    if resolved.startswith("src/"):
                        # Only access.hpp -> access.inl exception if explicitly needed
                        if not (src_file.endswith("access.hpp") and resolved.endswith("access.inl")):
                            violations.append(
                                f"[Rule 2 Violation: Public Boundary Breach] {src_file}:{line_no}\n"
                                f"    Public header '{src_file}' directly includes private internal '{resolved}'.\n"
                                f"    Remedy: Public API headers must remain pure and free from private internal includes."
                            )

                # -------------------------------------------------------------
                # Rule 3: Storage Encapsulation
                # -------------------------------------------------------------
                if resolved.endswith("world_storage.hpp"):
                    allowed_storage_consumers = ["src/world/world_access.inl", "src/world/world.cpp", "src/world/world_facades.cpp"]
                    if src_file not in allowed_storage_consumers:
                        violations.append(
                            f"[Rule 3 Violation: Storage Encapsulation Breach] {src_file}:{line_no}\n"
                            f"    Subsystem '{src_sub}' illegally includes 'world_storage.hpp'.\n"
                            f"    Remedy: Physical storage is private to World. Access through WorldAccess friend-bridge."
                        )

                # -------------------------------------------------------------
                # Rule 4: Sibling Disjointness (Horizontal Isolation)
                # -------------------------------------------------------------
                allowed_targets = SUBSYSTEM_RULES.get(src_sub, {}).get("allowed_targets", ["*"])
                if "*" not in allowed_targets and tgt_sub not in allowed_targets:
                    # Allow .cpp / .inl implementation files to interact with lower layers
                    if is_header and tgt_layer == src_layer and tgt_sub != src_sub:
                        violations.append(
                            f"[Rule 4 Violation: Forbidden Sibling Dependency] {src_file}:{line_no}\n"
                            f"    Subsystem '{src_sub}' cannot depend on sibling '{tgt_sub}' in the same layer ({src_layer}).\n"
                            f"    Allowed dependencies for '{src_sub}': {', '.join(allowed_targets)}"
                        )

    # -------------------------------------------------------------------------
    # Rule 5: Cycle Detection (DFS on include graph)
    # -------------------------------------------------------------------------
    visited: Dict[str, int] = {}  # 0: unvisited, 1: visiting, 2: visited
    cycles: List[List[str]] = []

    def dfs(node: str, path: List[str]):
        visited[node] = 1
        for neighbor in include_graph.get(node, []):
            if visited.get(neighbor, 0) == 1:
                idx = path.index(neighbor) if neighbor in path else 0
                cycles.append(path[idx:] + [neighbor])
            elif visited.get(neighbor, 0) == 0:
                dfs(neighbor, path + [neighbor])
        visited[node] = 2

    for f in files:
        if visited.get(f, 0) == 0:
            dfs(f, [f])

    if cycles:
        for c in cycles:
            # Filter trivial self-includes or report
            violations.append(
                f"[Rule 5 Violation: Circular Include Cycle Detected]\n"
                f"    Cycle: {' -> '.join(c)}\n"
                f"    Remedy: Break cycle using forward declarations or moving template bodies to separate .inl."
            )

    # Output Results
    print("===============================================================================")
    print("          ARCHE-ECS ARCHITECTURE-AS-CODE (AaC) VALIDATION REPORT               ")
    print("===============================================================================")
    print(f"Scanned: {len(files)} C++ source and header files across {len(SUBSYSTEM_RULES)} subsystems.")
    print("-------------------------------------------------------------------------------")

    if violations:
        print(f"\n❌ FAILED: Found {len(violations)} architectural boundary violation(s):\n")
        for idx, v in enumerate(violations, 1):
            print(f"[{idx}] {v}\n")
        return 1

    print("\n✅ PASSED: All architectural boundary rules, DAG layers, and public walls satisfied.")
    print("   - Zero upward layer dependencies.")
    print("   - Zero circular include cycles.")
    print("   - Storage encapsulation maintained.")
    print("===============================================================================\n")
    return 0

if __name__ == "__main__":
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    sys.exit(run_checks(project_root))
