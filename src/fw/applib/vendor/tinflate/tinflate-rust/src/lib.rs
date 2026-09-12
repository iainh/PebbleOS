// SPDX-FileCopyrightText: 2003 Joergen Ibsen
// SPDX-FileCopyrightText: 2014 Paul Sokolovsky
// SPDX-FileCopyrightText: 2015 Pebble Inc.
// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Zlib
//
// This Rust implementation is derived from tinflate 1.2. It replaces the
// original byte-at-a-time decoder and adds explicit source and destination
// bounds checking.

#![no_std]

use core::ffi::{c_int, c_uint, c_void};

const TINF_OK: c_int = 0;
const TINF_MEMORY_ERROR: c_int = -1;
const TINF_DATA_ERROR: c_int = -3;
const TINF_DEST_OVERFLOW: c_int = -4;

const MAX_BITS: usize = 15;
const MAX_LITLEN_SYMBOLS: usize = 288;
const MAX_DIST_SYMBOLS: usize = 32;
const MAX_CODE_LENGTHS: usize = MAX_LITLEN_SYMBOLS + MAX_DIST_SYMBOLS;
const LIT_LOOKUP_BITS: u8 = 8;
const DIST_LOOKUP_BITS: u8 = 6;
const LIT_LOOKUP_SIZE: usize = 1 << LIT_LOOKUP_BITS;
const DIST_LOOKUP_SIZE: usize = 1 << DIST_LOOKUP_BITS;
const LOOKUP_SYMBOL_MASK: u16 = 0x01ff;

const CODE_LENGTH_ORDER: [usize; 19] = [
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
];

const LENGTH_EXTRA_BITS: [u8; 29] = [
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
const LENGTH_BASE: [u16; 29] = [
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
];
const DIST_EXTRA_BITS: [u8; 30] = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13,
];
const DIST_BASE: [u16; 30] = [
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
];

#[derive(Clone, Copy)]
enum Error {
    Memory,
    Data,
    Overflow,
}

struct Tree<const SYMBOLS: usize> {
    counts: [u16; MAX_BITS + 1],
    symbols: [u16; SYMBOLS],
}

union Scratch {
    lengths: [u8; MAX_CODE_LENGTHS],
    lookup: [u16; LIT_LOOKUP_SIZE + DIST_LOOKUP_SIZE],
}

struct Workspace {
    litlen: Tree<MAX_LITLEN_SYMBOLS>,
    distance: Tree<MAX_DIST_SYMBOLS>,
    scratch: Scratch,
}

struct Decoder {
    source: *const u8,
    source_len: usize,
    source_pos: usize,
    output: *mut u8,
    output_len: usize,
    output_pos: usize,
    bits: u32,
    bit_count: u8,
    workspace: *mut Workspace,
}

unsafe extern "C" {
    fn task_malloc(bytes: usize) -> *mut c_void;
    fn task_free(ptr: *mut c_void);
}

impl Decoder {
    fn ensure_bits(&mut self, count: u8) -> Result<(), Error> {
        while self.bit_count < count {
            if self.source_pos >= self.source_len {
                return Err(Error::Data);
            }
            let byte = unsafe { *self.source.add(self.source_pos) };
            self.bits |= u32::from(byte) << self.bit_count;
            self.bit_count += 8;
            self.source_pos += 1;
        }
        Ok(())
    }

    fn peek_bits(&mut self, count: u8) -> Result<u32, Error> {
        self.ensure_bits(count)?;
        Ok(self.bits & ((1_u32 << count) - 1))
    }

    fn drop_bits(&mut self, count: u8) {
        self.bits >>= count;
        self.bit_count -= count;
    }

    fn read_bits(&mut self, count: u8) -> Result<u32, Error> {
        if count == 0 {
            return Ok(0);
        }
        let value = self.peek_bits(count)?;
        self.drop_bits(count);
        Ok(value)
    }

    fn align_to_byte(&mut self) {
        self.drop_bits(self.bit_count & 7);
    }

    fn write_literal(&mut self, value: u8) -> Result<(), Error> {
        if self.output_pos >= self.output_len {
            return Err(Error::Overflow);
        }
        unsafe { *self.output.add(self.output_pos) = value };
        self.output_pos += 1;
        Ok(())
    }

