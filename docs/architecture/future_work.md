# Performance-oriented Rust work

This page records candidate PebbleOS subsystem rewrites where a new algorithm,
rather than a change of implementation language alone, could improve visible
latency, animation smoothness or battery life. Each implementation should use
`no_std` Rust, fixed-capacity storage and a narrow C ABI, following the existing
graphics and codec modules.

Except where results are recorded below, these are proposals rather than
measured conclusions. Establish a real-hardware baseline before starting each
rewrite and retain the existing implementation behind a Kconfig option until
the replacement is equivalent and faster.

## Compositor and damage tracking

Pebble tracks the smallest rectangle containing all changed pixels. Two small,
separated changes can therefore cause most of the framebuffer to be redrawn,
and several transitions deliberately dirty the complete framebuffer every
frame. Legacy app scaling also runs nested per-pixel loops.

Fuchsia's Forma-based UI retains ordered composition state, caches unchanged
geometry and rebuilds damaged ranges. Pebble should adapt the idea rather than
the implementation:

- represent damage with a fixed 8×8 or 16×16 tile bitmap;
- retain immutable transition regions and commonly reused rasters;
- specialize Rust scanline and scaling kernels for Pebble's pixel formats;
- merge adjacent dirty tiles when sending rows to the display.

Measure CPU time from render request through display completion, dirty pixels
per frame, missed animation deadlines and display-bus bytes. Tile metadata and
cache storage must cost less energy than the avoided rendering and transfer.

The first implementation is available behind `CONFIG_COMPOSITOR_DAMAGE_RUST`.
It uses a fixed bitmap of 16-row tiles in each framebuffer and a `no_std` Rust
core to retain disjoint damage. The display sends complete rows, so horizontal
tiles would add bookkeeping without reducing transfers. A 260×260 framebuffer
adds 4 bytes of damage metadata and can skip clean tile rows between dirty
regions.

Obelix retains the bounding-rectangle path. Its JDI driver converts supplied
rows in place before submitting one contiguous region, so skipping rows inside
that region would display unconverted pixels. Getafix retains its forced
full-screen updates because partial updates cause animation issues.

## PFS and notification storage

PFS performs page traversal and flash I/O under a global mutex. Allocation can
trigger an erase lasting seconds. Notification storage can scan and rewrite its
entire fixed-size file when it runs out of contiguous space. These operations
can delay notification display and app loading.

The useful Fxfs ideas are sequential journalling, recoverable checkpoints and
bounded metadata caches. A Pebble-sized design should use:

- append-only notification records and tombstones;
- two recoverable checkpoints aligned to the flash erase geometry;
- incremental compaction with a strict page or time budget;
- a background-maintained reserve of erased sectors;
- a small fixed cache for file page-chain metadata;
- no erase operation while holding the global filesystem mutex.

Do not port Fxfs or its constants. Recovery time, write amplification and wear
must remain bounded after power loss at every write boundary.

The first implementation is available behind
`CONFIG_NOTIFICATION_COMPACTION_RUST`. Profiling showed that the immediate
latency was in notification storage's use of PFS, not PFS page lookup itself.
The replacement therefore preserves PFS's temporary-overwrite and recovery
boundary. A `no_std` Rust planner determines how many oldest records to evict;
C then copies retained serialized records directly through a fixed 256-byte
buffer. It avoids a separate tombstone-write pass, per-record heap allocation,
and payload deserialization and reserialization. Invalid headers, truncated
payloads and I/O failures abort compaction so a partial store is never accepted.

A deeper PFS rewrite remains future work. It should only proceed after traces
show page traversal or foreground erases dominate a representative workload;
changing the on-flash recovery protocol without that evidence adds much more
corruption risk than this bounded consumer-side change.

## Bluetooth queues

The communication send queue is a linked list. Queue length, offset copying and
consumption traverse jobs while the Bluetooth lock is held. The sender's byte
budget bounds its common buffers, but queue latency and traversal work can still
grow during bursts.

Fuchsia commonly uses bounded channels, delta delivery and explicit pagination.
For Pebble, replace queue metadata with fixed-capacity rings and:

