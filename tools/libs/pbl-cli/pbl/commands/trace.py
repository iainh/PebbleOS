# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Read the flight recorder without executing code on the stopped target."""

import json
import re
import shutil
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

from pbl.command import CommandError, PblCommand
from pbl.trace_analysis import FORMAT, analyze, render_report


def elf_metadata(path):
    from elftools.elf.elffile import ELFFile

    with open(path, "rb") as source:
        elf = ELFFile(source)
        if elf.elfclass != 32 or not elf.little_endian:
            raise ValueError("trace capture requires a 32-bit little-endian ELF")
        note = elf.get_section_by_name(".note.gnu.build-id")
        if note is None:
            raise ValueError("ELF has no GNU build ID")
        build_id = next(
            n["n_desc"] for n in note.iter_notes() if n["n_type"] == "NT_GNU_BUILD_ID"
        )
        symbols = elf.get_section_by_name(".symtab")
        objects = []
        if symbols:
            for symbol in symbols.iter_symbols():
                if symbol["st_info"]["type"] == "STT_OBJECT" and symbol["st_size"]:
                    objects.append((symbol["st_value"], symbol["st_size"], symbol.name))
        return build_id, note["sh_addr"], note.data(), objects


def capture_script(target, note_address, note_data):
    if not re.fullmatch(r"[A-Za-z0-9_.:\[\]-]+", target):
        raise ValueError("target must be a GDB TCP host:port, without whitespace")
    # Check bytes before following any firmware pointers. No target function calls.
    matches = " && ".join(
        f"*(unsigned char *){note_address + i} == {value}"
        for i, value in enumerate(note_data)
    )
    return f"""set pagination off
set confirm off
set remotetimeout 10
target remote {target}
if {matches}
  printf "PBLTRACE_LAYOUT %u %u\\n", (unsigned int)sizeof(void *), (unsigned int)(sizeof('trace.c'::s_records) / sizeof('trace.c'::s_records[0]))
  printf "PBLTRACE_META %u %u %u %u %u\\n", s_ticks, 'trace.c'::s_next_sequence, 'trace.c'::s_overwritten, 'trace.c'::s_frozen, 'trace.c'::s_mask
  set $thread = pbl_all_threads
  set $count = 0
  while $thread != 0 && $count < 256
    printf "PBLTRACE_THREAD %u %.16s\\n", $thread->id, $thread->name
    set $thread = $thread->backend.all_next
    set $count = $count + 1
  end
  if $thread != 0
    printf "PBLTRACE_BAD_THREADS\\n"
  end
  set $capacity = sizeof('trace.c'::s_records) / sizeof('trace.c'::s_records[0])
  set $i = 0
  while $i < $capacity
    set $r = &'trace.c'::s_records[('trace.c'::s_next_slot + $i) % $capacity]
    if $r->sequence != 0
      printf "PBLTRACE_RECORD %u %u %u %u %u %u\\n", $r->sequence, $r->timestamp, $r->thread_id, $r->event, $r->arg0, $r->arg1
    end
    set $i = $i + 1
  end
  printf "PBLTRACE_END\\n"
else
  printf "PBLTRACE_MISMATCH\\n"
end
detach
"""


def parse_capture(output):
    if "PBLTRACE_MISMATCH" in output:
        raise ValueError("running firmware does not match the ELF build ID")
    if "PBLTRACE_BAD_THREADS" in output:
        raise ValueError("thread list is corrupt or exceeds the capture limit")
    sample = {"threads": [], "records": []}
    ended = False
    for line in output.splitlines():
        if line.startswith("PBLTRACE_LAYOUT "):
            pointer_bytes, capacity = map(int, line.split()[1:])
            if pointer_bytes != 4 or not 1 <= capacity <= 4096:
                raise ValueError("unsupported trace layout")
            sample["capacity"] = capacity
        elif line.startswith("PBLTRACE_META "):
            tick, sequence, overwritten, frozen, mask = map(int, line.split()[1:])
            sample.update(
                uptime_ticks=tick,
                next_sequence=sequence,
                overwritten=overwritten,
                frozen=bool(frozen),
                recording_mask=mask,
            )
        elif line.startswith("PBLTRACE_THREAD "):
            _, ident, name = line.split(" ", 2)
            sample["threads"].append({"id": int(ident), "name": name})
        elif line.startswith("PBLTRACE_RECORD "):
            values = list(map(int, line.split()[1:]))
            keys = ("sequence", "timestamp", "thread_id", "event", "arg0", "arg1")
            if len(values) != len(keys):
                raise ValueError("malformed trace record")
            sample["records"].append(dict(zip(keys, values)))
        elif line == "PBLTRACE_END":
            ended = True
    if not ended or "capacity" not in sample or "next_sequence" not in sample:
        raise ValueError(
            "incomplete GDB capture; enable CONFIG_KERNEL_TRACE and use a matching ELF"
        )
    return sample


