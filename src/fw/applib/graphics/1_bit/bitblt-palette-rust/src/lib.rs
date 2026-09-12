// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RowLookUp {
    transparent_mask: [u8; 4],
    palette_pattern: [u32; 4],
}

#[repr(C)]
pub struct PaletteBlitData {
    dest_block_x_begin: *mut u32,
    src: *const u8,
    look_ups: *const [RowLookUp; 2],
    dest_origin_y: i16,
    dest_end_y: i16,
    dest_width: i16,
    src_begin_x: i16,
    src_begin_y: i16,
    src_end_x: i16,
    src_end_y: i16,
    src_offset_x: i16,
    src_offset_y: i16,
    src_row_size_bytes: u16,
    dest_row_length_words: i16,
    dest_shift_at_line_begin: u8,
    num_dest_blocks_per_row: u8,
    src_bpp: u8,
}

#[inline(always)]
unsafe fn palette_index(data: &PaletteBlitData, x: i16, y: i16) -> usize {
    let x = x as usize;
    let row = y as usize * usize::from(data.src_row_size_bytes);
    if data.src_bpp == 1 {
        let byte = *data.src.add(row + (x >> 3));
        usize::from((byte >> (7 - (x & 7))) & 1)
    } else {
        let byte = *data.src.add(row + (x >> 2));
        usize::from((byte >> ((3 - (x & 3)) << 1)) & 3)
    }
}

#[no_mangle]
pub unsafe extern "C" fn bitblt_palette_to_1bit_blit_rust(data: *const PaletteBlitData) {
    let data = &*data;
    let look_ups = data.look_ups.cast::<RowLookUp>();
    let mut src_y = data.src_begin_y + data.src_offset_y;

    for dest_y in data.dest_origin_y..data.dest_end_y {
        if src_y >= data.src_end_y {
            src_y = data.src_begin_y;
        }
        let look_up = &*look_ups.add((dest_y & 1) as usize);
        let mut src_x = data.src_begin_x + data.src_offset_x;
        let mut bits_left = data.dest_width;
        let mut dest_shift = data.dest_shift_at_line_begin;
        let mut dest_block = data
            .dest_block_x_begin
            .offset(isize::from(dest_y) * isize::from(data.dest_row_length_words));

        for _ in 0..data.num_dest_blocks_per_row {
            let mut write_mask = 0_u32;
            let mut pattern = 0_u32;
            let mut dest_x = dest_shift;
            while dest_x < 32 && bits_left > 0 {
                if src_x >= data.src_end_x {
                    src_x = data.src_begin_x;
                }
                let index = palette_index(data, src_x, src_y);
                let bit = 1_u32 << dest_x;
                if *look_up.transparent_mask.get_unchecked(index) != 0 {
                    write_mask |= bit;
                    pattern |= *look_up.palette_pattern.get_unchecked(index) & bit;
                }
                dest_x += 1;
                bits_left -= 1;
                src_x += 1;
            }
            // Compose the complete destination word in registers, then update it once.
            let previous = *dest_block;
            *dest_block = (previous & !write_mask) | pattern;
            dest_block = dest_block.add(1);
            dest_shift = 0;
        }
        src_y += 1;
    }
}
