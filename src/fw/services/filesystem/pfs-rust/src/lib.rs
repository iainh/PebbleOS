// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::{
    cmp::min,
    ffi::{c_char, c_int, c_void},
    ptr, slice,
};

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

const PAGE: usize = 4096;
const SECTOR: usize = 65536;
const PH: usize = 28;
const FH: usize = 16;
const META: usize = 32;
const NAME_AT: usize = PH + FH + META;
const INVALID: u16 = 0xffff;
const MAX_FD: usize = 9;
const USER_FD: usize = 8;
const FD_BASE: i32 = 1001;
const MAX_WATCH: usize = 16;
const MAX_PAGE_MAP_ENTRIES: usize = 32;
const V1: u16 = 0x5001;
const V2: u16 = 0x5002;
const GC_MAGIC: [u8; 8] = *b"PFSGC2\0\0";
const START: u8 = 1 << 2;
const CONT: u8 = 1 << 3;
const DELETED: u8 = 1 << 1;
const OP_READ: u8 = 1;
const OP_WRITE: u8 = 2;
const OP_OVERWRITE: u8 = 4;
const SKIP_CRC: u8 = 8;
const USE_PAGE_CACHE: u8 = 16;
const FILE_STATIC: u8 = 0xfe;
const SUCCESS: i32 = 0;
const ERR: i32 = -1;
const INVALID_ARG: i32 = -4;
const NO_STORAGE: i32 = -6;
const NO_RESOURCES: i32 = -7;
const RANGE: i32 = -8;
const NOENT: i32 = -9;
const INVALID_OP: i32 = -10;

extern "C" {
    fn ftl_get_size() -> u32;
    fn ftl_populate_region_list();
    fn ftl_read(buf: *mut c_void, size: usize, offset: u32);
    fn ftl_write(buf: *const c_void, size: usize, offset: u32);
    fn ftl_erase_sector(size: u32, offset: u32);
    fn pfs_rust_lock();
    fn pfs_rust_unlock();
    fn pfs_rust_alloc(size: usize) -> *mut c_void;
    fn pfs_rust_free(ptr: *mut c_void);
    fn legacy_defective_checksum_init(checksum: *mut LegacyChecksum);
    fn legacy_defective_checksum_update(
        checksum: *mut LegacyChecksum,
        data: *const c_void,
        length: usize,
    );
    fn legacy_defective_checksum_finish(checksum: *mut LegacyChecksum) -> u32;
}

#[repr(C)]
struct LegacyChecksum {
    reg: u32,
    accumulator: [u8; 3],
    accumulated_length: u8,
}

#[derive(Clone, Copy)]
struct Handle {
    used: bool,
    name: *mut u8,
    nl: u8,
    size: u32,
    off: u32,
    start: u16,
    old: u16,
    flags: u8,
    dirty: bool,
    generation: u32,
    page_map: *mut u16,
    page_count: u16,
    page_stride: u16,
}
const EMPTY_HANDLE: Handle = Handle {
    used: false,
    name: ptr::null_mut(),
    nl: 0,
    size: 0,
    off: 0,
    start: INVALID,
    old: INVALID,
    flags: 0,
    dirty: false,
    generation: 0,
    page_map: ptr::null_mut(),
    page_count: 0,
    page_stride: 0,
};
type WatchCb = unsafe extern "C" fn(*mut c_void);
#[derive(Clone, Copy)]
struct Watch {
    used: bool,
    name: *mut u8,
    nl: u8,
    cb: Option<WatchCb>,
    events: u8,
    data: *mut c_void,
}
const EMPTY_WATCH: Watch = Watch {
    used: false,
    name: ptr::null_mut(),
    nl: 0,
    cb: None,
    events: 0,
    data: ptr::null_mut(),
};

static mut HANDLES: [Handle; MAX_FD] = [EMPTY_HANDLE; MAX_FD];
static mut WATCHES: [Watch; MAX_WATCH] = [EMPTY_WATCH; MAX_WATCH];
static mut SIZE: u32 = 0;
static mut PAGES: u16 = 0; // excludes the reserved GC sector
static mut PAGE_FLAGS: *mut u8 = ptr::null_mut();
static mut LAST_WRITTEN: u16 = INVALID;
static mut INITIALIZED: bool = false;

struct Guard;
impl Guard {
    unsafe fn new() -> Self {
        pfs_rust_lock();
        Self
    }
}
impl Drop for Guard {
    fn drop(&mut self) {
        unsafe { pfs_rust_unlock() }
    }
}

