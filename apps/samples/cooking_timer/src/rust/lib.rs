// SPDX-FileCopyrightText: 2026 Core Devices LLC
// SPDX-License-Identifier: Apache-2.0

#![no_std]

use core::cell::UnsafeCell;
use core::ffi::{c_char, c_int};

const DEFAULT_MINUTES: u8 = 5;
const DEFAULT_REPEATS: u8 = 4;
const MAX_MINUTES: u8 = 60;
const MAX_REPEATS: u8 = 20;

#[derive(Clone, Copy, Debug, PartialEq)]
enum Mode {
    Ready,
    Running,
    Paused,
    BetweenBatches,
    Finished,
}

#[derive(Debug, PartialEq)]
struct Timer {
    minutes: u8,
    repeats: u8,
    batch: u8,
    seconds_left: u32,
    mode: Mode,
}

#[derive(Default, Debug, PartialEq)]
struct Effects {
    schedule_tick: bool,
    cancel_tick: bool,
    vibe: Option<bool>,
}

impl Timer {
    const fn new() -> Self {
        Self {
            minutes: DEFAULT_MINUTES,
            repeats: DEFAULT_REPEATS,
            batch: 1,
            seconds_left: DEFAULT_MINUTES as u32 * 60,
            mode: Mode::Ready,
        }
    }

    fn select(&mut self) -> Effects {
        match self.mode {
            Mode::Ready | Mode::Paused | Mode::BetweenBatches => {
                self.mode = Mode::Running;
                Effects {
                    schedule_tick: true,
                    ..Effects::default()
                }
            }
            Mode::Running => {
                self.mode = Mode::Paused;
                Effects {
                    cancel_tick: true,
                    ..Effects::default()
                }
            }
            Mode::Finished => {
                self.batch = 1;
                self.seconds_left = self.minutes as u32 * 60;
                self.mode = Mode::Ready;
                Effects::default()
            }
        }
    }

    fn tick(&mut self) -> Effects {
        if self.mode != Mode::Running {
            return Effects::default();
        }

        self.seconds_left -= 1;
        if self.seconds_left != 0 {
            return Effects {
                schedule_tick: true,
                ..Effects::default()
            };
        }

        if self.batch < self.repeats {
            self.batch += 1;
            self.seconds_left = self.minutes as u32 * 60;
            self.mode = Mode::BetweenBatches;
            Effects {
                vibe: Some(false),
                ..Effects::default()
            }
        } else {
            self.mode = Mode::Finished;
            Effects {
                vibe: Some(true),
                ..Effects::default()
            }
        }
    }

    fn increment_minutes(&mut self) {
        if self.mode == Mode::Ready {
            self.minutes = if self.minutes == MAX_MINUTES {
                1
            } else {
                self.minutes + 1
            };
            self.seconds_left = self.minutes as u32 * 60;
        }
    }

    fn increment_repeats(&mut self) {
        if self.mode == Mode::Ready {
            self.repeats = if self.repeats == MAX_REPEATS {
                1
            } else {
                self.repeats + 1
            };
        }
    }
}

struct TextBuffer<const N: usize> {
    bytes: [u8; N],
    len: usize,
}

impl<const N: usize> TextBuffer<N> {
    const fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }

    fn clear(&mut self) {
        self.len = 0;
        unsafe { *self.bytes.get_unchecked_mut(0) = 0 };
    }

    fn push(&mut self, byte: u8) {
        if self.len + 1 < N {
            unsafe { *self.bytes.get_unchecked_mut(self.len) = byte };
            self.len += 1;
            unsafe { *self.bytes.get_unchecked_mut(self.len) = 0 };
        }
    }

    fn push_str(&mut self, text: &str) {
        for byte in text.bytes() {
            self.push(byte);
        }
    }

    fn push_number(&mut self, number: u32) {
        if number >= 10 {
            self.push_number(number / 10);
        }
        self.push(b'0' + (number % 10) as u8);
    }

    fn as_ptr(&self) -> *const c_char {
        self.bytes.as_ptr().cast()
    }
}

struct Global<T>(UnsafeCell<T>);

unsafe impl<T> Sync for Global<T> {}

static TIMER: Global<Timer> = Global(UnsafeCell::new(Timer::new()));
static TIME_TEXT: Global<TextBuffer<8>> = Global(UnsafeCell::new(TextBuffer::new()));
static BATCH_TEXT: Global<TextBuffer<24>> = Global(UnsafeCell::new(TextBuffer::new()));

unsafe extern "C" {
    fn cooking_timer_schedule_tick();
    fn cooking_timer_cancel_tick();
    fn cooking_timer_vibe(final_batch: c_int);
    fn cooking_timer_set_text(time: *const c_char, batch: *const c_char, status: *const c_char);
}

