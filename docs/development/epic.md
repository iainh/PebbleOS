# ePicasso graphics accelerator

Getafix uses the SF32LB52 ePicasso 2.0 (EPIC) graphics accelerator for
framebuffer conversion. The firmware also provides a small synchronous driver
for fill, copy, blend, palette expansion, rotation, scaling and mirroring.

## Configuration

`CONFIG_GPU_EPIC` enables the driver and SiFli EPIC HAL. Getafix also enables
`CONFIG_DISPLAY_JDI_SF32LB_EPIC`, which converts the compositor's ARGB2222
framebuffer to RGB565 before LCDC sends it to the display.

EPIC can't output ARGB2222 directly. A full 260 × 260 RGB565 intermediate
framebuffer would require 135,200 bytes, so the display driver converts and
sends 20-row strips. Two strips and the palette require about 21 KiB. While
LCDC sends one strip, EPIC converts the next. Apps continue to use the existing
ARGB2222 framebuffer and APIs.

## Driver API

Include `<pbl/drivers/gpu/epic.h>` and describe each input as an `EpicLayer`.
The driver supports these formats:

- RGB565, ARGB8565, RGB888 and ARGB8888 colour data
- L8 colour data with an ARGB8888 palette
- A2, A4 and A8 alpha data; A4 and A8 layers can also mask the preceding layer

`epic_fill`, `epic_copy` and `epic_blend` serialize access to the peripheral,
perform cache maintenance and wait for interrupt-driven completion. Rotation
angles use tenths of a degree. A scale value of zero selects a 1:1 scale.
Set `color_argb8888` to choose the RGB colour drawn by an alpha-only layer.

The base API is synchronous. Don't call it from an interrupt handler.

The corresponding `*_async` functions return immediately and report
completion through an `EpicCompleteCallback`. The callback may run in interrupt
context and must not block. An asynchronous submission returns `false` when
its arguments are invalid or the operation can't start. Submission waits for
the current operation to finish before starting the new operation.

## Hardware validation

Build and flash Getafix firmware, open the serial console, then run:

```text
epic benchmark
```

The command checks fill, copy, alpha blending, rotation and L8 palette
expansion on private 32 × 32 buffers. It reports the CPU cycles spent waiting
for each operation and ends with `output=valid` when the generated pixels match
the expected values.

Also check these display states on hardware:

1. Boot splash followed by the normal launcher.
2. Full-screen and partial updates.
3. Both display orientations.
4. Repeated animations and rapid screen changes.

QEMU doesn't emulate EPIC or the SF32LB52 LCDC, so it can't validate this path.
