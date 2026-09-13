# Obelix (Pebble Time 2)

This guide covers firmware development on Pebble Time 2 (PT2), whose PebbleOS
board name is `obelix`. Read the safety section before connecting a programmer,
probe, power supply or meter.

## How to read this guide

The available PT2 documentation is incomplete. This guide uses these labels to
keep evidence and advice separate:

- **PT2 fact** — confirmed by the current PebbleOS source or the published PT2
  block diagram and pinout.
- **Project workflow** — implemented by the current PebbleOS tools, but it
  still depends on having suitable hardware and a known way to put the watch
  into the required state.
- **Generic guidance** — normal embedded-development practice, not a verified
  PT2 electrical procedure.
- **Unknown** — do not infer the answer from another Pebble or a SiFli
  development board.

The *Emery* platform name also appears in legacy Pebble material, and current
PT2 firmware selects `CONFIG_PLATFORM_EMERY`. That shared software name does
**not** make an older watch's connector, voltage, button, bootloader or flashing
instructions valid for current PT2 hardware.

## Safety first

```{danger}
PT2 pin electrical limits, debug-connector orientation, charge-contact wiring,
battery-connector pinout and a bench-power procedure have not been published.
Do not apply power to an unidentified pad or rail. Do not assume that a signal
is 3.3 V tolerant merely because the diagram labels a nearby rail `VDD_3V3`.
```

- Disconnect USB, the charger, probes and the battery before changing wiring.
  Make the ground connection first and remove it last.
- Never connect two power sources unless the hardware is documented to support
  it. A programmer, USB-UART adapter, debug probe, bench supply, power profiler
  and installed battery can back-power one another through signal or supply
  pins even when one source appears to be off.
- Treat the diagram's `Vbus` and `VDDmcu` connector pins as unidentified power
  interfaces, not convenient supply inputs or outputs. Confirm direction,
  normal voltage, current limit and power-path isolation on the exact hardware
  revision before connecting either one.
- Use a current-limited, isolated bench supply only after verifying the target
  rail and acceptable range from a schematic, board owner or measurements on
  an undisturbed working unit. Start with output disabled and verify polarity
  at the disconnected lead. A battery label's nominal voltage is **not** a
  bench-supply setting.
- A lithium-ion/polymer cell can burn if shorted, overcharged, crushed,
  punctured or overheated. Don't probe under a live metal shield or bypass the
  protection/temperature connection. Stop if the cell is swollen, hot, dented,
  leaking or smells unusual. Move away from combustible material and follow
  local battery-emergency and disposal guidance.
- Remove rings, watches and other conductive jewellery. Use insulated,
  strain-relieved leads and cover exposed contacts. Check for shorts with the
  watch unpowered before every first power-up.

Use a second, expendable development watch for destructive work. Keep one
known-good watch and its matching release bundle as a reference.

## What is verified for PT2

**PT2 facts:**

- PT2 has three build targets: `obelix@bb2`, `obelix@dvt` and `obelix@pvt`.
  The current revision-specific configuration files are nearly empty, but
  revision-specific differences may still be introduced. Do not guess the
  revision. Confirm it from controlled hardware records, a board marking or
  the device supplier.
- Obelix uses an SF32LB52 SoC, PBLBOOT, two normal-firmware slots, a dedicated
  safe/recovery firmware region and a 32 MiB external GD25Q256E flash layout.
  The bootloader itself is not in this repository.
- The only runner declared by `boards/obelix/obelix.yml` is `sftool`, over a
  serial port. Obelix has no project OpenOCD configuration, so `pbl debug` is
  not currently a supported PT2 hardware workflow.
- Firmware configures the debug console as USART1 at 1,000,000 baud, 8 data
  bits, no parity, one stop bit and no flow control. Board TX is PA19 and board
  RX is PA18.
- The published PT2 pinout shows PA18 and PA19 multiplexed with SWDIO and
  SWCLK. UART and SWD therefore share pins; don't drive both interfaces at
  once.

### Published debug connector

The [PT2 block diagram and pinout][pt2-pinout] identifies J3 as a Molex
5050040812 and gives this board-side assignment:

