# ePicasso graphics accelerator

Obelix and Getafix use the SF32LB52 ePicasso 2.0 (EPIC) graphics accelerator
for framebuffer conversion. The firmware also provides a driver for fill,
copy, blend, palette expansion, rotation, scaling and mirroring.

## Configuration

`CONFIG_GPU_EPIC` enables the driver and SiFli EPIC HAL. Obelix and Getafix
also enable `CONFIG_DISPLAY_JDI_SF32LB_EPIC`, which converts the compositor's
ARGB2222 framebuffer to RGB565 before LCDC sends it to the display.

EPIC can't output ARGB2222 directly. A full-screen RGB565 intermediate buffer
would require 91,200 bytes on Obelix or 135,200 bytes on Getafix, so the display
driver converts and sends 20-row strips. Two strips and the palette require no
more than about 21 KiB. While LCDC sends one strip, EPIC converts the next. Apps
continue to use the existing ARGB2222 framebuffer and APIs.

## Driver API

Include `<pbl/drivers/gpu/epic.h>` and describe each input as an `EpicLayer`.
The driver supports these formats:

- RGB565, ARGB8565, RGB888 and ARGB8888 colour data
- L8 colour data with an ARGB8888 palette
- A2, A4, A8 and monochrome data; A4 and A8 layers can mask the preceding layer
- packed YUYV and UYVY YUV422, and three-plane YUV420 data
- hardware-compressed EZIP images

Set `u_data` and `v_data` for YUV420 input. Set `data_size` to the compressed
byte count for EZIP input. EPIC can process at most one YUV and one EZIP layer
per operation. EZIP source rectangles aren't supported.

`epic_fill`, `epic_fill_gradient`, `epic_copy` and `epic_blend` serialize access
to the peripheral, perform cache maintenance and wait for interrupt-driven
completion. A gradient supplies the ARGB8888 colour at each corner. Rotation
angles use tenths of a degree. A scale value of zero selects a 1:1 scale. Set
`color_argb8888` to choose the RGB colour drawn by an alpha-only layer.

Use `epic_layer_set_source_rect` and `epic_buffer_set_destination_rect` to
select a rectangle within a larger strided buffer. The driver adjusts the DMA
address and cache span. `epic_clip_layer` intersects an untransformed layer
with a canvas-space rectangle and adjusts its source rectangle. Packed A2 and
A4 source rectangles must start on a byte boundary. YUV422 rectangles must be
chroma-pair aligned; YUV420 rectangles must use even coordinates and sizes.

The base API is synchronous. Don't call it from an interrupt handler.

The corresponding `*_async` functions return immediately and report
completion through an `EpicCompleteCallback`. The callback may run in interrupt
context and must not block. An asynchronous submission returns `false` when
its arguments are invalid or the operation can't start. Submission waits for
the current operation to finish before starting the new operation.

## Hardware validation

Build and flash Obelix firmware, open the serial console, then run:

```text
epic benchmark
```

The command checks complete solid and gradient fills, copy output, alpha
blending, asymmetric rotation and mirroring, scaling, A8 masking, L8 palette
expansion, monochrome input and YUV422 conversion on private 32 × 32 buffers.
It reports the CPU cycles spent waiting for each operation and ends with
`output=valid` when the generated pixels match the expected values. Validate
EZIP with a product image because the benchmark doesn't embed compressed data.

Also check these display states on hardware:

1. Boot splash followed by the normal launcher.
2. Full-screen and partial updates.
3. Both display orientations.
4. Repeated animations and rapid screen changes.

QEMU doesn't emulate EPIC or the SF32LB52 LCDC, so it can't validate this path.
