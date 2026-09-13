// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![cfg_attr(not(test), no_std)]

#[repr(C)]
pub struct ReclaimPlan {
    remaining: u32,
}

fn reclaim_target(required: u32, increment: u32) -> u32 {
    if increment == 0 {
        return required;
    }
    required
        .saturating_add(increment - 1)
        .checked_div(increment)
        .unwrap_or(0)
        .saturating_mul(increment)
}

impl ReclaimPlan {
    fn new(required: u32, already_deleted: u32, increment: u32) -> Self {
        Self {
            remaining: reclaim_target(required, increment).saturating_sub(already_deleted),
        }
    }

    fn keep(&mut self, deleted: bool, record_size: u32) -> bool {
        if deleted {
            return false;
        }
        if self.remaining == 0 {
            return true;
        }
        self.remaining = self.remaining.saturating_sub(record_size);
        false
    }
}

#[no_mangle]
pub extern "C" fn notification_reclaim_plan_init(
    plan: &mut ReclaimPlan,
    required: u32,
    already_deleted: u32,
    increment: u32,
) {
    *plan = ReclaimPlan::new(required, already_deleted, increment);
}

#[no_mangle]
pub extern "C" fn notification_reclaim_plan_keep(
    plan: &mut ReclaimPlan,
    deleted: bool,
    record_size: u32,
) -> bool {
    plan.keep(deleted, record_size)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rounds_reclaim_to_increment_without_over_rounding() {
        assert_eq!(reclaim_target(1, 100), 100);
        assert_eq!(reclaim_target(100, 100), 100);
        assert_eq!(reclaim_target(101, 100), 200);
    }

    #[test]
    fn drops_tombstones_then_only_enough_old_live_records() {
        let mut plan = ReclaimPlan::new(100, 40, 100);

        assert!(!plan.keep(true, 40));
        assert!(!plan.keep(false, 35));
        assert!(!plan.keep(false, 30));
        assert!(plan.keep(false, 20));
    }

    #[test]
    fn keeps_live_records_when_tombstones_satisfy_target() {
        let mut plan = ReclaimPlan::new(80, 100, 100);

        assert!(!plan.keep(true, 100));
        assert!(plan.keep(false, 20));
    }
}