| Pin | Published net | Connection concept |
| --- | --- | --- |
| 1 | `DBG_UART_TXD` | Board output to adapter RX |
| 2 | `Vbus` | Power net; direction and limits are not specified |
| 3 | `VDDmcu` | Connected to `VDD_3V3` through 0 Ω in the diagram; don't drive it without confirmation |
| 4 | `SWDIO` | Bidirectional SWD data |
| 5 | `GND` | Common reference |
| 6 | `DBG_UART_RXD` | Board input from adapter TX |
| 7 | `HW_PWD_RST` | Reset-related net; polarity and drive method are not specified |
| 8 | `SWDCLK` | SWD clock |

This table transcribes the published document; it is not a hookup procedure.
Before connecting, verify the document revision against the physical PCB,
locate pin 1 from the connector drawing and board marks, and use unpowered
continuity/resistance checks to confirm ground and the intended cable mapping.
Measure idle signal and rail voltages with a high-impedance meter before
enabling any adapter output.

```{warning}
Pebble 2 Duo (Asterix) uses the same connector part number, but its documented
pin assignment and 1.8 V warning are for Asterix. Do not connect the Asterix
Core B2B v2 board or an Asterix cable to PT2 based on mechanical fit.
```

## Prepare a device before the first update

The least risky first change is a phone-mediated update on a healthy, charged
watch. Before changing firmware:

1. Confirm the exact board revision and build for that revision.
2. Record the displayed firmware version, serial/device identity, battery
   percentage and pairing state. Photograph each functional screen and test
   all buttons, touch, display, backlight, vibration, speaker, microphone,
   sensors, Bluetooth and charging. This creates a baseline for distinguishing
   a firmware regression from pre-existing damage.
3. Save the exact official `.pbz` bundle needed to return to the current
   release, if it is available. Verify its hash and source. A same-version file
   from an unverified location is not a recovery image.
4. Preserve app data through the app's supported synchronization/export
   features where available. No project-supported PT2 filesystem or complete
   device backup/restore procedure is documented.
5. Charge with the normal retail charging equipment on a non-combustible
   surface. Don't begin an update with a marginal battery or damaged cable.
6. Build and exercise the change in `qemu_emery` before using hardware.

### Optional expert flash readback

**Project limitation:** `pbl` can write and erase through its `sftool` runner;
it does not expose readback. Upstream [SFTool][sftool] supports `read_flash`,
and PebbleOS defines external NOR as `0x12000000` through `0x13ffffff`.

Once a known-safe PT2 serial fixture and loader-entry procedure already work,
an experienced developer can make a read-only 32 MiB external-flash capture
with a recent SFTool whose help shows the same argument syntax:

```shell
sftool --version
sftool read_flash --help
sftool -c SF32LB52 -p "$TTY" \
  read_flash pt2-external-flash-a.bin@0x12000000:0x02000000
sftool -c SF32LB52 -p "$TTY" \
  read_flash pt2-external-flash-b.bin@0x12000000:0x02000000
cmp pt2-external-flash-a.bin pt2-external-flash-b.bin
sha256sum pt2-external-flash-*.bin
```

**Generic guidance:** keep the capture encrypted and private. It may contain
pairing keys and personal data. This captures only the external NOR address
range established by the current source, not internal SoC storage or OTP, and
it has no project-supported whole-device restore path. Do **not** test a backup
by writing it back, and do not use `erase_flash`, `erase_region`, `--erase-all`
or an address copied from an upstream SF32LB52 example. A whole-flash write can
overwrite PBLBOOT, the partition table, recovery firmware, manufacturing data,
calibration, pairing state and the filesystem.

## Build firmware

Install the SDK and dependencies as described in {doc}`../../development/getting_started`.
Keep normal and recovery builds separate so one cannot silently replace the
other:

```shell
# Substitute the revision you have verified.
pbl configure -b build/obelix-pvt --board obelix@pvt
pbl build -b build/obelix-pvt
pbl bundle -b build/obelix-pvt

pbl configure -b build/obelix-pvt-prf --board obelix@pvt --variant prf
pbl build -b build/obelix-pvt-prf
```

The `.pbz` update bundle is written under the normal build directory. Keep the
matching `pebbleos.elf` and log-hash dictionary from every hardware test; they
are needed to decode logs and crashes from that exact build.

