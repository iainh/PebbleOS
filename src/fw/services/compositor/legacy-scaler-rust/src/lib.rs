// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

const FLAG_BILINEAR: u8 = 1;

#[repr(C)]
pub struct ScaleRowArgs {
    pub dst: *mut u8,
    pub dst_len: u16,
    pub src: *const u8,
    pub src_len: u16,
    pub src_next: *const u8,
    pub src_next_len: u16,
    pub dst_start: u16,
    pub dst_count: u16,
    pub src_min_x: i16,
    pub src_max_x: i16,
    pub src_next_min_x: i16,
    pub src_next_max_x: i16,
    pub first_coord: u16,
    pub coord_limit: u16,
    pub scale_x: u32,
    pub fy: u8,
    pub flags: u8,
}

#[inline]
fn interpolate(p00: u8, p10: u8, p01: u8, p11: u8, fx: u16, fy: u16) -> u8 {
    let mut result = 0xc0;
    for shift in (0..6).step_by(2) {
        let c00 = u16::from((p00 >> shift) & 3);
        let c10 = u16::from((p10 >> shift) & 3);
        let c01 = u16::from((p01 >> shift) & 3);
        let c11 = u16::from((p11 >> shift) & 3);
        let top = c00 * (16 - fx) + c10 * fx;
        let bottom = c01 * (16 - fx) + c11 * fx;
        let channel = (top * (16 - fy) + bottom * fy + 128) >> 8;
        result |= ((channel as u8) & 3) << shift;
    }
    result
}

unsafe fn scale_row(args: &ScaleRowArgs) -> u16 {
    let start = usize::from(args.dst_start);
    let end = start
        .saturating_add(usize::from(args.dst_count))
        .min(usize::from(args.dst_len));
    let mut written = 0;
    for index in start..end {
        let offset = index - start;
        let coord = u32::from(args.first_coord)
            .saturating_add(offset as u32)
            .min(u32::from(args.coord_limit));
        let fixed = coord.wrapping_mul(args.scale_x);
        let x = (fixed >> 16) as usize;
        if (x as i32) < i32::from(args.src_min_x)
            || (x as i32) > i32::from(args.src_max_x)
            || x >= usize::from(args.src_len)
        {
            continue;
        }
        if args.flags & FLAG_BILINEAR == 0 {
            args.dst.add(index).write(args.src.add(x).read());
            written += 1;
            continue;
        }

        let x1 = x
            .saturating_add(1)
            .min(usize::from(args.src_len).saturating_sub(1));
        let p00 = args.src.add(x).read();
        let p10 = if (x1 as i32) <= i32::from(args.src_max_x) {
            args.src.add(x1).read()
        } else {
            p00
        };
        let next_has_x = (x as i32) >= i32::from(args.src_next_min_x)
            && (x as i32) <= i32::from(args.src_next_max_x)
            && x < usize::from(args.src_next_len);
        let next_has_x1 = (x1 as i32) >= i32::from(args.src_next_min_x)
            && (x1 as i32) <= i32::from(args.src_next_max_x)
            && x1 < usize::from(args.src_next_len);
        let p01 = if next_has_x {
            args.src_next.add(x).read()
        } else {
            p00
        };
        let p11 = if next_has_x1 {
            args.src_next.add(x1).read()
        } else {
            p10
        };
        let fx = ((fixed >> 12) & 15) as u16;
        args.dst
            .add(index)
            .write(interpolate(p00, p10, p01, p11, fx, u16::from(args.fy & 15)));
        written += 1;
    }
    written
}

/// Scales one clipped ARGB2222 destination span. Returns the number of pixels written.
///
/// # Safety
/// Every non-null pointer must reference its corresponding `*_len` bytes for the call.
#[no_mangle]
pub unsafe extern "C" fn compositor_scale_argb2222_row(args: *const ScaleRowArgs) -> u16 {
    let Some(args) = args.as_ref() else { return 0 };
    if args.dst.is_null() || args.src.is_null() || args.src_next.is_null() {
        return 0;
    }
    scale_row(args)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reference(args: &ScaleRowArgs, dst: &mut [u8], src: &[u8], next: &[u8]) {
        for i in 0..usize::from(args.dst_count) {
            let coord = (u32::from(args.first_coord) + i as u32).min(u32::from(args.coord_limit));
            let fixed = coord * args.scale_x;
            let x = (fixed >> 16) as usize;
            if x < args.src_min_x.max(0) as usize || x > args.src_max_x as usize {
                continue;
            }
            if args.flags == 0 {
                dst[usize::from(args.dst_start) + i] = src[x];
                continue;
            }
            let x1 = (x + 1).min(src.len() - 1);
            let p00 = src[x];
            let p10 = if x1 <= args.src_max_x as usize {
                src[x1]
            } else {
                p00
            };
            let p01 =
                if x >= args.src_next_min_x.max(0) as usize && x <= args.src_next_max_x as usize {
                    next[x]
                } else {
                    p00
                };
            let p11 = if x1 >= args.src_next_min_x.max(0) as usize
                && x1 <= args.src_next_max_x as usize
            {
                next[x1]
            } else {
                p10
            };
            dst[usize::from(args.dst_start) + i] = interpolate(
                p00,
                p10,
                p01,
                p11,
                ((fixed >> 12) & 15) as u16,
                args.fy as u16,
            );
        }
    }

    #[test]
    fn differential_nearest_and_bilinear_with_clipped_rows() {
        let src: Vec<u8> = (0..37).map(|x| 0xc0 | ((x * 29) as u8 & 0x3f)).collect();
        let next: Vec<u8> = src.iter().rev().copied().collect();
        for flags in [0, FLAG_BILINEAR] {
            for scale_x in [32768, 65536, 87381, 131072] {
                for fy in [0, 3, 8, 15] {
                    let mut actual = [0x55; 48];
                    let mut expected = actual;
                    let args = ScaleRowArgs {
                        dst: actual.as_mut_ptr(),
                        dst_len: 48,
                        src: src.as_ptr(),
                        src_len: 37,
                        src_next: next.as_ptr(),
                        src_next_len: 37,
                        dst_start: 3,
                        dst_count: 31,
                        src_min_x: 2,
                        src_max_x: 30,
                        src_next_min_x: 4,
                        src_next_max_x: 28,
                        first_coord: 0,
                        coord_limit: 36,
                        scale_x,
                        fy,
                        flags,
                    };
                    reference(&args, &mut expected, &src, &next);
                    unsafe { scale_row(&args) };
                    assert_eq!(actual, expected, "flags={flags} scale={scale_x} fy={fy}");
                }
            }
        }
    }

    #[test]
    fn ffi_rejects_null_and_bounds_destination() {
        assert_eq!(
            unsafe { compositor_scale_argb2222_row(core::ptr::null()) },
            0
        );
        let src = [0xc1; 2];
        let mut dst = [0xaa; 2];
        let args = ScaleRowArgs {
            dst: dst.as_mut_ptr(),
            dst_len: 2,
            src: src.as_ptr(),
            src_len: 2,
            src_next: src.as_ptr(),
            src_next_len: 2,
            dst_start: 1,
            dst_count: u16::MAX,
            src_min_x: 0,
            src_max_x: 1,
            src_next_min_x: 0,
            src_next_max_x: 1,
            first_coord: 0,
            coord_limit: 1,
            scale_x: 65536,
            fy: 0,
            flags: 0,
        };
        assert_eq!(unsafe { compositor_scale_argb2222_row(&args) }, 1);
        assert_eq!(dst, [0xaa, 0xc1]);
    }
}
