/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

void glyph_rasterize_8bit_row(const uint32_t *source, uint32_t source_bit_offset,
                              uint8_t *destination, uint32_t width, uint8_t color,
                              const uint8_t *blend_33_lut);
