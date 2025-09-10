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

#include <stdlib.h>
#include <string.h>
#include "3ntrop1c_face.h"
#include "watch.h"
#include "watch_common_display.h"

static inline void set_pix(uint8_t com, uint8_t seg) { watch_set_pixel(com, seg); }
static inline void clr_pix(uint8_t com, uint8_t seg) { watch_clear_pixel(com, seg); }

// Build a unique list of physical segments from Segment_Map (positions 0..9, 8 segments each)
// plus colon and indicator segments and the special extra pixels used in watch_private_display.c
static void build_unique_segments(entrop1c_state_t *state) {
    // mark seen [3 COM x 24 SEG]
    uint8_t seen[3][24];
    memset(seen, 0, sizeof(seen));
    state->num_segments = 0;

    for (uint8_t position = 0; position < 10; position++) {
        const digit_mapping_t map = (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
            ? Custom_LCD_Display_Mapping[position]
            : Classic_LCD_Display_Mapping[position];
        for (int i = 0; i < 8; i++) {
            if (map.segment[i].value == segment_does_not_exist) continue;
            uint8_t com = map.segment[i].address.com;
            uint8_t seg = map.segment[i].address.seg;
            if (com <= 2 && seg < 24 && !seen[com][seg]) {
                seen[com][seg] = 1;
                state->seg_packed[state->num_segments++] = PACK_SEG(com, seg);
            }
        }
    }

    // Colon at (1,16)
    if (!seen[1][16]) {
        seen[1][16] = 1;
        state->seg_packed[state->num_segments++] = PACK_SEG(1, 16);
    }
    // Indicators from watch_private_display.c IndicatorSegments
    const uint8_t ind_list[][2] = { {0,17}, {0,16}, {2,17}, {2,16}, {1,10} };
    for (size_t i = 0; i < sizeof(ind_list)/sizeof(ind_list[0]); i++) {
        uint8_t com = ind_list[i][0], seg = ind_list[i][1];
        if (com <= 2 && seg < 24 && !seen[com][seg]) {
            seen[com][seg] = 1;
            state->seg_packed[state->num_segments++] = PACK_SEG(com, seg);
        }
    }
    // Special pixels used for funky ninth segments / descenders: (0,15), (0,12), (1,12)
    const uint8_t special[][2] = { {0,15}, {0,12}, {1,12} };
    for (size_t i = 0; i < sizeof(special)/sizeof(special[0]); i++) {
        uint8_t com = special[i][0], seg = special[i][1];
        if (com <= 2 && seg < 24 && !seen[com][seg]) {
            seen[com][seg] = 1;
            state->seg_packed[state->num_segments++] = PACK_SEG(com, seg);
        }
    }
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}

static void shuffle_order(entrop1c_state_t *state, uint32_t *rng) {
    for (uint8_t i = 0; i < state->num_segments; i++) state->order[i] = i;
    for (uint8_t i = state->num_segments; i > 1; i--) {
        uint32_t r = xorshift32(rng) % i;
        uint8_t j = (uint8_t)r;
        uint8_t tmp = state->order[i - 1];
        state->order[i - 1] = state->order[j];
        state->order[j] = tmp;
    }
}

static void assign_blink_rates(entrop1c_state_t *state, uint32_t *rng) {
    memset(state->blink_config, 0, sizeof(state->blink_config));
    memset(state->initial_state, 0, sizeof(state->initial_state));
    memset(state->current_state, 0, sizeof(state->current_state));
    
    for (uint8_t i = 0; i < state->num_segments; i++) {
        // Only use rates that divide evenly into 8Hz: 1Hz, 2Hz, 4Hz (skip 3Hz)
        uint8_t rate_choice = (uint8_t)(xorshift32(rng) % 3);  // 0, 1, 2
        uint8_t rate = (rate_choice == 0) ? 0 : (rate_choice == 1) ? 1 : 3; // maps to 1Hz, 2Hz, 4Hz
        uint8_t accum = (uint8_t)(xorshift32(rng) & 0x03); // 0..3 phase
        
        // Pack rate (2 bits) and accum (2 bits) into 4 bits
        uint8_t config = (rate << 2) | accum;
        uint8_t byte_idx = i / 2;
        if (i % 2 == 0) {
            state->blink_config[byte_idx] = (state->blink_config[byte_idx] & 0xF0) | config;
        } else {
            state->blink_config[byte_idx] = (state->blink_config[byte_idx] & 0x0F) | (config << 4);
        }
        
        // Set initial state bit
        if (xorshift32(rng) & 0x01) {
            SET_BIT(state->initial_state, i);
        }
    }
}

static inline uint8_t get_blink_rate(entrop1c_state_t *state, uint8_t idx) {
    uint8_t byte_idx = idx / 2;
    uint8_t config = (idx % 2 == 0) ? 
        (state->blink_config[byte_idx] & 0x0F) : 
        ((state->blink_config[byte_idx] >> 4) & 0x0F);
    return ((config >> 2) & 0x03) + 1; // Extract rate bits and add 1 for 1..4 Hz
}

static inline uint8_t get_tick_accum(entrop1c_state_t *state, uint8_t idx) {
    uint8_t byte_idx = idx / 2;
    uint8_t config = (idx % 2 == 0) ? 
        (state->blink_config[byte_idx] & 0x0F) : 
        ((state->blink_config[byte_idx] >> 4) & 0x0F);
    return config & 0x03; // Extract accum bits
}

static void compute_chunk_counts(entrop1c_state_t *state) {
    // Turn on 1/6 of the full set every 10 seconds, rounding as needed to sum to num_segments
    uint8_t base = state->num_segments / 6;
    uint8_t rem = state->num_segments % 6;
    uint8_t sum = 0;
    for (uint8_t k = 0; k < 6; k++) {
        uint8_t c = base + (k < rem ? 1 : 0);
        state->chunk_counts[k] = c;
        sum += c;
        state->cumulative_counts[k] = sum;
    }
}

static void turn_off_all(entrop1c_state_t *state) {
    for (uint8_t i = 0; i < state->num_segments; i++) {
        uint8_t packed = state->seg_packed[i];
        clr_pix(UNPACK_COM(packed), UNPACK_SEG(packed));
    }
    memset(state->current_state, 0, sizeof(state->current_state));
}

void entrop1c_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        entrop1c_state_t *state = (entrop1c_state_t *)malloc(sizeof(entrop1c_state_t));
        memset(state, 0, sizeof(*state));
        *context_ptr = state;
    }
}

