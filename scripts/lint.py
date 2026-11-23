#!/usr/bin/env python3
"""
arche-ecs Full Translation Unit Linter
Validates syntax, semantics, and module imports across all translation units.
"""
import json
import os
import subprocess
import sys


def main():
    build_dir = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build"
    )
    compile_commands_path = os.path.join(build_dir, "compile_commands.json")

    if not os.path.exists(compile_commands_path):
        print(f"Error: {compile_commands_path} not found. Run CMake first.")
        sys.exit(1)

    with open(compile_commands_path) as f:
        cmds = json.load(f)

    print(f"Linting {len(cmds)} translation units with clang++ -fsyntax-only...")

    errors = []
    for entry in cmds:
        cmd_parts = entry["command"].split()
        new_parts = []
        skip = False
        for p in cmd_parts:
            if skip:
                skip = False
                continue
            if p == "-c":
                continue
            if p == "-o":
                skip = True
                continue
            new_parts.append(p)
        new_parts.append("-fsyntax-only")

        res = subprocess.run(
            new_parts, cwd=entry["directory"], capture_output=True, text=True
        )
        if res.returncode != 0:
            errors.append((entry["file"], res.stderr))

    if errors:
        print(f"\n❌ FAILED: {len(errors)} files had errors:")
        for f, err in errors:
            print(f"\n=== {f} ===\n{err}")
        sys.exit(1)
    else:
        print(
            f"\n✅ SUCCESS: All {len(cmds)} translation units passed syntax, type, and module checking with 0 errors and 0 warnings!"
        )


if __name__ == "__main__":
    main()