For battery or release comparisons, configure both the baseline and candidate
with `-DCONFIG_RELEASE=y`. Debug builds include aids and defaults that can
change power use.

## Update and flash paths

Use the highest path in this list that still works.

### 1. Phone-mediated update

**Project workflow:** this is the preferred first-device path because it does
not require opening the watch or connecting unidentified electrical signals.
Build a bundle, then follow {ref}`loading-firmware-via-bluetooth` to enable the
mobile app's debug options and sideload the `.pbz`.

The current mobile-app and firmware sources implement transfer of firmware and
resources followed by install, and select the inactive normal-firmware slot for
a dual-slot platform. This reduces the risk of replacing the running slot, but
it is not permission to interrupt an update. Keep the phone nearby, leave the
app open, and don't reset or discharge the watch during transfer or install.

### 2. Wired serial development flash

**Project workflow:** with an electrically verified PT2 fixture connected and
the watch in the SiFli loader state expected by SFTool:

```shell
pbl flash -b build/obelix-pvt --tty "$TTY" --resources
pbl console -b build/obelix-pvt --tty "$TTY"
```

`--resources` is appropriate for the first matching firmware/resource load.
Later firmware-only iterations can omit it when resources have not changed.
The project does **not** currently document how a retail PT2 enters the loader,
how the fixture controls reset, or whether a particular PT2 revision must be
powered by its battery, charger or programmer. Resolve those unknowns with the
fixture owner before running either command. Seeing a serial device on the host
does not prove the watch-side wiring or power arrangement is safe.

### 3. Recovery firmware imaging

**Project workflow:** direct PRF imaging is an expert recovery/preparation step,
not a normal firmware update:

```shell
pbl image_recovery -b build/obelix-pvt-prf --tty "$TTY"
```

For SF32LB52 builds, this command uses SFTool to write the PRF binary to the
dedicated `SAFE_FIRMWARE` address from the repository's flash map. It does not
install a partition table, bootloader or complete factory image.

```{warning}
When the current PBLBOOT PRF starts, PebbleOS invalidates both normal-firmware
slots so that normal firmware must be reinstalled. Do not enter PRF merely to
see whether it works. Have a charged watch, a known-good matching normal `.pbz`
and a working phone or wired reinstall path ready first.
```

### Bootloader boundaries

**PT2 fact:** PebbleOS creates PBLBOOT-format images and defines flags for a new
image, firmware stability, reset-loop detection and forcing PRF. The bootloader
that interprets those flags is not in this repository. Therefore the exact
slot-selection order, rollback thresholds, ROM-loader entry and response to a
corrupt partition table are **unknown here**.

The current running SF32LB52 firmware implements these button actions after it
has initialized the button driver:

- Hold **Select + Back** for about 5 seconds to request a hard reset.
- Hold **Up + Select + Back** for about 5 seconds to set `BOOT_BIT_FORCE_PRF`
  and hard-reset.

The second combination only requests PRF; it requires a valid PRF and a
bootloader that honours the bit. Neither combination can be assumed to work if
the crash occurs before button initialization, the firmware is erased, power
is bad or the SoC is stuck outside normal firmware. These are not SiFli ROM
download-mode gestures.

## Recover a failing watch

Stop escalating as soon as the watch is healthy. Record every observed screen,
LED, vibration, serial line and host-tool error before changing state.

### Failed phone update

1. Keep the watch on its normal charger and the phone nearby. Wait for the app
   to report failure; don't reset solely because progress pauses briefly.
2. Reconnect in the mobile app and retry the same known-good, revision-matched
   bundle. If the watch boots its previous firmware, verify basic functions
   before another attempt.
3. If normal firmware no longer boots but PRF is already displayed, use the
   mobile app to reinstall normal firmware. Expect apps or pairing to need
   repair; do not factory-reset pre-emptively.
4. If normal firmware is responsive enough to process buttons and a valid PRF
   is known to be installed, use Up + Select + Back only when the normal
   reinstall path is ready. Remember that starting current PRF invalidates both
   normal slots.
5. Escalate to the verified wired SFTool fixture if neither normal firmware nor
   PRF can accept an update.

