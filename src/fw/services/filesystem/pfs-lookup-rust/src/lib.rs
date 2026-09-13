// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

const ENTRY_COUNT: usize = 4;

#[derive(Clone, Copy, Default)]
#[repr(C)]
struct Entry {
    hash: u32,
    page: u16,
    length: u8,
    valid: u8,
}

static mut ENTRIES: [Entry; ENTRY_COUNT] = [Entry {
    hash: 0,
    page: 0,
    length: 0,
    valid: 0,
}; ENTRY_COUNT];
static mut NEXT: usize = 0;

fn hash_bytes(bytes: &[u8]) -> u32 {
    // FNV-1a is small, deterministic, and deliberately not trusted for identity.
    bytes.iter().fold(2_166_136_261, |hash, byte| {
        (hash ^ u32::from(*byte)).wrapping_mul(16_777_619)
    })
}

#[no_mangle]
pub unsafe extern "C" fn pfs_lookup_cache_hash(name: *const u8, length: usize) -> u32 {
    hash_bytes(core::slice::from_raw_parts(name, length))
}

#[no_mangle]
pub unsafe extern "C" fn pfs_lookup_cache_reset() {
    ENTRIES = [Entry::default(); ENTRY_COUNT];
    NEXT = 0;
}

#[no_mangle]
pub unsafe extern "C" fn pfs_lookup_cache_get(hash: u32, length: u8, page: *mut u16) -> bool {
    let entries = core::ptr::addr_of!(ENTRIES).cast::<Entry>();
    for index in 0..ENTRY_COUNT {
        let entry = entries.add(index).read();
        if entry.valid != 0 && entry.hash == hash && entry.length == length {
            if !page.is_null() {
                page.write(entry.page);
            }
            return true;
        }
    }
    false
}

#[no_mangle]
pub unsafe extern "C" fn pfs_lookup_cache_put(hash: u32, length: u8, page: u16) {
    let entries = core::ptr::addr_of_mut!(ENTRIES).cast::<Entry>();
    for index in 0..ENTRY_COUNT {
        let entry = entries.add(index);
        let current = entry.read();
        if current.valid != 0 && current.hash == hash && current.length == length {
            entry.write(Entry { page, ..current });
            return;
        }
    }
    entries.add(NEXT).write(Entry {
        hash,
        page,
        length,
        valid: 1,
    });
    NEXT = (NEXT + 1) % ENTRY_COUNT;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bounded_round_robin_cache() {
        unsafe {
            pfs_lookup_cache_reset();
            for page in 0..5 {
                pfs_lookup_cache_put(u32::from(page), 3, page);
            }
            let mut page = 99;
            assert!(!pfs_lookup_cache_get(0, 3, &mut page));
            assert!(pfs_lookup_cache_get(4, 3, &mut page));
            assert_eq!(page, 4);
        }
    }

    #[test]
    fn length_is_part_of_candidate_key() {
        unsafe {
            pfs_lookup_cache_reset();
            pfs_lookup_cache_put(42, 4, 7);
            let mut page = 0;
            assert!(!pfs_lookup_cache_get(42, 5, &mut page));
        }
    }

    #[test]
    fn hash_is_stable() {
        assert_eq!(hash_bytes(b"settings"), 0x6806_7b08);
    }
}
