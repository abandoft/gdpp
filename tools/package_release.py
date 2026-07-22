#!/usr/bin/env python3
"""Assemble and validate one deterministic GDPP commercial plugin ZIP."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import stat
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path


SUPPORTED_GODOT_VERSIONS = ("4.4", "4.5", "4.6", "4.7")
SDK_SCHEMA = 9
STATIC_ADDON_FILES = (
    "THIRD_PARTY_NOTICES.md",
    "build_progress.gd",
    "export_plugin.gd",
    "gdpp.gdextension",
    "plugin.cfg",
    "plugin.gd",
)
HOST_SDK_PATHS = ("godot-cpp", "include", "lib", "src", "sdk.manifest")
FIXED_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


@dataclass(frozen=True)
class HostContract:
    platform: str
    architecture: str
    platform_minimum: str
    compiler_library: str
    fallback_library: str
    export_targets: tuple[str, ...]


HOSTS = {
    "mac-arm64": HostContract(
        platform="macos",
        architecture="arm64",
        platform_minimum="macOS_11.0",
        compiler_library="libgdpp_compiler.macos.arm64.dylib",
        fallback_library="libgdpp_fallback.macos.arm64.dylib",
        export_targets=("macos-arm64", "android-arm64", "ios-arm64"),
    ),
    "linux-x64": HostContract(
        platform="linux",
        architecture="x86_64",
        platform_minimum="Ubuntu_22.04",
        compiler_library="libgdpp_compiler.linux.x86_64.so",
        fallback_library="libgdpp_fallback.linux.x86_64.so",
        export_targets=("linux-x64", "android-arm64"),
    ),
    "windows-x64": HostContract(
        platform="windows",
        architecture="x86_64",
        platform_minimum="Windows_10",
        compiler_library="gdpp_compiler.windows.x86_64.dll",
        fallback_library="gdpp_fallback.windows.x86_64.dll",
        export_targets=("windows-x64", "android-arm64"),
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--addon", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host", choices=sorted(HOSTS), required=True)
    parser.add_argument("--godot-version", choices=SUPPORTED_GODOT_VERSIONS, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise ValueError(message)


def require_descendant(path: Path, parent: Path, label: str) -> None:
    try:
        path.relative_to(parent)
    except ValueError as error:
        raise ValueError(f"{label} must remain below {parent}: {path}") from error


def read_plugin_version(plugin_cfg: Path) -> str:
    content = plugin_cfg.read_text(encoding="utf-8")
    matches = re.findall(r'^version\s*=\s*"([^"]+)"\s*$', content, re.MULTILINE)
    if len(matches) != 1 or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", matches[0]):
        fail(f"plugin.cfg must contain exactly one unprefixed semantic version: {plugin_cfg}")
    return matches[0]


def read_extension_minimum(descriptor: Path) -> str:
    content = descriptor.read_text(encoding="utf-8")
    matches = re.findall(
        r'^compatibility_minimum\s*=\s*"([^"]+)"\s*$', content, re.MULTILINE
    )
    if len(matches) != 1:
        fail(f"GDExtension descriptor must declare one compatibility_minimum: {descriptor}")
    return matches[0]


def read_sdk_manifest(path: Path) -> tuple[int, dict[str, str]]:
    if not path.is_file():
        fail(f"SDK manifest is missing: {path}")
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        fail(f"SDK manifest is empty: {path}")
    header = re.fullmatch(r"GDPP_SDK ([0-9]+)", lines[0])
    if not header:
        fail(f"SDK manifest header is invalid: {path}")
    fields: dict[str, str] = {}
    for line in lines[1:]:
        key, separator, value = line.partition(" ")
        if not separator or not key or not value or key in fields:
            fail(f"SDK manifest field is invalid or duplicated in {path}: {line!r}")
        fields[key] = value
    return int(header.group(1)), fields


def require_fields(
    path: Path, schema: int, fields: dict[str, str], expected: dict[str, str]
) -> None:
    if schema != SDK_SCHEMA:
        fail(f"SDK schema must be {SDK_SCHEMA} in {path}, got {schema}")
    for key, value in expected.items():
        if fields.get(key) != value:
            fail(f"SDK field {key} must be {value!r} in {path}, got {fields.get(key)!r}")


def require_profile_libraries(directory: Path, profiles: tuple[str, ...]) -> None:
    if not directory.is_dir():
        fail(f"SDK library directory is missing: {directory}")
    names = [path.name for path in directory.iterdir() if path.is_file()]
    for profile in profiles:
        matches = [name for name in names if f".{profile}." in name]
        if len(matches) != 1:
            fail(f"SDK library for {profile} is missing from {directory}")
    debug_libraries = [name for name in names if ".template_debug." in name]
    if debug_libraries:
        fail(f"SDK contains forbidden template_debug libraries in {directory}: {debug_libraries}")


def validate_source(addon: Path, host: HostContract, godot_version: str) -> str:
    if addon.name != "gdpp" or addon.parent.name != "addons":
        fail(f"addon path must end in addons/gdpp: {addon}")
    for relative in STATIC_ADDON_FILES:
        if not (addon / relative).is_file():
            fail(f"required addon file is missing: {addon / relative}")
    version = read_plugin_version(addon / "plugin.cfg")
    if read_extension_minimum(addon / "gdpp.gdextension") != "4.4":
        fail("compiler GDExtension must retain the Godot 4.4 compatibility baseline")

    binary = addon / "binary"
    for filename in (host.compiler_library, host.fallback_library):
        if not (binary / filename).is_file():
            fail(f"required host binary is missing: {binary / filename}")

    sdk = addon / "sdk" / godot_version
    for relative in HOST_SDK_PATHS:
        if not (sdk / relative).exists():
            fail(f"host SDK component is missing: {sdk / relative}")
    host_manifest = sdk / "sdk.manifest"
    schema, host_fields = read_sdk_manifest(host_manifest)
    require_fields(
        host_manifest,
        schema,
        host_fields,
        {
            "api": godot_version,
            "platform": host.platform,
            "arch": host.architecture,
            "profiles": "development,debug,release",
            "distribution_binding": "template_release",
            "distribution_optimization": "Release",
            "editor_binding": "editor",
            "editor_optimization": "Release",
            "platform_minimum": host.platform_minimum,
            "gdpp_version": version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "static" if host.platform == "windows" else "not_applicable",
        },
    )
    if host.platform == "windows":
        if host_fields.get("compiler") != "MSVC":
            fail(f"Windows SDK compiler must be MSVC in {host_manifest}")
        if not re.fullmatch(r"19(?:\.[0-9]+)+", host_fields.get("compiler_version", "")):
            fail(f"Windows SDK compiler_version must identify an MSVC 19.x toolset")
    require_profile_libraries(sdk / "lib", ("editor", "template_release"))

    android_manifest = sdk / "android/arm64/sdk.manifest"
    android_schema, android_fields = read_sdk_manifest(android_manifest)
    require_fields(
        android_manifest,
        android_schema,
        android_fields,
        {
            "api": godot_version,
            "platform": "android",
            "arch": "arm64",
            "profiles": "debug,release",
            "distribution_binding": "template_release",
            "distribution_optimization": "Release",
            "platform_minimum": "Android_9",
            "android_api_level": "28",
            "android_stl": "c++_shared",
            "gdpp_version": version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "not_applicable",
        },
    )
    require_profile_libraries(sdk / "android/arm64/lib", ("template_release",))

    runtime_contract = (
        "runtime_abi",
        "runtime_header_sha256",
        "runtime_source_sha256",
        "attached_runtime_header_sha256",
        "attached_runtime_registry_source_sha256",
        "attached_runtime_instance_source_sha256",
        "attached_runtime_language_source_sha256",
        "integer_semantics_header_sha256",
    )
    for field in runtime_contract:
        if android_fields.get(field) != host_fields.get(field):
            fail(f"Android and host SDK disagree on {field}")

    ios = sdk / "ios/arm64"
    if host.platform == "macos":
        ios_manifest = ios / "sdk.manifest"
        ios_schema, ios_fields = read_sdk_manifest(ios_manifest)
        require_fields(
            ios_manifest,
            ios_schema,
            ios_fields,
            {
                "api": godot_version,
                "platform": "ios",
                "arch": "arm64",
                "profiles": "debug,release",
                "distribution_binding": "template_release",
                "distribution_optimization": "Release",
                "platform_minimum": "iOS_16.0",
                "ios_deployment_target": "16.0",
                "ios_slices": "device-arm64,simulator-arm64,simulator-x86_64",
                "gdpp_version": version,
                "cxx_standard": "17",
                "exceptions": "disabled",
                "msvc_runtime": "not_applicable",
            },
        )
        require_profile_libraries(ios / "lib/device", ("template_release",))
        require_profile_libraries(ios / "lib/simulator", ("template_release",))
        for field in runtime_contract:
            if ios_fields.get(field) != host_fields.get(field):
                fail(f"iOS and host SDK disagree on {field}")
    elif ios.exists():
        fail("iOS target pack is allowed only in the mac-arm64 package")
    return version


def copy_path(source: Path, destination: Path) -> None:
    if source.is_symlink():
        fail(f"release packages cannot contain symbolic links: {source}")
    if source.is_dir():
        shutil.copytree(source, destination, symlinks=False)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def stage_package(
    addon: Path, output: Path, host_name: str, host: HostContract, godot_version: str, version: str
) -> tuple[Path, str]:
    package_name = f"gdpp-{godot_version}-{host_name}"
    stage_root = output / ".staging" / package_name
    if stage_root.exists():
        shutil.rmtree(stage_root)
    staged_addon = stage_root / "addons/gdpp"
    staged_addon.mkdir(parents=True)

    for relative in STATIC_ADDON_FILES:
        copy_path(addon / relative, staged_addon / relative)
    for filename in (host.compiler_library, host.fallback_library):
        copy_path(addon / "binary" / filename, staged_addon / "binary" / filename)

    source_sdk = addon / "sdk" / godot_version
    staged_sdk = staged_addon / "sdk" / godot_version
    for relative in HOST_SDK_PATHS:
        copy_path(source_sdk / relative, staged_sdk / relative)
    copy_path(source_sdk / "android/arm64", staged_sdk / "android/arm64")
    if host.platform == "macos":
        copy_path(source_sdk / "ios/arm64", staged_sdk / "ios/arm64")
    (staged_addon / "sdk/.gdignore").write_text("", encoding="utf-8")

    exports = ",".join(host.export_targets)
    (staged_addon / "PACKAGE_MANIFEST.txt").write_text(
        "GDPP_PACKAGE 1\n"
        f"version {version}\n"
        "compiler_godot_api 4.4\n"
        f"target_godot_api {godot_version}\n"
        f"host {host_name}\n"
        f"host_platform_minimum {host.platform_minimum}\n"
        f"export_targets {exports}\n"
        "android_platform_minimum Android_9_API_28\n"
        + ("ios_platform_minimum iOS_16.0\n" if host.platform == "macos" else ""),
        encoding="utf-8",
    )
    return stage_root, package_name


def iter_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_symlink():
            fail(f"release packages cannot contain symbolic links: {path}")
        if path.is_file():
            files.append(path)
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def create_zip(stage_root: Path, archive: Path) -> None:
    files = iter_files(stage_root)
    if not files:
        fail("refusing to create an empty release archive")
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix(archive.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in files:
            relative = path.relative_to(stage_root).as_posix()
            info = zipfile.ZipInfo(relative, FIXED_ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            with path.open("rb") as source, output.open(info, "w", force_zip64=True) as target:
                shutil.copyfileobj(source, target, length=1024 * 1024)
    temporary.replace(archive)

    with zipfile.ZipFile(archive) as packaged:
        names = packaged.namelist()
        if names != sorted(names) or len(names) != len(set(names)):
            fail(f"archive order or uniqueness check failed: {archive}")
        if any(name.startswith("/") or ".." in Path(name).parts for name in names):
            fail(f"archive contains an unsafe path: {archive}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    args = parse_args()
    addon = args.addon.resolve()
    output = args.output.resolve()
    source_root = Path(__file__).resolve().parent.parent
    host = HOSTS[args.host]
    try:
        require_descendant(output, source_root / "build", "release output")
        version = validate_source(addon, host, args.godot_version)
        stage_root, package_name = stage_package(
            addon, output, args.host, host, args.godot_version, version
        )
        archive = output / f"{package_name}.zip"
        create_zip(stage_root, archive)
        shutil.rmtree(stage_root)
        print(f"{archive}  sha256={sha256(archive)}")
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
