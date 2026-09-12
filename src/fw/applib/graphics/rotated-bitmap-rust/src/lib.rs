// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

const TRIG_MAX_RATIO: i32 = 0xffff;
const FORMAT_1BIT_PALETTE: u8 = 2;
const FORMAT_2BIT_PALETTE: u8 = 3;
const FORMAT_4BIT_PALETTE: u8 = 4;
const COMP_OP_OR: u8 = 2;
const COMP_OP_SET: u8 = 5;

#[repr(C)]
struct RowInfo {
    data: *mut u8,
    min_x: i16,
    max_x: i16,
}

#[repr(C)]
pub struct RasterArgs {
    src: *const (),
    dest: *const (),
    src_addr: *const u8,
    clip_min_x: i32,
    clip_min_y: i32,
    clip_max_x: i32,
    clip_max_y: i32,
    src_ic_x: i32,
    src_ic_y: i32,
    dest_ic_x: i32,
    dest_ic_y: i32,
    cos_value: i32,
    sin_value: i32,
    src_width: i32,
    src_height: i32,
    src_row_size: u16,
    palette: *const u8,
    src_format: u8,
    src_bpp: u8,
    compositing_mode: u8,
    tint_color: u8,
    screen_is_bw: u8,
    foreground: u8,
    background: u8,
}

extern "C" {
    fn gbitmap_get_data_row_info(bitmap: *const (), y: u16) -> RowInfo;
    fn graphics_rotated_bitmap_blend(src: u8, dest: u8) -> u8;
}

#[inline(always)]
fn raw_value(row: *const u8, x: i32, bpp: u8) -> u8 {
    let x = x as usize;
    unsafe {
        match bpp {
            1 => (*row.add(x >> 3) >> (7 - (x & 7))) & 1,
            2 => (*row.add(x >> 2) >> ((3 - (x & 3)) * 2)) & 3,
            4 => (*row.add(x >> 1) >> ((1 - (x & 1)) * 4)) & 15,
            8 => *row.add(x),
            _ => 0,
        }
    }
}

#[inline(always)]
fn source_color(args: &RasterArgs, row: *const u8, x: i32) -> u8 {
    let index = raw_value(row, x, args.src_bpp);
    match args.src_format {
        FORMAT_1BIT_PALETTE | FORMAT_2BIT_PALETTE | FORMAT_4BIT_PALETTE => unsafe {
            *args.palette.add(index as usize)
        },
        _ => index,
    }
}

#[inline(always)]
unsafe fn source_bit(args: &RasterArgs, x: i32, y: i32) -> bool {
    let byte = *args
        .src_addr
        .add(y as usize * usize::from(args.src_row_size) + x as usize / 8);
    byte & (1 << (x & 7)) != 0
}

#[inline(always)]
unsafe fn draw_pixel_bw(
    args: &RasterArgs,
    dest_row: *mut u8,
    x: i32,
    src_x: i32,
    src_y: i32,
    rem_x: i32,
    rem_y: i32,
) {
    let horizontal = contributions(rem_x);
    let vertical = contributions(rem_y);
    let mut threshold = 0;
    for j in -1..=1 {
        for i in -1..=1 {
            let sample_x = src_x + i;
            let sample_y = src_y + j;
            if sample_x >= 0
                && sample_x < args.src_width
                && sample_y >= 0
                && sample_y < args.src_height
            {
                let weight = horizontal[(i + 1) as usize] * vertical[(j + 1) as usize];
                threshold += if source_bit(args, sample_x, sample_y) {
                    weight
                } else {
                    -weight
                };
            }
        }
    }
    let color = if threshold > 0 {
        args.foreground
    } else {
        args.background
    };
    if color >> 6 != 0 {
        let mask = 1_u8 << (x & 7);
        let byte = dest_row.add(x as usize / 8);
        if color == 0xff {
            *byte |= mask
        } else {
            *byte &= !mask
        }
    }
}

#[inline(always)]
fn contributions(rem: i32) -> [i32; 3] {
    if rem < 0 {
        [-rem >> 3, (TRIG_MAX_RATIO + rem) >> 3, 0]
    } else {
        [0, (TRIG_MAX_RATIO - rem) >> 3, rem >> 3]
    }
}

