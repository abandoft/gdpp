#!/usr/bin/env python3
"""Compile a pinned official Godot corpus and enforce compatibility safety gates."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def invoke(command: list[str], timeout: float) -> dict:
    started = time.monotonic()
    try:
        result = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        return {
            "exit_code": result.returncode,
            "elapsed_seconds": round(time.monotonic() - started, 4),
            "stdout": result.stdout[-4000:],
            "stderr": result.stderr[-4000:],
            "timed_out": False,
            "crashed": result.returncode < 0,
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout.decode() if isinstance(error.stdout, bytes) else (error.stdout or "")
        stderr = error.stderr.decode() if isinstance(error.stderr, bytes) else (error.stderr or "")
        return {
            "exit_code": None,
            "elapsed_seconds": round(time.monotonic() - started, 4),
            "stdout": stdout[-4000:],
            "stderr": stderr[-4000:],
            "timed_out": True,
            "crashed": False,
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target-godot", default="4.7")
    parser.add_argument("--file-timeout", type=float, default=10.0)
    parser.add_argument("--project-timeout", type=float, default=60.0)
    parser.add_argument("--native-build-timeout", type=float, default=300.0)
    args = parser.parse_args()

    manifest = load_json(args.manifest)
    corpus = args.corpus.resolve()
    compiler = args.compiler.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    actual_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=corpus,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout.strip()
    expected_commit = manifest["repository"]["commit"]
    if actual_commit != expected_commit:
        raise RuntimeError(f"corpus commit mismatch: expected {expected_commit}, got {actual_commit}")

    report = {
        "schema_version": 1,
        "repository": manifest["repository"],
        "target_godot": args.target_godot,
        "projects": [],
        "summary": {},
    }
    hard_failures: list[str] = []
    total_scripts = 0
    total_successes = 0

    for project_spec in manifest["projects"]:
        relative_project = project_spec["path"]
        project_root = corpus / relative_project
        scripts = sorted(project_root.rglob("*.gd"))
        project_report = {
            "path": relative_project,
            "script_count": len(scripts),
            "isolated_successes": 0,
            "minimum_isolated_successes": project_spec["minimum_isolated_successes"],
            "require_project_native_success": project_spec.get(
                "require_project_native_success", False
            ),
            "scripts": [],
        }

        for script in scripts:
            relative_script = script.relative_to(corpus).as_posix()
            digest = hashlib.sha256(relative_script.encode("utf-8")).hexdigest()[:16]
            generated = output / "generated" / digest
            result = invoke(
                [
                    str(compiler),
                    "compile",
                    str(script),
                    "--output",
                    str(generated),
                    "--target-godot",
                    args.target_godot,
                ],
                args.file_timeout,
            )
            result["path"] = relative_script
            project_report["scripts"].append(result)
            total_scripts += 1
            if result["exit_code"] == 0:
                project_report["isolated_successes"] += 1
                total_successes += 1
            elif result["timed_out"] or result["crashed"] or result["exit_code"] not in (1,):
                hard_failures.append(f"unsafe isolated compile result for {relative_script}")

        # ProjectCompiler intentionally requires its output to remain inside the Godot project.
        # The sparse corpus itself lives below the repository build/ root, so this still obeys
        # the global artifact-location invariant without modifying checked-in source.
        project_output = project_root / "addons/gdpp/build/compatibility"
        project_result = invoke(
            [
                str(compiler),
                "project",
                str(project_root),
                "--output",
                str(project_output),
                "--target-godot",
                args.target_godot,
                "--sdk-root",
                str(args.sdk_root.resolve()),
                "--godot-cpp",
                str((args.sdk_root.resolve() / "third/godot-cpp")),
            ],
            args.project_timeout,
        )
        project_report["project_compile"] = project_result
        if (
            project_result["timed_out"]
            or project_result["crashed"]
            or project_result["exit_code"] not in (0, 1)
        ):
            hard_failures.append(f"unsafe project compile result for {relative_project}")
        if project_report["require_project_native_success"] and project_result["exit_code"] != 0:
            hard_failures.append(f"required project compile failed for {relative_project}")
        if project_result["exit_code"] == 0:
            native_directory = project_output / "native"
            configure_result = invoke(
                [
                    "cmake",
                    "-S",
                    str(project_output),
                    "-B",
                    str(native_directory),
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Debug",
                ],
                args.project_timeout,
            )
            project_report["native_configure"] = configure_result
            if configure_result["exit_code"] != 0:
                hard_failures.append(f"native configure failed for {relative_project}")
            else:
                native_result = invoke(
                    ["cmake", "--build", str(native_directory), "--parallel"],
                    args.native_build_timeout,
                )
                project_report["native_build"] = native_result
                if native_result["exit_code"] != 0:
                    hard_failures.append(f"generated native code failed for {relative_project}")
                elif project_report["require_project_native_success"]:
                    project_report["project_native_gate_passed"] = True
        if project_report["isolated_successes"] < project_spec["minimum_isolated_successes"]:
            hard_failures.append(
                f"compatibility regression for {relative_project}: "
                f"{project_report['isolated_successes']} < {project_spec['minimum_isolated_successes']}"
            )
        report["projects"].append(project_report)

    report["summary"] = {
        "script_count": total_scripts,
        "isolated_successes": total_successes,
        "isolated_success_rate": round(total_successes / total_scripts, 4) if total_scripts else 0.0,
        "hard_failure_count": len(hard_failures),
        "hard_failures": hard_failures,
    }
    report_path = output / "report.json"
    with report_path.open("w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")

    print(
        f"official corpus: {total_successes}/{total_scripts} scripts compiled in isolation; "
        f"{len(hard_failures)} hard failure(s); report: {report_path}"
    )
    for project in report["projects"]:
        print(
            f"  {project['path']}: {project['isolated_successes']}/{project['script_count']}; "
            f"project exit={project['project_compile']['exit_code']}; "
            f"native exit={project.get('native_build', {}).get('exit_code', 'skipped')}"
        )
    if hard_failures:
        for failure in hard_failures:
            print(f"error: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