### Boot loop or repeated resets

1. Remove third-party peripherals and unverified debug wiring. Use only the
   normal charger, then retry after charging.
2. Capture the serial console if the fixture is already proven safe. Preserve
   the exact ELF and log dictionary. Note whether the loop reaches the PebbleOS
   banner, which slot it reports and whether the reset interval is consistent.
3. Try Select + Back. If the loop reaches button initialization, this can make
   a clean hard-reset request.
4. Reinstall the last known-good normal `.pbz`. Use forced PRF only under the
   conditions above.
5. If the loop began with a power-sensitive feature, test without that feature
   and inspect the power trace for a voltage collapse or current-limit event.
   Do not conclude that firmware is corrupt until power integrity is checked.

### Crash with otherwise working firmware

1. Reproduce once with `pbl console` attached and save the complete log.
2. Record the action, battery/charger state, phone connection and build commit.
3. Keep the matching `pebbleos.elf`. PebbleOS writes coredumps to external
   flash, but no PT2 coredump-extraction command is currently documented.
   If a coredump is obtained through a separately verified method, use
   `tools/analyze_coredump.py <symbols.elf> <coredump>` as described in
   {doc}`../../development/debugging`.
4. Confirm the same test on the known-good release and in `qemu_emery` before
   changing recovery firmware or hardware.

### No display and apparently bricked

“No display” can mean an empty battery, failed charger/display, a boot loop,
bad firmware or erased boot state. It does not by itself prove a dead MCU.

1. Stop immediately for heat, swelling, odour or visible damage. Don't charge
   or repeatedly power-cycle a damaged battery.
2. Otherwise, use the unmodified retail charger and allow a normal charge
   interval. Check the cable/supply with a known-good watch rather than probing
   unidentified PT2 contacts.
3. Try the firmware reset combination. Observe vibration, Bluetooth presence
   and charge current as well as the display.
4. Connect only a proven PT2 serial fixture. Look for console output, then use
   the readback procedure above as the first SFTool operation.
5. If readback succeeds, verify it before writing. Repair only
   the smallest known-bad region with a revision-matched artifact. Do not use
   generic SiFli bootloader or flash-table binaries.
6. If serial recovery cannot connect, have the hardware owner confirm loader
   entry, reset and power first. Use SWD only after those checks.

## SWD, JTAG and adapters

**PT2 fact:** the published connector exposes SWDIO and SWDCLK. It does not
publish JTAG signals, so there is no evidence for a PT2 JTAG connection. It
also does not specify logic thresholds, reset polarity, target-reference use,
debug access protection or a supported probe.

Use SWD when serial recovery cannot explain or repair early boot, when you need
to halt before UART initialization, or when diagnosing a hard fault or board
bring-up. It is not the first choice for routine firmware loading.

Possible interfaces, in descending order of confidence:

- The official [Pebble Programmer for SiFli boards][pblprog-sifli] is the best
  physical-fixture candidate. Its repository contains PCB/flex design files
  and a matching connector, but its README does not explicitly name PT2,
  provide assembly/usage instructions or state electrical limits. Obtain the
  exact assembled-board revision and instructions from its owner before use.
- A voltage-compatible USB-UART adapter may carry the 1 Mbaud 8N1 console once
  board TX/RX, ground and target logic levels are independently confirmed. It
  must not drive PA18/PA19 while those pins are used for SWD.
- OpenSiFli documents an SF32LB52 serial debug interface in `probe-rs` and a
  Windows SiFli USART-to-J-Link server. These are **generic SF32LB52
  possibilities**, not validated PT2 procedures. The documented `probe-rs`
  path is attach-only, not flash/download.
- A CMSIS-DAP, J-Link, ST-Link or other hardware SWD probe may be electrically
  capable of SWD, but no current PebbleOS Obelix target configuration or
  verified compatibility is published. Do not connect one until target
  voltage/reference behaviour, reset drive and SF32LB52 software support are
  confirmed from the probe vendor and SiFli documentation.

For an experimental SWD setup, first leave all probe outputs disabled. Confirm
ground, observe the target rail, configure the probe to sense rather than
supply the target, use short leads and begin at a low SWD clock. Check for
contention on shared PA18/PA19 and monitor current before attempting attach.
Never mass-erase, unlock protection or program internal/OTP storage as a
connectivity test.

