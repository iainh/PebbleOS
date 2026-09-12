# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from pbl.command import CommandError
from pbl.commands.trace import TraceCapture, capture_script, parse_capture


class TraceCaptureTests(unittest.TestCase):
    def test_parse_preserves_unsigned_arguments_and_names(self):
        capture = parse_capture("""Unrelated GDB output
PBLTRACE_LAYOUT 4 128
PBLTRACE_META 45 4 12 1 55
PBLTRACE_THREAD 8 App <TicToc>
PBLTRACE_RECORD 3 42 8 259 9 4294967295
PBLTRACE_END
""")
        self.assertEqual(capture["records"][0]["arg1"], 0xFFFFFFFF)
        self.assertEqual(capture["threads"][0]["name"], "App <TicToc>")
        self.assertEqual(capture["recording_mask"], 55)
        self.assertTrue(capture["frozen"])

    def test_rejects_incomplete_mismatched_and_corrupt_capture(self):
        for output in [
            "PBLTRACE_LAYOUT 4 128",
            "PBLTRACE_MISMATCH",
            "PBLTRACE_BAD_THREADS",
            "PBLTRACE_LAYOUT 8 128",
        ]:
            with self.subTest(output=output), self.assertRaises(ValueError):
                parse_capture(output)

    def test_target_cannot_inject_gdb_commands(self):
        for target in ["localhost:1234\nshell echo bad", "|command", "host 1234"]:
            with self.subTest(target=target), self.assertRaises(ValueError):
                capture_script(target, 0x8000000, b"abc")
        script = capture_script("localhost:1234", 0x8000000, b"abc")
        self.assertLess(
            script.index("if *(unsigned char *)"), script.index("set $thread")
        )
        self.assertIn("detach", script)
        self.assertNotIn("call ", script)

    def test_failed_second_snapshot_preserves_first_and_refuses_overwrite(self):
        command = TraceCapture()
        build = SimpleNamespace(
            config=SimpleNamespace(
                CONFIG_KERNEL_TRACE=True, CONFIG_KERNEL_TICK_HZ=1000, CONFIG_QEMU=True
            ),
            tool=lambda _: "gdb",
            elf="firmware.elf",
            board="test",
        )
        command.build_dir = Mock(return_value=build)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.json"
            command.workspace = SimpleNamespace(topdir=directory)
            args = SimpleNamespace(
                samples=2,
                interval=0,
                output=str(output),
                target="localhost:1234",
                workload="test",
            )
            first = SimpleNamespace(
                returncode=0,
                stdout=(
                    "PBLTRACE_LAYOUT 4 128\nPBLTRACE_META 10 2 0 0 55\n"
                    "PBLTRACE_RECORD 1 9 1 259 2 0\nPBLTRACE_END\n"
                ),
            )
            second = SimpleNamespace(returncode=1, stderr="connection lost")
            with (
                patch(
                    "pbl.commands.trace.elf_metadata", return_value=("abc", 1, b"x", [])
                ),
                patch(
                    "pbl.commands.trace.subprocess.run", side_effect=[first, second]
                ) as run,
            ):
                with self.assertRaises(CommandError):
                    command.do_run(args, [])
                saved = output.read_bytes()
                self.assertEqual(len(json.loads(saved)["samples"]), 1)
                with self.assertRaises(CommandError):
                    command.do_run(args, [])
                self.assertEqual(output.read_bytes(), saved)
                self.assertEqual(run.call_count, 2)
