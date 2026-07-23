#!/usr/bin/env python3
"""Fixture tests for the per-Godot-version complete plugin package."""

from __future__ import annotations

import hashlib
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


if len(sys.argv) < 3:
    raise SystemExit("usage: complete_release_packaging_test.py SOURCE_ROOT BINARY_ROOT")
SOURCE_ROOT = Path(sys.argv.pop(1)).resolve()
BINARY_ROOT = Path(sys.argv.pop(1)).resolve()
sys.dont_write_bytecode = True
sys.path.insert(0, str(SOURCE_ROOT / "tools"))

import package_complete_release  # noqa: E402
import package_release  # noqa: E402


RUNTIME_CONTENT = {
    "runtime_header_sha256": ("include/gdpp/runtime/variant_ops.hpp", "runtime-header"),
    "reference_semantics_header_sha256": (
        "include/gdpp/runtime/reference_semantics.hpp",
        "reference-semantics-header",
    ),
    "runtime_source_sha256": ("src/runtime/variant_ops.cpp", "runtime-source"),
    "attached_runtime_header_sha256": (
        "include/gdpp/runtime/attached_script.hpp",
        "attached-header",
    ),
    "attached_runtime_registry_source_sha256": (
        "src/runtime/attached_script_registry.cpp",
        "attached-registry",
    ),
    "attached_runtime_instance_source_sha256": (
        "src/runtime/attached_script_instance.cpp",
        "attached-instance",
    ),
    "attached_runtime_language_source_sha256": (
        "src/runtime/attached_script_language.cpp",
        "attached-language",
    ),
    "integer_semantics_header_sha256": (
        "include/gdpp/numeric/integer_semantics.hpp",
        "integer-semantics",
    ),
}
RUNTIME_FIELDS = {
    "runtime_abi": "2",
    **{
        field: hashlib.sha256(content.encode("utf-8")).hexdigest()
        for field, (_, content) in RUNTIME_CONTENT.items()
    },
}
COMMON_FIELDS = {
    "distribution_binding": "template_release",
    "distribution_optimization": "Release",
    "gdpp_version": "1.7.7",
    "cxx_standard": "17",
    "exceptions": "disabled",
    **RUNTIME_FIELDS,
}