- maintain queued bytes in constant time;
- keep a head offset for partial transmission;
- separate reliable control traffic from replaceable state updates;
- coalesce superseded state by endpoint and key;
- expose queue age, high-water and drop counters.

Backpressure is part of the protocol design. Never evict a reliable command to
make room for a newer state update.

The first implementation is available behind `CONFIG_COMM_SESSION_QUEUE_RUST`.
It retains polymorphic intrusive jobs, because send buffers and transports own
jobs with different lifetimes, but removes two unbounded operations. A cached
tail makes append constant-time, and a `no_std` Rust accounting state makes
queued-byte queries constant-time with checked overflow and underflow. Partial
and multi-job consumption update the count, while cleanup and an emptied queue
reset both metadata fields.

A fixed-capacity ring is not part of this phase. It would require a new ownership
and backpressure contract for heterogeneous reliable jobs. Priority lanes,
replaceable-state coalescing and telemetry remain future work and should be
benchmarked with realistic protocol traffic before changing delivery semantics.

## Heap allocator

The current allocator linearly searches physical heap segments. `realloc`
always allocates, copies and frees, even where the adjacent segment could grow
the allocation in place. Notification and timeline construction make this cost
visible through repeated short-lived allocations.

A bounded two-level segregated-fit allocator, or simpler size-class bins with a
large-block fallback, would provide predictable lookup. Add in-place shrink and
growth before changing call sites. The replacement must preserve coalescing,
instrumentation, task-specific heaps and corruption detection. Test fragmented
and nearly exhausted heaps, not only throughput on an empty heap.

The first implementation is available behind `CONFIG_HEAP_REALLOC_RUST`. A
small `no_std` Rust planner decides whether the current allocation and its next
free block can satisfy a resize without moving. C retains ownership of boundary
tags, locking, free-space coalescing, freed-memory fuzzing, metrics and the
allocate-copy-free fallback. Shrinks return reusable space immediately; growth
can consume an adjacent free block; and an unusably small remainder stays in the
allocation. The fallback now copies only the old payload capacity rather than
including the block header in its copy bound.

Segregated free lists remain future work. Before replacing the physical-block
scan, collect allocation-size and fragmentation traces from notification,
timeline and app-launch workloads. Compare worst-case lookup and peak headroom,
not only an empty-heap throughput benchmark.

## Timers

Task timers live in sorted linked lists and timer IDs are found by linear scan.
Fuchsia's executor instead uses a deadline heap and lazy cancellation. Pebble's
timer count is small, so algorithmic lookup alone may not be noticeable.

The more promising design is a fixed-capacity deadline heap for precise timers
combined with a coarse timing wheel for deferrable work. Wakeup slack should
coalesce background deadlines while animation, input and protocol timers remain
exact. Compare wakeups and sleep residency as well as insertion cost.

## End-to-end latency benchmark

The benchmark measures the path a person experiences when a notification
arrives:

```text
┌──────────────┐   ┌─────────┐   ┌────────────┐   ┌────────┐   ┌─────────┐
│ Notification │──▶│ Persist │──▶│ UI dispatch │──▶│ Render │──▶│ Display │
└──────────────┘   └─────────┘   └────────────┘   └────────┘   └─────────┘
```

Firmware records all boundaries with the monotonic RTC and emits one
machine-readable `LATENCY_RESULT` line containing:

- `storage_us`: ingestion through durable notification storage;
- `dispatch_us`: storage completion through UI event handling;
- `render_us`: UI handling through display submission;
- `flush_us`: submission through the display driver's completion callback;
- `total_us`: ingestion through the first completed visible frame.

The synthetic mode follows the real storage, event, modal, compositor and
display paths. It intentionally starts after Bluetooth decoding so it remains
repeatable without a companion. `arm` mode waits for the next real notification
on hardware. Use Bluetooth queue age and transport counters separately when
evaluating the queue rewrite.

Build and run under QEMU:

```shell
.venv/bin/pbl configure --board=qemu_gabbro -DCONFIG_PERFORMANCE_TESTS=y
.venv/bin/pbl build
.venv/bin/pbl qemu
```

With QEMU running, collect 20 samples and summary statistics:

```shell
.venv/bin/python tools/run_latency_benchmark.py \
  --iterations 20 --output build/latency-results.json
```

Use `damage` mode to compare compositor damage algorithms. It marks two
one-pixel regions in distant tile rows, then measures the display completion
and reports the number of rows submitted. Add
`-DCONFIG_COMPOSITOR_DAMAGE_RUST=y` when configuring the replacement build:

```shell
.venv/bin/python tools/run_latency_benchmark.py --mode damage \
  --iterations 20 --output build/damage-results.json
```

On `qemu_gabbro`, 20 same-host samples produced:

| Implementation | Rows, median | Flush, median | Flush, p95 |
| --- | ---: | ---: | ---: |
| Bounding rectangle | 244 | 3,000 µs | 4,000 µs |
| Rust tile damage | 32 | <1,000 µs | 1,000 µs |

This workload reduced rows and modelled display-bus traffic by 86.9%. QEMU's
1 ms clock quantization makes sub-millisecond timing indistinguishable from
zero, so the row count is the more reliable regression metric. Validate energy
and wall-clock latency on each physical display before enabling the option in a
production board configuration.

Use `storage` mode to fill the 30 KiB notification file with live records and
then add one more notification. This forces both implementations to evict the
same oldest 4 KiB before the notification proceeds through dispatch, rendering
and display completion:

```shell
.venv/bin/python tools/run_latency_benchmark.py --mode storage \
  --iterations 15 --output build/storage-results.json
```

On `qemu_gabbro`, 15 same-host samples produced:

| Implementation | Storage, median | Storage, p95 | End-to-end, median | End-to-end, p95 |
| --- | ---: | ---: | ---: | ---: |
| Legacy object rewrite | 72,000 µs | 90,000 µs | 84,000 µs | 105,000 µs |
| Rust-planned raw copy | 44,000 µs | 55,000 µs | 58,000 µs | 77,000 µs |

The replacement reduced median storage latency by 38.9% and median end-to-end
latency by 31.0%. The existing storage tests also verify oldest-record
eviction, tombstone removal, retained payload equality, subsequent writes and
corruption handling with the Rust option enabled. QEMU timings remain virtual;
repeat this workload on hardware to measure flash latency, energy and wear.

Use `queue` mode to enqueue 96 heterogeneous one-to-eight-byte jobs, perform
2,048 deterministic length and offset-copy probes, and consume the complete
queue. Add `-DCONFIG_COMM_SESSION_QUEUE_RUST=y` when configuring the replacement
build:

```shell
.venv/bin/python tools/run_latency_benchmark.py --mode queue \
  --iterations 20 --output build/queue-results.json
```

On `qemu_gabbro`, 20 same-host samples produced:

| Implementation | Total, median | Total, p95 | Integrity checksum |
| --- | ---: | ---: | ---: |
| Linked-list scans | 14,000 µs | 21,000 µs | 4,073,865,016 |
| Rust accounting + cached tail | 6,000 µs | 6,000 µs | 4,073,865,016 |

The replacement reduced median queue workload time by 57.1%. Every sample
produced the same copied-byte checksum, and the firmware asserts the expected
queue length, empty final head and 96 released jobs. Host tests additionally
cover asymmetric job contents, partial and overlong consumption, cleanup, and
enqueueing after the queue becomes empty. The workload isolates queue
operations rather than modelling radio airtime; physical-device testing should
also measure Bluetooth-lock hold time and end-to-end protocol latency.

Use `heap` mode to resize one allocation from 64 to 512 bytes and back 2,048
times. Every operation verifies three asymmetric payload sentinels. The final
checksum covers every retained byte, and firmware asserts that freeing the last
allocation leaves one fully coalesced free block. Add
`-DCONFIG_HEAP_REALLOC_RUST=y` when configuring the replacement build:

```shell
.venv/bin/python tools/run_latency_benchmark.py --mode heap \
  --iterations 20 --output build/heap-results.json
```

On `qemu_gabbro`, 20 same-host samples produced:

| Implementation | Total, median | Total, p95 | Stable resizes | Integrity checksum |
| --- | ---: | ---: | ---: | ---: |
| Allocate, copy and free | 2,500 µs | 3,000 µs | 0 / 4,096 | 904,026,885 |
| Rust-planned in-place resize | 2,000 µs | 2,000 µs | 4,096 / 4,096 | 904,026,885 |

