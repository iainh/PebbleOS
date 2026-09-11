// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::ffi::c_int;

const ATTRIBUTE_HEADER_LENGTH: usize = 3;
const NOTIFICATION_HEADER_LENGTH: usize = 5;
const OPTIONAL_ATTRIBUTE: u8 = 1;
const MAX_ATTRIBUTES: usize = 32;

#[repr(C)]
pub struct FetchedAttribute {
    id: u8,
    max_length: u8,
    flags: u8,
}

#[repr(C, packed)]
pub struct AncsAttribute {
    id: u8,
    length: u16,
}

const NOTIFICATION_ATTRIBUTES: [FetchedAttribute; 8] = [
    FetchedAttribute {
        id: 0,
        max_length: 0,
        flags: 0,
    },
    FetchedAttribute {
        id: 1,
        max_length: 128,
        flags: 0,
    },
    FetchedAttribute {
        id: 2,
        max_length: 40,
        flags: 0,
    },
    FetchedAttribute {
        id: 3,
        max_length: 200,
        flags: 0,
    },
    FetchedAttribute {
        id: 4,
        max_length: 0,
        flags: OPTIONAL_ATTRIBUTE,
    },
    FetchedAttribute {
        id: 5,
        max_length: 15,
        flags: 0,
    },
    FetchedAttribute {
        id: 6,
        max_length: 0,
        flags: OPTIONAL_ATTRIBUTE,
    },
    FetchedAttribute {
        id: 7,
        max_length: 0,
        flags: OPTIONAL_ATTRIBUTE,
    },
];

const APP_ATTRIBUTES: [FetchedAttribute; 1] = [FetchedAttribute {
    id: 0,
    max_length: 0,
    flags: 0,
}];

unsafe fn set_error(out_error: *mut bool, value: bool) {
    *out_error = value;
}

#[no_mangle]
pub unsafe extern "C" fn ancs_util_get_attr_ptrs(
    data: *const u8,
    length: usize,
    attr_list: *const FetchedAttribute,
    num_attrs: c_int,
    out_attr_ptrs: *mut *mut AncsAttribute,
    out_error: *mut bool,
) -> bool {
    set_error(out_error, false);
    if length < ATTRIBUTE_HEADER_LENGTH
        || num_attrs <= 0
        || num_attrs as usize > MAX_ATTRIBUTES
        || length > isize::MAX as usize
    {
        set_error(out_error, true);
        return false;
    }

    let data_slice = core::slice::from_raw_parts(data, length);
    let attributes = core::slice::from_raw_parts(attr_list, num_attrs as usize);
    let mut found = 0_u32;
    let mut offset = 0;
    let mut extracted_complete_attribute = false;

    while length - offset >= ATTRIBUTE_HEADER_LENGTH {
        // The remaining-length check above proves these three reads are in bounds.
        let header = data_slice.as_ptr().add(offset);
        let id = *header;
        let attribute_length = u16::from_le_bytes([*header.add(1), *header.add(2)]) as usize;
        let Some(attribute_index) = attributes.iter().position(|attribute| attribute.id == id)
        else {
            set_error(out_error, true);
            return false;
        };
        let attribute = &attributes[attribute_index];
        if attribute.max_length != 0 && attribute_length > usize::from(attribute.max_length) {
            set_error(out_error, true);
            return false;
        }

        found |= 1 << attribute_index;
        if !out_attr_ptrs.is_null() {
            *out_attr_ptrs.add(attribute_index) = data.add(offset).cast_mut().cast();
        }

        let value_offset = offset + ATTRIBUTE_HEADER_LENGTH;
        if attribute_length > length - value_offset {
            extracted_complete_attribute = false;
            break;
        }
        extracted_complete_attribute = true;
        offset = value_offset + attribute_length;
    }

    for (index, attribute) in attributes.iter().enumerate() {
        if attribute.flags & OPTIONAL_ATTRIBUTE == 0 && found & (1 << index) == 0 {
            return false;
        }
    }
    extracted_complete_attribute
}

#[no_mangle]
pub unsafe extern "C" fn ancs_util_is_complete_notif_attr_response(
    data: *const u8,
    length: usize,
    out_error: *mut bool,
) -> bool {
    if length <= NOTIFICATION_HEADER_LENGTH {
        return false;
    }
    ancs_util_get_attr_ptrs(
        data.add(NOTIFICATION_HEADER_LENGTH),
        length - NOTIFICATION_HEADER_LENGTH,
        NOTIFICATION_ATTRIBUTES.as_ptr(),
        NOTIFICATION_ATTRIBUTES.len() as c_int,
        core::ptr::null_mut(),
        out_error,
    )
}

#[no_mangle]
pub unsafe extern "C" fn ancs_util_is_complete_app_attr_dict(
    data: *const u8,
    length: usize,
    out_error: *mut bool,
) -> bool {
    if length == 0 || length > isize::MAX as usize {
        set_error(out_error, false);
        return false;
    }
    let data_slice = core::slice::from_raw_parts(data, length);
    let Some(terminator) = data_slice.iter().position(|&byte| byte == 0) else {
        set_error(out_error, false);
        return false;
    };
    let attribute_offset = terminator + 1;
    if attribute_offset == length {
        set_error(out_error, false);
        return false;
    }
    ancs_util_get_attr_ptrs(
        data.add(attribute_offset),
        length - attribute_offset,
        APP_ATTRIBUTES.as_ptr(),
        APP_ATTRIBUTES.len() as c_int,
        core::ptr::null_mut(),
        out_error,
    )
}
