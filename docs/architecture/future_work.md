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
It uses a fixed 16×16 tile bitmap in each framebuffer and a `no_std` Rust core
to retain disjoint damage. The display still sends complete rows because the
display API has no horizontal span, but it skips clean tile rows between dirty
regions. A 260×260 framebuffer adds 40 bytes of damage metadata.

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