In-place resizing reduced median workload time by 20.0% and p95 by 33.3% while
eliminating every allocation move. QEMU's 1 ms RTC quantization limits timing
precision, but stable-pointer counts and integrity checks are deterministic.
Host tests additionally cover shrinking, adjacent growth, fallback movement,
metrics, payload preservation and complete coalescing. Repeat representative
notification, timeline and app-launch traces on hardware before enabling this
option by default.

### Combined replacement results

The `rust-rewrites-combined` branch enables compositor damage tracking,
notification compaction, send-queue accounting and heap resizing together. The
controlled C build used the same commit with all four options disabled. The
replacement build used all four options enabled. Both were `qemu_gabbro`
performance builds and used the same QEMU host; storage used 15 samples and the
other workloads used 20.

| Workload | C median | Combined median | Change | C p95 | Combined p95 | Change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Synthetic notification, end-to-end | 11,000 µs | 12,000 µs | +9.1% | 12,000 µs | 21,000 µs | +75.0% |
| Full notification file, storage stage | 72,000 µs | 39,000 µs | −45.8% | 88,000 µs | 66,000 µs | −25.0% |
| Full notification file, end-to-end | 83,000 µs | 50,000 µs | −39.8% | 103,000 µs | 75,000 µs | −27.2% |
| Communication queue | 11,500 µs | 5,500 µs | −52.2% | 14,000 µs | 7,000 µs | −50.0% |
| Heap resize | 2,000 µs | 3,000 µs | +50.0% | 3,000 µs | 3,000 µs | 0.0% |
| Sparse display damage | 3,000 µs | 1,000 µs | −66.7% | 4,000 µs | 1,000 µs | −75.0% |

Sparse damage submitted 32 rows instead of 244, an 86.9% reduction. Queue and
heap checksums matched the C build at 4,073,865,016 and 904,026,885. All 4,096
replacement heap resizes retained their address, compared with none in the C
build.

An initial 2D damage bitmap increased synthetic notification rendering from a
7 ms to a 10 ms median because it tracked horizontal tiles that the row-only
display API cannot use. The row-tile design restored the 7 ms render median and
reduced the combined metadata cost. Total synthetic latency remained within one
1 ms clock tick at the median, while host scheduling outliers made its p95 worse.
The final firmware costs 800 bytes of flash and no additional RAM on Gabbro:
1,690,648 bytes flash and 155,712 bytes RAM, compared with 1,689,848 bytes and
155,712 bytes for the C build.

Combined host tests also exposed an interaction in the first notification
planner: it rounded the total request before accounting for existing tombstones
and could evict live notifications unnecessarily. The planner now subtracts
tombstone space first and rounds only the remaining deficit.

These are virtual-time regression results, not hardware latency measurements.
The deterministic row count, checksums and stable-resize count establish output
equivalence for the benchmark workloads; physical watches remain necessary to
measure wall-clock latency, display integrity, flash behaviour and energy use.

In an orb, run QEMU as a supervised service rather than a background shell:

```shell
amp orb service start pebble-qemu --command \
  "/nix/var/nix/profiles/default/bin/nix develop --command bash -c \
  'source .venv/bin/activate && \
  SDL_VIDEODRIVER=dummy pbl qemu'"

/nix/var/nix/profiles/default/bin/nix develop --command \
  .venv/bin/python tools/run_latency_benchmark.py \
  --iterations 20 --output build/latency-results.json
```

For a watch configured with `CONFIG_PERFORMANCE_TESTS=y`, enter the serial
prompt and run `latency benchmark arm`, then send a notification from the paired
phone. Run `latency benchmark synthetic` to exclude the phone and Bluetooth
transport while retaining the rest of the firmware path.

The RTC provides roughly 1 ms resolution. Report medians and 95th percentiles
over at least 20 samples. QEMU uses the same instrumentation and is suitable for
functional checks and same-host regression comparisons, but its virtual timing
is not a hardware performance result. Final decisions require the same firmware
and workload on each supported watch family.