    fn copy_match(&mut self, distance: usize, mut length: usize) -> Result<(), Error> {
        if distance == 0 || distance > self.output_pos {
            return Err(Error::Data);
        }
        if length > self.output_len - self.output_pos {
            return Err(Error::Overflow);
        }

        while distance >= 4 && length >= 4 {
            let value = unsafe {
                core::ptr::read_unaligned(self.output.add(self.output_pos - distance).cast::<u32>())
            };
            unsafe {
                core::ptr::write_unaligned(self.output.add(self.output_pos).cast::<u32>(), value)
            };
            self.output_pos += 4;
            length -= 4;
        }
        while length != 0 {
            let value = unsafe { *self.output.add(self.output_pos - distance) };
            unsafe { *self.output.add(self.output_pos) = value };
            self.output_pos += 1;
            length -= 1;
        }
        Ok(())
    }

    fn workspace(&mut self) -> Result<&mut Workspace, Error> {
        if self.workspace.is_null() {
            let workspace = unsafe { task_malloc(core::mem::size_of::<Workspace>()) };
            if workspace.is_null() {
                return Err(Error::Memory);
            }
            self.workspace = workspace.cast();
            unsafe { core::ptr::write_bytes(self.workspace, 0, 1) };
        }
        Ok(unsafe { &mut *self.workspace })
    }

    fn decode_fixed_symbol(&mut self) -> Result<u16, Error> {
        let code7 = self.peek_bits(7)?.reverse_bits() >> 25;
        if code7 <= 0x17 {
            self.drop_bits(7);
            return Ok(256 + code7 as u16);
        }

        let code8 = self.peek_bits(8)?.reverse_bits() >> 24;
        if code8 >= 0x30 && code8 <= 0xbf {
            self.drop_bits(8);
            return Ok((code8 - 0x30) as u16);
        }
        if code8 >= 0xc0 && code8 <= 0xc7 {
            self.drop_bits(8);
            return Ok((280 + code8 - 0xc0) as u16);
        }

        let code9 = self.peek_bits(9)?.reverse_bits() >> 23;
        if code9 >= 0x190 && code9 <= 0x1ff {
            self.drop_bits(9);
            return Ok((144 + code9 - 0x190) as u16);
        }
        Err(Error::Data)
    }

    fn decode_dynamic_symbol<const SYMBOLS: usize>(
        &mut self,
        tree: *const Tree<SYMBOLS>,
        lookup: *const u16,
        lookup_bits: u8,
    ) -> Result<u16, Error> {
        if let Ok(index) = self.peek_bits(lookup_bits) {
            let entry = unsafe { *lookup.add(index as usize) };
            if entry != 0 {
                self.drop_bits((entry >> 9) as u8);
                return Ok(entry & LOOKUP_SYMBOL_MASK);
            }
        }
        self.decode_symbol_slow(unsafe { &*tree })
    }

    fn decode_symbol_slow<const SYMBOLS: usize>(
        &mut self,
        tree: &Tree<SYMBOLS>,
    ) -> Result<u16, Error> {
        let mut code = 0_u32;
        let mut first = 0_u32;
        let mut index = 0_usize;

        for length in 1..=MAX_BITS {
            code |= self.read_bits(1)?;
            let count = unsafe { *tree.counts.get_unchecked(length) } as u32;
            if code < first + count {
                let symbol_index = index + (code - first) as usize;
                if symbol_index >= SYMBOLS {
                    return Err(Error::Data);
                }
                return Ok(unsafe { *tree.symbols.get_unchecked(symbol_index) });
            }
            index += count as usize;
            first = (first + count) << 1;
            code <<= 1;
        }
        Err(Error::Data)
    }

    fn inflate_compressed(
        &mut self,
        fixed: bool,
        litlen_tree: *const Tree<MAX_LITLEN_SYMBOLS>,
        distance_tree: *const Tree<MAX_DIST_SYMBOLS>,
        lookup: *const u16,
    ) -> Result<(), Error> {
        loop {
            let symbol = if fixed {
                self.decode_fixed_symbol()?
            } else {
                self.decode_dynamic_symbol(litlen_tree, lookup, LIT_LOOKUP_BITS)?
            };

            match symbol {
                0..=255 => self.write_literal(symbol as u8)?,
                256 => return Ok(()),
                257..=285 => {
                    let length_index = (symbol - 257) as usize;
                    let length_base = unsafe { *LENGTH_BASE.get_unchecked(length_index) };
                    let length_extra = unsafe { *LENGTH_EXTRA_BITS.get_unchecked(length_index) };
                    let length = usize::from(length_base) + self.read_bits(length_extra)? as usize;

                    let distance_symbol = if fixed {
                        (self.read_bits(5)?.reverse_bits() >> 27) as u16
                    } else {
                        self.decode_dynamic_symbol(
                            distance_tree,
                            unsafe { lookup.add(LIT_LOOKUP_SIZE) },
                            DIST_LOOKUP_BITS,
                        )?
                    };
                    if distance_symbol >= 30 {
                        return Err(Error::Data);
                    }
                    let distance_index = distance_symbol as usize;
                    let distance_base = unsafe { *DIST_BASE.get_unchecked(distance_index) };
                    let distance_extra = unsafe { *DIST_EXTRA_BITS.get_unchecked(distance_index) };
                    let distance =
                        usize::from(distance_base) + self.read_bits(distance_extra)? as usize;
                    self.copy_match(distance, length)?;
                }
                _ => return Err(Error::Data),
            }
        }
    }

