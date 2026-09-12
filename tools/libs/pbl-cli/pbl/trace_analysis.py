# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
# JSON schema violations use ValueError consistently.
# ruff: noqa: TRY004

"""Analysis of snapshots produced by the Pebble kernel trace recorder.

This module deliberately does no I/O.  In particular, counts describe records
which were observed, rather than estimates of activity between snapshots.
"""

import itertools
import math
import statistics
from collections import Counter, defaultdict

FORMAT = "pebble-kernel-trace-v1"
UINT32_MAX = (1 << 32) - 1

EVENT_NAMES = {
    0x101: "switch",
    0x102: "block",
    0x103: "wake",
    0x104: "priority",
    0x201: "contention",
    0x401: "queue_full",
    0x801: "idle_request",
    0x802: "idle_elapsed",
    0x1001: "lateness",
}


def _uint(value, field, maximum=UINT32_MAX):
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 <= value <= maximum
    ):
        raise ValueError(f"{field} must be an integer from 0 to {maximum}")
    return value


def _distance(old, new, *, skips_zero=False):
    """Return a forward uint32 distance, rejecting ambiguous/reversed values."""
    distance = (new - old) & UINT32_MAX
    if skips_zero and new < old:
        distance -= 1
    if distance < 0 or distance > (1 << 31):
        raise ValueError("values are not in forward order")
    return distance


def _record_key(record):
    return tuple(
        record[name]
        for name in ("sequence", "timestamp", "thread_id", "event", "arg0", "arg1")
    )


def _label(thread_id, names):
    return names.get(thread_id, f"unknown-{thread_id}")


def _summary(values, tick_hz):
    if not values:
        return {
            "count": 0,
            "max_ticks": None,
            "median_ticks": None,
            "p95_ticks": None,
            "max_seconds": None,
            "median_seconds": None,
            "p95_seconds": None,
        }
    ordered = sorted(values)
    median = statistics.median(ordered)
    p95 = ordered[math.ceil(0.95 * len(ordered)) - 1]
    return {
        "count": len(values),
        "max_ticks": max(values),
        "median_ticks": median,
        "p95_ticks": p95,
        "max_seconds": max(values) / tick_hz if tick_hz else None,
        "median_seconds": median / tick_hz if tick_hz else None,
        "p95_seconds": p95 / tick_hz if tick_hz else None,
    }


