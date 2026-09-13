// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

#[repr(C)]
pub struct QueueAccounting {
    queued_bytes: usize,
}

impl QueueAccounting {
    fn reset(&mut self) {
        self.queued_bytes = 0;
    }

    fn add(&mut self, length: usize) -> bool {
        let Some(queued_bytes) = self.queued_bytes.checked_add(length) else {
            return false;
        };
        self.queued_bytes = queued_bytes;
        true
    }

    fn consume(&mut self, length: usize) -> bool {
        let Some(queued_bytes) = self.queued_bytes.checked_sub(length) else {
            return false;
        };
        self.queued_bytes = queued_bytes;
        true
    }
}

#[no_mangle]
pub extern "C" fn comm_session_queue_accounting_reset(accounting: &mut QueueAccounting) {
    accounting.reset();
}

#[no_mangle]
pub extern "C" fn comm_session_queue_accounting_add(
    accounting: &mut QueueAccounting,
    length: usize,
) -> bool {
    accounting.add(length)
}

#[no_mangle]
pub extern "C" fn comm_session_queue_accounting_consume(
    accounting: &mut QueueAccounting,
    length: usize,
) -> bool {
    accounting.consume(length)
}

#[no_mangle]
pub extern "C" fn comm_session_queue_accounting_get(accounting: &QueueAccounting) -> usize {
    accounting.queued_bytes
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tracks_add_and_partial_consume() {
        let mut accounting = QueueAccounting { queued_bytes: 0 };

        assert!(accounting.add(6));
        assert!(accounting.add(11));
        assert!(accounting.consume(8));
        assert_eq!(accounting.queued_bytes, 9);
    }

    #[test]
    fn rejects_overflow_without_changing_length() {
        let mut accounting = QueueAccounting {
            queued_bytes: usize::MAX - 2,
        };

        assert!(!accounting.add(3));
        assert_eq!(accounting.queued_bytes, usize::MAX - 2);
    }

    #[test]
    fn rejects_overconsume_without_changing_length() {
        let mut accounting = QueueAccounting { queued_bytes: 7 };

        assert!(!accounting.consume(8));
        assert_eq!(accounting.queued_bytes, 7);
    }

    #[test]
    fn reset_clears_length() {
        let mut accounting = QueueAccounting { queued_bytes: 42 };

        accounting.reset();
        assert_eq!(accounting.queued_bytes, 0);
    }
}
