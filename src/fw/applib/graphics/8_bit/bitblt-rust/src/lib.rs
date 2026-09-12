// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::{cmp, ffi::c_void, ptr};

const COMP_OP_ASSIGN: i32 = 0;
const COMP_OP_ASSIGN_INVERTED: i32 = 1;
const COMP_OP_OR: i32 = 2;
const COMP_OP_AND: i32 = 3;
const COMP_OP_CLEAR: i32 = 4;
const COMP_OP_SET: i32 = 5;
const COMP_OP_TINT: i32 = 6;
const COMP_OP_TINT_LUMINANCE: i32 = 7;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GPoint {
    x: i16,
    y: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GSize {
    w: i16,
    h: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GRect {
    origin: GPoint,
    size: GSize,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct GBitmapDataRowInfo {
    data: *mut u8,
    min_x: i16,
    max_x: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GColor8 {
    argb: u8,
}

unsafe extern "C" {
    fn gbitmap_get_bounds(bitmap: *const c_void) -> GRect;
    fn gbitmap_get_data_row_info(bitmap: *const c_void, y: u16) -> GBitmapDataRowInfo;
    fn gbitmap_get_format(bitmap: *const c_void) -> i32;
    fn gbitmap_get_bits_per_pixel(format: i32) -> u8;
    fn gbitmap_get_palette(bitmap: *const c_void) -> *mut GColor8;
    fn gcolor_tint_luminance_lookup_table_init(tint: GColor8, output: *mut GColor8);

    static g_color_alpha_blend_33_lookup: [u8; 64 * 64];
    static g_color_luminance_lookup: [u8; 64];
}

#[inline(always)]
fn alpha_blend(src: u8, dest: u8) -> u8 {
    let src_rgb = usize::from(src & 0x3f);
    let dest_rgb = usize::from(dest & 0x3f);
    unsafe {
        let table = core::ptr::addr_of!(g_color_alpha_blend_33_lookup).cast::<u8>();
        match src >> 6 {
            0 => dest,
            1 => table.add(src_rgb * 64 + dest_rgb).read(),
            2 => table.add(dest_rgb * 64 + src_rgb).read(),
            _ => src,
        }
    }
}

struct CompositeContext {
    tint: u8,
    tint_luminance: [GColor8; 4],
}

trait PixelOp {
    unsafe fn write(dest: *mut u8, src: u8, context: &CompositeContext);
}

struct Assign;
struct Set;
struct Tint;
struct TintLuminance;

impl PixelOp for Assign {
    #[inline(always)]
    unsafe fn write(dest: *mut u8, src: u8, _context: &CompositeContext) {
        dest.write(src);
    }
}

impl PixelOp for Set {
    #[inline(always)]
    unsafe fn write(dest: *mut u8, src: u8, _context: &CompositeContext) {
        dest.write(alpha_blend(src, dest.read()));
    }
}

impl PixelOp for Tint {
    #[inline(always)]
    unsafe fn write(dest: *mut u8, src: u8, context: &CompositeContext) {
        let actual = (context.tint & 0x3f) | (src & 0xc0);
        dest.write(alpha_blend(actual, dest.read()));
    }
}

impl PixelOp for TintLuminance {
    #[inline(always)]
    unsafe fn write(dest: *mut u8, src: u8, context: &CompositeContext) {
        let luminance = core::ptr::addr_of!(g_color_luminance_lookup)
            .cast::<u8>()
            .add(usize::from(src & 0x3f))
            .read();
        let tinted = context
            .tint_luminance
            .get_unchecked(usize::from(luminance))
            .argb;
        const MULTIPLY_ALPHA: [u8; 16] = [0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 2, 0, 1, 2, 3];
        let alpha = *MULTIPLY_ALPHA.get_unchecked(usize::from(((src >> 6) << 2) | (tinted >> 6)));
        let actual = (tinted & 0x3f) | (alpha << 6);
        dest.write(alpha_blend(actual, dest.read()));
    }
}

#[inline]
fn normalized(value: i32, begin: i32, size: i32) -> i32 {
    begin + (value - begin).rem_euclid(size)
}

unsafe fn composite_8bit_chunk<P: PixelOp>(
    dest: *mut u8,
    src: *const u8,
    length: usize,
    context: &CompositeContext,
) {
    for index in 0..length {
        P::write(dest.add(index), src.add(index).read(), context);
    }
}

unsafe fn assign_8bit_chunk(dest: *mut u8, src: *const u8, length: usize) {
    let dest_begin = dest as usize;
    let dest_end = dest_begin + length;
    let src_begin = src as usize;
    let src_end = src_begin + length;
    if dest_end <= src_begin || src_end <= dest_begin {
        ptr::copy_nonoverlapping(src, dest, length);
    } else {
        // Preserve the C implementation's forward-copy semantics for self-blits.
        for index in 0..length {
            dest.add(index).write(src.add(index).read());
        }
    }
}

unsafe fn walk_8bit_rows<P: PixelOp>(
    dest_bitmap: *mut c_void,
    src_bitmap: *const c_void,
    dest_rect: GRect,
    src_origin_offset: GPoint,
    context: &CompositeContext,
    bulk_assign: bool,
) {
    let bounds = gbitmap_get_bounds(src_bitmap);
    let src_left = i32::from(bounds.origin.x);
    let src_width = i32::from(bounds.size.w);
    let src_top = i32::from(bounds.origin.y);
    let src_height = i32::from(bounds.size.h);
    if src_width <= 0 || src_height <= 0 || dest_rect.size.w <= 0 || dest_rect.size.h <= 0 {
        return;
    }

    let dest_left = i32::from(dest_rect.origin.x);
    let dest_right = dest_left + i32::from(dest_rect.size.w);
    let mut src_y = normalized(
        src_top + i32::from(src_origin_offset.y),
        src_top,
        src_height,
    );
    for dest_y in
        i32::from(dest_rect.origin.y)..i32::from(dest_rect.origin.y) + i32::from(dest_rect.size.h)
    {
        let dest_row = gbitmap_get_data_row_info(dest_bitmap, dest_y as u16);
        let clipped_left = cmp::max(dest_left, i32::from(dest_row.min_x));
        let clipped_right = cmp::min(dest_right, i32::from(dest_row.max_x) + 1);
        if clipped_left < clipped_right {
            let src_row = gbitmap_get_data_row_info(src_bitmap, src_y as u16);
            let valid_left = cmp::max(src_left, i32::from(src_row.min_x));
            let valid_right = cmp::min(src_left + src_width, i32::from(src_row.max_x) + 1);
            let dest_delta = clipped_left - dest_left;
            let mut source_x = normalized(
                src_left + dest_delta + i32::from(src_origin_offset.x),
                src_left,
                src_width,
            );
            let mut dest_x = clipped_left;

            while dest_x < clipped_right {
                let tile_length = cmp::min(src_left + src_width - source_x, clipped_right - dest_x);
                let draw_left = cmp::max(source_x, valid_left);
                let draw_right = cmp::min(source_x + tile_length, valid_right);
                if draw_left < draw_right {
                    let draw_dest_x = dest_x + draw_left - source_x;
                    let length = (draw_right - draw_left) as usize;
                    let dest_ptr = dest_row.data.offset(draw_dest_x as isize);
                    let src_ptr = src_row.data.offset(draw_left as isize);
                    if bulk_assign {
                        assign_8bit_chunk(dest_ptr, src_ptr, length);
                    } else {
                        composite_8bit_chunk::<P>(dest_ptr, src_ptr, length, context);
                    }
                }
                dest_x += tile_length;
                source_x = src_left;
            }
        }
        src_y += 1;
        if src_y == src_top + src_height {
            src_y = src_top;
        }
    }
}

#[inline(always)]
unsafe fn packed_index<const BPP: usize>(src: *const u8, x: i32) -> u8 {
    let bit = x as usize * BPP;
    let byte = src.add(bit >> 3).read();
    (byte >> (8 - BPP - (bit & 7))) & ((1 << BPP) - 1) as u8
}

unsafe fn expand_palette_chunk<const BPP: usize, P: PixelOp>(
    dest: *mut u8,
    src: *const u8,
    source_x: i32,
    length: usize,
    palette: *const GColor8,
    context: &CompositeContext,
) {
    let pixels_per_byte = 8 / BPP;
    let mut offset = 0;

    while offset < length && (source_x as usize + offset) % pixels_per_byte != 0 {
        let index = packed_index::<BPP>(src, source_x + offset as i32);
        P::write(
            dest.add(offset),
            palette.add(usize::from(index)).read().argb,
            context,
        );
        offset += 1;
    }

    while length - offset >= pixels_per_byte {
        let x = source_x as usize + offset;
        let packed = src.add(x / pixels_per_byte).read();
        for pixel in 0..pixels_per_byte {
            let shift = 8 - BPP * (pixel + 1);
            let index = (packed >> shift) & ((1 << BPP) - 1) as u8;
            P::write(
                dest.add(offset + pixel),
                palette.add(usize::from(index)).read().argb,
                context,
            );
        }
        offset += pixels_per_byte;
    }

    while offset < length {
        let index = packed_index::<BPP>(src, source_x + offset as i32);
        P::write(
            dest.add(offset),
            palette.add(usize::from(index)).read().argb,
            context,
        );
        offset += 1;
    }
}

unsafe fn walk_palette_rows<const BPP: usize, P: PixelOp>(
    dest_bitmap: *mut c_void,
    src_bitmap: *const c_void,
    dest_rect: GRect,
    src_origin_offset: GPoint,
    palette: *const GColor8,
    context: &CompositeContext,
) {
    let bounds = gbitmap_get_bounds(src_bitmap);
    let src_left = i32::from(bounds.origin.x);
    let src_width = i32::from(bounds.size.w);
    let src_top = i32::from(bounds.origin.y);
    let src_height = i32::from(bounds.size.h);
    if src_width <= 0 || src_height <= 0 || dest_rect.size.w <= 0 || dest_rect.size.h <= 0 {
        return;
    }

    let dest_left = i32::from(dest_rect.origin.x);
    let dest_right = dest_left + i32::from(dest_rect.size.w);
    let mut src_y = normalized(
        src_top + i32::from(src_origin_offset.y),
        src_top,
        src_height,
    );
    for dest_y in
        i32::from(dest_rect.origin.y)..i32::from(dest_rect.origin.y) + i32::from(dest_rect.size.h)
    {
        let dest_row = gbitmap_get_data_row_info(dest_bitmap, dest_y as u16);
        let clipped_left = cmp::max(dest_left, i32::from(dest_row.min_x));
        let clipped_right = cmp::min(dest_right, i32::from(dest_row.max_x) + 1);
        if clipped_left < clipped_right {
            let src_row = gbitmap_get_data_row_info(src_bitmap, src_y as u16);
            let valid_left = cmp::max(src_left, i32::from(src_row.min_x));
            let valid_right = cmp::min(src_left + src_width, i32::from(src_row.max_x) + 1);
            let dest_delta = clipped_left - dest_left;
            let mut source_x = normalized(
                src_left + dest_delta + i32::from(src_origin_offset.x),
                src_left,
                src_width,
            );
            let mut dest_x = clipped_left;

            while dest_x < clipped_right {
                let tile_length = cmp::min(src_left + src_width - source_x, clipped_right - dest_x);
                let draw_left = cmp::max(source_x, valid_left);
                let draw_right = cmp::min(source_x + tile_length, valid_right);
                if draw_left < draw_right {
                    let draw_dest_x = dest_x + draw_left - source_x;
                    expand_palette_chunk::<BPP, P>(
                        dest_row.data.offset(draw_dest_x as isize),
                        src_row.data,
                        draw_left,
                        (draw_right - draw_left) as usize,
                        palette,
                        context,
                    );
                }
                dest_x += tile_length;
                source_x = src_left;
            }
        }
        src_y += 1;
        if src_y == src_top + src_height {
            src_y = src_top;
        }
    }
}

fn make_context(compositing_mode: i32, tint: GColor8) -> CompositeContext {
    let mut context = CompositeContext {
        tint: tint.argb,
        tint_luminance: [GColor8 { argb: 0 }; 4],
    };
    if compositing_mode == COMP_OP_TINT_LUMINANCE {
        unsafe {
            gcolor_tint_luminance_lookup_table_init(tint, context.tint_luminance.as_mut_ptr())
        };
    }
    context
}

#[no_mangle]
pub unsafe extern "C" fn bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(
    dest_bitmap: *mut c_void,
    src_bitmap: *const c_void,
    dest_rect: GRect,
    src_origin_offset: GPoint,
    compositing_mode: i32,
    tint: GColor8,
) {
    if dest_bitmap.is_null() || src_bitmap.is_null() {
        return;
    }
    let context = make_context(compositing_mode, tint);
    match compositing_mode {
        COMP_OP_ASSIGN | COMP_OP_ASSIGN_INVERTED | COMP_OP_OR | COMP_OP_AND | COMP_OP_CLEAR => {
            walk_8bit_rows::<Assign>(
                dest_bitmap,
                src_bitmap,
                dest_rect,
                src_origin_offset,
                &context,
                true,
            );
        }
        COMP_OP_TINT => walk_8bit_rows::<Tint>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            &context,
            false,
        ),
        COMP_OP_TINT_LUMINANCE => walk_8bit_rows::<TintLuminance>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            &context,
            false,
        ),
        _ => walk_8bit_rows::<Set>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            &context,
            false,
        ),
    }
}

unsafe fn dispatch_palette<P: PixelOp>(
    bpp: u8,
    dest_bitmap: *mut c_void,
    src_bitmap: *const c_void,
    dest_rect: GRect,
    src_origin_offset: GPoint,
    palette: *const GColor8,
    context: &CompositeContext,
) {
    match bpp {
        1 => walk_palette_rows::<1, P>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            context,
        ),
        2 => walk_palette_rows::<2, P>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            context,
        ),
        4 => walk_palette_rows::<4, P>(
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            context,
        ),
        _ => {}
    }
}