class TraceCapture(PblCommand):
    group = "device"

    def __init__(self):
        super().__init__(
            "trace-capture",
            "Export flight-recorder snapshots using GDB",
            "Temporarily halt the target for each snapshot, verify the ELF build ID, "
            "read the ring and detach. Does not call target functions or freeze the ring.",
        )

    def do_add_parser(self, parser_adder):
        parser = self.add_subparser(parser_adder)
        parser.add_argument(
            "--target", default="localhost:1234", help="GDB server host:port"
        )
        parser.add_argument(
            "--output", required=True, help="New JSON capture file (never overwritten)"
        )
        parser.add_argument(
            "--workload", required=True, help="Description of the exercised workload"
        )
        parser.add_argument("--samples", type=int, default=1)
        parser.add_argument(
            "--interval",
            type=float,
            default=0.25,
            help="Seconds of running time between snapshots (default: %(default)s)",
        )
        return parser

    def do_run(self, args, unknown):
        if not 1 <= args.samples <= 1000 or not 0 <= args.interval <= 3600:
            self.die("samples must be 1..1000 and interval must be 0..3600 seconds")
        build = self.build_dir()
        if not build.config.CONFIG_KERNEL_TRACE:
            self.die("enable CONFIG_KERNEL_TRACE in this build first")
        gdb = build.tool("gdb") or shutil.which("arm-none-eabi-gdb")
        if not gdb:
            self.die("arm-none-eabi-gdb not found")
        output = Path(args.output)
        if output.exists():
            self.die(f"capture already exists: {output}")
        try:
            build_id, address, note, objects = elf_metadata(build.elf)
            script = capture_script(args.target, address, note)
        except (OSError, ValueError, StopIteration) as exc:
            raise CommandError(str(exc)) from exc
        if self.dry_run:
            self.inf(
                f"[dry-run] {gdb}: read {args.samples} snapshot(s) from {args.target} into {output}"
            )
            return
        document = {
            "format": FORMAT,
            "tick_hz": build.config.CONFIG_KERNEL_TICK_HZ,
            # Match RTC_TICKS_HZ in pbl/drivers/rtc.h, not the scheduler clock.
            "timer_tick_hz": 1000
            if build.config.CONFIG_QEMU or build.config.CONFIG_SOC_SF32LB52
            else 1024,
            "pointer_bits": 32,
            "build_id": build_id,
            "board": build.board,
            "workload": args.workload,
            "samples": [],
            "symbols": {},
        }
        self.wrn(
            "GDB sampling briefly halts the target and perturbs timing; it is not a power measurement."
        )
        try:
            # Reserve the output before touching the target; retain completed samples on failure.
            with (
                output.open("x") as destination,
                tempfile.TemporaryDirectory(prefix="pbl-trace-") as temp,
            ):
                commands = Path(temp) / "capture.gdb"
                commands.write_text(script)
                try:
                    for index in range(args.samples):
                        if index:
                            time.sleep(args.interval)
                        result = subprocess.run(
                            [
                                gdb,
                                "-nx",
                                "-q",
                                "-batch",
                                "-iex",
                                "set auto-load off",
                                build.elf,
                                "-x",
                                str(commands),
                            ],
                            cwd=self.topdir,
                            capture_output=True,
                            text=True,
                            timeout=60,
                            check=False,
                        )
                        if result.returncode:
                            raise ValueError(
                                f"GDB capture failed: {result.stderr.strip()}"
                            )
                        sample = parse_capture(result.stdout)
                        sample["captured_at"] = datetime.now(timezone.utc).isoformat()
                        document["samples"].append(sample)
                        for record in sample["records"]:
                            if record["event"] in (0x201, 0x401):
                                for base, size, name in objects:
                                    if base <= record["arg0"] < base + size:
                                        offset = record["arg0"] - base
                                        document["symbols"][str(record["arg0"])] = (
                                            f"{name}+0x{offset:x}"
                                        )
                                        break
                        self.inf(
                            f"Snapshot {index + 1}/{args.samples}: {len(sample['records'])} records"
                        )
                finally:
                    json.dump(document, destination, indent=2)
                    destination.write("\n")
            analyze(document)
        except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
            raise CommandError(
                f"{exc}. Completed snapshots are retained in {output}; "
                "if GDB failed, check whether the target needs to be resumed."
            ) from exc
        self.inf(f"Saved {output}")


class TraceReport(PblCommand):
    group = "other"

    def __init__(self):
        super().__init__("trace-report", "Analyze an exported kernel trace")

    def do_add_parser(self, parser_adder):
        parser = self.add_subparser(parser_adder)
        parser.add_argument("capture", help="JSON from trace-capture")
        parser.add_argument("--output", help="New report file (default: stdout)")
        parser.add_argument(
            "--json", action="store_true", help="Emit machine-readable analysis"
        )
        return parser

    def do_run(self, args, unknown):
        if self.dry_run:
            self.inf(f"[dry-run] analyze {args.capture}")
            return
        try:
            report = analyze(json.loads(Path(args.capture).read_text()))
            rendered = (
                json.dumps(report, indent=2) + "\n"
                if args.json
                else render_report(report)
            )
            if args.output:
                with Path(args.output).open("x") as output:
                    output.write(rendered)
            else:
                print(rendered, end="")
        except (OSError, ValueError) as exc:
            raise CommandError(str(exc)) from exc
