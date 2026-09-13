// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

use core::slice;

fn plan(
    numerator: u32,
    denominator: u32,
    state: &mut u32,
    available: u16,
    selected: &mut [u16],
) -> (u16, u16) {
    if numerator == 0 || denominator < numerator {
        return (0, 0);
    }

    let mut consumed = 0u16;
    let mut count = 0usize;
    while consumed < available && count < selected.len() {
        *state = state.wrapping_add(numerator);
        consumed += 1;
        if *state >= denominator {
            *state %= denominator;
            selected[count] = consumed - 1;
            count += 1;
        }
    }
    (consumed, count as u16)
}

/// Plans one bounded drain from a subscriber's shared-buffer cursor.
///
/// The low 16 bits of the result are the number of source items consumed and
/// the high 16 bits are the number of selected indexes written to `selected`.
#[no_mangle]
pub unsafe extern "C" fn accel_subsampling_plan(
    numerator: u32,
    denominator: u32,
    state: *mut u32,
    available: u16,
    selected: *mut u16,
    selected_capacity: u16,
) -> u32 {
    if state.is_null() || (selected.is_null() && selected_capacity != 0) {
        return 0;
    }
    let output = if selected_capacity == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(selected, usize::from(selected_capacity))
    };
    let (consumed, selected_count) = plan(numerator, denominator, &mut *state, available, output);
    u32::from(consumed) | (u32::from(selected_count) << 16)
}

#[cfg(test)]
mod tests {
    use super::plan;

    fn reference(n: u32, d: u32, state: &mut u32, available: u16, cap: usize) -> (u16, Vec<u16>) {
        let mut consumed = 0;
        let mut out = Vec::new();
        while consumed < available && out.len() < cap {
            *state = state.wrapping_add(n);
            if *state >= d {
                *state %= d;
                out.push(consumed);
            }
            consumed += 1;
        }
        (consumed, out)
    }

    #[test]
    fn matches_c_phase_across_batch_boundaries() {
        for denominator in 1..32 {
            for numerator in 1..=denominator {
                let mut actual_state = denominator - numerator;
                let mut expected_state = actual_state;
                for &(available, cap) in &[(1, 1), (17, 4), (3, 8), (31, 7), (0, 5)] {
                    let mut indexes = [u16::MAX; 16];
                    let (consumed, count) = plan(
                        numerator,
                        denominator,
                        &mut actual_state,
                        available,
                        &mut indexes[..cap],
                    );
                    let (expected_consumed, expected) =
                        reference(numerator, denominator, &mut expected_state, available, cap);
                    assert_eq!(
                        (consumed, &indexes[..expected.len()]),
                        (expected_consumed, expected.as_slice())
                    );
                    assert_eq!(usize::from(count), expected.len());
                    assert_eq!(actual_state, expected_state);
                }
            }
        }
    }

    #[test]
    fn full_output_does_not_consume_next_source_item() {
        let mut state = 3;
        let mut output = [u16::MAX; 2];
        assert_eq!(plan(2, 5, &mut state, 20, &mut output), (4, 2));
        assert_eq!(output, [0, 3]);
        assert_eq!(state, 1);
    }
}