void entrop1c_face_activate(void *context) {
    entrop1c_state_t *state = (entrop1c_state_t *)context;

    if (watch_sleep_animation_is_running()) watch_stop_sleep_animation();
    movement_request_tick_frequency(1); // Start at 1Hz, increase when segments activate
    state->current_freq = 1;

    watch_clear_display();

    if (!state->segments_initialized) {
        build_unique_segments(state);
        state->segments_initialized = true;
    }

    // Seed RNG from RTC time
            watch_date_time_t now = movement_get_local_date_time();
    uint32_t seed = (now.reg ^ 0xA5A5A5A5u) + (now.unit.second * 1664525u + 1013904223u);
    shuffle_order(state, &seed);
    assign_blink_rates(state, &seed);
    compute_chunk_counts(state);

    state->last_hour = 0xFF; // Force update on first tick
    turn_off_all(state);
}

static uint8_t segments_should_be_active(entrop1c_state_t *state, uint8_t minute) {
    // 0-9 min => chunk0, 10-19 => chunk1, ... 50-59 => chunk5
    uint8_t chunk = minute / 10; if (chunk > 5) chunk = 5;
    return state->cumulative_counts[chunk];
}

static void apply_activation_and_blink(entrop1c_state_t *state, uint8_t subsecond, uint8_t active_target) {
    // First ensure only the first N in shuffled order are considered active
    for (uint8_t idx = 0; idx < state->num_segments; idx++) {
        uint8_t seg_index = state->order[idx];
        bool is_active = (idx < active_target);
        uint8_t packed = state->seg_packed[seg_index];
        uint8_t com = UNPACK_COM(packed);
        uint8_t seg = UNPACK_SEG(packed);
        bool current_on = GET_BIT(state->current_state, seg_index);

        if (!is_active) {
            if (current_on) {
                clr_pix(com, seg);
                CLEAR_BIT(state->current_state, seg_index);
            }
            continue;
        }

        // Get blink configuration
        uint8_t rate = get_blink_rate(state, seg_index);    // 1, 2, or 4 Hz
        uint8_t phase = get_tick_accum(state, seg_index);   // 0..3 phase
        
        // At 8Hz base frequency, calculate proper toggling
        bool on;
        if (rate == 4) {
            // 4 Hz: toggle every 2 ticks (8Hz/4Hz = 2)
            on = (((subsecond + phase) / 2) & 1) == 0;
        } else if (rate == 2) {
            // 2 Hz: toggle every 4 ticks (8Hz/2Hz = 4)
            on = (((subsecond + phase) / 4) & 1) == 0;
        } else {
            // 1 Hz: toggle every 8 ticks (8Hz/1Hz = 8)
            on = (((subsecond + phase) / 8) & 1) == 0;
        }
        
        // Randomize initial state impact
        if (GET_BIT(state->initial_state, seg_index)) on = !on;

        if (on) {
            if (!current_on) {
                set_pix(com, seg);
                SET_BIT(state->current_state, seg_index);
            }
        } else {
            if (current_on) {
                clr_pix(com, seg);
                CLEAR_BIT(state->current_state, seg_index);
            }
        }
    }
}

