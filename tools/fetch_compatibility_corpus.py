#!/usr/bin/env python3
"""Fetch a pinned, sparse copy of the official Godot compatibility corpus."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


def run(command: list[str], cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    if manifest.get("schema_version") != 1:
        raise RuntimeError("unsupported compatibility corpus manifest schema")
    return manifest


def ensure_build_destination(destination: Path, build_root: Path) -> None:
    resolved_destination = destination.resolve()
    resolved_build_root = build_root.resolve()
    if resolved_destination == resolved_build_root or resolved_build_root not in resolved_destination.parents:
        raise RuntimeError(f"corpus destination must be below build root: {resolved_build_root}")


def current_commit(destination: Path) -> str | None:
    if not (destination / ".git").is_dir():
        return None
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=destination,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)
    repository = manifest["repository"]
    destination = args.destination.resolve()
    ensure_build_destination(destination, args.build_root)

    if current_commit(destination) == repository["commit"]:
        print(f"official compatibility corpus already present at {destination}")
        return 0

    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)

    run(["git", "init", "--quiet", str(destination)])
    run(["git", "remote", "add", "origin", repository["url"]], destination)
    run(["git", "config", "extensions.partialClone", "origin"], destination)
    run(["git", "config", "remote.origin.promisor", "true"], destination)
    run(["git", "config", "remote.origin.partialCloneFilter", "blob:none"], destination)
    run(["git", "sparse-checkout", "init", "--cone"], destination)
    sparse_paths = [repository["license_file"]]
    sparse_paths.extend(project["path"] for project in manifest["projects"])
    run(["git", "sparse-checkout", "set", "--"] + sparse_paths, destination)
    run(
        [
            "git",
            "fetch",
            "--quiet",
            "--depth=1",
            "--filter=blob:none",
            "origin",
            repository["commit"],
        ],
        destination,
    )
    run(["git", "checkout", "--quiet", "--detach", "FETCH_HEAD"], destination)

    actual_commit = current_commit(destination)
    if actual_commit != repository["commit"]:
        raise RuntimeError(f"corpus commit mismatch: expected {repository['commit']}, got {actual_commit}")
    if not (destination / repository["license_file"]).is_file():
        raise RuntimeError("official corpus license file is missing")
    for project in manifest["projects"]:
        project_root = destination / project["path"]
        if not (project_root / "project.godot").is_file():
            raise RuntimeError(f"Godot project is missing project.godot: {project['path']}")

    print(f"fetched {repository['name']} at {actual_commit} into {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
