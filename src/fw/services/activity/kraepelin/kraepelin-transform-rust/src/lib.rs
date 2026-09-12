// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

const AXES: usize = 3;
const WIDTH: usize = 128;
const MAGNITUDES: usize = WIDTH / 2;
const FULL_EPOCH: usize = 125;
const TRIG_SCALE: i32 = 65_536;
const TRIG_RATIO: i32 = 65_535;

extern "C" {
    fn sin_lookup(angle: i32) -> i32;
}

#[rustfmt::skip]
const FULL_WINDOW: [u16; FULL_EPOCH] = [
    0, 1645, 3290, 4933, 6573, 8209, 9839, 11470,
    13087, 14695, 16295, 17884, 19462, 21027, 22586, 24124,
    25646, 27153, 28643, 30114, 31566, 33005, 34416, 35806,
    37174, 38517, 39837, 41131, 42404, 43646, 44860, 46046,
    47203, 48329, 49426, 50495, 51528, 52529, 53497, 54431,
    55330, 56195, 57027, 57820, 58577, 59297, 59979, 60623,
    61229, 61799, 62327, 62816, 63265, 63675, 64044, 64373,
    64662, 64909, 65116, 65281, 65405, 65488, 65529, 65529,
    65488, 65405, 65281, 65116, 64910, 64663, 64374, 64045,
    63676, 63267, 62818, 62329, 61801, 61231, 60625, 59981,
    59299, 58580, 57823, 57030, 56198, 55334, 54434, 53501,
    52533, 51532, 50499, 49430, 48334, 47207, 46050, 44865,
    43651, 42409, 41136, 39842, 38522, 37179, 35811, 34422,
    33010, 31572, 30120, 28649, 27159, 25652, 24130, 22592,
    21033, 19468, 17890, 16301, 14701, 13093, 11476, 9846,
    8215, 6580, 4940, 3297, 1652,
];

#[rustfmt::skip]
const TWIDDLES: [(u16, u16); 57] = [
    (46340, 46340),
    (25079, 60546), (46340, 46340), (60546, 25079),
    (12785, 64276), (25079, 60546), (36409, 54490), (46340, 46340),
    (54490, 36409), (60546, 25079), (64276, 12785),
    (6423, 65219), (12785, 64276), (19024, 62713), (25079, 60546),
    (30893, 57797), (36409, 54490), (41575, 50659), (46340, 46340),
    (50659, 41575), (54490, 36409), (57797, 30893), (60546, 25079),
    (62713, 19024), (64276, 12785), (65219, 6423),
    (3215, 65456), (6423, 65219), (9616, 64825), (12785, 64276),
    (15923, 63571), (19024, 62713), (22078, 61704), (25079, 60546),
    (28020, 59243), (30893, 57797), (33692, 56211), (36409, 54490),
    (39039, 52638), (41575, 50659), (44010, 48558), (46340, 46340),
    (48558, 44010), (50659, 41575), (52638, 39039), (54490, 36409),
    (56211, 33692), (57797, 30893), (59243, 28020), (60546, 25079),
    (61704, 22078), (62713, 19024), (63571, 15923), (64276, 12785),
    (64825, 9616), (65219, 6423), (65456, 3215),
];

#[rustfmt::skip]
const BIT_REVERSE_SWAPS: [(u8, u8); 56] = [
    (1, 64), (2, 32), (3, 96), (4, 16), (5, 80), (6, 48), (7, 112),
    (9, 72), (10, 40), (11, 104), (12, 24), (13, 88), (14, 56), (15, 120),
    (17, 68), (18, 36), (19, 100), (21, 84), (22, 52), (23, 116),
    (25, 76), (26, 44), (27, 108), (29, 92), (30, 60), (31, 124),
    (33, 66), (35, 98), (37, 82), (38, 50), (39, 114), (41, 74),
    (43, 106), (45, 90), (46, 58), (47, 122), (49, 70), (51, 102),
    (53, 86), (55, 118), (57, 78), (59, 110), (61, 94), (63, 126),
    (67, 97), (69, 81), (71, 113), (75, 105), (77, 89), (79, 121),
    (83, 101), (87, 117), (91, 109), (95, 125), (103, 115), (111, 123),
];

#[inline(always)]
unsafe fn read(samples: *const i16, index: usize) -> i16 {
    *samples.add(index)
}

#[inline(always)]
unsafe fn write(samples: *mut i16, index: usize, value: i16) {
    *samples.add(index) = value;
}

#[inline(always)]
fn unsigned_divide(numerator: u32, denominator: u32) -> u32 {
    let mut quotient = 0_u32;
    let mut remainder = 0_u32;
    for shift in (0..32).rev() {
        remainder = (remainder << 1) | ((numerator >> shift) & 1);
        if remainder >= denominator {
            remainder -= denominator;
            quotient |= 1 << shift;
        }
    }
    quotient
}

#[inline(always)]
fn signed_divide(numerator: i32, denominator: i32) -> i32 {
    let quotient = unsigned_divide(numerator.unsigned_abs(), denominator as u32) as i32;
    if numerator < 0 {
        quotient.wrapping_neg()
    } else {
        quotient
    }
}

#[inline(always)]
unsafe fn mean(samples: *const i16, num_samples: usize) -> i32 {
    let mut sum = 0_i32;
    for index in 0..num_samples {
        sum += i32::from(read(samples, index));
    }
    if num_samples == FULL_EPOCH {
        sum / FULL_EPOCH as i32
    } else {
        signed_divide(sum, num_samples as i32)
    }
}

