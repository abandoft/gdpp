#!/usr/bin/env python3
"""Unit tests for deterministic Chromium profile cleanup."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SOURCE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE_ROOT / "tools"))

import run_web_browser_smoke as smoke  # noqa: E402


class BrowserProfileCleanupTest(unittest.TestCase):
    def test_retries_transient_directory_race(self) -> None:
        path = Path("profile")
        remove = mock.Mock(
            side_effect=[
                OSError(39, "Directory not empty"),
                OSError(39, "Directory not empty"),
                None,
            ]
        )

        with mock.patch.object(smoke.shutil, "rmtree", remove), mock.patch.object(
            smoke.time, "sleep"
        ) as sleep:
            smoke.remove_directory_with_retries(path, attempts=3, initial_delay=0.1)

        self.assertEqual(remove.call_count, 3)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [0.1, 0.2])

    def test_propagates_persistent_cleanup_failure(self) -> None:
        error = OSError(39, "Directory not empty")
        with mock.patch.object(smoke.shutil, "rmtree", side_effect=error), mock.patch.object(
            smoke.time, "sleep"
        ) as sleep:
            with self.assertRaisesRegex(OSError, "Directory not empty"):
                smoke.remove_directory_with_retries(
                    Path("profile"), attempts=2, initial_delay=0.0
                )

        sleep.assert_called_once_with(0.0)

    def test_profile_context_removes_created_directory(self) -> None:
        with smoke.browser_profile() as profile:
            self.assertTrue(profile.is_dir())
            marker = profile / "Default" / "marker"
            marker.parent.mkdir()
            marker.write_text("evidence", encoding="utf-8")

        self.assertFalse(profile.exists())

    def test_missing_directory_is_already_clean(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "missing"
            smoke.remove_directory_with_retries(missing)


if __name__ == "__main__":
    unittest.main()