    fn inflate_stored(&mut self) -> Result<(), Error> {
        self.align_to_byte();
        let length = self.read_bits(16)? as usize;
        let complement = self.read_bits(16)? as u16;
        if (length as u16) != !complement {
            return Err(Error::Data);
        }
        if length > self.output_len - self.output_pos {
            return Err(Error::Overflow);
        }
        let available = usize::from(self.bit_count / 8) + self.source_len - self.source_pos;
        if length > available {
            return Err(Error::Data);
        }
        for _ in 0..length {
            let byte = self.read_bits(8)? as u8;
            unsafe { *self.output.add(self.output_pos) = byte };
            self.output_pos += 1;
        }
        Ok(())
    }

    fn inflate_dynamic(&mut self) -> Result<(), Error> {
        let literal_count = self.read_bits(5)? as usize + 257;
        let distance_count = self.read_bits(5)? as usize + 1;
        let code_length_count = self.read_bits(4)? as usize + 4;
        if literal_count > 286 || distance_count > MAX_DIST_SYMBOLS {
            return Err(Error::Data);
        }

        let workspace = self.workspace()? as *mut Workspace;
        let lengths = unsafe { core::ptr::addr_of_mut!((*workspace).scratch.lengths).cast::<u8>() };
        unsafe { core::ptr::write_bytes(lengths, 0, MAX_CODE_LENGTHS) };
        for index in 0..code_length_count {
            let symbol = unsafe { *CODE_LENGTH_ORDER.get_unchecked(index) };
            unsafe { *lengths.add(symbol) = self.read_bits(3)? as u8 };
        }
        build_tree(unsafe { &mut (*workspace).litlen }, lengths, 19, false)?;

        let total = literal_count + distance_count;
        let mut index = 0;
        while index < total {
            let symbol = self.decode_symbol_slow(unsafe { &(*workspace).litlen })?;
            match symbol {
                0..=15 => {
                    unsafe { *lengths.add(index) = symbol as u8 };
                    index += 1;
                }
                16 => {
                    if index == 0 {
                        return Err(Error::Data);
                    }
                    let repeat = self.read_bits(2)? as usize + 3;
                    if repeat > total - index {
                        return Err(Error::Data);
                    }
                    let previous = unsafe { *lengths.add(index - 1) };
                    unsafe { core::ptr::write_bytes(lengths.add(index), previous, repeat) };
                    index += repeat;
                }
                17 => {
                    let repeat = self.read_bits(3)? as usize + 3;
                    if repeat > total - index {
                        return Err(Error::Data);
                    }
                    unsafe { core::ptr::write_bytes(lengths.add(index), 0, repeat) };
                    index += repeat;
                }
                18 => {
                    let repeat = self.read_bits(7)? as usize + 11;
                    if repeat > total - index {
                        return Err(Error::Data);
                    }
                    unsafe { core::ptr::write_bytes(lengths.add(index), 0, repeat) };
                    index += repeat;
                }
                _ => return Err(Error::Data),
            }
        }
        if unsafe { *lengths.add(256) } == 0 {
            return Err(Error::Data);
        }

        build_tree(
            unsafe { &mut (*workspace).litlen },
            lengths,
            literal_count,
            false,
        )?;
        build_tree(
            unsafe { &mut (*workspace).distance },
            unsafe { lengths.add(literal_count) },
            distance_count,
            true,
        )?;

        let lookup = unsafe { (*workspace).scratch.lookup.as_mut_ptr() };
        unsafe { core::ptr::write_bytes(lookup, 0, LIT_LOOKUP_SIZE + DIST_LOOKUP_SIZE) };
        build_lookup(unsafe { &(*workspace).litlen }, lookup, LIT_LOOKUP_BITS);
        build_lookup(
            unsafe { &(*workspace).distance },
            unsafe { lookup.add(LIT_LOOKUP_SIZE) },
            DIST_LOOKUP_BITS,
        );

        self.inflate_compressed(
            false,
            unsafe { &(*workspace).litlen },
            unsafe { &(*workspace).distance },
            lookup,
        )
    }

