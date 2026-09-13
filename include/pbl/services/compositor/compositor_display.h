/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! @file compositor_display.h
//!
//! This module handles copying the framebuffer content to the display driver.

void compositor_display_update(void (*handle_update_complete_cb)(void));

bool compositor_display_update_in_progress(void);

//! Number of rows submitted by the most recently completed update.
uint16_t compositor_display_get_last_row_count(void);
