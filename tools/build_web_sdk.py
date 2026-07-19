#!/usr/bin/env python3
"""构建可复现、线程模式隔离的 GDPP Web 目标 SDK。"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess


PROFILES = {"debug": "template_debug", "release": "template_release"}
VARIANTS = {"threads": True, "nothreads": False}


def clean_environment() -> dict[str, str]:
    environment = os.environ.copy()
    # godot-cpp's binding generator imports Python modules from its source
    # tree. Target-pack builds must never leave bytecode beside the submodule.
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return environment


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--addon-root", type=Path, required=True)
    parser.add_argument("--godot-version", required=True)
    parser.add_argument("--variant", choices=sorted(VARIANTS), required=True)
    parser.add_argument("--emcmake", default="emcmake")
    parser.add_argument("--schema", type=int, required=True)
    parser.add_argument("--runtime-abi", type=int, required=True)
    parser.add_argument("--gdpp-version", required=True)
    return parser.parse_args()


def run(command: list[str]) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, check=True, env=clean_environment())


def output(command: list[str]) -> str:
    return subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=clean_environment(),
    ).stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_descendant(path: Path, parent: Path, label: str) -> None:
    try:
        path.relative_to(parent)
    except ValueError as error:
        raise SystemExit(f"{label} must remain below {parent}: {path}") from error


def emscripten_version(emcmake: str) -> tuple[str, tuple[int, int, int]]:
    executable = shutil.which(emcmake)
    if executable is None:
        raise SystemExit(f"Emscripten CMake launcher was not found: {emcmake}")
    empp = Path(executable).with_name("em++")
    compiler = str(empp) if empp.is_file() else "em++"
    text = output([compiler, "--version"])
    match = re.search(r"(?:emcc|Emscripten[^\d]*)(\d+)\.(\d+)\.(\d+)", text, re.I)
    if match is None:
        raise SystemExit(f"cannot determine Emscripten version from: {text.splitlines()[:2]}")
    value = tuple(int(part) for part in match.groups())
    return ".".join(match.groups()), value


def validate_toolchain(godot_version: str, version: tuple[int, int, int]) -> None:
    if godot_version == "4.4":
        if version < (3, 1, 62) or version >= (4, 0, 0):
            raise SystemExit(
                "Godot 4.4 Web target packs require Emscripten 3.1.62.x; "
                f"detected {version[0]}.{version[1]}.{version[2]}"
            )
    elif version < (4, 0, 0):
        raise SystemExit(
            f"Godot {godot_version} Web target packs require Emscripten 4.0.0+; "
            f"detected {version[0]}.{version[1]}.{version[2]}"
        )


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    build_root = args.build_root.resolve()
    addon_root = args.addon_root.resolve()
    require_descendant(build_root, source_root / "build", "Web SDK build root")

    compiler_version, compiler_semver = emscripten_version(args.emcmake)
    validate_toolchain(args.godot_version, compiler_semver)

    godot_cpp = source_root / "third/godot-cpp"
    runtime_header = source_root / "include/gdpp/runtime/variant_ops.hpp"
    runtime_source = source_root / "src/runtime/variant_ops.cpp"
    integer_semantics_header = source_root / "include/gdpp/support/integer_semantics.hpp"
    for required in (
        godot_cpp / "CMakeLists.txt",
        runtime_header,
        runtime_source,
        integer_semantics_header,
    ):
        if not required.is_file():
            raise SystemExit(f"missing Web SDK input: {required}")

    variant = args.variant
    target_root = addon_root / "sdk" / args.godot_version / "web/wasm32" / variant
    stage = build_root / "stage" / args.godot_version / variant
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "include/gdpp/runtime").mkdir(parents=True)
    (stage / "include/gdpp/support").mkdir(parents=True)
    (stage / "src/runtime").mkdir(parents=True)
    (stage / "godot-cpp/gen").mkdir(parents=True)
    (stage / "lib").mkdir(parents=True)

    generated_include: Path | None = None
    for profile, godot_target in PROFILES.items():
        directory = build_root / args.godot_version / variant / profile
        run(
            [
                args.emcmake,
                "cmake",
                "-S",
                str(godot_cpp),
                "-B",
                str(directory),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DGODOTCPP_API_VERSION={args.godot_version}",
                f"-DGODOTCPP_TARGET={godot_target}",
                f"-DGODOTCPP_THREADS={'ON' if VARIANTS[variant] else 'OFF'}",
                "-DGODOTCPP_ENABLE_TESTING=OFF",
                "-DGODOTCPP_SYSTEM_HEADERS=ON",
                f"-DCMAKE_CXX_FLAGS=-ffile-prefix-map={source_root}=/gdpp",
            ]
        )
        run(["cmake", "--build", str(directory), "--target", "godot-cpp", "--parallel"])
        suffix = "" if VARIANTS[variant] else ".nothreads"
        expected = directory / "bin" / (
            f"libgodot-cpp.web.{godot_target}.wasm32{suffix}.a"
        )
        if not expected.is_file():
            candidates = sorted((directory / "bin").glob(f"*{godot_target}*wasm32*.a"))
            if len(candidates) != 1:
                raise SystemExit(
                    f"expected one Web {profile}/{variant} godot-cpp library, found {candidates}"
                )
            expected = candidates[0]
        shutil.copy2(expected, stage / "lib" / expected.name)
        generated_include = directory / "gen/include"

    assert generated_include is not None
    shutil.copytree(godot_cpp / "include", stage / "godot-cpp/include", dirs_exist_ok=True)
    shutil.copytree(generated_include, stage / "godot-cpp/gen/include", dirs_exist_ok=True)
    shutil.copy2(godot_cpp / "LICENSE.md", stage / "godot-cpp/LICENSE.md")
    shutil.copy2(runtime_header, stage / "include/gdpp/runtime/variant_ops.hpp")
    shutil.copy2(runtime_source, stage / "src/runtime/variant_ops.cpp")
    shutil.copy2(
        integer_semantics_header,
        stage / "include/gdpp/support/integer_semantics.hpp",
    )

    manifest = (
        f"GDPP_SDK {args.schema}\n"
        f"api {args.godot_version}\n"
        "platform web\n"
        "arch wasm32\n"
        "profiles debug,release\n"
        "platform_minimum none\n"
        f"web_threads {variant}\n"
        f"gdpp_version {args.gdpp_version}\n"
        f"runtime_abi {args.runtime_abi}\n"
        f"runtime_header_sha256 {sha256(runtime_header)}\n"
        f"runtime_source_sha256 {sha256(runtime_source)}\n"
        f"integer_semantics_header_sha256 {sha256(integer_semantics_header)}\n"
        "compiler Emscripten\n"
        f"compiler_version {compiler_version}\n"
        "source_paths mapped\n"
    )
    (stage / "sdk.manifest").write_text(manifest, encoding="utf-8", newline="\n")

    target_root.parent.mkdir(parents=True, exist_ok=True)
    if target_root.exists():
        shutil.rmtree(target_root)
    shutil.copytree(stage, target_root)
    total = sum(path.stat().st_size for path in target_root.rglob("*") if path.is_file())
    print(
        f"Packaged Godot {args.godot_version} Web/wasm32/{variant} SDK: "
        f"{total / (1024 * 1024):.2f} MiB -> {target_root}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