#[no_mangle]
pub unsafe extern "C" fn bitblt_bitmap_into_bitmap_tiled_palette_to_8bit(
    dest_bitmap: *mut c_void,
    src_bitmap: *const c_void,
    dest_rect: GRect,
    src_origin_offset: GPoint,
    compositing_mode: i32,
    tint: GColor8,
) {
    if dest_bitmap.is_null() || src_bitmap.is_null() {
        return;
    }
    let palette = gbitmap_get_palette(src_bitmap);
    if palette.is_null() {
        return;
    }
    let bpp = gbitmap_get_bits_per_pixel(gbitmap_get_format(src_bitmap));
    let context = make_context(compositing_mode, tint);
    match compositing_mode {
        COMP_OP_ASSIGN => dispatch_palette::<Assign>(
            bpp,
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            &context,
        ),
        COMP_OP_SET => dispatch_palette::<Set>(
            bpp,
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            &context,
        ),
        COMP_OP_TINT => dispatch_palette::<Tint>(
            bpp,
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            &context,
        ),
        COMP_OP_TINT_LUMINANCE => dispatch_palette::<TintLuminance>(
            bpp,
            dest_bitmap,
            src_bitmap,
            dest_rect,
            src_origin_offset,
            palette,
            &context,
        ),
        _ => {}
    }
}