#[inline(always)]
fn integer_sqrt(value: u32) -> u32 {
    let mut remainder = value;
    let mut result = 0_u32;
    let mut bit = 1_u32 << 30;
    while bit > remainder {
        bit >>= 2;
    }
    while bit != 0 {
        if remainder >= result.wrapping_add(bit) {
            remainder = remainder.wrapping_sub(result.wrapping_add(bit));
            result = result.wrapping_add(bit << 1);
        }
        result >>= 1;
        bit >>= 2;
    }
    result
}

#[inline(always)]
fn square(value: i16) -> u32 {
    let value = i32::from(value);
    value.wrapping_mul(value) as u32
}

unsafe fn window(samples: *mut i16, num_samples: usize) {
    let sample_mean = mean(samples, num_samples);
    for index in 0..num_samples {
        let coefficient = if num_samples == FULL_EPOCH {
            i32::from(*FULL_WINDOW.get_unchecked(index))
        } else {
            sin_lookup(signed_divide(
                TRIG_SCALE * index as i32,
                2 * num_samples as i32,
            ))
        };
        let centred = i32::from(read(samples, index)).wrapping_sub(sample_mean);
        write(
            samples,
            index,
            (centred.wrapping_mul(coefficient) / TRIG_RATIO) as i16,
        );
    }
}

unsafe fn fft(samples: *mut i16) {
    for &(left, right) in &BIT_REVERSE_SWAPS {
        let left = usize::from(left);
        let right = usize::from(right);
        let value = read(samples, left);
        write(samples, left, read(samples, right));
        write(samples, right, value);
    }

    let mut index = 0;
    while index < WIDTH {
        let value = read(samples, index);
        let next = read(samples, index + 1);
        write(samples, index, value.wrapping_add(next));
        write(samples, index + 1, value.wrapping_sub(next));
        index += 2;
    }

    const STAGES: [(usize, usize, usize); 6] = [
        (1, 4, 0),
        (2, 8, 0),
        (4, 16, 1),
        (8, 32, 4),
        (16, 64, 11),
        (32, 128, 26),
    ];
    for &(quarter, group_width, twiddle_start) in &STAGES {
        let half = quarter * 2;
        let mut group = 0;
        while group < WIDTH {
            let value = read(samples, group);
            let half_value = read(samples, group + half);
            write(samples, group, value.wrapping_add(half_value));
            write(samples, group + half, value.wrapping_sub(half_value));
            write(
                samples,
                group + quarter + half,
                read(samples, group + quarter + half).wrapping_neg(),
            );

            for offset in 1..quarter {
                let i1 = group + offset;
                let i2 = group + half - offset;
                let i3 = group + half + offset;
                let i4 = group + group_width - offset;
                let (sine, cosine) = *TWIDDLES.get_unchecked(twiddle_start + offset - 1);
                let sine = i32::from(sine);
                let cosine = i32::from(cosine);
                let t1 = i32::from(read(samples, i3))
                    .wrapping_mul(cosine)
                    .wrapping_add(i32::from(read(samples, i4)).wrapping_mul(sine))
                    / TRIG_SCALE;
                let t2 = i32::from(read(samples, i3))
                    .wrapping_mul(sine)
                    .wrapping_sub(i32::from(read(samples, i4)).wrapping_mul(cosine))
                    / TRIG_SCALE;
                let t1 = t1 as i16;
                let t2 = t2 as i16;
                let value1 = read(samples, i1);
                let value2 = read(samples, i2);
                write(samples, i4, value2.wrapping_sub(t2));
                write(samples, i3, value2.wrapping_neg().wrapping_sub(t2));
                write(samples, i2, value1.wrapping_sub(t1));
                write(samples, i1, value1.wrapping_add(t1));
            }
            group += group_width;
        }
    }
}

unsafe fn magnitudes(samples: *mut i16) {
    for index in 1..MAGNITUDES {
        write(
            samples,
            index,
            integer_sqrt(
                square(read(samples, index)).wrapping_add(square(read(samples, WIDTH - index))),
            ) as i16,
        );
    }
}

#[no_mangle]
pub unsafe extern "C" fn kraepelin_transform_window(samples: *mut i16, num_samples: u16) {
    window(samples, usize::from(num_samples));
}

#[no_mangle]
pub unsafe extern "C" fn kraepelin_transform_fft(samples: *mut i16) {
    fft(samples);
}

#[no_mangle]
pub unsafe extern "C" fn kraepelin_transform_magnitudes(samples: *mut i16) {
    magnitudes(samples);
}

#[no_mangle]
pub unsafe extern "C" fn kraepelin_transform_epoch(
    samples: *mut i16,
    num_samples: u16,
    output: *mut i16,
) {
    let num_samples = usize::from(num_samples);
    for axis_index in 0..AXES {
        let axis = samples.add(axis_index * WIDTH);
        window(axis, num_samples);
        for index in 0..num_samples {
            write(axis, index, read(axis, index) / 2);
        }
        let sample_mean = mean(axis, num_samples) as i16;
        for index in 0..num_samples {
            write(axis, index, read(axis, index).wrapping_sub(sample_mean));
        }
        for index in num_samples..WIDTH {
            write(axis, index, 0);
        }
        fft(axis);
        magnitudes(axis);
    }

    for index in 0..MAGNITUDES {
        write(
            output,
            index,
            integer_sqrt(
                square(read(samples, index))
                    .wrapping_add(square(read(samples, WIDTH + index)))
                    .wrapping_add(square(read(samples, 2 * WIDTH + index))),
            ) as i16,
        );
    }
}
