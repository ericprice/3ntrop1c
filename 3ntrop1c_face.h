/*
 * MIT License
 *
 * Copyright (c) 2025
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef N3TROP1C_FACE_H_
#define N3TROP1C_FACE_H_

#include "movement.h"

typedef struct {
    // unique physical segments discovered from Segment_Map + extras
    uint8_t seg_com[96]; // packed: high nibble unused, upper byte not used; store COM in high 8 bits? Keep two arrays for clarity
    uint8_t seg_seg[96];
    uint16_t num_segments;

    // randomized order per hour
    uint16_t order[96];
    // blinking configuration per segment
    uint8_t blink_rate_hz[96]; // 1..4 toggles per second
    uint8_t tick_accum[96];    // 0..3 accumulator for fractional rates
    uint8_t initial_state[96]; // 0/1 starting on/off to add phase variety
    uint8_t current_state[96]; // 0/1 currently displayed

    // hour scheduling
    uint16_t chunk_counts[6];       // number of segments activated per 10-minute chunk
    uint16_t cumulative_counts[6];  // cumulative activation thresholds

    uint8_t last_hour;
    bool segments_initialized;
} entrop1c_state_t;

void entrop1c_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr);
void entrop1c_face_activate(movement_settings_t *settings, void *context);
bool entrop1c_face_loop(movement_event_t event, movement_settings_t *settings, void *context);
void entrop1c_face_resign(movement_settings_t *settings, void *context);

#define entrop1c_face ((const watch_face_t){ \
    entrop1c_face_setup, \
    entrop1c_face_activate, \
    entrop1c_face_loop, \
    entrop1c_face_resign, \
    NULL, \
})

#endif // N3TROP1C_FACE_H_


