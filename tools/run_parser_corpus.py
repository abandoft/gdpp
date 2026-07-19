#!/usr/bin/env python3
"""Differentially gate GDPP against the pinned Godot 4.7 parser corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


FRONTEND_DIAGNOSTIC = re.compile(r"\[GDS[12][0-9]{3}\]")
ANY_DIAGNOSTIC = re.compile(r"\[GDS[0-9]{4}\]")
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    if manifest.get("schema_version") != 1:
        raise RuntimeError("unsupported parser corpus manifest schema")
    return manifest


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
        stderr = ANSI_ESCAPE.sub("", result.stderr)
        return {
            "exit_code": result.returncode,
            "elapsed_seconds": round(time.monotonic() - started, 4),
            "timed_out": False,
            "crashed": result.returncode < 0,
            "frontend_rejected": bool(FRONTEND_DIAGNOSTIC.search(stderr)),
            "diagnostics": ANY_DIAGNOSTIC.findall(stderr),
            "stderr": stderr[-4000:],
        }
    except subprocess.TimeoutExpired as error:
        stderr = error.stderr.decode() if isinstance(error.stderr, bytes) else (error.stderr or "")
        return {
            "exit_code": None,
            "elapsed_seconds": round(time.monotonic() - started, 4),
            "timed_out": True,
            "crashed": False,
            "frontend_rejected": False,
            "diagnostics": [],
            "stderr": ANSI_ESCAPE.sub("", stderr)[-4000:],
        }


def collect_scripts(corpus_root: Path, directories: list[str]) -> list[Path]:
    scripts: list[Path] = []
    for directory in directories:
        root = corpus_root / directory
        if not root.is_dir():
            raise RuntimeError(f"parser corpus directory is missing: {root}")
        scripts.extend(root.rglob("*.gd"))
    return sorted(scripts)


def write_report(report: dict, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".json.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    temporary.replace(destination)


def ensure_build_destination(destination: Path, build_root: Path) -> None:
    resolved_build_root = build_root.resolve()
    if destination == resolved_build_root or resolved_build_root not in destination.parents:
        raise RuntimeError(f"parser report output must be below build root: {resolved_build_root}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--target-godot", default="4.7")
    parser.add_argument("--file-timeout", type=float, default=10.0)
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)
    corpus = args.corpus.resolve()
    compiler = args.compiler.resolve()
    output = args.output.resolve()
    ensure_build_destination(output, args.build_root)
    generated_root = output / "generated"
    if output.exists():
        shutil.rmtree(output)
    generated_root.mkdir(parents=True)

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
        raise RuntimeError(
            f"parser corpus commit mismatch: expected {expected_commit}, got {actual_commit}"
        )

    specification = manifest["parser_corpus"]
    parser_root = corpus / specification["root"]
    valid_scripts = collect_scripts(parser_root, specification["valid_directories"])
    invalid_scripts = collect_scripts(parser_root, specification["invalid_directories"])
    expected_valid = specification["expected_valid_scripts"]
    expected_invalid = specification["expected_invalid_scripts"]
    if len(valid_scripts) != expected_valid or len(invalid_scripts) != expected_invalid:
        raise RuntimeError(
            "pinned parser corpus count changed: "
            f"valid {len(valid_scripts)} != {expected_valid}, "
            f"invalid {len(invalid_scripts)} != {expected_invalid}"
        )

    report = {
        "schema_version": 1,
        "repository": manifest["repository"],
        "target_godot": args.target_godot,
        "valid": [],
        "invalid": [],
        "semantic_accepted": [],
        "semantic_rejected": [],
        "failures": [],
    }

    def compile_script(script: Path, category: str) -> dict:
        relative = script.relative_to(corpus).as_posix()
        digest = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:16]
        result = invoke(
            [
                str(compiler),
                "compile",
                str(script),
                "--output",
                str(generated_root / category / digest),
                "--target-godot",
                args.target_godot,
            ],
            args.file_timeout,
        )
        result["path"] = relative
        return result

    for script in valid_scripts:
        result = compile_script(script, "valid")
        report["valid"].append(result)
        if result["timed_out"] or result["crashed"]:
            report["failures"].append(f"unsafe result for valid script: {result['path']}")
        elif result["exit_code"] not in (0, 1):
            report["failures"].append(
                f"unexpected compiler exit for valid script: {result['path']}"
            )
        elif result["frontend_rejected"]:
            report["failures"].append(f"frontend rejected valid script: {result['path']}")

    for script in invalid_scripts:
        result = compile_script(script, "invalid")
        report["invalid"].append(result)
        if result["timed_out"] or result["crashed"]:
            report["failures"].append(f"unsafe result for invalid script: {result['path']}")
        elif result["exit_code"] not in (0, 1):
            report["failures"].append(
                f"unexpected compiler exit for invalid script: {result['path']}"
            )
        elif result["exit_code"] == 0:
            report["failures"].append(f"compiler accepted invalid script: {result['path']}")

    semantic_cases = manifest.get("semantic_cases", {})
    for relative in semantic_cases.get("accepted", []):
        script = corpus / relative
        if not script.is_file():
            raise RuntimeError(f"semantic acceptance case is missing: {relative}")
        result = compile_script(script, "semantic-accepted")
        report["semantic_accepted"].append(result)
        if result["timed_out"] or result["crashed"]:
            report["failures"].append(f"unsafe semantic acceptance result: {relative}")
        elif result["exit_code"] != 0:
            report["failures"].append(f"compiler rejected semantic acceptance case: {relative}")

    for relative in semantic_cases.get("rejected", []):
        script = corpus / relative
        if not script.is_file():
            raise RuntimeError(f"semantic rejection case is missing: {relative}")
        result = compile_script(script, "semantic-rejected")
        report["semantic_rejected"].append(result)
        if result["timed_out"] or result["crashed"]:
            report["failures"].append(f"unsafe semantic rejection result: {relative}")
        elif result["exit_code"] == 0:
            report["failures"].append(f"compiler accepted semantic rejection case: {relative}")

    report["summary"] = {
        "valid_script_count": len(valid_scripts),
        "valid_frontend_acceptances": sum(
            not result["frontend_rejected"]
            and not result["timed_out"]
            and not result["crashed"]
            for result in report["valid"]
        ),
        "valid_full_compilations": sum(
            result["exit_code"] == 0 for result in report["valid"]
        ),
        "invalid_script_count": len(invalid_scripts),
        "invalid_rejections": sum(
            result["exit_code"] != 0
            and not result["timed_out"]
            and not result["crashed"]
            for result in report["invalid"]
        ),
        "invalid_frontend_rejections": sum(
            result["frontend_rejected"] for result in report["invalid"]
        ),
        "semantic_acceptances": sum(
            result["exit_code"] == 0 for result in report["semantic_accepted"]
        ),
        "semantic_rejections": sum(
            result["exit_code"] != 0 for result in report["semantic_rejected"]
        ),
        "failure_count": len(report["failures"]),
    }
    report["status"] = "passed" if not report["failures"] else "failed"
    report_path = output / "report.json"
    write_report(report, report_path)

    summary = report["summary"]
    print(
        "Godot 4.7 parser corpus: "
        f"valid frontend {summary['valid_frontend_acceptances']}/{len(valid_scripts)}, "
        f"invalid rejected {summary['invalid_rejections']}/{len(invalid_scripts)} "
        f"({summary['invalid_frontend_rejections']} at frontend), "
        f"semantic accepted {summary['semantic_acceptances']}/"
        f"{len(report['semantic_accepted'])}, semantic rejected "
        f"{summary['semantic_rejections']}/{len(report['semantic_rejected'])}; "
        f"report: {report_path}",
        flush=True,
    )
    for failure in report["failures"]:
        print(f"error: {failure}", file=sys.stderr)
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
