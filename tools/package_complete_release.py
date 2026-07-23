#!/usr/bin/env python3
"""Assemble one install-ready GDPP package for every supported host and export target."""

from __future__ import annotations

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

import package_release


WEB_VARIANTS = ("nothreads", "threads")
RUNTIME_FILES = {
    "runtime_header_sha256": "include/gdpp/runtime/variant_ops.hpp",
    "reference_semantics_header_sha256": "include/gdpp/runtime/reference_semantics.hpp",
    "runtime_source_sha256": "src/runtime/variant_ops.cpp",
    "attached_runtime_header_sha256": "include/gdpp/runtime/attached_script.hpp",
    "attached_runtime_registry_source_sha256": "src/runtime/attached_script_registry.cpp",
    "attached_runtime_instance_source_sha256": "src/runtime/attached_script_instance.cpp",
    "attached_runtime_language_source_sha256": "src/runtime/attached_script_language.cpp",
    "integer_semantics_header_sha256": "include/gdpp/numeric/integer_semantics.hpp",
}
RUNTIME_FIELDS = ("runtime_abi", *RUNTIME_FILES)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--components", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--godot-version", choices=package_release.SUPPORTED_GODOT_VERSIONS, required=True
    )
    return parser.parse_args()


def fail(message: str) -> None:
    raise ValueError(message)


def require_no_symlinks(root: Path) -> None:
    if root.is_symlink():
        fail(f"complete package component cannot be a symbolic link: {root}")
    if not root.exists():
        fail(f"complete package component is missing: {root}")
    for path in root.rglob("*"):
        if path.is_symlink():
            fail(f"complete package component contains a symbolic link: {path}")


def require_runtime_contract(
    sdk: Path,
    fields: dict[str, str],
    expected: dict[str, str] | None,
) -> dict[str, str]:
    contract = {field: fields.get(field, "") for field in RUNTIME_FIELDS}
    if any(not value for value in contract.values()):
        fail(f"SDK runtime contract is incomplete: {sdk / 'sdk.manifest'}")
    if expected is not None and contract != expected:
        fail(f"SDK runtime contract conflicts with another complete-package component: {sdk}")
    for field, relative in RUNTIME_FILES.items():
        path = sdk / relative
        if not path.is_file():
            fail(f"SDK runtime file is missing: {path}")
        if package_release.sha256(path) != fields[field]:
            fail(f"SDK runtime file does not match {field}: {path}")
    return contract


def validate_static_addon(addon: Path, reference: Path | None) -> str:
    if addon.name != "gdpp" or addon.parent.name != "addons":
        fail(f"host component path must end in addons/gdpp: {addon}")
    require_no_symlinks(addon)
    for relative in package_release.STATIC_ADDON_FILES:
        path = addon / relative
        if not path.is_file():
            fail(f"host component is missing static addon file: {path}")
        if reference is not None and path.read_bytes() != (reference / relative).read_bytes():
            fail(f"host components disagree on static addon file: {relative}")
    version = package_release.read_plugin_version(addon / "plugin.cfg")
    if package_release.read_extension_minimum(addon / "gdpp.gdextension") != "4.4":
        fail("complete package compiler GDExtension must retain the Godot 4.4 baseline")
    return version


def validate_host_sdk(
    sdk: Path,
    host: package_release.HostContract,
    godot_version: str,
    gdpp_version: str,
    runtime_contract: dict[str, str] | None,
) -> dict[str, str]:
    for relative in package_release.HOST_SDK_PATHS:
        if not (sdk / relative).exists():
            fail(f"host SDK component is missing: {sdk / relative}")
    manifest = sdk / "sdk.manifest"
    schema, fields = package_release.read_sdk_manifest(manifest)
    package_release.require_fields(
        manifest,
        schema,
        fields,
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
            "gdpp_version": gdpp_version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "static" if host.platform == "windows" else "not_applicable",
        },
    )
    if host.platform == "windows":
        if fields.get("compiler") != "MSVC" or not fields.get("compiler_version", "").startswith(
            "19."
        ):
            fail(f"complete package Windows SDK must use an MSVC 19.x toolset: {manifest}")
    package_release.require_profile_libraries(sdk / "lib", ("editor", "template_release"))
    return require_runtime_contract(sdk, fields, runtime_contract)


def validate_android_sdk(
    sdk: Path,
    godot_version: str,
    gdpp_version: str,
    runtime_contract: dict[str, str],
) -> None:
    require_no_symlinks(sdk)
    manifest = sdk / "sdk.manifest"
    schema, fields = package_release.read_sdk_manifest(manifest)
    package_release.require_fields(
        manifest,
        schema,
        fields,
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
            "gdpp_version": gdpp_version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "not_applicable",
        },
    )
    package_release.require_profile_libraries(sdk / "lib", ("template_release",))
    require_runtime_contract(sdk, fields, runtime_contract)