def analyze(document):
    """Validate and analyse a ``pebble-kernel-trace-v1`` document.

    The returned dictionary contains only JSON-compatible values.
    ``complete_coverage`` is intentionally conservative: rates are emitted only
    when no overwrite, sequence gap, reset boundary, or disconnected snapshot
    was seen.
    """
    if not isinstance(document, dict) or document.get("format") != FORMAT:
        raise ValueError(f"format must be {FORMAT!r}")
    tick_hz = _uint(document.get("tick_hz"), "tick_hz")
    if tick_hz == 0:
        raise ValueError("tick_hz must be positive")
    timer_tick_hz = document.get("timer_tick_hz")
    if timer_tick_hz is not None and not _uint(timer_tick_hz, "timer_tick_hz"):
        raise ValueError("timer_tick_hz must be positive")
    if document.get("pointer_bits") != 32:
        raise ValueError("pointer_bits must be 32")
    for field in ("build_id", "board", "workload"):
        if not isinstance(document.get(field), str):
            raise ValueError(f"{field} must be a string")
    samples = document.get("samples")
    if not isinstance(samples, list):
        raise ValueError("samples must be a list")

    names = {}
    snapshots = []
    overwritten = 0
    for si, sample in enumerate(samples):
        if not isinstance(sample, dict):
            raise ValueError(f"samples[{si}] must be an object")
        for field in ("uptime_ticks", "next_sequence", "overwritten", "capacity"):
            _uint(sample.get(field), f"samples[{si}].{field}")
        if "recording_mask" in sample:
            _uint(sample["recording_mask"], "recording_mask")
        if sample["next_sequence"] == 0:
            raise ValueError("next_sequence cannot be zero")
        if not isinstance(sample.get("frozen"), bool):
            raise ValueError("frozen must be boolean")
        threads = sample.get("threads")
        records = sample.get("records")
        if not isinstance(threads, list) or not isinstance(records, list):
            raise ValueError("threads and records must be lists")
        if len(records) > sample["capacity"]:
            raise ValueError("record count exceeds snapshot capacity")
        for thread in threads:
            if not isinstance(thread, dict):
                raise ValueError("thread must be an object")
            tid = _uint(thread.get("id"), "thread.id")
            name = thread.get("name")
            if not isinstance(name, str):
                raise ValueError("thread.name must be a string")
            if tid in names and names[tid] != name:
                raise ValueError(f"thread {tid} has conflicting names")
            names[tid] = name
        clean = []
        previous = None
        for ri, record in enumerate(records):
            if not isinstance(record, dict) or set(record) != {
                "sequence",
                "timestamp",
                "thread_id",
                "event",
                "arg0",
                "arg1",
            }:
                raise ValueError(f"samples[{si}].records[{ri}] has invalid fields")
            for field in record:
                _uint(
                    record[field],
                    f"record.{field}",
                    0xFFFF if field == "event" else UINT32_MAX,
                )
            if record["sequence"] == 0:
                raise ValueError("record sequence cannot be zero")
            if previous is not None:
                if (
                    _distance(previous["sequence"], record["sequence"], skips_zero=True)
                    == 0
                ):
                    raise ValueError("duplicate sequence within a snapshot")
                _distance(previous["timestamp"], record["timestamp"])
            previous = record
            clean.append(dict(record))
        overwritten = max(overwritten, sample["overwritten"])
        snapshots.append((sample, clean))

    # Merge rolling snapshots.  An identical overlap anchors a snapshot.  With
    # no anchor we retain its records but put them beyond a hard boundary.
    timeline = []
    boundaries = set()  # indices at which a new, non-contiguous region starts
    resets = 0
    disconnected = 0
    missing_between = 0
    for _sample, records in snapshots:
        if not records:
            continue
        positions = {_record_key(record): i for i, record in enumerate(timeline)}
        overlap = [
            (ri, positions[_record_key(record)])
            for ri, record in enumerate(records)
            if _record_key(record) in positions
        ]
        if overlap:
            ri, ti = overlap[-1]
            # Existing portions must agree in order; otherwise the sequence was
            # reused after a reset and this is a separate region.
            if any(
                positions.get(_record_key(records[j])) != ti - (ri - j)
                for j in range(ri + 1)
            ):
                overlap = []
            else:
                timeline.extend(records[ri + 1 :])
                continue
        if timeline:
            try:
                distance = _distance(
                    timeline[-1]["sequence"], records[0]["sequence"], skips_zero=True
                )
                _distance(timeline[-1]["timestamp"], records[0]["timestamp"])
                if distance == 0:
                    raise ValueError("sequence reused")
                missing_between += distance - 1
            except ValueError:
                resets += 1
                distance = 0
            if distance != 1:
                boundaries.add(len(timeline))
                disconnected += 1
        timeline.extend(records)

    # Split at explicit boundaries and sequence gaps.  Timestamp rollover is
    # normal; a backwards jump of half the uint32 range is corrupt.
    segments = []
    missing = missing_between
    current = []
    for index, record in enumerate(timeline):
        if current:
            if index in boundaries:
                segments.append(current)
                current = []
            else:
                seq_distance = _distance(
                    current[-1]["sequence"], record["sequence"], skips_zero=True
                )
                _distance(current[-1]["timestamp"], record["timestamp"])
                if seq_distance != 1:
                    missing += seq_distance - 1
                    segments.append(current)
                    current = []
        current.append(record)
    if current:
        segments.append(current)

    counts = Counter(
        EVENT_NAMES.get(r["event"], f"unknown_0x{r['event']:x}") for r in timeline
    )
    wakes = Counter()
    delays = defaultdict(list)
    censored = Counter()
    residence = Counter()
    contention = Counter()
    queues = Counter()
    lateness = defaultdict(list)
    idle = []
    spans = []
    for segment in segments:
        pending = defaultdict(list)
        if segment:
            spans.append(_distance(segment[0]["timestamp"], segment[-1]["timestamp"]))
        for record in segment:
            event = record["event"]
            if event == 0x103:
                target = record["arg0"]
                wakes[target] += 1
                censored[target] += len(pending.pop(target, []))
                pending[target].append(record["timestamp"])
            elif event == 0x102:
                censored[record["arg0"]] += len(pending.pop(record["arg0"], []))
            elif event == 0x101:
                target = record["arg1"]
                for timestamp in pending.pop(target, []):
                    delays[target].append(_distance(timestamp, record["timestamp"]))
            elif event == 0x201:
                contention[(record["arg0"], record["arg1"])] += 1
            elif event == 0x401:
                queues[record["arg0"]] += 1
            elif event == 0x801:
                idle.append(record["arg0"])
            elif event == 0x1001:
                lateness[(record["thread_id"], record["arg1"])].append(record["arg0"])
        for target, values in pending.items():
            censored[target] += len(values)
        switches = [record for record in segment if record["event"] == 0x101]
        for first, second in itertools.pairwise(switches):
            if first["arg1"] == second["arg0"]:
                residence[first["arg1"]] += _distance(
                    first["timestamp"], second["timestamp"]
                )

    thread_ids = set(names) | set(wakes) | set(delays) | set(censored) | set(residence)
    thread_rows = []
    for tid in sorted(thread_ids):
        thread_rows.append(
            {
                "id": tid,
                "name": _label(tid, names),
                "wake_count": wakes[tid],
                "wake_to_switch": _summary(delays[tid], tick_hz),
                "unmatched_censored_wakes": censored[tid],
                "residence_ticks": residence[tid],
                "residence_seconds": residence[tid] / tick_hz,
            }
        )
    complete = (
        bool(timeline)
        and overwritten == 0
        and missing == 0
        and disconnected == 0
        and resets == 0
    )
    span_ticks = sum(spans)
    rates = None
    if complete and span_ticks:
        rates = {
            name: count * tick_hz / span_ticks for name, count in sorted(counts.items())
        }
    return {
        "format": FORMAT,
        "metadata": {
            key: document[key]
            for key in ("tick_hz", "pointer_bits", "build_id", "board", "workload")
        },
        "sample_count": len(samples),
        "recording_masks": sorted(
            {s["recording_mask"] for s in samples if "recording_mask" in s}
        ),
        "record_count": len(timeline),
        "event_counts": dict(sorted(counts.items())),
        "observed_contiguous_span_ticks": span_ticks,
        "observed_contiguous_span_seconds": span_ticks / tick_hz,
        "contiguous_segment_count": len(segments),
        "missing_record_count": missing,
        "overwritten_record_count": overwritten,
        "reset_boundary_count": resets,
        "complete_coverage": complete,
        "event_rates_per_second": rates,
        "threads": thread_rows,
        "mutex_contention": [
            {
                "address": address,
                "owner_id": owner,
                "owner_name": _label(owner, names),
                "count": count,
            }
            for (address, owner), count in sorted(contention.items())
        ],
        "full_queues": [
            {"address": address, "pressure_count": count}
            for address, count in sorted(queues.items())
        ],
        "timer_lateness": [
            {
                "thread_id": tid,
                "thread_name": _label(tid, names),
                "timer_id": timer,
                **_summary(values, timer_tick_hz),
            }
            for (tid, timer), values in sorted(lateness.items())
        ],
        "idle_requests": {
            "count": len(idle),
            "total_ticks": sum(idle),
            "max_ticks": max(idle) if idle else None,
        },
        "warnings": [
            "Timing is quantized to one kernel tick.",
            "Only enabled recording categories are represented; an absent event is not proof of absence.",
            "Thread residence is approximate and includes interrupt execution time.",
            "GDB sampling halts the target and can perturb timing.",
            "Trace snapshots provide incomplete workload coverage; counts are observed evidence only."
            if not complete
            else "Coverage applies only to the observed capture interval.",
            "Queue-full events measure pressure, not lost messages.",
            "Idle requests are requested durations, not proof of actual deep sleep.",
        ],
    }


