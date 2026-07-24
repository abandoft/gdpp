#!/usr/bin/env python3
"""Snapshot and verify export-sensitive Godot project metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


REGISTRY_PATH = Path(".godot/extension_list.cfg")
TRANSACTION_BACKUPS = (
    Path(".godot/gdpp_compiler_descriptor.export-backup"),
    Path(".godot/gdpp_extension_list.export-backup"),
    Path(".godot/gdpp_provider_descriptors.export-backup.json"),
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def is_generated_descriptor(relative: Path) -> bool:
    parts = relative.parts
    return len(parts) >= 4 and parts[:3] == ("addons", "gdpp", "build")


def collect(project: Path) -> dict[str, str]:
    files: set[Path] = set()
    registry = project / REGISTRY_PATH
    if not registry.is_file():
        fail(f"Godot extension registry is missing: {registry}")
    files.add(REGISTRY_PATH)

    for descriptor in project.rglob("*.gdextension"):
        relative = descriptor.relative_to(project)
        if ".godot" in relative.parts or is_generated_descriptor(relative):
            continue
        if descriptor.is_file():
            files.add(relative)

    compiler = Path("addons/gdpp/gdpp.gdextension")
    if compiler not in files:
        fail(f"GDPP compiler descriptor is missing: {project / compiler}")

    return {
        relative.as_posix(): digest(project / relative)
        for relative in sorted(files, key=lambda value: value.as_posix())
    }


def snapshot(project: Path, state: Path) -> None:
    payload = {
        "schema": 1,
        "project": str(project),
        "files": collect(project),
    }
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def verify(project: Path, state: Path) -> None:
    if not state.is_file():
        fail(f"immutability snapshot is missing: {state}")
    payload = json.loads(state.read_text(encoding="utf-8"))
    if payload.get("schema") != 1 or not isinstance(payload.get("files"), dict):
        fail(f"invalid immutability snapshot: {state}")

    expected: dict[str, str] = payload["files"]
    actual = collect(project)
    if expected != actual:
        expected_paths = set(expected)
        actual_paths = set(actual)
        details = [
            *(f"removed descriptor or metadata: {path}" for path in sorted(expected_paths - actual_paths)),
            *(f"added descriptor or metadata: {path}" for path in sorted(actual_paths - expected_paths)),
            *(
                f"modified descriptor or metadata: {path}"
                for path in sorted(expected_paths & actual_paths)
                if expected[path] != actual[path]
            ),
        ]
        fail("export mutated project extension state:\n" + "\n".join(details))

    leftovers = [str(path) for path in TRANSACTION_BACKUPS if (project / path).exists()]
    if leftovers:
        fail("export left transaction backups behind: " + ", ".join(leftovers))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=("snapshot", "verify"))
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    project = arguments.project.resolve()
    state = arguments.state.resolve()
    try:
        if arguments.operation == "snapshot":
            snapshot(project, state)
        else:
            verify(project, state)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"GDPP export immutability check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
