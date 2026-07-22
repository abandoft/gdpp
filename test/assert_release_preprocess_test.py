#!/usr/bin/env python3
"""Verify that the host C++ preprocessor removes every debug-only assert operand."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, text=True, capture_output=True)


def preprocess(compiler: pathlib.Path, compiler_id: str, source: pathlib.Path, debug: bool) -> str:
    if compiler_id == "MSVC":
        command = [str(compiler), "/nologo", "/EP", "/TP"]
        if debug:
            command.append("/DGDPP_SCRIPT_DEBUG_ENABLED")
    else:
        command = [str(compiler), "-E", "-P", "-x", "c++"]
        if debug:
            command.append("-DGDPP_SCRIPT_DEBUG_ENABLED")
    command.append(str(source))
    result = run(command)
    if result.returncode != 0:
        raise RuntimeError(
            f"C++ preprocessing failed ({result.returncode}):\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=pathlib.Path, required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--gdpp", type=pathlib.Path, required=True)
    parser.add_argument("--fixture", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    shutil.rmtree(arguments.output, ignore_errors=True)
    generated = arguments.output / "generated"
    generated.mkdir(parents=True)
    compilation = run(
        [str(arguments.gdpp), "compile", str(arguments.fixture), "--output", str(generated)]
    )
    if compilation.returncode != 0:
        raise RuntimeError(
            f"GDPP fixture compilation failed ({compilation.returncode}):\n"
            f"{compilation.stdout}\n{compilation.stderr}"
        )

    generated_source = generated / "assert_release.gd.cpp"
    if not generated_source.is_file():
        raise RuntimeError(f"expected generated source is missing: {generated_source}")

    # The C++ preprocessor does not parse C++ grammar. Removing only include directives keeps the
    # exact generated conditional structure while making this contract independent of a packaged
    # godot-cpp SDK and therefore portable across the core CI matrix.
    isolated_source = arguments.output / "assert_release.isolated.cpp"
    isolated_source.write_text(
        "".join(
            line
            for line in generated_source.read_text(encoding="utf-8").splitlines(keepends=True)
            if not line.lstrip().startswith("#include")
        ),
        encoding="utf-8",
    )

    release = preprocess(arguments.compiler, arguments.compiler_id, isolated_source, False)
    forbidden_release = (
        "GDPP_ASSERT_CONDITION_SENTINEL",
        "GDPP_ASSERT_MESSAGE_SENTINEL",
        "gdpp::runtime::await_signal",
        "ERR_FAIL_EDMSG",
    )
    leaked = [marker for marker in forbidden_release if marker in release]
    if leaked:
        raise RuntimeError(f"release preprocessing retained debug-only assert code: {leaked}")
    if "GDPP_ASSERT_AFTER_SENTINEL" not in release:
        raise RuntimeError("release preprocessing removed the continuation after assert")

    debug = preprocess(arguments.compiler, arguments.compiler_id, isolated_source, True)
    required_debug = (
        "GDPP_ASSERT_CONDITION_SENTINEL",
        "GDPP_ASSERT_MESSAGE_SENTINEL",
        "ERR_FAIL_EDMSG",
        "GDPP_ASSERT_AFTER_SENTINEL",
    )
    missing = [marker for marker in required_debug if marker not in debug]
    if missing:
        raise RuntimeError(f"debug preprocessing removed required assert code: {missing}")
    if debug.count("gdpp::runtime::await_signal") != 2:
        raise RuntimeError("debug preprocessing did not preserve both lazy assert suspension points")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