def write(path: Path, content: str = "fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_manifest(path: Path, fields: dict[str, str]) -> None:
    write(
        path,
        f"GDPP_SDK {package_release.SDK_SCHEMA}\n"
        + "".join(f"{key} {value}\n" for key, value in fields.items()),
    )


def write_runtime(sdk: Path) -> None:
    for relative, content in RUNTIME_CONTENT.values():
        write(sdk / relative, content)
    write(sdk / "godot-cpp/include/godot_cpp/classes/object.hpp")
    write(sdk / "godot-cpp/gen/include/godot_cpp/classes/node.hpp")
    write(sdk / "godot-cpp/LICENSE.md")


def create_host_component(root: Path, host_name: str, godot_version: str) -> Path:
    host = package_release.HOSTS[host_name]
    addon = root / f"gdpp-host-{host_name}-godot-{godot_version}" / "addons/gdpp"
    for relative in package_release.STATIC_ADDON_FILES:
        write(addon / relative)
    write(addon / "plugin.cfg", '[plugin]\nversion="1.7.7"\n')
    write(addon / "gdpp.gdextension", '[configuration]\ncompatibility_minimum = "4.4"\n')
    write(addon / "binary" / host.compiler_library, f"compiler-{host_name}")
    write(addon / "binary" / host.fallback_library, f"fallback-{host_name}")

    sdk = addon / "sdk" / godot_version
    write_runtime(sdk)
    extension = ".lib" if host.platform == "windows" else ".a"
    prefix = "libgodot-cpp" if host.platform != "windows" else "libgodot-cpp"
    write(
        sdk
        / "lib"
        / f"{prefix}.{host.platform}.template_release.{host.architecture}{extension}"
    )
    write_manifest(
        sdk / "sdk.manifest",
        {
            "api": godot_version,
            "platform": host.platform,
            "arch": host.architecture,
            "profiles": "debug,release",
            "platform_minimum": host.platform_minimum,
            "msvc_runtime": "static" if host.platform == "windows" else "not_applicable",
            **(
                {"compiler": "MSVC", "compiler_version": "19.44.35207.1"}
                if host.platform == "windows"
                else {"compiler": "Clang", "compiler_version": "18.1.0"}
            ),
            **COMMON_FIELDS,
        },
    )
    return addon


def create_target_sdk(root: Path, target: str, godot_version: str) -> Path:
    sdk = (
        root
        / f"gdpp-target-{target}-godot-{godot_version}"
        / "addons/gdpp/sdk"
        / godot_version
        / target.split("-", 1)[0]
        / "arm64"
    )
    write_runtime(sdk)
    if target == "android-arm64":
        write(sdk / "lib/libgodot-cpp.android.template_release.arm64.a")
        fields = {
            "api": godot_version,
            "platform": "android",
            "arch": "arm64",
            "profiles": "debug,release",
            "platform_minimum": "Android_9",
            "android_api_level": "28",
            "android_stl": "c++_shared",
            "msvc_runtime": "not_applicable",
            **COMMON_FIELDS,
        }
    else:
        write(sdk / "lib/device/libgodot-cpp.ios.template_release.arm64.a")
        write(sdk / "lib/simulator/libgodot-cpp.ios.template_release.universal.a")
        fields = {
            "api": godot_version,
            "platform": "ios",
            "arch": "arm64",
            "profiles": "debug,release",
            "platform_minimum": "iOS_16.0",
            "ios_deployment_target": "16.0",
            "ios_slices": "device-arm64,simulator-arm64,simulator-x86_64",
            "source_paths": "mapped",
            "msvc_runtime": "not_applicable",
            **COMMON_FIELDS,
        }
    write_manifest(sdk / "sdk.manifest", fields)
    return sdk


def create_web_sdk(root: Path, godot_version: str, variant: str) -> Path:
    sdk = root / f"gdpp-web-godot-{godot_version}-{variant}"
    write_runtime(sdk)
    suffix = ".nothreads" if variant == "nothreads" else ""
    write(sdk / "lib" / f"libgodot-cpp.web.template_release.wasm32{suffix}.a")
    write_manifest(
        sdk / "sdk.manifest",
        {
            "api": godot_version,
            "platform": "web",
            "arch": "wasm32",
            "profiles": "debug,release",
            "platform_minimum": "none",
            "web_threads": variant,
            "source_paths": "mapped",
            "compiler": "Emscripten",
            "compiler_version": "4.0.0",
            "msvc_runtime": "not_applicable",
            **COMMON_FIELDS,
        },
    )
    return sdk


def create_components(root: Path, godot_version: str) -> None:
    for host_name in package_release.HOSTS:
        create_host_component(root, host_name, godot_version)
    create_target_sdk(root, "android-arm64", godot_version)
    create_target_sdk(root, "ios-arm64", godot_version)
    for variant in package_complete_release.WEB_VARIANTS:
        create_web_sdk(root, godot_version, variant)


class CompleteReleasePackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        BINARY_ROOT.mkdir(parents=True, exist_ok=True)
        # Complete-package fixtures intentionally reproduce the deepest installed SDK paths.
        # Keeping another temporary root below the already-deep CMake build directory exceeds
        # Win32 MAX_PATH before the packager itself is exercised.
        self.temporary = Path(tempfile.mkdtemp(prefix="gdpp-complete-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.temporary)

    def stage(self, godot_version: str = "4.6") -> tuple[Path, str]:
        components = self.temporary / "components"
        create_components(components, godot_version)
        stage, name, version = package_complete_release.stage_complete_package(
            components, self.temporary / "release", godot_version
        )
        self.assertEqual(version, "1.7.7")
        return stage, name

    def test_each_sdk_version_has_one_install_ready_complete_package(self) -> None:
        for godot_version in package_release.SUPPORTED_GODOT_VERSIONS:
            with self.subTest(godot=godot_version):
                stage, name = self.stage(godot_version)
                self.assertEqual(name, f"gdpp-{godot_version}")
                addon = stage / "addons/gdpp"
                binaries = {path.name for path in (addon / "binary").iterdir()}
                self.assertEqual(len(binaries), 6)
                for host in package_release.HOSTS.values():
                    self.assertTrue(
                        (
                            addon
                            / "sdk"
                            / godot_version
                            / host.platform
                            / host.architecture
                            / "sdk.manifest"
                        ).is_file()
                    )
                self.assertTrue(
                    (addon / f"sdk/{godot_version}/android/arm64/sdk.manifest").is_file()
                )
                self.assertTrue(
                    (addon / f"sdk/{godot_version}/ios/arm64/sdk.manifest").is_file()
                )
                self.assertTrue(
                    (addon / f"sdk/{godot_version}/web/wasm32/threads/sdk.manifest").is_file()
                )
                self.assertFalse((addon / f"sdk/{godot_version}/sdk.manifest").exists())
                self.assertEqual(
                    [path.name for path in (addon / "sdk").iterdir() if path.is_dir()],
                    [godot_version],
                )
                self.assertIn("kind complete", (addon / "PACKAGE_MANIFEST.txt").read_text())
                shutil.rmtree(stage.parent.parent)

    def test_complete_zip_is_reproducible_and_contains_files_not_nested_archives(self) -> None:
        stage, name = self.stage()
        archive = self.temporary / "first.zip"
        package_release.create_zip(stage, archive)
        first = hashlib.sha256(archive.read_bytes()).hexdigest()
        with zipfile.ZipFile(archive) as packaged:
            names = packaged.namelist()
        self.assertFalse(any(path.endswith(".zip") for path in names))
        self.assertFalse(any("template_debug" in path for path in names))
        self.assertIn("addons/gdpp/build_progress.gd", names)
        self.assertIn("addons/gdpp/native_build_job.gd", names)
        shutil.rmtree(stage)

        components = self.temporary / "components"
        second_stage, second_name, _ = package_complete_release.stage_complete_package(
            components, self.temporary / "second", "4.6"
        )
        self.assertEqual(second_name, name)
        second_archive = self.temporary / "second.zip"
        package_release.create_zip(second_stage, second_archive)
        self.assertEqual(first, hashlib.sha256(second_archive.read_bytes()).hexdigest())

    def test_missing_desktop_host_fails_closed(self) -> None:
        components = self.temporary / "missing"
        create_components(components, "4.6")
        shutil.rmtree(components / "gdpp-host-windows-x64-godot-4.6")
        with self.assertRaisesRegex(ValueError, "component is missing"):
            package_complete_release.stage_complete_package(
                components, self.temporary / "release", "4.6"
            )

    def test_static_addon_conflict_fails_closed(self) -> None:
        components = self.temporary / "conflict"
        create_components(components, "4.6")
        write(
            components
            / "gdpp-host-linux-x64-godot-4.6/addons/gdpp/export_plugin.gd",
            "conflicting export plugin",
        )
        with self.assertRaisesRegex(ValueError, "static addon file"):
            package_complete_release.stage_complete_package(
                components, self.temporary / "release", "4.6"
            )

    def test_runtime_contract_conflict_fails_closed(self) -> None:
        components = self.temporary / "abi-conflict"
        create_components(components, "4.6")
        manifest = (
            components
            / "gdpp-web-godot-4.6-threads/sdk.manifest"
        )
        manifest.write_text(
            manifest.read_text(encoding="utf-8").replace("runtime_abi 2", "runtime_abi 3"),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(ValueError, "runtime contract conflicts"):
            package_complete_release.stage_complete_package(
                components, self.temporary / "release", "4.6"
            )

    def test_single_target_artifacts_download_into_named_component_roots(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for target in ("android-arm64", "ios-arm64"):
            artifact = f"gdpp-target-{target}-godot-${{{{ matrix.godot }}}}"
            self.assertIn(f"name: {artifact}\n", workflow)
            self.assertIn(f"path: build/complete-components/{artifact}\n", workflow)


if __name__ == "__main__":
    unittest.main()
