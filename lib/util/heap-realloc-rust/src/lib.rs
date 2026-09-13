// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

/// Returns the allocation extent to retain, or zero when the block must move.
#[no_mangle]
pub extern "C" fn heap_realloc_plan(
    current_units: u16,
    next_free_units: u16,
    requested_units: u16,
    minimum_free_units: u16,
) -> u16 {
    let Some(available_units) = current_units.checked_add(next_free_units) else {
        return 0;
    };

    if requested_units > available_units {
        return 0;
    }

    if available_units - requested_units >= minimum_free_units {
        requested_units
    } else {
        available_units
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shrinks_and_returns_reusable_remainder() {
        assert_eq!(heap_realloc_plan(20, 0, 8, 2), 8);
    }

    #[test]
    fn grows_into_adjacent_free_block() {
        assert_eq!(heap_realloc_plan(8, 12, 15, 2), 15);
    }

    #[test]
    fn retains_tiny_remainder_in_allocation() {
        assert_eq!(heap_realloc_plan(8, 8, 15, 2), 16);
    }

    #[test]
    fn rejects_move_and_overflow_cases() {
        assert_eq!(heap_realloc_plan(8, 3, 12, 2), 0);
        assert_eq!(heap_realloc_plan(u16::MAX, 1, 8, 2), 0);
    }
}
