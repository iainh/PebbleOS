# Kernel flight recorder

The optional kernel flight recorder retains recent scheduling and timing events
in RAM. Enable `CONFIG_KERNEL_TRACE` and set `CONFIG_KERNEL_TRACE_RECORDS` to
the required capacity (128 by default). Any capacity from 1 through 4096 is
valid; it does not need to be a power of two.
Tracing starts at boot when enabled. It is disabled by default in firmware.

Each fixed-size `struct pbl_trace_record` contains a tick timestamp, current
thread ID (zero when no thread is current), event ID, two machine-word
arguments, and a sequence number that wraps from `UINT32_MAX` to one. The recorder
uses no heap. Its static RAM cost is:

```
CONFIG_KERNEL_TRACE_RECORDS * sizeof(struct pbl_trace_record) + bookkeeping
```

The record is 24 bytes on 32-bit targets (3,072 bytes at the default capacity)
and 32 bytes on 64-bit targets. Bookkeeping is approximately 20 bytes on the watch.

## Control and extraction

Include `pbl/kernel/trace.h`. A typical setup is:

```c
pbl_trace_reset();
pbl_trace_set_mask(PBL_TRACE_CAT_SCHED | PBL_TRACE_CAT_FAULT);
pbl_trace_start();
```

`pbl_trace_stop()` pauses recording and `pbl_trace_start()` resumes it without
clearing records. `pbl_trace_freeze()` stops recording permanently until
`pbl_trace_reset()`; it is suitable for preserving a fault-time trace.
`pbl_trace_reset()` clears the ring, stops recording, and removes a freeze.
The category mask can be changed at runtime.

Call `pbl_trace_snapshot()` with an output array. Records are returned in
chronological order, including across sequence wrap. If the output is too small,
it receives the newest records.
The accompanying `pbl_trace_snapshot_info` reports records available, the
newest sequence, and how many records the ring has overwritten. Snapshotting
an active recorder briefly locks kernel-capable interrupts. A frozen snapshot
does not take that lock, allowing extraction from fault handling.

Normal thread and kernel-capable ISR producers are serialized with the
nestable `pbl_irq_lock()`. A record's sequence is published only after all its
other fields; a fault that freezes an interrupted write therefore leaves that
record out of snapshots. This does not make recording itself safe from NMI or
other interrupts above the kernel syscall priority. Fault handlers should
freeze rather than produce concurrently with an interrupted writer. The
context-switch hook uses `pbl_trace_record_locked()` because PendSV masks interrupts directly,
outside the interrupt-lock nesting counter.

App faults, kernel faults, stack overflows and core-dump entry freeze the recorder.
It stays frozen after an app is terminated until explicitly reset. The ring is
included in the existing full-RAM core dump when a dump is generated; this does
not add persistence to reboot paths that do not produce a dump. With matching
firmware symbols, GDB can inspect `'trace.c'::s_records` and
`'trace.c'::s_next_slot` in live RAM or a recovered dump. The next slot marks the
start of chronological traversal; skip records whose sequence is zero.

When `CONFIG_KERNEL_TRACE` is disabled, all functions are inline no-ops and
the recorder consumes no RAM.

## Hook arguments

Hooks call `pbl_trace_record(event, arg0, arg1)`. Argument meanings
are:

| Event | `arg0` | `arg1` |
| --- | --- | --- |
| `PBL_TRACE_SCHED_SWITCH` | previous thread ID | next thread ID |
| `PBL_TRACE_SCHED_BLOCK` | thread ID | timeout in ticks |
| `PBL_TRACE_SCHED_WAKE` | thread ID | signed wake result |
| `PBL_TRACE_SCHED_PRIORITY` | thread ID | `(old << 8) \| new` |
| `PBL_TRACE_MUTEX_CONTENTION` | mutex address | owner thread ID |
| `PBL_TRACE_QUEUE_FULL` | queue address | queue capacity |
| `PBL_TRACE_IDLE_REQUEST` | requested ticks | zero |
| `PBL_TRACE_IDLE_ELAPSED` | elapsed ticks | zero |
| `PBL_TRACE_TIMER_LATENESS` | ticks beyond the allowed slack window | manager-local timer ID |
| `PBL_TRACE_FAULT` | caller-defined fault code | caller-defined value |

`PBL_TRACE_FAULT` is available for thread-context diagnostics. Hardware fault
handlers freeze the ring without appending a record; existing crash metadata
supplies the fault details.

Addresses are stored as `uintptr_t`. Hook sites need no conditional
compilation because the disabled declarations compile away.

## Validation

The host tests exercise the scheduler with tracing enabled and disabled, plus
ring overwrite, filtering, truncated snapshots and fault-time publication:

```sh
pbl test -R '^(test_kernel|test_kernel_traced|test_trace|test_task_timer)$'
```

For an emulator build, use:

```sh
pbl configure --board qemu_emery -DCONFIG_KERNEL_TRACE=y
pbl build
pbl qemu
```

Measure recording overhead and wakeup frequency on hardware before enabling
tracing in production. An idle-request record describes the requested sleep
budget, not proof that the hardware entered deep sleep.
