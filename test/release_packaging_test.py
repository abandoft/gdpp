#!/usr/bin/env python3
"""Fast fixture tests for the 12-package release assembler."""

from __future__ import annotations

import hashlib
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


if len(sys.argv) < 3:
    raise SystemExit("usage: release_packaging_test.py SOURCE_ROOT BINARY_ROOT")
SOURCE_ROOT = Path(sys.argv.pop(1)).resolve()
BINARY_ROOT = Path(sys.argv.pop(1)).resolve()
sys.dont_write_bytecode = True
sys.path.insert(0, str(SOURCE_ROOT / "tools"))

import extract_changelog  # noqa: E402
import package_release  # noqa: E402


RUNTIME_FIELDS = {
    "runtime_abi": "2",
    "runtime_header_sha256": "a" * 64,
    "runtime_source_sha256": "b" * 64,
    "attached_runtime_header_sha256": "c" * 64,
    "attached_runtime_registry_source_sha256": "d" * 64,
    "attached_runtime_instance_source_sha256": "e" * 64,
    "attached_runtime_language_source_sha256": "f" * 64,
    "integer_semantics_header_sha256": "0" * 64,
}


def write(path: Path, content: str = "fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def manifest(path: Path, fields: dict[str, str]) -> None:
    body = f"GDPP_SDK {package_release.SDK_SCHEMA}\n" + "".join(
        f"{key} {value}\n" for key, value in fields.items()
    )
    write(path, body)


def create_addon(root: Path, host_name: str, godot_version: str = "4.6") -> Path:
    host = package_release.HOSTS[host_name]
    addon = root / "addons/gdpp"
    for relative in package_release.STATIC_ADDON_FILES:
        write(addon / relative)
    write(addon / "plugin.cfg", '[plugin]\nversion="1.0.0"\n')
    write(
        addon / "gdpp.gdextension",
        '[configuration]\ncompatibility_minimum = "4.4"\n',
    )
    write(addon / "binary" / host.compiler_library)
    write(addon / "binary" / host.fallback_library)
    write(addon / "binary/libgdpp_project.release.unwanted.so")

    sdk = addon / "sdk" / godot_version
    for directory in ("include", "src", "godot-cpp"):
        write(sdk / directory / "fixture")
    for profile in ("editor", "template_debug", "template_release"):
        write(sdk / "lib" / f"libgodot-cpp.{host.platform}.{profile}.{host.architecture}.a")
    manifest(
        sdk / "sdk.manifest",
        {
            "api": godot_version,
            "platform": host.platform,
            "arch": host.architecture,
            "profiles": "development,debug,release",
            "platform_minimum": host.platform_minimum,
            "gdpp_version": "1.0.0",
            **RUNTIME_FIELDS,
        },
    )

    android = sdk / "android/arm64"
    write(android / "lib/libgodot-cpp.android.template_debug.arm64.a")
    write(android / "lib/libgodot-cpp.android.template_release.arm64.a")
    manifest(
        android / "sdk.manifest",
        {
            "api": godot_version,
            "platform": "android",
            "arch": "arm64",
            "profiles": "debug,release",
            "platform_minimum": "Android_9",
            "android_api_level": "28",
            "gdpp_version": "1.0.0",
            **RUNTIME_FIELDS,
        },
    )

    if host.platform == "macos":
        ios = sdk / "ios/arm64"
        for target in ("device", "simulator"):
            write(ios / f"lib/{target}/libgodot-cpp.ios.template_debug.arm64.a")
            write(ios / f"lib/{target}/libgodot-cpp.ios.template_release.arm64.a")
        manifest(
            ios / "sdk.manifest",
            {
                "api": godot_version,
                "platform": "ios",
                "arch": "arm64",
                "profiles": "debug,release",
                "platform_minimum": "iOS_16.0",
                "ios_deployment_target": "16.0",
                "ios_slices": "device-arm64,simulator-arm64,simulator-x86_64",
                "gdpp_version": "1.0.0",
                **RUNTIME_FIELDS,
            },
        )
    write(sdk / "web/wasm32/unwanted")
    return addon


class ReleasePackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        BINARY_ROOT.mkdir(parents=True, exist_ok=True)
        self.temporary = Path(tempfile.mkdtemp(prefix="release-package-", dir=BINARY_ROOT))

    def tearDown(self) -> None:
        shutil.rmtree(self.temporary)

    def package(self, host_name: str, godot_version: str = "4.6") -> tuple[Path, list[str]]:
        addon = create_addon(
            self.temporary / f"{host_name}-{godot_version}", host_name, godot_version
        )
        host = package_release.HOSTS[host_name]
        version = package_release.validate_source(addon, host, godot_version)
        output = self.temporary / "output"
        stage, name = package_release.stage_package(
            addon, output, host_name, host, godot_version, version
        )
        archive = output / f"{name}.zip"
        package_release.create_zip(stage, archive)
        with zipfile.ZipFile(archive) as packaged:
            names = packaged.namelist()
        return archive, names

    def test_twelve_package_matrix_selects_only_required_targets(self) -> None:
        archives: set[str] = set()
        for godot_version in package_release.SUPPORTED_GODOT_VERSIONS:
            for host_name in package_release.HOSTS:
                with self.subTest(host=host_name, godot=godot_version):
                    archive, names = self.package(host_name, godot_version)
                    archives.add(archive.name)
                    self.assertEqual(
                        archive.name,
                        f"gdpp-{godot_version}-{host_name}.zip",
                    )
                    self.assertTrue(
                        any(
                            f"/sdk/{godot_version}/android/arm64/" in name
                            for name in names
                        )
                    )
                    self.assertEqual(
                        any(
                            f"/sdk/{godot_version}/ios/arm64/" in name for name in names
                        ),
                        host_name == "mac-arm64",
                    )
                    self.assertFalse(any("/web/" in name for name in names))
                    self.assertFalse(any("gdpp_project" in name for name in names))
                    self.assertTrue(any(name.endswith("sdk/.gdignore") for name in names))
        self.assertEqual(len(archives), 12)

    def test_zip_is_reproducible(self) -> None:
        archive, _ = self.package("linux-x64")
        first = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.unlink()
        second_archive, _ = self.package("linux-x64")
        second = hashlib.sha256(second_archive.read_bytes()).hexdigest()
        self.assertEqual(first, second)

    def test_minimum_platform_mismatch_fails_closed(self) -> None:
        addon = create_addon(self.temporary / "bad", "windows-x64")
        path = addon / "sdk/4.6/android/arm64/sdk.manifest"
        path.write_text(path.read_text(encoding="utf-8").replace("Android_9", "Android_8"))
        with self.assertRaisesRegex(ValueError, "platform_minimum"):
            package_release.validate_source(
                addon, package_release.HOSTS["windows-x64"], "4.6"
            )

    def test_changelog_uses_unprefixed_exact_version_section(self) -> None:
        content = "## 1.1.0\n\n- New\n\n## 1.0.0\n\n- Initial release\n"
        self.assertEqual(extract_changelog.extract(content, "1.0.0"), "- Initial release\n")
        with self.assertRaisesRegex(ValueError, "without a v prefix"):
            extract_changelog.extract(content, "v1.0.0")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            extract_changelog.extract(content, "2.0.0")


if __name__ == "__main__":
    unittest.main()
