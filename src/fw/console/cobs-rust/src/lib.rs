// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::{c_char, c_void};

#[repr(C)]
pub struct CobsDecodeContext {
    output: *mut u8,
    output_length: usize,
    decoded_length: usize,
    payload_remaining: u8,
    block_is_terminated: bool,
}

#[no_mangle]
pub unsafe extern "C" fn cobs_streaming_decode_start(
    ctx: *mut CobsDecodeContext,
    output: *mut c_void,
    length: usize,
) {
    *ctx = CobsDecodeContext {
        output: output.cast(),
        output_length: length,
        decoded_length: 0,
        payload_remaining: 0,
        block_is_terminated: false,
    };
}

#[no_mangle]
pub unsafe extern "C" fn cobs_streaming_decode(ctx: *mut CobsDecodeContext, input: c_char) -> bool {
    let ctx = &mut *ctx;
    if ctx.output.is_null() {
        return false;
    }

    let input = input as u8;
    if input == 0 {
        ctx.output = core::ptr::null_mut();
        return false;
    }

    if ctx.payload_remaining == 0 {
        let payload_remaining = input - 1;
        let zero_length = usize::from(ctx.block_is_terminated);
        let Some(available) = ctx.output_length.checked_sub(ctx.decoded_length) else {
            ctx.output = core::ptr::null_mut();
            return false;
        };
        if usize::from(payload_remaining) + zero_length > available {
            ctx.output = core::ptr::null_mut();
            return false;
        }

        ctx.payload_remaining = payload_remaining;
        if ctx.block_is_terminated {
            *ctx.output.add(ctx.decoded_length) = 0;
            ctx.decoded_length += 1;
        }
        ctx.block_is_terminated = input != 0xff;
    } else {
        if ctx.decoded_length >= ctx.output_length {
            ctx.output = core::ptr::null_mut();
            return false;
        }
        *ctx.output.add(ctx.decoded_length) = input;
        ctx.decoded_length += 1;
        ctx.payload_remaining -= 1;
    }
    true
}

#[no_mangle]
pub unsafe extern "C" fn cobs_streaming_decode_finish(ctx: *mut CobsDecodeContext) -> usize {
    let ctx = &*ctx;
    if ctx.output.is_null() || ctx.payload_remaining != 0 {
        usize::MAX
    } else {
        ctx.decoded_length
    }
}

#[no_mangle]
pub unsafe extern "C" fn cobs_encode(
    destination: *mut c_void,
    source: *const c_void,
    length: usize,
) -> usize {
    // PULSE passes overlapping buffers, which cannot be represented by Rust slices.
    let destination = destination.cast::<u8>();
    let source = source.cast::<u8>();
    let mut code = 1_u8;
    let mut code_index = 0;
    let mut destination_index = 1;
    let mut source_index = 0;

    while source_index < length {
        let byte = *source.add(source_index);
        if byte == 0 {
            *destination.add(code_index) = code;
            code_index = destination_index;
            destination_index += 1;
            code = 1;
        } else {
            *destination.add(destination_index) = byte;
            destination_index += 1;
            code = code.wrapping_add(1);
            if code == 0xff {
                if source_index == length - 1 {
                    break;
                }
                *destination.add(code_index) = code;
                code_index = destination_index;
                destination_index += 1;
                code = 1;
            }
        }
        source_index += 1;
    }
    *destination.add(code_index) = code;
    destination_index
}
