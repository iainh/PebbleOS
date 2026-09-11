// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::c_int;

const OUTPUT_STATE_SIZE: usize = 4;
const INPUT_COEFFICIENT: i64 = 0x0721_d150;
const OUTPUT_COEFFICIENTS: [(i32, u32); OUTPUT_STATE_SIZE] = [
    (-4, 0x92b0_910c),
    (4, 0x73f9_a693),
    (-3, 0x633c_7d23),
    (0, 0x9640_5b5c),
];

#[repr(C, packed)]
pub struct FixedS64_32 {
    raw_value: i64,
}

#[inline(always)]
fn multiply_output(coefficient: (i32, u32), value: i64) -> i64 {
    let (integer, fraction) = coefficient;
    let value_integer = value >> 32;
    let value_fraction = value as u32;
    let integer_term = value.wrapping_mul(i64::from(integer));
    let mixed_term = i64::from(fraction).wrapping_mul(value_integer);
    let fraction_term = (u64::from(fraction).wrapping_mul(u64::from(value_fraction)) >> 32) as i64;
    integer_term
        .wrapping_add(mixed_term)
        .wrapping_add(fraction_term)
}

#[inline(always)]
unsafe fn read_state(state: *const FixedS64_32, index: usize) -> i64 {
    core::ptr::read_unaligned(state.add(index).cast())
}

#[inline(always)]
unsafe fn write_state(state: *mut FixedS64_32, index: usize, value: i64) {
    core::ptr::write_unaligned(state.add(index).cast(), value);
}

#[no_mangle]
pub unsafe extern "C" fn kraepelin_pim_sum(
    samples: *const i16,
    num_samples: c_int,
    state_x: *mut FixedS64_32,
    state_y: *mut FixedS64_32,
) -> i32 {
    if num_samples <= 0 {
        return 0;
    }

    let mut x0 = read_state(state_x, 0);
    let mut x1 = read_state(state_x, 1);
    let mut x2 = read_state(state_x, 2);
    let mut x3 = read_state(state_x, 3);
    let mut x4 = read_state(state_x, 4);
    let mut y0 = read_state(state_y, 0);
    let mut y1 = read_state(state_y, 1);
    let mut y2 = read_state(state_y, 2);
    let mut y3 = read_state(state_y, 3);
    let mut pim = 0_i32;

    for index in 0..num_samples as usize {
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
        let sample = i64::from(*samples.add(index));
        x0 = sample << 32;

        let feed_forward = sample
            .wrapping_sub(2_i64.wrapping_mul(x2 >> 32))
            .wrapping_add(x4 >> 32);
        let mut output = INPUT_COEFFICIENT.wrapping_mul(feed_forward);
        output = output.wrapping_sub(multiply_output(OUTPUT_COEFFICIENTS[0], y0));
        output = output.wrapping_sub(multiply_output(OUTPUT_COEFFICIENTS[1], y1));
        output = output.wrapping_sub(multiply_output(OUTPUT_COEFFICIENTS[2], y2));
        output = output.wrapping_sub(multiply_output(OUTPUT_COEFFICIENTS[3], y3));

        y3 = y2;
        y2 = y1;
        y1 = y0;
        y0 = output;
        pim = pim.wrapping_add(((output >> 32) as i32).wrapping_abs());
    }

    write_state(state_x, 0, x0);
    write_state(state_x, 1, x1);
    write_state(state_x, 2, x2);
    write_state(state_x, 3, x3);
    write_state(state_x, 4, x4);
    write_state(state_y, 0, y0);
    write_state(state_y, 1, y1);
    write_state(state_y, 2, y2);
    write_state(state_y, 3, y3);

    pim
}
