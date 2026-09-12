import unittest

from pbl.trace_analysis import analyze, render_report


def rec(sequence, timestamp, event, thread=1, arg0=0, arg1=0):
    return {
        "sequence": sequence,
        "timestamp": timestamp,
        "thread_id": thread,
        "event": event,
        "arg0": arg0,
        "arg1": arg1,
    }


def document(samples):
    return {
        "format": "pebble-kernel-trace-v1",
        "tick_hz": 100,
        "pointer_bits": 32,
        "build_id": "abc",
        "board": "test",
        "workload": "asymmetric",
        "samples": samples,
    }


def sample(records, **changes):
    value = {
        "uptime_ticks": 100,
        "next_sequence": 99,
        "overwritten": 0,
        "frozen": False,
        "capacity": 16,
        "threads": [{"id": 1, "name": "runner"}, {"id": 2, "name": "worker"}],
        "records": records,
    }
    value.update(changes)
    return value


class TraceAnalysisTests(unittest.TestCase):
    def test_exact_metrics_and_overlapping_snapshots(self):
        records = [
            rec(1, 10, 0x103, arg0=2),
            rec(2, 13, 0x101, arg0=1, arg1=2),
            rec(3, 19, 0x101, thread=2, arg0=2, arg1=1),
            rec(4, 21, 0x103, arg0=2),
            rec(5, 30, 0x102),
        ]
        report = analyze(document([sample(records[:3]), sample(records[1:])]))
        self.assertEqual(report["record_count"], 5)
        self.assertEqual(report["event_counts"], {"block": 1, "switch": 2, "wake": 2})
        worker = next(row for row in report["threads"] if row["id"] == 2)
        self.assertEqual(worker["wake_to_switch"]["count"], 1)
        self.assertEqual(worker["wake_to_switch"]["max_ticks"], 3)
        self.assertEqual(worker["unmatched_censored_wakes"], 1)
        self.assertEqual(worker["residence_ticks"], 6)
        self.assertEqual(report["observed_contiguous_span_ticks"], 20)

    def test_gap_censors_and_suppresses_rates(self):
        records = [rec(10, 2, 0x103, arg0=2), rec(12, 8, 0x101, arg0=1, arg1=2)]
        report = analyze(document([sample(records)]))
        self.assertEqual(report["missing_record_count"], 1)
        self.assertEqual(report["contiguous_segment_count"], 2)
        self.assertEqual(report["threads"][1]["wake_to_switch"]["count"], 0)
        self.assertEqual(report["threads"][1]["unmatched_censored_wakes"], 1)
        self.assertIsNone(report["event_rates_per_second"])

    def test_sequence_and_timestamp_rollover(self):
        records = [
            rec(0xFFFFFFFF, 0xFFFFFFFE, 0x101, arg0=1, arg1=2),
            rec(1, 1, 0x104),
            rec(2, 3, 0x101, thread=2, arg0=2, arg1=1),
        ]
        report = analyze(document([sample(records)]))
        self.assertEqual(report["missing_record_count"], 0)
        self.assertEqual(report["observed_contiguous_span_ticks"], 5)
        self.assertEqual(
            next(x for x in report["threads"] if x["id"] == 2)["residence_ticks"], 5
        )

    def test_reset_is_a_hard_boundary(self):
        before = [rec(20, 90, 0x103, arg0=2)]
        after = [rec(1, 2, 0x101, arg0=1, arg1=2)]
        report = analyze(document([sample(before), sample(after)]))
        self.assertEqual(report["reset_boundary_count"], 1)
        worker = next(row for row in report["threads"] if row["id"] == 2)
        self.assertEqual(worker["wake_to_switch"]["count"], 0)
        self.assertEqual(worker["unmatched_censored_wakes"], 1)

    def test_adjacent_snapshots_wrap_without_reset(self):
        report = analyze(
            document(
                [
                    sample([rec(0xFFFFFFFF, 0xFFFFFFFE, 0x103, arg0=2)]),
                    sample([rec(1, 3, 0x101, arg0=1, arg1=2)]),
                ]
            )
        )
        self.assertEqual(report["reset_boundary_count"], 0)
        self.assertEqual(report["threads"][1]["wake_to_switch"]["max_ticks"], 5)

    def test_gap_between_snapshots_counts_missing(self):
        report = analyze(
            document(
                [
                    sample([rec(10, 1, 0x103, arg0=2)]),
                    sample([rec(14, 6, 0x101, arg0=1, arg1=2)]),
                ]
            )
        )
        self.assertEqual(report["missing_record_count"], 3)
        self.assertEqual(report["threads"][1]["wake_to_switch"]["count"], 0)

    def test_block_censors_old_wake(self):
        report = analyze(
            document(
                [
                    sample(
                        [
                            rec(1, 1, 0x103, arg0=2),
                            rec(2, 4, 0x102, arg0=2),
                            rec(3, 8, 0x103, arg0=2),
                            rec(4, 10, 0x101, arg0=1, arg1=2),
                        ]
                    )
                ]
            )
        )
        self.assertEqual(report["threads"][1]["wake_to_switch"]["max_ticks"], 2)
        self.assertEqual(report["threads"][1]["unmatched_censored_wakes"], 1)

    def test_contention_queue_timer_and_idle_attribution(self):
        records = [
            rec(1, 1, 0x201, thread=2, arg0=0x1000, arg1=1),
            rec(2, 2, 0x201, thread=2, arg0=0x1000, arg1=1),
            rec(3, 3, 0x401, arg0=0x2000),
            rec(4, 4, 0x801, arg0=17),
            rec(5, 5, 0x1001, thread=2, arg0=7, arg1=42),
        ]
        report = analyze(document([sample(records)]))
        self.assertEqual(
            report["mutex_contention"],
            [{"address": 0x1000, "owner_id": 1, "owner_name": "runner", "count": 2}],
        )
        self.assertEqual(report["full_queues"][0]["pressure_count"], 1)
        self.assertEqual(report["timer_lateness"][0]["thread_id"], 2)
        self.assertEqual(report["timer_lateness"][0]["timer_id"], 42)
        self.assertEqual(report["timer_lateness"][0]["max_ticks"], 7)
        self.assertEqual(report["idle_requests"]["total_ticks"], 17)
        self.assertIn("not measured deep sleep", render_report(report))

    def test_empty_and_zero_duration(self):
        empty = analyze(document([sample([])]))
        self.assertEqual(empty["record_count"], 0)
        self.assertIsNone(empty["event_rates_per_second"])
        zero = analyze(document([sample([rec(1, 4, 0x103, arg0=99)])]))
        self.assertEqual(zero["observed_contiguous_span_ticks"], 0)
        self.assertEqual(
            next(x for x in zero["threads"] if x["id"] == 99)["name"], "unknown-99"
        )
        self.assertIsNone(zero["event_rates_per_second"])

    def test_timer_duration_uses_rtc_not_scheduler_frequency(self):
        trace = document([sample([rec(1, 4, 0x1001, arg0=512, arg1=1)])])
        self.assertIsNone(analyze(trace)["timer_lateness"][0]["max_seconds"])
        trace["timer_tick_hz"] = 1024
        self.assertEqual(analyze(trace)["timer_lateness"][0]["max_seconds"], 0.5)

    def test_malformed_records_and_timestamps(self):
        bad = rec(1, 1, 0x101)
        bad["extra"] = 2
        with self.assertRaises(ValueError):
            analyze(document([sample([bad])]))
        with self.assertRaises(ValueError):
            analyze(document([sample([rec(1, 10, 0x101), rec(2, 9, 0x101)])]))
        with self.assertRaises(ValueError):
            analyze({**document([]), "tick_hz": 0})
        with self.assertRaises(ValueError):
            analyze({**document([]), "pointer_bits": 64})


if __name__ == "__main__":
    unittest.main()
