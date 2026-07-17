#!/usr/bin/env python3
"""交替运行 GDScript 与 GDPP AOT 导出，生成行为差分和性能统计报告。"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gds-executable", type=Path, required=True)
    parser.add_argument("--aot-executable", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=Path("test/performance/runtime_matrix.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work-directory", type=Path, default=Path("build/runtime-matrix"))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--samples", type=int, default=9)
    parser.add_argument("--iterations", type=int, default=20000)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--frame-warmup", type=int, default=30)
    parser.add_argument("--frame-iterations", type=int, default=2000)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--gds-pck", type=Path)
    parser.add_argument("--aot-pck", type=Path)
    parser.add_argument("--gds-artifact-directory", type=Path)
    parser.add_argument("--aot-artifact-directory", type=Path)
    parser.add_argument("--enforce", action="store_true")
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact_directory(path: Path) -> dict[str, Any]:
    files = sorted(item for item in path.rglob("*") if item.is_file())
    digest = hashlib.sha256()
    entries: list[dict[str, Any]] = []
    total = 0
    for item in files:
        relative = item.relative_to(path).as_posix()
        size = item.stat().st_size
        file_digest = sha256(item)
        total += size
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(file_digest.encode("ascii"))
        digest.update(b"\n")
        entries.append({"path": relative, "bytes": size, "sha256": file_digest})
    return {
        "path": str(path.resolve()),
        "bytes": total,
        "sha256": digest.hexdigest(),
        "file_count": len(entries),
        "files": entries,
    }


def executable_version(path: Path) -> str:
    result = subprocess.run(
        [str(path.resolve()), "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=15.0,
        check=False,
    )
    version = result.stdout.strip().splitlines()
    if result.returncode != 0 or not version:
        raise RuntimeError(f"无法读取测试引擎版本：{path}")
    return version[-1].strip()


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        try:
            for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.lower().startswith("model name") and ":" in line:
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor() or platform.machine()


def percentile(values: list[float], quantile: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (upper - position) + ordered[upper] * (position - lower)


def stats(values: list[float]) -> dict[str, float | int]:
    mean = statistics.fmean(values) if values else 0.0
    deviation = statistics.pstdev(values) if len(values) > 1 else 0.0
    return {
        "count": len(values),
        "minimum": min(values, default=0.0),
        "mean": mean,
        "median": statistics.median(values) if values else 0.0,
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "maximum": max(values, default=0.0),
        "standard_deviation": deviation,
        "coefficient_of_variation": deviation / mean if mean else 0.0,
    }


def equivalent(left: Any, right: Any, absolute: float, relative: float, path: str = "$") -> list[str]:
    if isinstance(left, bool) or isinstance(right, bool):
        return [] if left is right else [f"{path}: {left!r} != {right!r}"]
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return [] if math.isclose(float(left), float(right), abs_tol=absolute, rel_tol=relative) else [
            f"{path}: {left!r} != {right!r}"
        ]
    if type(left) is not type(right):
        return [f"{path}: type {type(left).__name__} != {type(right).__name__}"]
    if isinstance(left, dict):
        failures: list[str] = []
        if set(left) != set(right):
            failures.append(f"{path}: keys {sorted(left)} != {sorted(right)}")
        for key in sorted(set(left) & set(right)):
            failures.extend(equivalent(left[key], right[key], absolute, relative, f"{path}.{key}"))
        return failures
    if isinstance(left, list):
        if len(left) != len(right):
            return [f"{path}: length {len(left)} != {len(right)}"]
        failures: list[str] = []
        for index, (left_value, right_value) in enumerate(zip(left, right)):
            failures.extend(equivalent(left_value, right_value, absolute, relative, f"{path}[{index}]"))
        return failures
    return [] if left == right else [f"{path}: {left!r} != {right!r}"]


def run_once(
    executable: Path,
    variant: str,
    sequence: int,
    args: argparse.Namespace,
    forbidden: list[str],
    mode: str,
) -> dict[str, Any]:
    result_path = args.work_directory / f"{sequence:02d}-{mode}-{variant}.json"
    log_path = args.work_directory / f"{sequence:02d}-{mode}-{variant}.log"
    result_path.unlink(missing_ok=True)
    log_path.unlink(missing_ok=True)
    command = [
        str(executable.resolve()),
        "--headless",
        "--audio-driver",
        "Dummy",
        "--",
    ]
    if mode == "startup":
        command.append("--gdpp-matrix-startup")
        expected_marker = "GDPP_MATRIX_STARTUP_OK"
    elif mode == "benchmark":
        command.extend(
            [
                "--gdpp-matrix-output",
                str(result_path.resolve()),
                "--gdpp-matrix-samples",
                str(args.samples),
                "--gdpp-matrix-iterations",
                str(args.iterations),
            ]
        )
        expected_marker = "GDPP_MATRIX_OK"
    else:
        command.extend(
            [
                "--gdpp-matrix-frame-output",
                str(result_path.resolve()),
                "--gdpp-matrix-frames",
                str(args.frames),
                "--gdpp-matrix-frame-warmup",
                str(args.frame_warmup),
                "--gdpp-matrix-frame-iterations",
                str(args.frame_iterations),
            ]
        )
        expected_marker = "GDPP_MATRIX_FRAME_OK"
    started = time.perf_counter_ns()
    peak_rss_bytes: int | None = None
    with log_path.open("wb") as log_stream:
        process = subprocess.Popen(
            command,
            cwd=args.work_directory,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
        )
        deadline = time.monotonic() + args.timeout
        while process.poll() is None:
            # Linux exposes a per-process high-water mark without adding a benchmark dependency.
            # Other platforms report null instead of fabricating a cross-platform equivalent.
            status_path = Path(f"/proc/{process.pid}/status")
            if status_path.is_file():
                try:
                    for line in status_path.read_text(encoding="utf-8").splitlines():
                        if line.startswith("VmHWM:") or line.startswith("VmRSS:"):
                            value = int(line.split()[1]) * 1024
                            peak_rss_bytes = max(peak_rss_bytes or 0, value)
                except (OSError, ValueError):
                    pass
            if time.monotonic() >= deadline:
                process.kill()
                process.wait()
                raise RuntimeError(f"{variant} {mode} run {sequence} timed out")
            time.sleep(0.001)
        return_code = process.wait()
    elapsed_ns = time.perf_counter_ns() - started
    combined_log = log_path.read_text(encoding="utf-8", errors="replace")
    violations = [pattern for pattern in forbidden if pattern in combined_log]
    if return_code != 0 or expected_marker not in combined_log:
        raise RuntimeError(f"{variant} {mode} run {sequence} failed with {return_code}\n{combined_log[-4000:]}")
    if violations:
        raise RuntimeError(f"{variant} {mode} run {sequence} contains forbidden logs: {violations}")
    payload = None
    if mode != "startup":
        if not result_path.is_file():
            raise RuntimeError(f"{variant} {mode} run {sequence} produced no result")
        payload = json.loads(result_path.read_text(encoding="utf-8"))
        if payload.get("schema") != 1:
            raise RuntimeError(f"{variant} run {sequence} returned an incompatible matrix schema")
    return {
        "variant": variant,
        "mode": mode,
        "sequence": sequence,
        "process_ns": elapsed_ns,
        "peak_rss_bytes": peak_rss_bytes,
        "payload": payload,
        "log_sha256": hashlib.sha256(combined_log.encode("utf-8")).hexdigest(),
        "log_tail": combined_log[-2000:],
    }


def main() -> int:
    args = parse_args()
    if (
        args.repeats < 2
        or args.samples < 3
        or args.iterations < 1
        or args.frames < 30
        or args.frame_warmup < 1
        or args.frame_iterations < 1
    ):
        raise SystemExit(
            "repeats>=2、samples>=3、iterations>=1、frames>=30、frame-warmup>=1 "
            "且 frame-iterations>=1"
        )
    for executable in (args.gds_executable, args.aot_executable):
        if not executable.is_file():
            raise SystemExit(f"找不到测试程序：{executable}")
    for directory in (args.gds_artifact_directory, args.aot_artifact_directory):
        if directory and not directory.is_dir():
            raise SystemExit(f"找不到发行目录：{directory}")
    config = json.loads(args.config.read_text(encoding="utf-8"))
    engine_versions = {
        "gds": executable_version(args.gds_executable),
        "aot": executable_version(args.aot_executable),
    }
    args.work_directory.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    forbidden = config.get("forbidden_log_patterns", [])
    runs: list[dict[str, Any]] = []
    startup_runs: list[dict[str, Any]] = []
    frame_runs: list[dict[str, Any]] = []
    sequence = 0
    # AB/BA 交替，降低温度、后台任务和首次磁盘缓存对单一版本的系统性偏差。
    for repeat in range(args.repeats):
        order = [("gds", args.gds_executable), ("aot", args.aot_executable)]
        if repeat % 2:
            order.reverse()
        for variant, executable in order:
            startup_runs.append(run_once(executable, variant, sequence, args, forbidden, "startup"))
            sequence += 1
        for variant, executable in order:
            runs.append(run_once(executable, variant, sequence, args, forbidden, "benchmark"))
            sequence += 1
        for variant, executable in order:
            frame_runs.append(run_once(executable, variant, sequence, args, forbidden, "frame"))
            sequence += 1

    oracle_config = config["oracle"]
    absolute = float(oracle_config["absolute_float_tolerance"])
    relative = float(oracle_config["relative_float_tolerance"])
    baseline = next(run for run in runs if run["variant"] == "gds")["payload"]["oracle"]
    differences: list[str] = []
    if engine_versions["gds"] != engine_versions["aot"]:
        differences.append(
            "engine version: "
            f"GDScript {engine_versions['gds']!r} != AOT {engine_versions['aot']!r}"
        )
    for run in runs:
        differences.extend(
            f"{run['variant']}#{run['sequence']} {failure}"
            for failure in equivalent(
                baseline, run["payload"]["oracle"], absolute, relative
            )
        )
    frame_checksum = next(run for run in frame_runs if run["variant"] == "gds")["payload"][
        "checksum"
    ]
    for run in frame_runs:
        if run["payload"].get("checksum") != frame_checksum:
            differences.append(
                f"{run['variant']}#{run['sequence']} frame checksum "
                f"{run['payload'].get('checksum')} != {frame_checksum}"
            )

    startup: dict[str, dict[str, float | int]] = {}
    for variant in ("gds", "aot"):
        startup[variant] = stats(
            [run["process_ns"] / 1_000_000.0 for run in startup_runs if run["variant"] == variant]
        )
    memory: dict[str, dict[str, float | int] | None] = {}
    for variant in ("gds", "aot"):
        samples = [
            float(run["peak_rss_bytes"])
            for run in runs
            if run["variant"] == variant and run["peak_rss_bytes"] is not None
        ]
        memory[variant] = stats(samples) if samples else None
    cases: dict[str, Any] = {}
    threshold_failures: list[str] = []
    for name, threshold in config["cases"].items():
        variant_stats: dict[str, dict[str, float | int]] = {}
        for variant in ("gds", "aot"):
            samples_ns: list[float] = []
            for run in runs:
                if run["variant"] != variant:
                    continue
                case = run["payload"]["performance"].get(name)
                if not case:
                    differences.append(f"{variant}#{run['sequence']} missing case {name}")
                    continue
                iterations = int(case["iterations"])
                samples_ns.extend(float(value) * 1000.0 / iterations for value in case["samples_us"])
            variant_stats[variant] = stats(samples_ns)
        gds_mean = float(variant_stats["gds"]["mean"])
        aot_mean = float(variant_stats["aot"]["mean"])
        regression = ((aot_mean / gds_mean) - 1.0) * 100.0 if gds_mean else math.inf
        speedup = gds_mean / aot_mean if aot_mean else math.inf
        cases[name] = {
            **variant_stats,
            "aot_regression_percent": regression,
            "aot_speedup": speedup,
        }
        maximum = float(threshold["maximum_aot_regression_percent"])
        if regression > maximum:
            threshold_failures.append(f"{name}: AOT regression {regression:.2f}% > {maximum:.2f}%")

    frame_statistics: dict[str, Any] = {}
    for variant in ("gds", "aot"):
        intervals_ms: list[float] = []
        workload_ns: list[float] = []
        for run in frame_runs:
            if run["variant"] != variant:
                continue
            payload = run["payload"]
            intervals_ms.extend(float(value) / 1000.0 for value in payload["frame_intervals_us"])
            workload_ns.extend(
                float(value) * 1000.0 / int(payload["iterations_per_frame"])
                for value in payload["workload_us"]
            )
        interval_stats = stats(intervals_ms)
        mean_interval = float(interval_stats["mean"])
        frame_statistics[variant] = {
            "frame_interval_ms": interval_stats,
            "effective_fps_from_mean_interval": 1000.0 / mean_interval if mean_interval else None,
            "workload_ns_per_iteration": stats(workload_ns),
        }
    frame_gds_workload = float(
        frame_statistics["gds"]["workload_ns_per_iteration"]["mean"]
    )
    frame_aot_workload = float(
        frame_statistics["aot"]["workload_ns_per_iteration"]["mean"]
    )
    frame_regression = (
        ((frame_aot_workload / frame_gds_workload) - 1.0) * 100.0
        if frame_gds_workload
        else math.inf
    )
    frame_limit = float(config["frame"]["maximum_aot_workload_regression_percent"])
    frame_statistics["aot_workload_regression_percent"] = frame_regression
    frame_statistics["maximum_aot_workload_regression_percent"] = frame_limit
    if frame_regression > frame_limit:
        threshold_failures.append(
            f"frame workload: AOT regression {frame_regression:.2f}% > {frame_limit:.2f}%"
        )

    startup_gds = float(startup["gds"]["median"])
    startup_aot = float(startup["aot"]["median"])
    startup_regression = ((startup_aot / startup_gds) - 1.0) * 100.0 if startup_gds else math.inf
    startup_limit = float(config["startup"]["maximum_aot_regression_percent"])
    if startup_regression > startup_limit:
        threshold_failures.append(
            f"startup: AOT regression {startup_regression:.2f}% > {startup_limit:.2f}%"
        )

    artifacts: dict[str, Any] = {}
    for name, path in {
        "gds_executable": args.gds_executable,
        "aot_executable": args.aot_executable,
        "gds_pck": args.gds_pck,
        "aot_pck": args.aot_pck,
    }.items():
        if path and path.is_file():
            artifacts[name] = {"path": str(path.resolve()), "bytes": path.stat().st_size, "sha256": sha256(path)}
    for variant, directory in (
        ("gds", args.gds_artifact_directory),
        ("aot", args.aot_artifact_directory),
    ):
        if directory:
            bundle = artifact_directory(directory)
            artifacts[f"{variant}_bundle"] = bundle
            artifacts[f"{variant}_total_bytes"] = bundle["bytes"]
        else:
            artifacts[f"{variant}_total_bytes"] = sum(
                int(value["bytes"])
                for name, value in artifacts.items()
                if isinstance(value, dict) and name.startswith(f"{variant}_")
            )
    gds_bytes = int(artifacts["gds_total_bytes"])
    aot_bytes = int(artifacts["aot_total_bytes"])
    artifacts["aot_size_delta_bytes"] = aot_bytes - gds_bytes
    artifacts["aot_size_delta_percent"] = (
        ((aot_bytes / gds_bytes) - 1.0) * 100.0 if gds_bytes else None
    )

    report = {
        "schema": 1,
        "passed": not differences and (not args.enforce or not threshold_failures),
        "environment": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "processor": platform.processor(),
            "cpu_model": cpu_model(),
            "cpu_count": os.cpu_count(),
            "godot_versions": engine_versions,
        },
        "configuration": {
            "repeats": args.repeats,
            "samples_per_run": args.samples,
            "iterations_per_sample": args.iterations,
            "frames_per_run": args.frames,
            "frame_warmup": args.frame_warmup,
            "frame_iterations": args.frame_iterations,
            "thresholds_enforced": args.enforce,
        },
        "oracle": {"passed": not differences, "differences": differences, "baseline": baseline},
        "startup_ms": {
            **startup,
            "aot_regression_percent": startup_regression,
            "maximum_aot_regression_percent": startup_limit,
        },
        "benchmark_peak_rss_bytes": memory,
        "performance_ns_per_iteration": cases,
        "frame_stability": frame_statistics,
        "threshold_failures": threshold_failures,
        "artifacts": artifacts,
        "startup_runs": startup_runs,
        "benchmark_runs": runs,
        "frame_runs": frame_runs,
    }
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"GDPP_RUNTIME_MATRIX_REPORT={args.output}")
    print(f"GDPP_RUNTIME_MATRIX_ORACLE={'PASS' if not differences else 'FAIL'}")
    print(f"GDPP_RUNTIME_MATRIX_THRESHOLDS={'PASS' if not threshold_failures else 'FAIL'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
