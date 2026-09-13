// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::c_void;

// Slicing-by-4 removes the dependency between per-byte table lookups.
const fn lookup_tables() -> [[u32; 256]; 4] {
    let mut tables = [[0; 256]; 4];
    let mut value = 0;
    while value < 256 {
        let mut crc = value as u32;
        let mut bit = 0;
        while bit < 8 {
            crc = (crc >> 1) ^ ((0u32.wrapping_sub(crc & 1)) & 0xedb88320);
            bit += 1;
        }
        tables[0][value] = crc;
        value += 1;
    }

    value = 0;
    while value < 256 {
        let mut crc = tables[0][value];
        let mut slice = 1;
        while slice < tables.len() {
            crc = (crc >> 8) ^ tables[0][(crc & 0xff) as usize];
            tables[slice][value] = crc;
            slice += 1;
        }
        value += 1;
    }
    tables
}

const LOOKUP_TABLES: [[u32; 256]; 4] = lookup_tables();

#[no_mangle]
pub unsafe extern "C" fn crc32(mut crc: u32, data: *const c_void, length: usize) -> u32 {
    if data.is_null() {
        return 0;
    }

    let bytes = core::slice::from_raw_parts(data.cast::<u8>(), length);
    crc ^= u32::MAX;
    let mut chunks = bytes.chunks_exact(4);
    for chunk in &mut chunks {
        crc ^= u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]);
        crc = LOOKUP_TABLES[3][(crc & 0xff) as usize]
            ^ LOOKUP_TABLES[2][((crc >> 8) & 0xff) as usize]
            ^ LOOKUP_TABLES[1][((crc >> 16) & 0xff) as usize]
            ^ LOOKUP_TABLES[0][(crc >> 24) as usize];
    }
    for &byte in chunks.remainder() {
        crc = (crc >> 8) ^ LOOKUP_TABLES[0][((crc ^ u32::from(byte)) & 0xff) as usize];
    }
    crc ^ u32::MAX
}