fn le16(b: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([b[o], b[o + 1]])
}
fn le32(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}
fn put16(b: &mut [u8], o: usize, v: u16) {
    b[o..o + 2].copy_from_slice(&v.to_le_bytes())
}
fn put32(b: &mut [u8], o: usize, v: u32) {
    b[o..o + 4].copy_from_slice(&v.to_le_bytes())
}
unsafe fn read(off: usize, b: &mut [u8]) -> bool {
    if off.checked_add(b.len()).map_or(true, |x| x > SIZE as usize) {
        return false;
    }
    ftl_read(b.as_mut_ptr().cast(), b.len(), off as u32);
    true
}
unsafe fn page_flags(page: u16) -> u8 {
    *PAGE_FLAGS.add(page as usize)
}
unsafe fn set_page_flags(page: u16, flags: u8) {
    *PAGE_FLAGS.add(page as usize) = flags;
}
unsafe fn fill_page_flags(first: u16, count: u16, flags: u8) {
    slice::from_raw_parts_mut(PAGE_FLAGS.add(first as usize), count as usize).fill(flags);
}
unsafe fn program(off: usize, b: &[u8]) -> bool {
    if off.checked_add(b.len()).map_or(true, |x| x > SIZE as usize) {
        return false;
    }
    if b.is_empty() {
        return true;
    }
    ftl_write(b.as_ptr().cast(), b.len(), off as u32);
    let mut v = [0u8; 256];
    let mut n = 0;
    while n < b.len() {
        let z = min(v.len(), b.len() - n);
        read(off + n, &mut v[..z]);
        if v[..z] != b[n..n + z] {
            return false;
        }
        n += z
    }
    let first_page = off / PAGE;
    let last_page = (off + b.len() - 1) / PAGE;
    for page in first_page..=last_page {
        let flag_offset = page * PAGE + 3;
        if page < PAGES as usize && flag_offset >= off && flag_offset < off + b.len() {
            set_page_flags(page as u16, b[flag_offset - off]);
        }
    }
    true
}
const fn crc32_table() -> [u32; 256] {
    let mut table = [0; 256];
    let mut i = 0;
    while i < table.len() {
        let mut crc = i as u32;
        let mut bit = 0;
        while bit < 8 {
            crc = (crc >> 1) ^ ((0u32.wrapping_sub(crc & 1)) & 0xedb88320);
            bit += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
}

fn crc32(mut crc: u32, data: &[u8]) -> u32 {
    const TABLE: [u32; 256] = crc32_table();
    crc = !crc;
    for &byte in data {
        crc = (crc >> 8) ^ TABLE[((crc ^ u32::from(byte)) & 0xff) as usize];
    }
    !crc
}
fn legacy(data: &[u8]) -> u32 {
    let mut c = 0xffff_ffffu32;
    let byte = |mut c: u32, x: u8| {
        c ^= (x as u32) << 24;
        for _ in 0..8 {
            c = if c & 0x80000000 != 0 {
                (c << 1) ^ 0x04c11db7
            } else {
                c << 1
            }
        }
        c
    };
    let mut chunks = data.chunks_exact(4);
    for q in &mut chunks {
        for &x in q.iter().rev() {
            c = byte(c, x)
        }
    }
    let r = chunks.remainder();
    if !r.is_empty() {
        for _ in r.len()..4 {
            c = byte(c, 0)
        }
        for &x in r {
            c = byte(c, x)
        }
    }
    c
}
fn c_name(p: *const c_char) -> Result<([u8; 256], u8), i32> {
    if p.is_null() {
        return Err(INVALID_ARG);
    }
    let mut a = [0; 256];
    let mut n = 0;
    unsafe {
        while n < 255 {
            let x = *p.add(n) as u8;
            if x == 0 {
                if n == 0 {
                    return Err(INVALID_ARG);
                }
                return Ok((a, n as u8));
            }
            a[n] = x;
            n += 1
        }
    }
    Err(INVALID_ARG)
}
fn is_type(f: u8, m: u8) -> bool {
    f & m == 0 && f & DELETED != 0
}
unsafe fn stored_name<'a>(name: *const u8, len: u8) -> &'a [u8] {
    slice::from_raw_parts(name, len as usize)
}
unsafe fn alloc_name(name: &[u8]) -> *mut u8 {
    let copy = pfs_rust_alloc(name.len()).cast::<u8>();
    if !copy.is_null() {
        ptr::copy_nonoverlapping(name.as_ptr(), copy, name.len());
    }
    copy
}
unsafe fn clear_handles() {
    for i in 0..MAX_FD {
        let handle = &mut HANDLES[i];
        if !handle.name.is_null() {
            pfs_rust_free(handle.name.cast());
        }
        if !handle.page_map.is_null() {
            pfs_rust_free(handle.page_map.cast());
        }
        *handle = EMPTY_HANDLE;
    }
}

#[derive(Clone, Copy)]
struct Found {
    page: u16,
    size: u32,
    nl: u8,
    generation: u32,
    version: u16,
    crc: u32,
}
unsafe fn header(page: u16, skip: bool) -> Option<Found> {
    if page >= PAGES || !is_type(page_flags(page), START) {
        return None;
    }
    let mut h = [0u8; NAME_AT];
    if !read(page as usize * PAGE, &mut h) {
        return None;
    }
    let ver = le16(&h, 0);
    if ver != V1 && ver != V2 {
        return None;
    }
    if !skip {
        let mut q = [0u8; PH];
        q.copy_from_slice(&h[..PH]);
        q[2] = 0xff;
        if legacy(&q[..24]) != le32(&h, 24) || legacy(&h[PH..PH + 12]) != le32(&h, PH + 12) {
            return None;
        }
    }
    let size = le32(&h, PH);
    let nl = h[PH + 5];
    if nl == 0 {
        return None;
    }
    let first = NAME_AT.checked_add(nl as usize)?;
    if first > PAGE {
        return None;
    }
    let cap = (PAGE - first) as u64 + ((PAGES as u64).saturating_sub(1)) * ((PAGE - PH) as u64);
    if size as u64 > cap {
        return None;
    }
    let meta = PH + FH;
    if le16(&h, meta + 4) == 0 {
        return None;
    } // retired
    if ver == V2 && le16(&h, meta + 2) != 0 {
        return None;
    } // not committed
    Some(Found {
        page,
        size,
        nl,
        generation: if ver == V2 { le32(&h, meta + 6) } else { 0 },
        version: ver,
        crc: if ver == V2 { le32(&h, meta + 10) } else { 0 },
    })
}
unsafe fn find(name: &[u8], skip: bool) -> Option<Found> {
    let mut best = None;
    for p in 0..PAGES {
        if let Some(f) = header(p, skip) {
            if f.nl as usize != name.len() {
                continue;
            }
            let mut n = [0u8; 255];
            read(p as usize * PAGE + NAME_AT, &mut n[..name.len()]);
            if &n[..name.len()] != name {
                continue;
            }
            if best.map_or(true, |x: Found| f.generation > x.generation) {
                best = Some(f)
            }
        }
    }
    // Retire corrupt duplicates only when a valid copy can satisfy the lookup.
    // The page-flags cache keeps this recovery pass proportional to live files.
    if best.is_some() && !skip {
        for p in 0..PAGES {
            if !is_type(page_flags(p), START) || header(p, false).is_some() {
                continue;
            }
            let mut h = [0xff; NAME_AT];
            read(p as usize * PAGE, &mut h);
            if (le16(&h, 0) == V1 || le16(&h, 0) == V2) && h[PH + 5] as usize == name.len() {
                let mut candidate = [0; 255];
                read(p as usize * PAGE + NAME_AT, &mut candidate[..name.len()]);
                if &candidate[..name.len()] == name {
                    mark_deleted(p);
                }
            }
        }
    }
    best
}
unsafe fn next(p: u16) -> Option<u16> {
    let mut b = [0; 2];
    if !read(p as usize * PAGE + 22, &mut b) {
        return None;
    }
    let n = u16::from_le_bytes(b);
    if n == INVALID {
        None
    } else if n < PAGES {
        Some(n)
    } else {
        None
    }
}
unsafe fn data_io(
    start: u16,
    nl: u8,
    size: u32,
    mut off: u32,
    buf: *mut u8,
    len: usize,
    write_it: bool,
    page_map: *const u16,
    page_count: u16,
    page_stride: u16,
) -> Result<usize, i32> {
    if off as u64 + len as u64 > size as u64 {
        return Err(RANGE);
    }
    if len == 0 {
        return Ok(0);
    }
    let mut p = start;
    let mut base = (NAME_AT + nl as usize) as u32;
    let mut seen = 0u16;
    if !page_map.is_null() {
        let first_capacity = PAGE as u32 - base;
        let page_index = if off < first_capacity {
            0
        } else {
            off -= first_capacity;
            base = PH as u32;
            let index = 1 + off / (PAGE - PH) as u32;
            off %= (PAGE - PH) as u32;
            index
        };
        let map_index = page_index / page_stride as u32;
        if map_index >= page_count as u32 {
            return Err(ERR);
        }
        p = *page_map.add(map_index as usize);
        seen = (map_index * page_stride as u32) as u16;
        while seen < page_index as u16 {
            p = next(p).ok_or(ERR)?;
            seen += 1;
        }
    } else {
        while off >= PAGE as u32 - base {
            off -= PAGE as u32 - base;
            p = next(p).ok_or(ERR)?;
            base = PH as u32;
            seen += 1;
            if seen >= PAGES {
                return Err(ERR);
            }
        }
    }
    let mut done = 0;
    while done < len {
        let n = min(len - done, (PAGE as u32 - base - off) as usize);
        let at = p as usize * PAGE + base as usize + off as usize;
        let s = slice::from_raw_parts_mut(buf.add(done), n);
        if write_it {
            if !program(at, s) {
                return Err(ERR);
            }
        } else if !read(at, s) {
            return Err(ERR);
        }
        done += n;
        off = 0;
        if done < len {
            p = next(p).ok_or(ERR)?;
            base = PH as u32;
            seen += 1;
            if seen >= PAGES {
                return Err(ERR);
            }
        }
    }
    Ok(done)
}

fn file_page_count(nl: u8, size: u32) -> usize {
    let first_capacity = PAGE - NAME_AT - nl as usize;
    if size as usize <= first_capacity {
        1
    } else {
        1 + (size as usize - first_capacity + PAGE - PH - 1) / (PAGE - PH)
    }
}

unsafe fn build_page_map(start: u16, nl: u8, size: u32) -> Result<(*mut u16, u16, u16), i32> {
    let page_count = file_page_count(nl, size);
    if page_count <= 1 {
        return Ok((ptr::null_mut(), 0, 0));
    }
    if page_count > PAGES as usize {
        return Err(ERR);
    }
    let stride = page_count.div_ceil(MAX_PAGE_MAP_ENTRIES);
    let map_count = page_count.div_ceil(stride);
    let map = pfs_rust_alloc(map_count * core::mem::size_of::<u16>()).cast::<u16>();
    if map.is_null() {
        return Err(NO_RESOURCES);
    }
    let mut page = start;
    let mut map_index = 0;
    for i in 0..page_count {
        if i % stride == 0 {
            *map.add(map_index) = page;
            map_index += 1;
        }
        if i + 1 < page_count {
            let Some(next_page) = next(page) else {
                pfs_rust_free(map.cast());
                return Err(ERR);
            };
            page = next_page;
        }
    }
    Ok((map, map_count as u16, stride as u16))
}
unsafe fn mark_deleted(start: u16) {
    let mut p = Some(start);
    let mut n = 0;
    while let Some(x) = p {
        let deleted = [page_flags(x) & !DELETED];
        program(x as usize * PAGE + 3, &deleted);
        p = next(x);
        n += 1;
        if n >= PAGES {
            break;
        }
    }
}
unsafe fn page_blank(p: u16) -> bool {
    page_flags(p) == 0xff || page_flags(p) == !1
}
unsafe fn page_deleted(p: u16) -> bool {
    page_flags(p) & DELETED == 0
}
unsafe fn free_page_after(after: u16) -> Option<u16> {
    for d in 1..=PAGES {
        let p = after.wrapping_add(d) % PAGES;
        if page_blank(p) {
            return Some(p);
        }
    }
    None
}
unsafe fn reclaim_deleted_sector() -> bool {
    let sectors = PAGES as usize / 16;
    for s in 0..sectors {
        let first = (s * 16) as u16;
        let mut any = false;
        let mut reclaimable = true;
        for p in first..first + 16 {
            if page_blank(p) {
                continue;
            }
            any = true;
            if !page_deleted(p) {
                reclaimable = false;
                break;
            }
        }
        if any && reclaimable {
            ftl_erase_sector(SECTOR as u32, first as u32 * PAGE as u32);
            fill_page_flags(first, 16, 0xff);
            return true;
        }
    }
    false
}
unsafe fn allocatable_page(after: u16) -> Option<u16> {
    if let Some(p) = free_page_after(after) {
        return Some(p);
    }
    if reclaim_deleted_sector() {
        if let Some(p) = free_page_after(after) {
            return Some(p);
        }
    }
    for p in (0..PAGES).step_by(16) {
        if (p..min(p + 16, PAGES)).any(|x| page_deleted(x)) {
            compact_sector(p, true);
            if let Some(q) = free_page_after(after) {
                return Some(q);
            }
        }
    }
    None
}
unsafe fn allocate(name: &[u8], size: u32, generation: u32) -> Result<u16, i32> {
    let count = file_page_count(name.len() as u8, size);
    if count > PAGES as usize {
        return Err(NO_STORAGE);
    }
    // Allocate and link incrementally.  This deliberately trades scan time for bounded RAM.
    let first = allocatable_page(LAST_WRITTEN).ok_or(NO_STORAGE)?;
    let mut current = first;
    for i in 0..count {
        // Reserve the current page before searching for its successor so the
        // circular allocator cannot return the same blank page again.
        let flags = if i == 0 { !(START | 1) } else { !(CONT | 1) };
        if !program(current as usize * PAGE, &[2, 0x50, 0xff, flags]) {
            return Err(ERR);
        }
        let next = if i + 1 < count {
            allocatable_page(current).ok_or(NO_STORAGE)?
        } else {
            INVALID
        };
        let mut h = [0xff; PH];
        put16(&mut h, 0, V2);
        h[3] = flags;
        put16(&mut h, 22, next);
        h[21] = crc8(&h[22..24]);
        let c = legacy(&h[..24]);
        put32(&mut h, 24, c);
        if !program(current as usize * PAGE, &h) {
            return Err(ERR);
        }
        LAST_WRITTEN = current;
        current = next;
    }
    let mut x = [0xff; FH + META];
    put32(&mut x, 0, size);
    x[4] = FILE_STATIC;
    x[5] = name.len() as u8;
    let c = legacy(&x[..12]);
    put32(&mut x, 12, c);
    put32(&mut x, FH + 6, generation);
    if !program(first as usize * PAGE + PH, &x) || !program(first as usize * PAGE + NAME_AT, name) {
        return Err(ERR);
    }
    Ok(first)
}
fn crc8(d: &[u8]) -> u8 {
    let mut c = 0xff;
    for &b in d {
        c ^= b;
        for _ in 0..8 {
            c = if c & 0x80 != 0 {
                (c << 1) ^ 0x07
            } else {
                c << 1
            }
        }
    }
    c
}
unsafe fn validate(f: Found) -> bool {
    if f.version == V1 {
        return true;
    }
    file_crc(f.page, f.nl, f.size) == Some(f.crc)
}
unsafe fn file_crc(start: u16, nl: u8, size: u32) -> Option<u32> {
    let mut c = 0;
    let mut b = [0u8; 256];
    let mut remaining = size as usize;
    let mut page = start;
    let mut base = NAME_AT + nl as usize;
    let mut seen = 0u16;
    while remaining != 0 {
        let page_bytes = min(remaining, PAGE - base);
        let mut offset = 0;
        while offset < page_bytes {
            let n = min(b.len(), page_bytes - offset);
            if !read(page as usize * PAGE + base + offset, &mut b[..n]) {
                return None;
            }
            c = crc32(c, &b[..n]);
            offset += n;
        }
        remaining -= page_bytes;
        if remaining != 0 {
            page = next(page)?;
            base = PH;
            seen += 1;
            if seen >= PAGES {
                return None;
            }
        }
    }
    Some(c)
}
unsafe fn notify(name: &[u8], event: u8) {
    for i in 0..MAX_WATCH {
        let w = WATCHES[i];
        if w.used
            && w.events & event != 0
            && w.nl as usize == name.len()
            && stored_name(w.name, w.nl) == name
        {
            if let Some(cb) = w.cb {
                pfs_rust_unlock();
                cb(w.data);
                pfs_rust_lock()
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn pfs_init(_: bool) -> i32 {
    let _g = Guard::new();
    clear_handles();
    ftl_populate_region_list();
    SIZE = ftl_get_size();
    if SIZE <= SECTOR as u32 || SIZE as usize % PAGE != 0 {
        return INVALID_ARG;
    }
    let physical = SIZE as usize / PAGE;
    if physical > u16::MAX as usize {
        return INVALID_ARG;
    }
    PAGES = (physical - 16) as u16;
    if !PAGE_FLAGS.is_null() {
        pfs_rust_free(PAGE_FLAGS.cast());
    }
    PAGE_FLAGS = pfs_rust_alloc(PAGES as usize).cast();
    if PAGE_FLAGS.is_null() {
        PAGES = 0;
        return NO_RESOURCES;
    }
    INITIALIZED = true;
    LAST_WRITTEN = INVALID;
    fill_page_flags(0, PAGES, 0xff);
    recover_gc();
    let mut nonblank = false;
    let mut valid = false;
    for p in 0..PAGES {
        let mut b = [0; PH];
        read(p as usize * PAGE, &mut b);
        set_page_flags(p, b[3]);
        nonblank |= b.iter().any(|&x| x != 0xff);
        valid |= (le16(&b, 0) >> 8) == 0x50;
        if !page_blank(p) {
            LAST_WRITTEN = p
        }
    }
    if nonblank && !valid {
        return ERR;
    }
    SUCCESS
}
#[no_mangle]
pub unsafe extern "C" fn pfs_format(write_headers: bool) {
    let _g = Guard::new();
    let mut offset = 0;
    while offset < SIZE {
        ftl_erase_sector(SECTOR as u32, offset);
        offset += SECTOR as u32
    }
    if write_headers {
        for p in 0..PAGES {
            program(p as usize * PAGE, &[2, 0x50, 0xff, 0xfe]);
        }
    } else {
        fill_page_flags(0, PAGES, 0xff);
    }
    clear_handles();
    LAST_WRITTEN = INVALID
}
#[no_mangle]
pub unsafe extern "C" fn pfs_open(
    n: *const c_char,
    flags: u8,
    typ: u8,
    start_size: usize,
) -> c_int {
    let (name, nl) = match c_name(n) {
        Ok(x) => x,
        Err(e) => return e,
    };
    if flags & OP_OVERWRITE != 0 {
        for i in 0..USER_FD {
            if HANDLES[i].used
                && HANDLES[i].dirty
                && HANDLES[i].nl == nl
                && stored_name(HANDLES[i].name, nl) == &name[..nl as usize]
            {
                if pfs_close(FD_BASE + i as i32) < 0 {
                    return ERR;
                }
                break;
            }
        }
    }
    let _g = Guard::new();
    if !INITIALIZED {
        return INVALID_OP;
    }
    let found = find(&name[..nl as usize], flags & SKIP_CRC != 0);
    let read_only = flags & OP_READ != 0 && flags & (OP_WRITE | OP_OVERWRITE) == 0;
    if read_only {
        if let Some(f) = found {
            if flags & SKIP_CRC == 0 && !validate(f) {
                return ERR;
            }
        } else {
            return NOENT;
        }
    }
    if flags & OP_OVERWRITE != 0 && found.is_none() {
        return NOENT;
    }
    if flags & (OP_WRITE | OP_OVERWRITE) != 0 && typ != FILE_STATIC {
        return INVALID_ARG;
    }
    if flags & (OP_WRITE | OP_OVERWRITE) != 0 && start_size > u32::MAX as usize {
        return RANGE;
    }
    let limit = if flags & (OP_WRITE | OP_OVERWRITE) != 0 {
        USER_FD
    } else {
        MAX_FD
    };
    let i = match (0..limit).find(|&i| !HANDLES[i].used) {
        Some(x) => x,
        None => return NO_RESOURCES,
    };
    let mut h = EMPTY_HANDLE;
    h.used = true;
    h.name = alloc_name(&name[..nl as usize]);
    if h.name.is_null() {
        return NO_RESOURCES;
    }
    h.nl = nl;
    h.flags = flags;
    if flags & OP_OVERWRITE != 0 || flags & OP_WRITE != 0 && found.is_none() {
        let sz = start_size as u32;
        h.old = if flags & OP_OVERWRITE != 0 {
            found.map_or(INVALID, |f| f.page)
        } else {
            INVALID
        };
        h.generation = found.map_or(1, |f| f.generation.saturating_add(1));
        h.start = match allocate(&name[..nl as usize], sz, h.generation) {
            Ok(p) => p,
            Err(e) => {
                pfs_rust_free(h.name.cast());
                return e;
            }
        };
        h.size = sz;
        h.dirty = true;
    } else {
        let f = found.unwrap();
        h.start = f.page;
        h.size = f.size;
        h.generation = f.generation;
        if flags & USE_PAGE_CACHE != 0 {
            match build_page_map(h.start, h.nl, h.size) {
                Ok((map, count, stride)) => {
                    h.page_map = map;
                    h.page_count = count;
                    h.page_stride = stride;
                }
                Err(e) => {
                    pfs_rust_free(h.name.cast());
                    return e;
                }
            }
        }
    }
    HANDLES[i] = h;
    FD_BASE + i as i32
}
fn fd_index(fd: i32) -> Result<usize, i32> {
    let i = fd - FD_BASE;
    if i < 0 || i >= MAX_FD as i32 {
        Err(INVALID_ARG)
    } else {
        Ok(i as usize)
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_read(fd: i32, b: *mut c_void, n: usize) -> i32 {
    if b.is_null() && n != 0 {
        return INVALID_ARG;
    }
    let _g = Guard::new();
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(e) => return e,
    };
    let h = &mut HANDLES[i];
    if !h.used {
        return INVALID_ARG;
    }
    if h.flags & OP_READ == 0 {
        return INVALID_ARG;
    }
    if n as u64 + h.off as u64 > h.size as u64 {
        return RANGE;
    }
    match data_io(
        h.start,
        h.nl,
        h.size,
        h.off,
        b.cast(),
        n,
        false,
        h.page_map,
        h.page_count,
        h.page_stride,
    ) {
        Ok(x) => {
            h.off += x as u32;
            x as i32
        }
        Err(e) => e,
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_write(fd: i32, b: *const c_void, n: usize) -> i32 {
    if b.is_null() && n != 0 {
        return INVALID_ARG;
    }
    let _g = Guard::new();
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(e) => return e,
    };
    let h = &mut HANDLES[i];
    if !h.used {
        return INVALID_ARG;
    }
    if h.flags & (OP_WRITE | OP_OVERWRITE) == 0 {
        return INVALID_ARG;
    }
    if n as u64 + h.off as u64 > h.size as u64 {
        return RANGE;
    }
    match data_io(
        h.start,
        h.nl,
        h.size,
        h.off,
        b as *mut u8,
        n,
        true,
        h.page_map,
        h.page_count,
        h.page_stride,
    ) {
        Ok(x) => {
            h.off += x as u32;
            x as i32
        }
        Err(e) => e,
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_seek(fd: i32, o: i32, kind: i32) -> i32 {
    let _g = Guard::new();
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(e) => return e,
    };
    let h = &mut HANDLES[i];
    if !h.used {
        return INVALID_ARG;
    }
    let x = if kind == 0 {
        o as i64
    } else if kind == 1 {
        h.off as i64 + o as i64
    } else {
        return INVALID_ARG;
    };
    if x < 0 || x > h.size as i64 {
        return RANGE;
    }
    h.off = x as u32;
    x as i32
}
#[no_mangle]
pub unsafe extern "C" fn pfs_get_file_size(fd: i32) -> usize {
    let _g = Guard::new();
    fd_index(fd)
        .ok()
        .filter(|&i| HANDLES[i].used)
        .map_or(0, |i| HANDLES[i].size as usize)
}
#[no_mangle]
pub unsafe extern "C" fn pfs_close(fd: i32) -> i32 {
    let _g = Guard::new();
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(e) => return e,
    };
    let h = HANDLES[i];
    if !h.used {
        return INVALID_ARG;
    }
    if h.dirty {
        let Some(c) = file_crc(h.start, h.nl, h.size) else {
            return ERR;
        };
        if !program(h.start as usize * PAGE + PH + FH + 10, &c.to_le_bytes()) {
            return ERR;
        }
        if !program(h.start as usize * PAGE + PH + FH + 2, &[0, 0]) {
            return ERR;
        }
        if h.old != INVALID {
            mark_deleted(h.old)
        }
        notify(stored_name(h.name, h.nl), 1)
    }
    HANDLES[i] = EMPTY_HANDLE;
    pfs_rust_free(h.name.cast());
    if !h.page_map.is_null() {
        pfs_rust_free(h.page_map.cast());
    }
    SUCCESS
}
#[no_mangle]
pub unsafe extern "C" fn pfs_remove(n: *const c_char) -> i32 {
    let (name, nl) = match c_name(n) {
        Ok(x) => x,
        Err(e) => return e,
    };
    let _g = Guard::new();
    if let Some(f) = find(&name[..nl as usize], false) {
        mark_deleted(f.page);
        notify(&name[..nl as usize], 2);
        SUCCESS
    } else {
        NOENT
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_close_and_remove(fd: i32) -> i32 {
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(e) => return e,
    };
    if !HANDLES[i].used {
        return INVALID_ARG;
    }
    let mut name = [0u8; 256];
    let nl = HANDLES[i].nl;
    name[..nl as usize].copy_from_slice(stored_name(HANDLES[i].name, nl));
    let r = pfs_close(fd);
    if r < 0 {
        return r;
    }
    pfs_remove(name.as_ptr().cast())
}
#[no_mangle]
pub unsafe extern "C" fn pfs_get_size() -> u32 {
    SIZE.saturating_sub(SECTOR as u32)
}
#[no_mangle]
pub unsafe extern "C" fn pfs_set_size(n: u32, erased: bool) {
    let _g = Guard::new();
    let old = SIZE;
    let physical = n as usize / PAGE;
    let new_pages = physical.saturating_sub(16).min(u16::MAX as usize) as u16;
    let new_flags = if new_pages == 0 {
        ptr::null_mut()
    } else {
        pfs_rust_alloc(new_pages as usize).cast::<u8>()
    };
    if new_pages != 0 && new_flags.is_null() {
        return;
    }
    if !new_flags.is_null() {
        slice::from_raw_parts_mut(new_flags, new_pages as usize).fill(0xff);
        let retained = min(PAGES as usize, new_pages as usize);
        if retained != 0 {
            ptr::copy_nonoverlapping(PAGE_FLAGS, new_flags, retained);
        }
    }
    if !PAGE_FLAGS.is_null() {
        pfs_rust_free(PAGE_FLAGS.cast());
    }
    PAGE_FLAGS = new_flags;
    PAGES = new_pages;
    SIZE = n;
    if erased && n > old {
        for p in old as usize / PAGE..new_pages as usize {
            program(p * PAGE, &[1, 0x50, 0xff, 0xfe]);
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_active() -> bool {
    INITIALIZED && PAGES != 0
}
#[no_mangle]
pub unsafe extern "C" fn pfs_active_in_region(s: u32, e: u32) -> bool {
    if s >= e || s >= SIZE {
        return false;
    }
    let first = s as usize / PAGE;
    let end = min(e as usize / PAGE, PAGES as usize);
    for p in first..end {
        let mut b = [0xff; 4];
        read(p * PAGE, &mut b);
        if (le16(&b, 0) >> 8) == 0x50 {
            return true;
        }
    }
    false
}
#[no_mangle]
pub unsafe extern "C" fn get_available_pfs_space() -> u32 {
    let _g = Guard::new();
    let mut n: u32 = 0;
    for p in 0..PAGES {
        let mut b = [0];
        read(p as usize * PAGE + 3, &mut b);
        if b[0] == 0xff {
            n += 1
        }
    }
    n.saturating_sub(16) * PAGE as u32
}
#[no_mangle]
pub extern "C" fn pfs_sector_optimal_size(m: i32, n: i32) -> i32 {
    if m < 0 || n < 0 {
        return INVALID_ARG;
    }
    let first = PAGE as i32 - NAME_AT as i32 - n;
    if m <= first {
        first
    } else {
        m + (PAGE as i32 - PH as i32 - 1 - m.max(first)) % (PAGE as i32 - PH as i32)
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_crc_calculate_file(fd: i32, o: u32, n: u32) -> u32 {
    let _g = Guard::new();
    let i = match fd_index(fd) {
        Ok(x) => x,
        Err(_) => return 0,
    };
    let h = HANDLES[i];
    if !h.used || o.checked_add(n).map_or(true, |x| x > h.size) {
        return 0;
    }
    let mut checksum = LegacyChecksum {
        reg: 0,
        accumulator: [0; 3],
        accumulated_length: 0,
    };
    legacy_defective_checksum_init(&mut checksum);
    let mut b = [0u8; 256];
    let mut x = 0;
    while x < n {
        let z = min(b.len(), (n - x) as usize);
        if data_io(
            h.start,
            h.nl,
            h.size,
            o + x,
            b.as_mut_ptr(),
            z,
            false,
            h.page_map,
            h.page_count,
            h.page_stride,
        )
        .is_err()
        {
            return 0;
        }
        legacy_defective_checksum_update(&mut checksum, b.as_ptr().cast(), z);
        x += z as u32
    }
    HANDLES[i].off = o + n;
    legacy_defective_checksum_finish(&mut checksum)
}
#[no_mangle]
pub unsafe extern "C" fn pfs_watch_file(
    n: *const c_char,
    cb: Option<WatchCb>,
    ev: u8,
    data: *mut c_void,
) -> *mut c_void {
    let (name, nl) = match c_name(n) {
        Ok(x) => x,
        Err(_) => return ptr::null_mut(),
    };
    if cb.is_none() {
        return ptr::null_mut();
    }
    let _g = Guard::new();
    if let Some(i) = (0..MAX_WATCH).find(|&i| !WATCHES[i].used) {
        let name = alloc_name(&name[..nl as usize]);
        if name.is_null() {
            return ptr::null_mut();
        }
        WATCHES[i] = Watch {
            used: true,
            name,
            nl,
            cb,
            events: ev,
            data,
        };
        (i + 1) as *mut c_void
    } else {
        ptr::null_mut()
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_unwatch_file(h: *mut c_void) {
    let _g = Guard::new();
    let i = h as usize;
    if i > 0 && i <= MAX_WATCH {
        pfs_rust_free(WATCHES[i - 1].name.cast());
        WATCHES[i - 1] = EMPTY_WATCH
    }
}

#[repr(C)]
struct ListNode {
    next: *mut ListNode,
    prev: *mut ListNode,
}
#[no_mangle]
pub unsafe extern "C" fn pfs_create_file_list(
    filter: Option<unsafe extern "C" fn(*const c_char) -> bool>,
) -> *mut c_void {
    for i in 0..USER_FD {
        if HANDLES[i].used && HANDLES[i].dirty && pfs_close(FD_BASE + i as i32) < 0 {
            return ptr::null_mut();
        }
    }
    let _g = Guard::new();
    let mut head: *mut ListNode = ptr::null_mut();
    let mut tail = head;
    for p in 0..PAGES {
        let Some(f) = header(p, false) else { continue };
        let mut name = [0u8; 256];
        read(p as usize * PAGE + NAME_AT, &mut name[..f.nl as usize]);
        if find(&name[..f.nl as usize], false).map_or(true, |x| x.page != p) {
            continue;
        }
        if let Some(cb) = filter {
            pfs_rust_unlock();
            let yes = cb(name.as_ptr().cast());
            pfs_rust_lock();
            if !yes {
                continue;
            }
        }
        let q =
            pfs_rust_alloc(core::mem::size_of::<ListNode>() + f.nl as usize + 1) as *mut ListNode;
        if q.is_null() {
            continue;
        }
        (*q).next = ptr::null_mut();
        (*q).prev = tail;
        ptr::copy_nonoverlapping(name.as_ptr(), q.add(1).cast(), f.nl as usize);
        *q.add(1).cast::<u8>().add(f.nl as usize) = 0;
        if tail.is_null() {
            head = q
        } else {
            (*tail).next = q
        }
        tail = q
    }
    head.cast()
}
#[no_mangle]
pub unsafe extern "C" fn pfs_delete_file_list(mut h: *mut c_void) {
    while !h.is_null() {
        let n = (*(h as *mut ListNode)).next;
        pfs_rust_free(h);
        h = n.cast()
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_remove_files(cb: Option<unsafe extern "C" fn(*const c_char) -> bool>) {
    let list = pfs_create_file_list(cb);
    let mut p = list as *mut ListNode;
    while !p.is_null() {
        pfs_remove(p.add(1).cast());
        p = (*p).next
    }
    pfs_delete_file_list(list)
}
#[no_mangle]
pub unsafe extern "C" fn pfs_reboot_cleanup() {
    let _g = Guard::new();
    for p in 0..PAGES {
        if let Some(f) = header(p, true) {
            if f.version == V2 && le_generation_committed(p).is_none() {
                mark_deleted(p)
            }
        }
    }
}
unsafe fn le_generation_committed(p: u16) -> Option<u32> {
    let mut b = [0; 10];
    read(p as usize * PAGE + PH + FH, &mut b);
    if le16(&b, 2) == 0 {
        Some(le32(&b, 6))
    } else {
        None
    }
}
#[no_mangle]
pub unsafe extern "C" fn pfs_reset_all_state() {
    clear_handles();
    for i in 0..MAX_WATCH {
        let watch = &mut WATCHES[i];
        if !watch.name.is_null() {
            pfs_rust_free(watch.name.cast());
        }
        *watch = EMPTY_WATCH;
    }
    INITIALIZED = false
}
#[no_mangle]
pub unsafe extern "C" fn test_get_file_start_page(fd: i32) -> u16 {
    fd_index(fd).ok().map_or(INVALID, |i| HANDLES[i].start)
}
unsafe fn gc_marker() -> Option<(u16, u16)> {
    let reserve = PAGES as usize * PAGE;
    let mut marker = [0xffu8; 16];
    for slot in 0..16 {
        read(reserve + slot * PAGE, &mut marker);
        if marker[..8] != GC_MAGIC {
            continue;
        }
        let victim = le16(&marker, 8);
        let live = le16(&marker, 10);
        if crc32(0, &marker[..12]) == le32(&marker, 12)
            && victim < PAGES
            && victim % 16 == 0
            && live & (1 << slot) == 0
        {
            return Some((victim, live));
        }
    }
    None
}
unsafe fn restore_gc(victim: u16, live: u16, erase_victim: bool) {
    let reserve = PAGES;
    let mut b = [0u8; 256];
    if erase_victim {
        ftl_erase_sector(SECTOR as u32, victim as u32 * PAGE as u32);
        fill_page_flags(victim, 16, 0xff);
    }
    for i in 0..16 {
        if live & (1 << i) != 0 {
            for o in (0..PAGE).step_by(b.len()) {
                read((reserve + i) as usize * PAGE + o, &mut b);
                program((victim + i) as usize * PAGE + o, &b);
            }
        }
    }
    ftl_erase_sector(SECTOR as u32, reserve as u32 * PAGE as u32);
}
unsafe fn recover_gc() {
    if let Some((victim, live)) = gc_marker() {
        restore_gc(victim, live, true);
        return;
    }
    let reserve = PAGES as usize * PAGE;
    let mut b = [0xffu8; 16];
    let dirty = (0..16).any(|slot| {
        read(reserve + slot * PAGE, &mut b);
        b.iter().any(|&x| x != 0xff)
    });
    if dirty {
        ftl_erase_sector(SECTOR as u32, reserve as u32);
    }
}
unsafe fn compact_sector(page: u16, complete: bool) {
    let first = page / 16 * 16;
    if first >= PAGES {
        return;
    }
    let reserve = PAGES;
    let mut live = 0u16;
    let mut marker_slot = None;
    let mut b = [0u8; 256];
    ftl_erase_sector(SECTOR as u32, reserve as u32 * PAGE as u32);
    for i in 0..16 {
        if !page_blank(first + i) && !page_deleted(first + i) {
            live |= 1 << i;
            for o in (0..PAGE).step_by(b.len()) {
                read((first + i) as usize * PAGE + o, &mut b);
                program((reserve + i) as usize * PAGE + o, &b);
            }
        } else {
            marker_slot = Some(i);
        }
    }
    let Some(marker_slot) = marker_slot else {
        return;
    };
    let mut marker = [0xffu8; 16];
    marker[..8].copy_from_slice(&GC_MAGIC);
    put16(&mut marker, 8, first);
    put16(&mut marker, 10, live);
    let checksum = crc32(0, &marker[..12]);
    put32(&mut marker, 12, checksum);
    if !program((reserve + marker_slot) as usize * PAGE, &marker) {
        return;
    }
    ftl_erase_sector(SECTOR as u32, first as u32 * PAGE as u32);
    fill_page_flags(first, 16, 0xff);
    if complete {
        restore_gc(first, live, false);
    }
}
#[no_mangle]
pub unsafe extern "C" fn test_force_garbage_collection(p: u16) {
    let _g = Guard::new();
    compact_sector(p, true)
}
#[no_mangle]
pub unsafe extern "C" fn test_force_reboot_during_garbage_collection(p: u16) {
    let _g = Guard::new();
    compact_sector(p, false)
}
#[no_mangle]
pub extern "C" fn test_force_recalc_of_gc_region() {}
#[no_mangle]
pub unsafe extern "C" fn test_override_last_written_page(p: u16) {
    LAST_WRITTEN = p
}
#[no_mangle]
pub unsafe extern "C" fn test_scan_for_last_written() -> i32 {
    if LAST_WRITTEN == INVALID {
        -1
    } else {
        LAST_WRITTEN as i32
    }
}