    fn inflate(&mut self) -> Result<(), Error> {
        loop {
            let final_block = self.read_bits(1)? != 0;
            match self.read_bits(2)? {
                0 => self.inflate_stored()?,
                1 => self.inflate_compressed(
                    true,
                    core::ptr::null(),
                    core::ptr::null(),
                    core::ptr::null(),
                )?,
                2 => self.inflate_dynamic()?,
                _ => return Err(Error::Data),
            }
            if final_block {
                return Ok(());
            }
        }
    }
}

fn build_tree<const SYMBOLS: usize>(
    tree: &mut Tree<SYMBOLS>,
    lengths: *const u8,
    length_count: usize,
    allow_empty: bool,
) -> Result<(), Error> {
    tree.counts.fill(0);
    for index in 0..length_count {
        let length = unsafe { *lengths.add(index) };
        if usize::from(length) > MAX_BITS {
            return Err(Error::Data);
        }
        let count = unsafe { tree.counts.get_unchecked_mut(usize::from(length)) };
        *count += 1;
    }
    tree.counts[0] = 0;

    let mut remaining = 1_i32;
    for length in 1..=MAX_BITS {
        let count = unsafe { *tree.counts.get_unchecked(length) };
        remaining = (remaining << 1) - i32::from(count);
        if remaining < 0 {
            return Err(Error::Data);
        }
    }

    let mut offsets = [0_u16; MAX_BITS + 1];
    let mut sum = 0_u16;
    for length in 1..=MAX_BITS {
        unsafe { *offsets.get_unchecked_mut(length) = sum };
        sum += unsafe { *tree.counts.get_unchecked(length) };
    }
    if usize::from(sum) > SYMBOLS {
        return Err(Error::Data);
    }
    if sum == 0 {
        return if allow_empty {
            Ok(())
        } else {
            Err(Error::Data)
        };
    }
    if remaining != 0 && !(sum == 1 && unsafe { *tree.counts.get_unchecked(1) } == 1) {
        return Err(Error::Data);
    }
    for symbol in 0..length_count {
        let length = unsafe { *lengths.add(symbol) };
        if length != 0 {
            let offset = unsafe { offsets.get_unchecked_mut(usize::from(length)) };
            unsafe { *tree.symbols.get_unchecked_mut(usize::from(*offset)) = symbol as u16 };
            *offset += 1;
        }
    }
    Ok(())
}

fn build_lookup<const SYMBOLS: usize>(tree: &Tree<SYMBOLS>, lookup: *mut u16, lookup_bits: u8) {
    let mut code = 0_u32;
    let mut symbol_index = 0_usize;
    let lookup_size = 1_usize << lookup_bits;

    for length in 1..=MAX_BITS {
        code = (code + u32::from(unsafe { *tree.counts.get_unchecked(length - 1) })) << 1;
        let count = usize::from(unsafe { *tree.counts.get_unchecked(length) });
        if length <= usize::from(lookup_bits) {
            for offset in 0..count {
                let reversed = (code + offset as u32).reverse_bits() >> (32 - length);
                let symbol = unsafe { *tree.symbols.get_unchecked(symbol_index + offset) };
                let entry = symbol | ((length as u16) << 9);
                let step = 1_usize << length;
                let mut index = reversed as usize;
                while index < lookup_size {
                    unsafe { *lookup.add(index) = entry };
                    index += step;
                }
            }
        }
        symbol_index += count;
    }
}

#[no_mangle]
pub unsafe extern "C" fn tinflate_uncompress(
    destination: *mut c_void,
    destination_length: *mut c_uint,
    source: *const c_void,
    source_length: c_uint,
) -> c_int {
    if destination_length.is_null() {
        return TINF_DATA_ERROR;
    }
    let output_len = *destination_length as usize;
    if (destination.is_null() && output_len != 0) || (source.is_null() && source_length != 0) {
        return TINF_DATA_ERROR;
    }

    let mut decoder = Decoder {
        source: source.cast(),
        source_len: source_length as usize,
        source_pos: 0,
        output: destination.cast(),
        output_len,
        output_pos: 0,
        bits: 0,
        bit_count: 0,
        workspace: core::ptr::null_mut(),
    };
    let result = decoder.inflate();
    if !matches!(result, Err(Error::Memory)) {
        *destination_length = decoder.output_pos as c_uint;
    }
    if !decoder.workspace.is_null() {
        task_free(decoder.workspace.cast());
    }

    match result {
        Ok(()) => TINF_OK,
        Err(Error::Memory) => TINF_MEMORY_ERROR,
        Err(Error::Data) => TINF_DATA_ERROR,
        Err(Error::Overflow) => TINF_DEST_OVERFLOW,
    }
}