def render_report(report):
    """Render an :func:`analyze` result as a compact Markdown report."""
    meta = report["metadata"]
    lines = [
        "# Pebble kernel trace analysis",
        "",
        f"- Board: `{meta['board']}`",
        f"- Workload: `{meta['workload']}`",
        f"- Build: `{meta['build_id']}`",
        f"- Tick frequency: {meta['tick_hz']} Hz",
        f"- Samples / unique records: {report['sample_count']} / {report['record_count']}",
        f"- Observed contiguous span: {report['observed_contiguous_span_ticks']} ticks",
        f"- Contiguous recorded-event coverage: {'yes' if report['complete_coverage'] else 'no'}",
        f"- Recording masks: {report['recording_masks']} (idle category is bit 3)",
        "",
        "## Event counts",
        "",
        "| Event | Count |",
        "|---|---:|",
    ]
    lines += [f"| {name} | {count} |" for name, count in report["event_counts"].items()]
    if not report["event_counts"]:
        lines.append("| *(none)* | 0 |")
    lines += [
        "",
        "## Threads",
        "",
        "| Thread | Wakes | Matched | Max delay (ticks) | Median | P95 | Censored | Residence (ticks) |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in report["threads"]:
        delay = row["wake_to_switch"]

        def value(key, delay=delay):
            return "—" if delay[key] is None else str(delay[key])

        lines.append(
            f"| {row['name']} (`{row['id']}`) | {row['wake_count']} | {delay['count']} | {value('max_ticks')} | {value('median_ticks')} | {value('p95_ticks')} | {row['unmatched_censored_wakes']} | {row['residence_ticks']} |"
        )
    if not report["threads"]:
        lines.append("| *(none)* | 0 | 0 | — | — | — | 0 | 0 |")
    lines += ["", "## Resource evidence", ""]
    lines.append("### Mutex contention")
    lines += [
        f"- `0x{x['address']:08x}`: {x['count']} (owner {x['owner_name']} / `{x['owner_id']}`)"
        for x in report["mutex_contention"]
    ] or ["- None observed."]
    lines += ["", "### Full queues"]
    lines += [
        f"- `0x{x['address']:08x}`: {x['pressure_count']} pressure events"
        for x in report["full_queues"]
    ] or ["- None observed."]
    lines += ["", "### Timer lateness"]
    lines += [
        f"- {x['thread_name']} / timer `{x['timer_id']}`: {x['count']} samples, max {x['max_ticks']} RTC ticks"
        for x in report["timer_lateness"]
    ] or ["- None observed."]
    idle = report["idle_requests"]
    lines += [
        "",
        "### Requested idle durations",
        f"- {idle['count']} requests; {idle['total_ticks']} ticks total (not measured deep sleep).",
        "",
        "## Warnings",
        "",
    ]
    lines += [f"- {warning}" for warning in report["warnings"]]
    return "\n".join(lines) + "\n"
