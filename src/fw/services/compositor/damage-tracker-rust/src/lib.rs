// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

use core::{cmp, slice};

const TILE_SIZE: usize = 16;
const NO_DIRTY_ROW: u16 = u16::MAX;

fn mark(
    tiles: &mut [u32],
    tile_columns: usize,
    tile_rows: usize,
    x: i16,
    y: i16,
    width: i16,
    height: i16,
) {
    if tile_columns == 0 || tile_rows == 0 || width <= 0 || height <= 0 {
        return;
    }

    let x0 = cmp::max(i32::from(x), 0) as usize / TILE_SIZE;
    let y0 = cmp::max(i32::from(y), 0) as usize / TILE_SIZE;
    let x1 = cmp::min(
        i32::from(x) + i32::from(width),
        (tile_columns * TILE_SIZE) as i32,
    );
    let y1 = cmp::min(
        i32::from(y) + i32::from(height),
        (tile_rows * TILE_SIZE) as i32,
    );
    if x1 <= 0 || y1 <= 0 || x0 >= tile_columns || y0 >= tile_rows {
        return;
    }

    let last_row = (y1 as usize - 1) / TILE_SIZE;
    for tile_y in y0..=last_row {
        if let Some(word) = tiles.get_mut(tile_y / 32) {
            *word |= 1 << (tile_y % 32);
        }
    }
}

fn next_row(
    tiles: &[u32],
    _tile_columns: usize,
    tile_rows: usize,
    start_y: usize,
    height: usize,
) -> u16 {
    let mut row = start_y;
    while row < height {
        let tile_y = row / TILE_SIZE;
        if tile_y >= tile_rows {
            break;
        }
        let dirty = tiles
            .get(tile_y / 32)
            .is_some_and(|word| word & (1 << (tile_y % 32)) != 0);
        if dirty {
            return row as u16;
        }
        row = (tile_y + 1) * TILE_SIZE;
    }
    NO_DIRTY_ROW
}

#[no_mangle]
pub unsafe extern "C" fn compositor_damage_reset(tiles: *mut u32, word_count: u16) {
    if tiles.is_null() {
        return;
    }
    slice::from_raw_parts_mut(tiles, usize::from(word_count)).fill(0);
}

#[no_mangle]
pub unsafe extern "C" fn compositor_damage_mark(
    tiles: *mut u32,
    word_count: u16,
    tile_columns: u16,
    tile_rows: u16,
    x: i16,
    y: i16,
    width: i16,
    height: i16,
) {
    if tiles.is_null() {
        return;
    }
    mark(
        slice::from_raw_parts_mut(tiles, usize::from(word_count)),
        usize::from(tile_columns),
        usize::from(tile_rows),
        x,
        y,
        width,
        height,
    );
}

#[no_mangle]
pub unsafe extern "C" fn compositor_damage_next_row(
    tiles: *const u32,
    word_count: u16,
    tile_columns: u16,
    tile_rows: u16,
    start_y: u16,
    height: u16,
) -> u16 {
    if tiles.is_null() {
        return NO_DIRTY_ROW;
    }
    next_row(
        slice::from_raw_parts(tiles, usize::from(word_count)),
        usize::from(tile_columns),
        usize::from(tile_rows),
        usize::from(start_y),
        usize::from(height),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preserves_disjoint_tile_rows() {
        let mut tiles = [0; 1];
        mark(&mut tiles, 17, 17, 0, 8, 1, 1);
        mark(&mut tiles, 17, 17, 0, 251, 1, 1);

        assert_eq!(next_row(&tiles, 17, 17, 0, 260), 0);
        assert_eq!(next_row(&tiles, 17, 17, 15, 260), 15);
        assert_eq!(next_row(&tiles, 17, 17, 16, 260), 240);
        assert_eq!(next_row(&tiles, 17, 17, 256, 260), u16::MAX);
    }

    #[test]
    fn clips_rectangles_to_the_grid() {
        let mut tiles = [0; 1];
        mark(&mut tiles, 2, 2, -4, -4, 8, 8);
        mark(&mut tiles, 2, 2, 40, 40, 5, 5);

        assert_eq!(tiles[0], 1);
    }

    #[test]
    fn records_each_vertical_tile_only_once() {
        let mut tiles = [0; 1];
        mark(&mut tiles, 17, 17, 0, 0, 260, 260);

        assert_eq!(tiles[0], (1 << 17) - 1);
    }
}