## Measure current and battery behaviour

No PT2 measurement jumper, shunt location or approved battery-interposer setup
is published. The block diagram labels a 3.8 V, 185 mAh battery and several
rails, but those labels do not specify a safe source voltage, battery pinout or
current limit.

### Equipment

**Generic guidance:** choose based on the question being asked.

- A power analyzer such as a Nordic Power Profiler Kit II (PPK2), Joulescope or
  Qoitech Otii can capture sleep current and short radio/display peaks. Confirm
  the instrument's voltage, current, bandwidth and transient limits against
  the verified PT2 rail before connection.
- A bench supply with current limiting is useful for controlled fault work,
  but only after a safe injection point and operating range are known. It is
  not a battery charger.
- A digital multimeter is useful for unpowered continuity, polarity and steady
  current. Its burden voltage and slow sampling can reset a low-power device or
  miss short peaks.
- An oscilloscope with a suitable differential probe, or a current probe, can
  correlate rail droop with reset or load events. Do not ground a conventional
  earth-referenced probe to an arbitrary live node.
- A USB power meter between the USB supply and retail charge dongle is a
  non-invasive first check. It measures charger input, including charging and
  conversion losses, not battery current or MCU-only current.

### Setup concepts

Start non-invasively. Establish idle and active charger-input profiles on a
healthy watch. For true system-current measurement, an ammeter must be in
series at a verified power path; clipping it across a rail creates a short.

Replacing the battery with a source-measure unit requires opening the battery
path and independently identifying power, return and any temperature/sense
contacts. Disconnect the cell completely, ensure no charger/programmer is
back-powering the watch, then validate polarity and voltage with output off.
Do not parallel a profiler with the cell, bypass an NTC/safety lead or charge a
cell through a profiler unless that exact arrangement is supported by the
instrument and PT2 power design.

Debug fixtures alter power results through target power, pull resistors and
UART/SWD traffic. Measure both with the fixture disconnected and, when needed,
with it attached. Record firmware mode, screen/backlight state, radio state,
battery state of charge, charging state, temperature, sample rate and wiring so
profiles are comparable.

Useful repeatable scenarios include cold boot, settled idle, Bluetooth
advertising and connected idle, notification delivery, touch/button use,
backlight levels, sensor sampling, vibration, audio, firmware update, charging
and post-charge idle. Integrate current over representative time; a single
instantaneous reading does not predict battery life.

## Recommended test progression

1. Run host tests for the changed subsystem with `pbl test`.
2. Build `qemu_emery`, run the affected flow and keep a fresh-flash test
   separate from a `--keep-flash-image` persistence test.
3. Build the exact Obelix revision and inspect build size and warnings. Keep the
   baseline and candidate artifacts.
4. Sideload a minimal normal-firmware change onto the expendable watch. Verify
   boot, update, reset, reconnect and all affected peripherals.
5. Repeat several phone-mediated updates before moving to wired SFTool. Prove
   console receive/transmit before relying on it for recovery.
6. Establish a verified external-flash readback. Then test wired normal
   firmware and resources without erase-all operations.
7. Only after normal reinstall is reliable, image and deliberately enter PRF,
   then prove recovery to a known-good normal build.
8. Add fault tests one variable at a time: interrupted transfer before install,
   watchdog/crash reproduction, low-battery behaviour and power profiling.
   Do not deliberately interrupt bootloader writes or remove power during a
   documented non-atomic phase.
9. Reserve SWD and invasive power work for a fixture-reviewed unit with a
   written wiring record and a second person checking power and orientation.

## Troubleshooting checklist