#[inline(always)]
unsafe fn draw_pixel_color(
    args: &RasterArgs,
    dest_row: *mut u8,
    x: i32,
    src_row: *const u8,
    src_x: i32,
) {
    let src = source_color(args, src_row, src_x);
    let dest = dest_row.add(x as usize);
    let color = match args.compositing_mode {
        COMP_OP_SET => graphics_rotated_bitmap_blend(src, *dest),
        COMP_OP_OR if args.tint_color >> 6 != 0 => {
            let actual = (args.tint_color & 0x3f) | (src & 0xc0);
            graphics_rotated_bitmap_blend(actual, *dest)
        }
        _ => src,
    };
    *dest = color | 0xc0;
}

fn tightened_bounds(args: &RasterArgs) -> (i32, i32, i32, i32) {
    let mut min_x = i32::MAX;
    let mut min_y = i32::MAX;
    let mut max_x = i32::MIN;
    let mut max_y = i32::MIN;
    let xs = [-args.src_ic_x - 1, args.src_width - args.src_ic_x + 1];
    let ys = [-args.src_ic_y - 1, args.src_height - args.src_ic_y + 1];
    for source_y in ys {
        for source_x in xs {
            let x = args.dest_ic_x
                + (args.cos_value * source_x + args.sin_value * source_y) / TRIG_MAX_RATIO;
            let y = args.dest_ic_y
                + (-args.sin_value * source_x + args.cos_value * source_y) / TRIG_MAX_RATIO;
            min_x = min_x.min(x);
            min_y = min_y.min(y);
            max_x = max_x.max(x);
            max_y = max_y.max(y);
        }
    }
    (
        args.clip_min_x.max(min_x - 2),
        args.clip_min_y.max(min_y - 2),
        args.clip_max_x.min(max_x + 3),
        args.clip_max_y.min(max_y + 3),
    )
}

#[no_mangle]
pub unsafe extern "C" fn graphics_draw_rotated_bitmap_rust(args: &RasterArgs) {
    let (min_x, min_y, max_x, max_y) = tightened_bounds(args);
    if min_x >= max_x || min_y >= max_y {
        return;
    }

    let mut row_numerator_x =
        args.cos_value * (min_x - args.dest_ic_x) - args.sin_value * (min_y - args.dest_ic_y);
    let mut row_numerator_y =
        args.cos_value * (min_y - args.dest_ic_y) + args.sin_value * (min_x - args.dest_ic_x);

    for y in min_y..max_y {
        let dest_info = gbitmap_get_data_row_info(args.dest, y as u16);
        let start_x = min_x.max(i32::from(dest_info.min_x));
        let end_x = max_x.min(i32::from(dest_info.max_x) + 1);
        let skipped = start_x - min_x;
        let mut numerator_x = row_numerator_x + skipped * args.cos_value;
        let mut numerator_y = row_numerator_y + skipped * args.sin_value;
        let mut cached_y = i32::MIN;
        let mut source_row = core::ptr::null();
        let mut source_min_x = 0;
        let mut source_max_x = -1;

        for x in start_x..end_x {
            let src_vector_x = numerator_x / TRIG_MAX_RATIO;
            let src_vector_y = numerator_y / TRIG_MAX_RATIO;
            let src_x = args.src_ic_x + src_vector_x;
            let src_y = args.src_ic_y + src_vector_y;
            if src_x >= 0 && src_x < args.src_width && src_y >= 0 && src_y < args.src_height {
                if cached_y != src_y {
                    let src_info = gbitmap_get_data_row_info(args.src, src_y as u16);
                    cached_y = src_y;
                    source_row = src_info.data;
                    source_min_x = i32::from(src_info.min_x);
                    source_max_x = i32::from(src_info.max_x);
                }
                if src_x >= source_min_x && src_x <= source_max_x {
                    if args.screen_is_bw != 0 {
                        draw_pixel_bw(
                            args,
                            dest_info.data,
                            x,
                            src_x,
                            src_y,
                            numerator_x % TRIG_MAX_RATIO,
                            numerator_y % TRIG_MAX_RATIO,
                        );
                    } else {
                        draw_pixel_color(args, dest_info.data, x, source_row, src_x);
                    }
                }
            }
            numerator_x += args.cos_value;
            numerator_y += args.sin_value;
        }
        row_numerator_x -= args.sin_value;
        row_numerator_y += args.cos_value;
    }
}