def validate_ios_sdk(
    sdk: Path,
    godot_version: str,
    gdpp_version: str,
    runtime_contract: dict[str, str],
) -> None:
    require_no_symlinks(sdk)
    manifest = sdk / "sdk.manifest"
    schema, fields = package_release.read_sdk_manifest(manifest)
    package_release.require_fields(
        manifest,
        schema,
        fields,
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
            "source_paths": "mapped",
            "gdpp_version": gdpp_version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "not_applicable",
        },
    )
    package_release.require_profile_libraries(sdk / "lib/device", ("template_release",))
    package_release.require_profile_libraries(sdk / "lib/simulator", ("template_release",))
    require_runtime_contract(sdk, fields, runtime_contract)


def validate_web_sdk(
    sdk: Path,
    godot_version: str,
    variant: str,
    gdpp_version: str,
    runtime_contract: dict[str, str],
) -> None:
    require_no_symlinks(sdk)
    manifest = sdk / "sdk.manifest"
    schema, fields = package_release.read_sdk_manifest(manifest)
    package_release.require_fields(
        manifest,
        schema,
        fields,
        {
            "api": godot_version,
            "platform": "web",
            "arch": "wasm32",
            "profiles": "debug,release",
            "distribution_binding": "template_release",
            "distribution_optimization": "Release",
            "platform_minimum": "none",
            "web_threads": variant,
            "source_paths": "mapped",
            "compiler": "Emscripten",
            "gdpp_version": gdpp_version,
            "cxx_standard": "17",
            "exceptions": "disabled",
            "msvc_runtime": "not_applicable",
        },
    )
    package_release.require_profile_libraries(sdk / "lib", ("template_release",))
    archives = [path.name for path in (sdk / "lib").glob("*.a")]
    if variant == "nothreads" and not any(".nothreads." in name for name in archives):
        fail(f"single-threaded Web SDK archive is not marked nothreads: {sdk}")
    if variant == "threads" and any(".nothreads." in name for name in archives):
        fail(f"multi-threaded Web SDK contains a nothreads archive: {sdk}")
    require_runtime_contract(sdk, fields, runtime_contract)


def host_component(components: Path, host: str, godot_version: str) -> Path:
    return components / f"gdpp-host-{host}-godot-{godot_version}" / "addons/gdpp"


def target_component(components: Path, target: str, godot_version: str) -> Path:
    return (
        components
        / f"gdpp-target-{target}-godot-{godot_version}"
        / "addons/gdpp/sdk"
        / godot_version
        / target.split("-", 1)[0]
    )