- [ ] Exact `obelix@bb2`, `obelix@dvt` or `obelix@pvt` revision confirmed
- [ ] Firmware, resources, PRF and `.pbz` all built for that revision
- [ ] Known-good release bundle, ELF and log dictionary retained
- [ ] Baseline functions and battery/charging behaviour recorded
- [ ] Battery healthy, watch charged and no unexpected heat
- [ ] Connector pin 1 and cable mapping checked against the current PT2 PDF
- [ ] Ground continuity checked with all power disconnected
- [ ] Adapter logic voltage and input tolerance measured/confirmed, not assumed
- [ ] Exactly one intended source is powering each rail
- [ ] No back-power path through UART, SWD, USB, charger or profiler
- [ ] PA18/PA19 not being driven simultaneously by UART and SWD
- [ ] Correct serial port selected; no other process has it open
- [ ] Console configured for 1,000,000 baud, 8N1, no flow control
- [ ] Normal phone update retried before PRF or wired escalation
- [ ] Consequences of entering PRF understood; normal reinstall path ready
- [ ] SFTool connectivity/readback tested before any write
- [ ] No erase-all, generic flash table, generic bootloader or OTP operation
- [ ] Reset timing, first log line, reported slot and host error saved
- [ ] Power integrity checked before diagnosing firmware corruption
- [ ] SWD target support, voltage reference and reset drive verified before attach
- [ ] Changes reproduced against a known-good build or watch

## Sources and remaining unknowns

PT2-specific primary sources:

- [Pebble Time 2 block diagram and pinout, v2 (2026-03-29)][pt2-pinout]
- [`boards/obelix/obelix.yml`](https://github.com/coredevices/PebbleOS/blob/main/boards/obelix/obelix.yml)
  and [`boards/obelix/defconfig`](https://github.com/coredevices/PebbleOS/blob/main/boards/obelix/defconfig)
- [`board_obelix.c`](https://github.com/coredevices/PebbleOS/blob/main/src/fw/board/boards/board_obelix.c)
  for UART and button assignments
- [`flash_region_gd25q256e.h`](https://github.com/coredevices/PebbleOS/blob/main/src/fw/flash_region/flash_region_gd25q256e.h)
  for the external-flash map
- [`debounced_button.c`](https://github.com/coredevices/PebbleOS/blob/main/src/fw/drivers/sf32lb52/debounced_button.c),
  [`bootbits.h`](https://github.com/coredevices/PebbleOS/blob/main/src/fw/system/bootbits.h)
  and [`main.c`](https://github.com/coredevices/PebbleOS/blob/main/src/fw/main.c)
  for reset/PRF behaviour
- [`sftool.py`](https://github.com/coredevices/PebbleOS/blob/main/tools/libs/pbl-cli/pbl/runners/sftool.py)
  and [`sftool_flash_imaging.py`](https://github.com/coredevices/PebbleOS/blob/main/tools/sftool_flash_imaging.py)
  for project-supported serial writes
- [`FirmwareUpdater.kt`](https://github.com/coredevices/mobileapp/blob/main/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/connection/endpointmanager/FirmwareUpdater.kt)
  for the phone-mediated dual-slot update flow

Related but not proof of PT2 compatibility:

- [Pebble Programmer for SiFli boards][pblprog-sifli]
- [OpenSiFli SFTool documentation][sftool]
- [OpenSiFli SF32LB52 flash/debug guide][sifli-debug]
- [Nordic PPK2 user guide][ppk2] for generic profiler modes and limits

As of this guide's update, authoritative public sources do not specify PT2
logic thresholds/absolute maximums, connector mating orientation on an
assembled watch, `Vbus`/`VDDmcu` direction and limits, retail charge-contact
pinout, battery connector/NTC pinout, safe bench-supply settings, ROM-loader
entry, complete factory backup/restore, debug protection, a validated SWD probe
or a power-measurement insertion point. Verify these with a revision-matched
schematic, approved fixture instructions or the hardware owner before making
an electrical connection.

[pt2-pinout]: https://github.com/coredevices/hardware/blob/main/watch/Pebble%20Time%202%20%28obelix%29/2026-03-29%20Pebble%20Time%202%20%28Obelix%29%20Block%20Diagram%20and%20Pinout%20v2.pdf
[pblprog-sifli]: https://github.com/coredevices/pblprog-sifli
[sftool]: https://github.com/OpenSiFli/sftool/blob/master/README_EN.md
[sifli-debug]: https://github.com/OpenSiFli/sifli-rs/blob/main/docs/flash_and_debug.md
[ppk2]: https://docs.nordicsemi.com/bundle/ug_ppk2
