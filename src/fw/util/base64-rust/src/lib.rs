// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::{c_char, c_int};

fn decode_char(byte: u8) -> Option<u8> {
    match byte {
        b'A'..=b'Z' => Some(byte - b'A'),
        b'a'..=b'z' => Some(byte - b'a' + 26),
        b'0'..=b'9' => Some(byte - b'0' + 52),
        b'+' => Some(62),
        b'/' => Some(63),
        _ => None,
    }
}

fn decode(buffer: &mut [u8]) -> usize {
    if buffer.len() % 4 != 0 {
        return 0;
    }

    let mut read_index = 0;
    let mut write_index = 0;
    while read_index < buffer.len() {
        let Some([a, b, c, d]) = buffer.get(read_index..read_index + 4) else {
            return 0;
        };
        let quad = [*a, *b, *c, *d];
        let data_count = match quad.iter().position(|&byte| byte == b'=') {
            Some(index) => index,
            None => 4,
        };
        let padding = 4 - data_count;
        if padding > 2 || quad[data_count..].iter().any(|&byte| byte != b'=') {
            return 0;
        }

        let mut value = 0_u32;
        for &byte in &quad[..data_count] {
            let Some(decoded) = decode_char(byte) else {
                return 0;
            };
            value = value * 64 + u32::from(decoded);
        }
        value >>= padding * 2;

        let decoded_len = 3 - padding;
        let Some(output) = buffer.get_mut(write_index..write_index + decoded_len) else {
            return 0;
        };
        for (index, byte) in output.iter_mut().enumerate() {
            let shift = (decoded_len - index - 1) * 8;
            *byte = (value >> shift) as u8;
        }

        read_index += 4;
        write_index += decoded_len;
        if padding != 0 && read_index < buffer.len() {
            return 0;
        }
    }
    write_index
}

#[no_mangle]
pub unsafe extern "C" fn base64_decode_inplace(buffer: *mut c_char, length: u32) -> u32 {
    if buffer.is_null() || length == 0 {
        return 0;
    }

    let bytes = core::slice::from_raw_parts_mut(buffer.cast::<u8>(), length as usize);
    decode(bytes) as u32
}

#[inline(never)]
fn encode_char(value: u8) -> u8 {
    value
        .wrapping_add(b'A')
        .wrapping_add(u8::from(value > 25) * 6)
        .wrapping_sub(u8::from(value > 51) * 75)
        .wrapping_sub(u8::from(value > 61) * 15)
        .wrapping_add(u8::from(value > 62) * 3)
}

#[inline(never)]
unsafe fn encode(mut output: *mut u8, data: *const u8, data_len: usize) {
    let mut read_index = 0;
    while read_index + 2 < data_len {
        let first = *data.add(read_index);
        let second = *data.add(read_index + 1);
        let third = *data.add(read_index + 2);
        *output = encode_char(first >> 2);
        *output.add(1) = encode_char(((first & 0x03) << 4) | (second >> 4));
        *output.add(2) = encode_char(((second & 0x0f) << 2) | (third >> 6));
        *output.add(3) = encode_char(third & 0x3f);
        output = output.add(4);
        read_index += 3;
    }

    if read_index < data_len {
        let first = *data.add(read_index);
        *output = encode_char(first >> 2);
        if read_index + 1 < data_len {
            let second = *data.add(read_index + 1);
            *output.add(1) = encode_char(((first & 0x03) << 4) | (second >> 4));
            *output.add(2) = encode_char((second & 0x0f) << 2);
            *output.add(3) = b'=';
        } else {
            *output.add(1) = encode_char((first & 0x03) << 4);
            *output.add(2) = b'=';
            *output.add(3) = b'=';
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn base64_encode(
    out: *mut c_char,
    out_len: c_int,
    data: *const u8,
    data_len: i32,
) -> i32 {
    if data_len as u32 > 1_610_612_733 {
        return 0;
    }

    let required = (data_len + 2) / 3 * 4;
    if required > out_len {
        return required;
    }

    encode(out.cast::<u8>(), data, data_len as usize);
    if required < out_len {
        *out.cast::<u8>().add(required as usize) = 0;
    }
    required
}