def stage_complete_package(
    components: Path, output: Path, godot_version: str
) -> tuple[Path, str, str]:
    reference_addon: Path | None = None
    gdpp_version = ""
    runtime_contract: dict[str, str] | None = None

    for host_name, host in package_release.HOSTS.items():
        addon = host_component(components, host_name, godot_version)
        component_version = validate_static_addon(addon, reference_addon)
        if not gdpp_version:
            gdpp_version = component_version
            reference_addon = addon
        elif component_version != gdpp_version:
            fail(
                f"host component GDPP version mismatch: expected {gdpp_version}, "
                f"got {component_version} in {addon}"
            )
        for filename in (host.compiler_library, host.fallback_library):
            if not (addon / "binary" / filename).is_file():
                fail(f"host component binary is missing: {addon / 'binary' / filename}")
        runtime_contract = validate_host_sdk(
            addon / "sdk" / godot_version,
            host,
            godot_version,
            gdpp_version,
            runtime_contract,
        )

    if reference_addon is None or runtime_contract is None:
        fail("complete package has no host components")

    package_name = f"gdpp-{godot_version}"
    stage_root = output / ".staging" / package_name
    if stage_root.exists():
        shutil.rmtree(stage_root)
    staged_addon = stage_root / "addons/gdpp"
    staged_addon.mkdir(parents=True)

    for relative in package_release.STATIC_ADDON_FILES:
        package_release.copy_path(reference_addon / relative, staged_addon / relative)

    copied_binaries: set[str] = set()
    for host_name, host in package_release.HOSTS.items():
        addon = host_component(components, host_name, godot_version)
        for filename in (host.compiler_library, host.fallback_library):
            if filename in copied_binaries:
                fail(f"complete package host binary name collides: {filename}")
            package_release.copy_path(
                addon / "binary" / filename, staged_addon / "binary" / filename
            )
            copied_binaries.add(filename)

    for host_name, host in package_release.HOSTS.items():
        source = host_component(components, host_name, godot_version) / "sdk" / godot_version
        destination = (
            staged_addon / "sdk" / godot_version / host.platform / host.architecture
        )
        for relative in package_release.HOST_SDK_PATHS:
            package_release.copy_path(source / relative, destination / relative)

    android = target_component(components, "android-arm64", godot_version) / "arm64"
    validate_android_sdk(android, godot_version, gdpp_version, runtime_contract)
    package_release.copy_path(
        android, staged_addon / "sdk" / godot_version / "android/arm64"
    )

    ios = target_component(components, "ios-arm64", godot_version) / "arm64"
    validate_ios_sdk(ios, godot_version, gdpp_version, runtime_contract)
    package_release.copy_path(ios, staged_addon / "sdk" / godot_version / "ios/arm64")

    for variant in WEB_VARIANTS:
        web = components / f"gdpp-web-godot-{godot_version}-{variant}"
        validate_web_sdk(web, godot_version, variant, gdpp_version, runtime_contract)
        package_release.copy_path(
            web,
            staged_addon / "sdk" / godot_version / "web/wasm32" / variant,
        )

    (staged_addon / "sdk/.gdignore").write_text("", encoding="utf-8")
    (staged_addon / "PACKAGE_MANIFEST.txt").write_text(
        "GDPP_PACKAGE 2\n"
        "kind complete\n"
        f"version {gdpp_version}\n"
        "compiler_godot_api 4.4\n"
        f"target_godot_api {godot_version}\n"
        "editor_hosts macos-arm64,linux-x86_64,windows-x86_64\n"
        "export_targets macos-arm64,linux-x86_64,windows-x86_64,android-arm64,"
        "ios-arm64,web-wasm32-nothreads,web-wasm32-threads\n"
        "android_platform_minimum Android_9_API_28\n"
        "ios_platform_minimum iOS_16.0\n"
        "sdk_layout platform-scoped\n",
        encoding="utf-8",
    )
    validate_complete_stage(staged_addon, gdpp_version, godot_version)
    return stage_root, package_name, gdpp_version


def validate_complete_stage(addon: Path, gdpp_version: str, godot_version: str) -> None:
    require_no_symlinks(addon)
    expected_binaries = {
        filename
        for host in package_release.HOSTS.values()
        for filename in (host.compiler_library, host.fallback_library)
    }
    actual_binaries = {path.name for path in (addon / "binary").iterdir() if path.is_file()}
    if actual_binaries != expected_binaries:
        fail(
            "complete package must contain exactly the three supported desktop compiler and "
            f"fallback pairs; expected {sorted(expected_binaries)}, got {sorted(actual_binaries)}"
        )
    if package_release.read_plugin_version(addon / "plugin.cfg") != gdpp_version:
        fail("complete package metadata version changed during staging")
    sdk_versions = sorted(path.name for path in (addon / "sdk").iterdir() if path.is_dir())
    if sdk_versions != [godot_version]:
        fail(f"complete package must contain only Godot SDK {godot_version}, got {sdk_versions}")
    version_root = addon / "sdk" / godot_version
    if (version_root / "sdk.manifest").exists():
        fail(f"complete package cannot contain an ambiguous root host manifest: {version_root}")
    for host in package_release.HOSTS.values():
        if not (
            version_root / host.platform / host.architecture / "sdk.manifest"
        ).is_file():
            fail(f"complete package host SDK is missing for {host.platform}/{host.architecture}")
    for relative in (
        "android/arm64/sdk.manifest",
        "ios/arm64/sdk.manifest",
        "web/wasm32/nothreads/sdk.manifest",
        "web/wasm32/threads/sdk.manifest",
    ):
        if not (version_root / relative).is_file():
            fail(f"complete package target SDK is missing: {version_root / relative}")
    forbidden = [
        path
        for path in addon.rglob("*")
        if "template_debug" in path.name
        or path.name == ".DS_Store"
        or path.name.startswith("._")
        or "gdpp_project" in path.name
        or path.name == "build"
    ]
    if forbidden:
        fail(f"complete package contains forbidden generated or debug products: {forbidden[:5]}")


def main() -> int:
    args = parse_args()
    components = args.components.resolve()
    output = args.output.resolve()
    source_root = Path(__file__).resolve().parent.parent
    try:
        package_release.require_descendant(components, source_root / "build", "component root")
        package_release.require_descendant(output, source_root / "build", "release output")
        stage_root, package_name, _ = stage_complete_package(
            components, output, args.godot_version
        )
        archive = output / f"{package_name}.zip"
        package_release.create_zip(stage_root, archive)
        shutil.rmtree(stage_root)
        print(f"{archive}  sha256={package_release.sha256(archive)}")
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"complete release packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
