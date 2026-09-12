// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

#[inline]
unsafe fn load_bits(source: *const u32, bit_offset: usize, count: u32) -> u32 {
    let word_index = bit_offset / 32;
    let shift = (bit_offset % 32) as u32;
    let mut bits = source.add(word_index).read_unaligned() >> shift;
    if shift != 0 && count > 32 - shift {
        bits |= source.add(word_index + 1).read_unaligned() << (32 - shift);
    }
    if count < 32 {
        bits &= (1_u32 << count) - 1;
    }
    bits
}

#[inline]
unsafe fn rasterize(
    mut offset: usize,
    mut remaining: u32,
    source: *const u32,
    mut write: impl FnMut(usize),
) {
    let mut destination_offset = 0_usize;
    while remaining != 0 {
        let count = remaining.min(32);
        let mut set_bits = load_bits(source, offset, count);
        while set_bits != 0 {
            let bit = set_bits.trailing_zeros() as usize;
            write(destination_offset + bit);
            set_bits &= set_bits - 1;
        }
        offset += count as usize;
        destination_offset += count as usize;
        remaining -= count;
    }
}

/// Rasterize a clipped, contiguous glyph row into an 8-bit framebuffer row.
///
/// `blend_33_lut`, when non-null, is the platform's 64-by-64 33% blend table.
/// Otherwise `color` is assigned directly.
#[no_mangle]
pub unsafe extern "C" fn glyph_rasterize_8bit_row(
    source: *const u32,
    source_bit_offset: u32,
    destination: *mut u8,
    width: u32,
    color: u8,
    blend_33_lut: *const u8,
) {
    if blend_33_lut.is_null() {
        rasterize(source_bit_offset as usize, width, source, |index| {
            *destination.add(index) = color;
        });
    } else if color >> 6 == 1 {
        let source_row = 64 * (color & 0x3f) as usize;
        rasterize(source_bit_offset as usize, width, source, |index| {
            let pixel = destination.add(index);
            *pixel = *blend_33_lut.add(source_row + (*pixel & 0x3f) as usize);
        });
    } else {
        let source_column = (color & 0x3f) as usize;
        rasterize(source_bit_offset as usize, width, source, |index| {
            let pixel = destination.add(index);
            *pixel = *blend_33_lut.add(source_column + 64 * (*pixel & 0x3f) as usize);
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reference(
        source: &[u32],
        bit_offset: usize,
        destination: &mut [u8],
        color: u8,
        blend_lut: Option<&[u8; 4096]>,
    ) {
        for (index, pixel) in destination.iter_mut().enumerate() {
            let source_index = bit_offset + index;
            if source[source_index / 32] & (1 << (source_index % 32)) != 0 {
                *pixel = blend_lut.map_or(color, |lookup| {
                    let source_rgb = (color & 0x3f) as usize;
                    let destination_rgb = (*pixel & 0x3f) as usize;
                    let lookup_index = if color >> 6 == 1 {
                        destination_rgb + 64 * source_rgb
                    } else {
                        source_rgb + 64 * destination_rgb
                    };
                    lookup[lookup_index]
                });
            }
        }
    }

    fn differential(source: &[u32], offset: usize, width: usize, blend_alpha: Option<u8>) {
        let mut actual = [0_u8; 80];
        for (index, pixel) in actual.iter_mut().enumerate() {
            *pixel = (index * 37) as u8;
        }
        let mut expected = actual;
        let mut lookup = [0_u8; 4096];
        for (index, value) in lookup.iter_mut().enumerate() {
            *value = 0xc0 | ((index * 29) & 0x3f) as u8;
        }
        let lookup_ptr = if blend_alpha.is_some() {
            lookup.as_ptr()
        } else {
            core::ptr::null()
        };
        let color = blend_alpha.map_or(0xea, |alpha| (alpha << 6) | 0x2a);
        reference(
            source,
            offset,
            &mut expected[5..5 + width],
            color,
            blend_alpha.map(|_| &lookup),
        );
        unsafe {
            glyph_rasterize_8bit_row(
                source.as_ptr(),
                offset as u32,
                actual[5..].as_mut_ptr(),
                width as u32,
                color,
                lookup_ptr,
            );
        }
        assert_eq!(
            actual, expected,
            "offset={offset}, width={width}, blend_alpha={blend_alpha:?}"
        );
    }

    #[test]
    fn rasterizes_unaligned_sparse_and_dense_spans() {
        let source = [0x8000_0001, 0xffff_fffe, 0x0000_0001];
        let mut destination = [0_u8; 65];
        unsafe {
            glyph_rasterize_8bit_row(
                source.as_ptr(),
                31,
                destination.as_mut_ptr(),
                34,
                0xd5,
                core::ptr::null(),
            );
        }
        assert_eq!(destination[0], 0xd5);
        assert_eq!(destination[1], 0);
        assert!(destination[2..33].iter().all(|&pixel| pixel == 0xd5));
        assert_eq!(destination[33], 0xd5);
        assert!(destination[34..].iter().all(|&pixel| pixel == 0));
    }

    #[test]
    fn uses_blend_lookup_only_for_set_pixels() {
        let source = [0b10101_u32];
        let mut destination = [1_u8, 2, 3, 4, 5];
        let mut lookup = [0_u8; 4096];
        for (index, value) in lookup.iter_mut().enumerate() {
            *value = 0xc0 | (index & 0x3f) as u8;
        }
        unsafe {
            glyph_rasterize_8bit_row(
                source.as_ptr(),
                0,
                destination.as_mut_ptr(),
                5,
                0x40,
                lookup.as_ptr(),
            );
        }
        assert_eq!(destination, [0xc1, 2, 0xc3, 4, 0xc5]);
    }

    #[test]
    fn matches_reference_for_clipped_sparse_and_dense_glyph_rows() {
        let sparse = [0x8000_0001, 0x0001_0000, 0x4000_0002, 0x0000_0001];
        let dense = [0xffff_ffff, 0xefff_fffe, 0xffff_ffff, 0x0000_0001];
        for source in [&sparse, &dense] {
            // Width 3 exercises narrow glyphs. Width 47 crosses words. Offsets model
            // left/top clipping and the destination starts at an unaligned x (5).
            for (offset, width) in [(0, 3), (1, 2), (32, 47), (37, 41), (63, 34)] {
                differential(source, offset, width, None);
                differential(source, offset, width, Some(1));
                differential(source, offset, width, Some(2));
            }
        }
    }

    #[test]
    fn matches_reference_for_every_bit_alignment() {
        let source = [0xa5a5_5a5a, 0x0123_4567, 0x89ab_cdef];
        for offset in 0..32 {
            differential(&source, offset, 47, None);
            differential(&source, offset, 47, Some(1));
            differential(&source, offset, 47, Some(2));
        }
    }
}