bool entrop1c_face_loop(movement_event_t event, void *context) {
    entrop1c_state_t *state = (entrop1c_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE: {
            watch_date_time_t now = watch_rtc_get_date_time();

            // On hour rollover, turn off all segments and re-randomize
            if (now.unit.hour != state->last_hour) {
                turn_off_all(state);  // More efficient than watch_clear_display()
                uint32_t seed = (now.reg ^ 0xC3C3C3C3u) + (now.unit.second * 1103515245u + 12345u);
                shuffle_order(state, &seed);
                assign_blink_rates(state, &seed);
                compute_chunk_counts(state);
                state->last_hour = now.unit.hour;
            }

            uint8_t active_target = segments_should_be_active(state, now.unit.minute);

            bool low_energy = (event.event_type == EVENT_LOW_ENERGY_UPDATE);

            if (low_energy) {
                // Low-power mode: render a static snapshot at 1 Hz (no blinking)
                if (state->current_freq != 1) {
                    movement_request_tick_frequency(1);
                    state->current_freq = 1;
                }
                // Ensure only the first N are on, rest are off
                for (uint8_t idx = 0; idx < state->num_segments; idx++) {
                    uint8_t seg_index = state->order[idx];
                    uint8_t packed = state->seg_packed[seg_index];
                    uint8_t com = UNPACK_COM(packed);
                    uint8_t seg = UNPACK_SEG(packed);
                    bool should_on = (idx < active_target);
                    bool is_on = GET_BIT(state->current_state, seg_index);
                    if (should_on && !is_on) {
                        set_pix(com, seg);
                        SET_BIT(state->current_state, seg_index);
                    } else if (!should_on && is_on) {
                        clr_pix(com, seg);
                        CLEAR_BIT(state->current_state, seg_index);
                    }
                }
            } else {
                // Adaptive frequency: only use high frequency when segments are active
                if (active_target > 0 && state->current_freq != 8) {
                    movement_request_tick_frequency(8);
                    state->current_freq = 8;
                } else if (active_target == 0 && state->current_freq != 1) {
                    movement_request_tick_frequency(1);
                    state->current_freq = 1;
                }
                uint8_t subsecond = (state->current_freq == 8) ? (event.subsecond & 0x07) : 0;
                apply_activation_and_blink(state, subsecond, active_target);
            }

            break;
        }
        default:
            return movement_default_loop_handler(event);
    }
    return true;
}

void entrop1c_face_resign(void *context) {
    entrop1c_state_t *state = (entrop1c_state_t *)context;
    (void) state;
    movement_request_tick_frequency(1);
}