const READY_TEXT: &[u8] = b"SELECT to start\nUP min  DOWN repeats\0";
const RUNNING_TEXT: &[u8] = b"SELECT to pause\0";
const PAUSED_TEXT: &[u8] = b"Paused - SELECT resume\0";
const BETWEEN_TEXT: &[u8] = b"Batch done! SELECT next\0";
const FINISHED_TEXT: &[u8] = b"All batches done!\nSELECT to reset\0";

fn apply(effects: Effects) {
    unsafe {
        if effects.cancel_tick {
            cooking_timer_cancel_tick();
        }
        if effects.schedule_tick {
            cooking_timer_schedule_tick();
        }
        if let Some(final_batch) = effects.vibe {
            cooking_timer_vibe(final_batch as c_int);
        }
    }
}

#[no_mangle]
pub extern "C" fn cooking_timer_render() {
    unsafe {
        let timer = &*TIMER.0.get();
        let time_text = &mut *TIME_TEXT.0.get();
        let batch_text = &mut *BATCH_TEXT.0.get();

        time_text.clear();
        time_text.push_number(timer.seconds_left / 60);
        time_text.push(b':');
        let seconds = timer.seconds_left % 60;
        if seconds < 10 {
            time_text.push(b'0');
        }
        time_text.push_number(seconds);

        batch_text.clear();
        batch_text.push_str("Batch ");
        batch_text.push_number(timer.batch as u32);
        batch_text.push_str(" of ");
        batch_text.push_number(timer.repeats as u32);

        let status = match timer.mode {
            Mode::Ready => READY_TEXT,
            Mode::Running => RUNNING_TEXT,
            Mode::Paused => PAUSED_TEXT,
            Mode::BetweenBatches => BETWEEN_TEXT,
            Mode::Finished => FINISHED_TEXT,
        };
        cooking_timer_set_text(
            time_text.as_ptr(),
            batch_text.as_ptr(),
            status.as_ptr().cast(),
        );
    }
}

#[no_mangle]
pub extern "C" fn cooking_timer_select() {
    let effects = unsafe { (&mut *TIMER.0.get()).select() };
    apply(effects);
    cooking_timer_render();
}

#[no_mangle]
pub extern "C" fn cooking_timer_up() {
    unsafe { (&mut *TIMER.0.get()).increment_minutes() };
    cooking_timer_render();
}

#[no_mangle]
pub extern "C" fn cooking_timer_down() {
    unsafe { (&mut *TIMER.0.get()).increment_repeats() };
    cooking_timer_render();
}

#[no_mangle]
pub extern "C" fn cooking_timer_tick() {
    let effects = unsafe { (&mut *TIMER.0.get()).tick() };
    apply(effects);
    cooking_timer_render();
}

#[cfg(test)]
mod tests {
    extern crate std;

    use super::*;

    #[test]
    fn starts_pauses_and_resumes_without_resetting_time() {
        let mut timer = Timer::new();
        assert!(timer.select().schedule_tick);
        timer.tick();
        assert_eq!(timer.seconds_left, 299);

        assert!(timer.select().cancel_tick);
        assert_eq!(timer.mode, Mode::Paused);
        assert_eq!(timer.seconds_left, 299);

        assert!(timer.select().schedule_tick);
        assert_eq!(timer.mode, Mode::Running);
        assert_eq!(timer.seconds_left, 299);
    }

    #[test]
    fn completed_batch_waits_for_user_before_next_batch() {
        let mut timer = Timer::new();
        timer.seconds_left = 1;
        timer.mode = Mode::Running;

        let effects = timer.tick();
        assert_eq!(effects.vibe, Some(false));
        assert!(!effects.schedule_tick);
        assert_eq!(timer.mode, Mode::BetweenBatches);
        assert_eq!(timer.batch, 2);
        assert_eq!(timer.seconds_left, 300);

        assert!(timer.select().schedule_tick);
        assert_eq!(timer.mode, Mode::Running);
    }

    #[test]
    fn final_batch_finishes_and_select_resets_the_session() {
        let mut timer = Timer::new();
        timer.batch = 4;
        timer.seconds_left = 1;
        timer.mode = Mode::Running;

        assert_eq!(timer.tick().vibe, Some(true));
        assert_eq!(timer.mode, Mode::Finished);
        assert_eq!(timer.seconds_left, 0);

        timer.select();
        assert_eq!(timer.mode, Mode::Ready);
        assert_eq!(timer.batch, 1);
        assert_eq!(timer.seconds_left, 300);
    }

    #[test]
    fn settings_only_change_before_the_session_starts() {
        let mut timer = Timer::new();
        timer.increment_minutes();
        timer.increment_repeats();
        assert_eq!(
            (timer.minutes, timer.repeats, timer.seconds_left),
            (6, 5, 360)
        );

        timer.select();
        timer.increment_minutes();
        timer.increment_repeats();
        assert_eq!((timer.minutes, timer.repeats), (6, 5));
    }
}
