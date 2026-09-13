# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Run the notification-to-display latency benchmark against PebbleOS QEMU."""

import argparse
import json
import math
import re
import socket
import statistics
import time
from pathlib import Path

from pebble import commander, pulse2

RESULT_RE = re.compile(
    rb"LATENCY_RESULT version=(?P<version>\d+)"
    rb" total_us=(?P<total_us>\d+)"
    rb" storage_us=(?P<storage_us>\d+)"
    rb" dispatch_us=(?P<dispatch_us>\d+)"
    rb" render_us=(?P<render_us>\d+)"
    rb" flush_us=(?P<flush_us>\d+)"
    rb" rows=(?P<rows>\d+)"
)
QUEUE_RESULT_RE = re.compile(
    rb"QUEUE_RESULT version=(?P<version>\d+)"
    rb" total_us=(?P<total_us>\d+)"
    rb" checksum=(?P<checksum>\d+)"
    rb" jobs=(?P<jobs>\d+)"
    rb" probes=(?P<probes>\d+)"
)
HEAP_RESULT_RE = re.compile(
    rb"HEAP_RESULT version=(?P<version>\d+)"
    rb" total_us=(?P<total_us>\d+)"
    rb" checksum=(?P<checksum>\d+)"
    rb" cycles=(?P<cycles>\d+)"
    rb" stable=(?P<stable>\d+)"
)
SCALER_RESULT_RE = re.compile(
    rb"SCALER_RESULT version=(?P<version>\d+)"
    rb" total_us=(?P<total_us>\d+)"
    rb" checksum=(?P<checksum>\d+)"
    rb" rows=(?P<rows>\d+)"
)
FIELDS = ("total_us", "storage_us", "dispatch_us", "render_us", "flush_us", "rows")
QUEUE_INTEGRITY = {"checksum": 4073865016, "jobs": 96, "probes": 2048}
HEAP_INTEGRITY = {"checksum": 904026885, "cycles": 2048}
SCALER_INTEGRITY = {"checksum": 3412336069, "rows": 4096}


def _read_until(sock: socket.socket, marker: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while marker not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                f"timed out waiting for {marker!r}; received {bytes(data)!r}"
            )
        sock.settimeout(remaining)
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("QEMU console closed")
        data.extend(chunk)
    return bytes(data)


def _monitor_command(path: Path, command: str) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect(str(path))
        _read_until(sock, b"(qemu) ", 5)
        sock.sendall(command.encode() + b"\n")
        _read_until(sock, b"(qemu) ", 5)


def _percentile(values: list[int], percentile: float) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(percentile * len(ordered)) - 1)]


def _summarize(
    results: list[dict[str, int]], fields: tuple[str, ...] = FIELDS
) -> dict[str, dict[str, int]]:
    return {
        field: {
            "min": min(values := [result[field] for result in results]),
            "median": int(statistics.median(values)),
            "p95": _percentile(values, 0.95),
            "max": max(values),
        }
        for field in fields
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument(
        "--mode",
        choices=("synthetic", "storage", "damage", "queue", "heap", "scaler"),
        default="synthetic",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=12345)
    parser.add_argument("--monitor", type=Path, default=Path("build/qemu-mon.sock"))
    parser.add_argument(
        "--output", type=Path, help="write complete JSON results to this path"
    )
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")

    results = []
    interface = pulse2.Interface.open_dbgserial(url=f"socket://{args.host}:{args.port}")
    try:
        link = interface.get_link(timeout=30)
        if link is None:
            raise TimeoutError("timed out establishing the QEMU PULSE connection")
        prompt = commander.apps.Prompt(link)
        for iteration in range(args.iterations):
            command_timeout = 30 if args.mode == "storage" else 15
            response = "\n".join(
                prompt.command_and_response(
                    f"latency benchmark {args.mode}", timeout=command_timeout
                )
            ).encode()
            result_re = {
                "queue": QUEUE_RESULT_RE,
                "heap": HEAP_RESULT_RE,
                "scaler": SCALER_RESULT_RE,
            }.get(args.mode, RESULT_RE)
            match = result_re.search(response)
            if not match:
                raise RuntimeError(
                    f"benchmark failed on iteration {iteration + 1}: {response!r}"
                )
            fields = (
                ("total_us", "checksum", "jobs", "probes")
                if args.mode == "queue"
                else (
                    ("total_us", "checksum", "cycles", "stable")
                    if args.mode == "heap"
                    else (
                        ("total_us", "checksum", "rows")
                        if args.mode == "scaler"
                        else FIELDS
                    )
                )
            )
            result = {field: int(match.group(field)) for field in fields}
            if args.mode == "queue":
                actual_integrity = {field: result[field] for field in QUEUE_INTEGRITY}
                if actual_integrity != QUEUE_INTEGRITY:
                    raise RuntimeError(
                        "queue integrity check failed: "
                        f"expected {QUEUE_INTEGRITY}, got {actual_integrity}"
                    )
            elif args.mode == "heap":
                actual_integrity = {field: result[field] for field in HEAP_INTEGRITY}
                if actual_integrity != HEAP_INTEGRITY:
                    raise RuntimeError(
                        "heap integrity check failed: "
                        f"expected {HEAP_INTEGRITY}, got {actual_integrity}"
                    )
            elif args.mode == "scaler":
                actual_integrity = {field: result[field] for field in SCALER_INTEGRITY}
                if actual_integrity != SCALER_INTEGRITY:
                    raise RuntimeError(
                        "scaler integrity check failed: "
                        f"expected {SCALER_INTEGRITY}, got {actual_integrity}"
                    )
            results.append(result)
            detail = (
                f"checksum={result['checksum']}"
                if args.mode in ("queue", "heap", "scaler")
                else f"rows={result['rows']}"
            )
            print(f"{iteration + 1:02d}: total={result['total_us']} us, {detail}")

            if args.mode not in ("damage", "queue", "heap", "scaler"):
                # Let the notification transition settle, then dismiss it before the next sample.
                time.sleep(0.5)
                _monitor_command(args.monitor, "sendkey left")
                time.sleep(0.5)
        prompt.close()
    finally:
        # Let the receive loop observe closure before PySerial drops its socket.
        interface.closed = True
        interface.receive_thread.join(timeout=1)
        interface.iostream.close()

    summary_fields = (
        ("total_us",) if args.mode in ("queue", "heap", "scaler") else FIELDS
    )
    report = {
        "schema_version": 1,
        "clock": "firmware RTC; QEMU values are virtual time",
        "iterations": results,
        "summary_us": _summarize(results, summary_fields),
    }
    print(json.dumps(report["summary_us"], indent=2))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
