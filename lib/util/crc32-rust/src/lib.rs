// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::c_void;

const LOOKUP_TABLE: [u32; 16] = [
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
];

#[no_mangle]
pub unsafe extern "C" fn crc32(mut crc: u32, data: *const c_void, length: usize) -> u32 {
    if data.is_null() {
        return 0;
    }

    let bytes = core::slice::from_raw_parts(data.cast::<u8>(), length);
    crc ^= u32::MAX;
    for &byte in bytes {
        crc = (crc >> 4) ^ LOOKUP_TABLE[((crc ^ u32::from(byte)) & 0xf) as usize];
        crc = (crc >> 4) ^ LOOKUP_TABLE[((crc ^ u32::from(byte >> 4)) & 0xf) as usize];
    }
    crc ^ u32::MAX
}
