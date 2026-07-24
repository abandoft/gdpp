#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


SOURCE_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = SOURCE_ROOT / "tools/verify_export_immutability.py"
SPEC = importlib.util.spec_from_file_location("verify_export_immutability", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ExportImmutabilityTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.project = Path(self.temporary.name) / "project"
        (self.project / ".godot").mkdir(parents=True)
        (self.project / "addons/gdpp/build/project").mkdir(parents=True)
        (self.project / "addons/vendor").mkdir(parents=True)
        (self.project / "addons/gdpp/gdpp.gdextension").write_text(
            "compiler\n", encoding="utf-8"
        )
        (self.project / "addons/vendor/vendor.gdextension").write_text(
            "provider\n", encoding="utf-8"
        )
        (self.project / "addons/gdpp/build/project/generated.gdextension").write_text(
            "generated-before\n", encoding="utf-8"
        )
        (self.project / ".godot/extension_list.cfg").write_text(
            "res://addons/gdpp/gdpp.gdextension\n"
            "res://addons/vendor/vendor.gdextension\n",
            encoding="utf-8",
        )
        self.state = Path(self.temporary.name) / "snapshot.json"
        MODULE.snapshot(self.project, self.state)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_generated_build_descriptor_changes_are_ignored(self) -> None:
        generated = self.project / "addons/gdpp/build/project/generated.gdextension"
        generated.write_text("generated-after\n", encoding="utf-8")
        MODULE.verify(self.project, self.state)

    def test_customer_descriptor_mutation_is_rejected(self) -> None:
        provider = self.project / "addons/vendor/vendor.gdextension"
        provider.write_text("mutated\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "modified descriptor"):
            MODULE.verify(self.project, self.state)

    def test_registry_mutation_is_rejected(self) -> None:
        registry = self.project / ".godot/extension_list.cfg"
        registry.write_text("", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "modified descriptor or metadata"):
            MODULE.verify(self.project, self.state)

    def test_transaction_backup_is_rejected(self) -> None:
        backup = self.project / ".godot/gdpp_compiler_descriptor.export-backup"
        backup.write_text("compiler\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "transaction backups"):
            MODULE.verify(self.project, self.state)


if __name__ == "__main__":
    unittest.main()
