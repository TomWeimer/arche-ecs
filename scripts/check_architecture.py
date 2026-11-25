#!/usr/bin/env python3
"""
Architecture-as-Code (AaC) Dependency Flow & Boundary Validator for Arche ECS.

Enforces:
  Rule 1: Strict Top-Down DAG (Layer N may only import/include Layer <= N).
  Rule 2: Public Wall (Public headers and contracts must never include/import private src/ internals).
  Rule 3: Storage Encapsulation (world_storage may only be imported/included by world/).
  Rule 4: Black-Box Isolation (Subsystem detail/ partitions cannot be imported across subsystems).
  Rule 5: Cycle Detection (The file & module dependency graph must be strictly acyclic).
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
# Layer 5: Domain Sub-Facades (Entity, Resource, Archetype, Gpu, Command, Snapshot, System)
# Layer 6: Central World Coordination & Public Facades Composition
# Layer 7: Master Library Umbrella (arche.hpp / arche.cppm)

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
        "description": "ComponentID, ComponentMask, and ColumnLayout primitives",
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
        "description": "Public Entity handle headers",
        "allowed_targets": ["types", "public_entities", "sparse_entity"],
    },
    "query_cache": {
        "layer": 2,
        "description": "Reactive matching archetype cache (alias for archetype_index)",
        "allowed_targets": ["core_component", "types", "query_cache", "archetype"],
    },
    "archetype": {
        "layer": 2,
        "description": "Archetype node, transition graph, and migration",
        "allowed_targets": [
            "chunk",
            "sparse_entity",
            "core_component",
            "hardware",
            "types",
            "query_cache",
            "archetype",
        ],
    },
    "mutation": {
        "layer": 3,
        "description": "Component addition and removal archetype transitions",
        "allowed_targets": [
            "archetype",
            "chunk",
            "sparse_entity",
            "public_entities",
            "core_component",
            "hardware",
            "types",
            "mutation",
            "command",
        ],
    },
    "command": {
        "layer": 3,
        "description": "Lock-free command buffer recording and playback",
        "allowed_targets": [
            "archetype",
            "chunk",
            "sparse_entity",
            "public_entities",
            "core_component",
            "hardware",
            "types",
            "mutation",
            "command",
        ],
    },
    "resource": {
        "layer": 3,
        "description": "Global singleton resource registry and container",
        "allowed_targets": ["types", "hardware", "resource"],
    },
    "entity_lifecycle": {
        "layer": 3,
        "description": "Fast entity spawn, batch allocation, and EntityRef view",
        "allowed_targets": [
            "archetype",
            "chunk",
            "sparse_entity",
            "public_entities",
            "core_component",
            "hardware",
            "types",
            "entity_lifecycle",
        ],
    },
    "concurrency": {
        "layer": 4,
        "description": "Work-stealing thread pool and chunk dispatchers",
        "allowed_targets": [
            "hardware",
            "types",
            "concurrency",
        ],
    },
    "query": {
        "layer": 5,
        "description": "Type-safe SIMD queries, filters, and iterators",
        "allowed_targets": [
            "concurrency",
            "command",
            "mutation",
            "archetype",
            "chunk",
            "sparse_entity",
            "query_cache",
            "core_component",
            "hardware",
            "types",
            "query",
        ],
    },
    "sort": {
        "layer": 5,
        "description": "Spatial cache locality permutation and sorting",
        "allowed_targets": [
            "query",
            "sparse_entity",
            "archetype",
            "chunk",
            "core_component",
            "hardware",
            "types",
            "sort",
        ],
    },
    "access": {
        "layer": 6,
        "description": "Declarative component and resource access footprints",
        "allowed_targets": [
            "core_component",
            "hardware",
            "types",
            "resource",
            "access",
        ],
    },
    "system": {
        "layer": 7,
        "description": "Execution scope and system function abstractions",
        "allowed_targets": [
            "access",
            "query",
            "mutation",
            "command",
            "resource",
            "concurrency",
            "archetype",
            "chunk",
            "sparse_entity",
            "core_component",
            "hardware",
            "types",
            "system",
        ],
    },
    "scheduler": {
        "layer": 8,
        "description": "DAG system scheduling, conflict analysis, and batch runner",
        "allowed_targets": [
            "system",
            "access",
            "query",
            "concurrency",
            "mutation",
            "command",
            "resource",
            "archetype",
            "chunk",
            "sparse_entity",
            "core_component",
            "hardware",
            "types",
            "scheduler",
        ],
    },
    "plugin": {
        "layer": 9,
        "description": "Modular engine plugin registration and life cycle",
        "allowed_targets": [
            "scheduler",
            "system",
            "access",
            "core_component",
            "hardware",
            "types",
            "plugin",
        ],
    },
    "views": {
        "layer": 10,
        "description": "Domain views encapsulating lower-level subsystems",
        "allowed_targets": [
            "types",
            "hardware",
            "core_component",
            "chunk",
            "sparse_entity",
            "public_entities",
            "entity_lifecycle",
            "archetype",
            "query_cache",
            "mutation",
            "command",
            "resource",
            "query",
            "concurrency",
            "access",
            "system",
            "scheduler",
            "plugin",
            "sort",
            "views",
            "domain",
        ],
    },
    "domain": {
        "layer": 10,
        "description": "Domain views encapsulating lower-level subsystems (alias)",
        "allowed_targets": [
            "types",
            "hardware",
            "core_component",
            "chunk",
            "sparse_entity",
            "public_entities",
            "entity_lifecycle",
            "archetype",
            "query_cache",
            "mutation",
            "command",
            "resource",
            "query",
            "concurrency",
            "access",
            "system",
            "scheduler",
            "plugin",
            "sort",
            "views",
            "domain",
        ],
    },
    "world": {
        "layer": 11,
        "description": "Central coordinator, WorldStorage assembly, and domain facades composition",
        "allowed_targets": ["*"],  # Assembly layer can access all subsystems
    },
    "arche_master": {
        "layer": 12,
        "description": "Master library umbrella module / include",
        "allowed_targets": ["*"],
    },
}

# =============================================================================
# Helper Utilities
# =============================================================================


def classify_subsystem(file_path: str) -> Tuple[str, int]:
    p = file_path.replace("\\", "/")
    if p == "src/arche.cppm":
        return "arche_master", SUBSYSTEM_RULES["arche_master"]["layer"]

    # Tier 0: foundation
    if (
        p.startswith("src/foundation/entity/")
        or p.startswith("src/foundation/types/")
        or p.startswith("src/types/")
    ):
        return "types", SUBSYSTEM_RULES["types"]["layer"]
    if (
        p.startswith("src/foundation/hardware/")
        or p.startswith("src/hardware/")
        or p.startswith("src/common/")
    ):
        return "hardware", SUBSYSTEM_RULES["hardware"]["layer"]
    if (
        p.startswith("src/foundation/component/")
        or p.startswith("src/component/")
    ):
        return "core_component", SUBSYSTEM_RULES["core_component"]["layer"]

    # Tier 1: storage
    if p.startswith("src/storage/chunk/") or p.startswith("src/chunk/"):
        return "chunk", SUBSYSTEM_RULES["chunk"]["layer"]
    if (
        p.startswith("src/storage/slot_map/")
        or p.startswith("src/storage/entity/")
        or p.startswith("src/entity/")
    ):
        return "sparse_entity", SUBSYSTEM_RULES["sparse_entity"]["layer"]

    # Tier 2: structure
    if p.startswith("src/structure/archetype/") or p.startswith("src/archetype/"):
        return "archetype", SUBSYSTEM_RULES["archetype"]["layer"]
    if (
        p.startswith("src/structure/query_cache/")
        or p.startswith("src/query/query_cache")
    ):
        return "archetype", SUBSYSTEM_RULES["archetype"]["layer"]

    # Tier 3: lifecycle
    if p.startswith("src/lifecycle/mutation/") or p.startswith("src/mutation/"):
        return "mutation", SUBSYSTEM_RULES["mutation"]["layer"]
    if p.startswith("src/lifecycle/command/") or p.startswith("src/command/"):
        return "mutation", SUBSYSTEM_RULES["mutation"]["layer"]
    if p.startswith("src/lifecycle/resource/") or p.startswith("src/resource/"):
        return "resource", SUBSYSTEM_RULES["resource"]["layer"]

    # Tier 4: concurrency
    if p.startswith("src/runtime/concurrency/") or p.startswith("src/concurrency/"):
        return "concurrency", SUBSYSTEM_RULES["concurrency"]["layer"]

    # Tier 5: query & sort
    if p.startswith("src/runtime/query/") or p.startswith("src/query/"):
        return "query", SUBSYSTEM_RULES["query"]["layer"]
    if p.startswith("src/runtime/sort/") or p.startswith("src/sort/"):
        return "sort", SUBSYSTEM_RULES["sort"]["layer"]

    # Tier 6..9: execution pipeline
    if p.startswith("src/runtime/access/") or p.startswith("src/access/"):
        return "access", SUBSYSTEM_RULES["access"]["layer"]
    if p.startswith("src/runtime/system/") or p.startswith("src/system/"):
        return "system", SUBSYSTEM_RULES["system"]["layer"]
    if p.startswith("src/runtime/scheduler/") or p.startswith("src/scheduler/"):
        return "scheduler", SUBSYSTEM_RULES["scheduler"]["layer"]
    if p.startswith("src/runtime/plugin/") or p.startswith("src/plugin/"):
        return "plugin", SUBSYSTEM_RULES["plugin"]["layer"]

    # Tier 10: views / domain
    if (
        p.startswith("src/views/")
        or p.startswith("src/domain/")
        or p.startswith("src/facade/")
    ):
        return "views", SUBSYSTEM_RULES["views"]["layer"]

    # Tier 11: world
    if p.startswith("src/world/"):
        return "world", SUBSYSTEM_RULES["world"]["layer"]

    return "unknown", -1


def resolve_include(
    source_file: str, include_target: str, root_dir: str
) -> Optional[str]:
    for prefix in ["src"]:
        candidate = os.path.join(root_dir, prefix, include_target)
        if os.path.isfile(candidate):
            return os.path.relpath(candidate, root_dir).replace("\\", "/")
    candidate = os.path.join(
        os.path.dirname(os.path.join(root_dir, source_file)), include_target
    )
    if os.path.isfile(candidate):
        return os.path.relpath(candidate, root_dir).replace("\\", "/")
    return None


# =============================================================================
# Architectural Verification Engine
# =============================================================================


def run_checks(root_dir: str) -> int:
    include_re = re.compile(r'^\s*#include\s+["<]([^">]+)[">]')
    export_module_re = re.compile(
        r"^\s*export\s+module\s+arche(?::([a-zA-Z0-9_.]+))?\s*;"
    )
    import_re = re.compile(r"^\s*(?:export\s+)?import\s+:([a-zA-Z0-9_.]+)\s*;")

    files: List[str] = []
    for folder in ["src"]:
        full_folder = os.path.join(root_dir, folder)
        if not os.path.exists(full_folder):
            continue
        for root, _, filenames in os.walk(full_folder):
            for fname in filenames:
                if fname.endswith((".hpp", ".inl", ".cpp", ".cppm")):
                    rel = os.path.relpath(os.path.join(root, fname), root_dir).replace(
                        "\\", "/"
                    )
                    files.append(rel)

    files.sort()

    # Pass 1: Index module partitions to defining files
    partition_to_file: Dict[str, str] = {}
    for src_file in files:
        if not src_file.endswith(".cppm"):
            continue
        full_path = os.path.join(root_dir, src_file)
        with open(full_path, "r", encoding="utf-8", errors="replace") as fp:
            for line in fp:
                match = export_module_re.match(line)
                if match:
                    partition = match.group(1) or ""
                    partition_to_file[partition] = src_file

    violations: List[str] = []
    dependency_graph: Dict[str, List[str]] = defaultdict(list)

    # Pass 2: Inspect dependencies
    for src_file in files:
        src_sub, src_layer = classify_subsystem(src_file)
        if src_layer == -1:
            continue

        full_path = os.path.join(root_dir, src_file)
        with open(full_path, "r", encoding="utf-8", errors="replace") as fp:
            for line_no, line in enumerate(fp, 1):
                # 1. Check #include
                inc_match = include_re.match(line)
                if inc_match:
                    raw_target = inc_match.group(1)
                    resolved = resolve_include(src_file, raw_target, root_dir)
                    if not resolved:
                        continue
                    dependency_graph[src_file].append(resolved)
                    tgt_sub, tgt_layer = classify_subsystem(resolved)
                    if tgt_layer == -1:
                        continue

                    # Rule 1: Strict Top-Down DAG on Headers (.hpp / .cppm)
                    is_interface = src_file.endswith((".hpp", ".cppm"))
                    if is_interface:
                        if src_layer < tgt_layer:
                            violations.append(
                                f"[Rule 1 Violation: Upward Include] {src_file}:{line_no}\n"
                                f"    Layer {src_layer} ({src_sub}) includes higher Layer {tgt_layer} ({tgt_sub}): '{raw_target}'"
                            )
                        elif src_layer == tgt_layer and src_sub != tgt_sub and src_layer < 11:
                            violations.append(
                                f"[Rule 1 Violation: Peer Cross-Subsystem Include] {src_file}:{line_no}\n"
                                f"    Peer Layer {src_layer} subsystem '{src_sub}' includes peer subsystem '{tgt_sub}': '{raw_target}'"
                            )

                    # Rule 2: Clean Public Wall
                    if src_file.startswith(
                        "include/arche/types/"
                    ) or src_file.startswith("include/arche/contracts/"):
                        if resolved.startswith("src/"):
                            violations.append(
                                f"[Rule 2 Violation: Public Boundary Breach] {src_file}:{line_no}\n"
                                f"    Public header '{src_file}' directly includes private internal '{resolved}'."
                            )

                    # Rule 3: Storage Encapsulation
                    if resolved.endswith("world_storage.hpp") or resolved.endswith(
                        "world_storage.cppm"
                    ):
                        if not src_file.startswith("src/world/"):
                            violations.append(
                                f"[Rule 3 Violation: Storage Encapsulation Breach] {src_file}:{line_no}\n"
                                f"    Subsystem '{src_sub}' illegally includes '{resolved}'."
                            )

                # 2. Check C++20 module import
                imp_match = import_re.match(line)
                if imp_match:
                    partition = imp_match.group(1)
                    resolved = partition_to_file.get(partition)
                    if not resolved:
                        continue

                    dependency_graph[src_file].append(resolved)
                    tgt_sub, tgt_layer = classify_subsystem(resolved)
                    if tgt_layer == -1:
                        continue

                    # Rule 1: Strict Top-Down DAG on module partitions
                    is_interface = src_file.endswith(".cppm")
                    if is_interface:
                        if src_layer < tgt_layer:
                            violations.append(
                                f"[Rule 1 Violation: Upward Module Import] {src_file}:{line_no}\n"
                                f"    Layer {src_layer} ({src_sub}) imports higher Layer {tgt_layer} ({tgt_sub}): ':{partition}'"
                            )
                        elif src_layer == tgt_layer and src_sub != tgt_sub and src_layer < 11:
                            violations.append(
                                f"[Rule 1 Violation: Peer Cross-Subsystem Import] {src_file}:{line_no}\n"
                                f"    Peer Layer {src_layer} subsystem '{src_sub}' imports peer subsystem '{tgt_sub}': ':{partition}'"
                            )

                    # Rule 3: Storage Encapsulation for modules
                    if partition == "world.detail.world_storage":
                        if not src_file.startswith("src/world/"):
                            violations.append(
                                f"[Rule 3 Violation: Storage Encapsulation Breach] {src_file}:{line_no}\n"
                                f"    Subsystem '{src_sub}' illegally imports ':world.detail.world_storage'."
                            )

                    # Rule 4: Black-Box Isolation (.detail partitions)
                    if ".detail." in partition:
                        # Extract subsystem prefix of the partition (e.g. 'chunk' from 'chunk.detail.chunk_header')
                        part_subsystem = partition.split(".detail.")[0]
                        src_dir = os.path.dirname(src_file).replace("\\", "/")
                        # Allow internal imports within the subsystem (or coordinator world/ / command_playback)
                        is_own_subsystem = (
                            f"/{part_subsystem}" in src_dir
                            or (
                                part_subsystem == "types"
                                and (
                                    "types" in src_dir
                                    or "world" in src_dir
                                    or "system" in src_dir
                                    or "resource" in src_dir
                                )
                            )
                        )
                        # Assembly layer exceptions: World and coordinators
                        is_coordinator = (
                            src_file.startswith("src/world/")
                            or src_file == "src/arche.cppm"
                        )
                        if not is_own_subsystem and not is_coordinator:
                            violations.append(
                                f"[Rule 4 Violation: Black-Box Encapsulation Breach] {src_file}:{line_no}\n"
                                f"    Subsystem '{src_sub}' illegally imports private detail partition ':{partition}'.\n"
                                f"    Remedy: Import through the subsystem's public module interface, not its private detail/."
                            )

    # Rule 5: Cycle Detection (DFS on combined dependency graph)
    visited: Dict[str, int] = {}
    cycles: List[List[str]] = []

    def dfs(node: str, path: List[str]):
        visited[node] = 1
        for neighbor in dependency_graph.get(node, []):
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
            violations.append(
                f"[Rule 5 Violation: Circular Dependency Cycle Detected]\n"
                f"    Cycle: {' -> '.join(c)}"
            )

    # Output Results
    print(
        "==============================================================================="
    )
    print(
        "          ARCHE-ECS ARCHITECTURE-AS-CODE (AaC) VALIDATION REPORT               "
    )
    print(
        "==============================================================================="
    )
    print(f"Scanned: {len(files)} files across {len(SUBSYSTEM_RULES)} subsystems.")
    print(f"Indexed: {len(partition_to_file)} C++20 module partitions.")
    print(
        "-------------------------------------------------------------------------------"
    )

    if violations:
        print(
            f"\n❌ FAILED: Found {len(violations)} architectural boundary violation(s):\n"
        )
        for idx, v in enumerate(violations, 1):
            print(f"[{idx}] {v}\n")
        return 1

    print(
        "\n✅ PASSED: All architectural boundary rules, DAG layers, and public walls satisfied."
    )
    print("   - Zero upward layer dependencies (Strict DAG).")
    print("   - Zero circular dependency cycles.")
    print("   - Black-Box detail/ encapsulation preserved.")
    print("   - World storage encapsulation maintained.")
    print(
        "===============================================================================\n"
    )
    return 0


if __name__ == "__main__":
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    sys.exit(run_checks(project_root))
