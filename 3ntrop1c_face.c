/*
 * MIT License
 *
 * Copyright (c) 2025
 */

#include <stdlib.h>
#include <string.h>
#include "3ntrop1c_face.h"
#include "watch.h"
#include "watch_private_display.h"

// Helpers
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
        uint64_t segmap = Segment_Map[position];
        // each character uses 8 segment slots; COM3 means absent
        for (int i = 0; i < 8; i++) {
            uint8_t com = (segmap & 0xFF) >> 6;
            uint8_t seg = segmap & 0x3F;
            if (com <= 2 && seg < 24 && !seen[com][seg]) {
                seen[com][seg] = 1;
                state->seg_com[state->num_segments] = com;
                state->seg_seg[state->num_segments] = seg;
                state->num_segments++;
            }
            segmap >>= 8;
        }
    }

    // Colon at (1,16)
    if (!seen[1][16]) { seen[1][16] = 1; state->seg_com[state->num_segments] = 1; state->seg_seg[state->num_segments++] = 16; }
    // Indicators from watch_private_display.c IndicatorSegments
    const uint8_t ind_list[][2] = { {0,17}, {0,16}, {2,17}, {2,16}, {1,10} };
    for (size_t i = 0; i < sizeof(ind_list)/sizeof(ind_list[0]); i++) {
        uint8_t com = ind_list[i][0], seg = ind_list[i][1];
        if (com <= 2 && seg < 24 && !seen[com][seg]) {
            seen[com][seg] = 1; state->seg_com[state->num_segments] = com; state->seg_seg[state->num_segments++] = seg;
        }
    }
    // Special pixels used for funky ninth segments / descenders: (0,15), (0,12), (1,12)
    const uint8_t special[][2] = { {0,15}, {0,12}, {1,12} };
    for (size_t i = 0; i < sizeof(special)/sizeof(special[0]); i++) {
        uint8_t com = special[i][0], seg = special[i][1];
        if (com <= 2 && seg < 24 && !seen[com][seg]) {
            seen[com][seg] = 1; state->seg_com[state->num_segments] = com; state->seg_seg[state->num_segments++] = seg;
        }
    }
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}

static void shuffle_order(entrop1c_state_t *state, uint32_t *rng) {
    for (uint16_t i = 0; i < state->num_segments; i++) state->order[i] = i;
    for (uint16_t i = state->num_segments; i > 1; i--) {
        uint32_t r = xorshift32(rng) % i;
        uint16_t j = (uint16_t)r;
        uint16_t tmp = state->order[i - 1];
        state->order[i - 1] = state->order[j];
        state->order[j] = tmp;
    }
}

static void assign_blink_rates(entrop1c_state_t *state, uint32_t *rng) {
    for (uint16_t i = 0; i < state->num_segments; i++) {
        state->blink_rate_hz[i] = (uint8_t)(1 + (xorshift32(rng) % 4)); // 1..4 Hz
        state->tick_accum[i] = (uint8_t)(xorshift32(rng) & 0x03);        // initial tick phase 0..3
        state->initial_state[i] = (uint8_t)(xorshift32(rng) & 0x01);
        state->current_state[i] = 0;
    }
}

static void compute_chunk_counts(entrop1c_state_t *state) {
    // Turn on 1/6 of the full set every 10 seconds, rounding as needed to sum to num_segments
    uint16_t base = state->num_segments / 6;
    uint16_t rem = state->num_segments % 6;
    uint16_t sum = 0;
    for (uint8_t k = 0; k < 6; k++) {
        uint16_t c = base + (k < rem ? 1 : 0);
        state->chunk_counts[k] = c;
        sum += c;
        state->cumulative_counts[k] = sum;
    }
}

static void turn_off_all(entrop1c_state_t *state) {
    for (uint16_t i = 0; i < state->num_segments; i++) {
        clr_pix(state->seg_com[i], state->seg_seg[i]);
        state->current_state[i] = 0;
    }
}

void entrop1c_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings; (void) watch_face_index;
    if (*context_ptr == NULL) {
        entrop1c_state_t *state = (entrop1c_state_t *)malloc(sizeof(entrop1c_state_t));
        memset(state, 0, sizeof(*state));
        *context_ptr = state;
    }
}

void entrop1c_face_activate(movement_settings_t *settings, void *context) {
    (void) settings;
    entrop1c_state_t *state = (entrop1c_state_t *)context;

    if (watch_tick_animation_is_running()) watch_stop_tick_animation();
    movement_request_tick_frequency(4); // 4 Hz updates to support 1..4 Hz blinking

    watch_clear_display();

    if (!state->segments_initialized) {
        build_unique_segments(state);
        state->segments_initialized = true;
    }

    // Seed RNG from RTC time
    watch_date_time now = watch_rtc_get_date_time();
    uint32_t seed = (now.reg ^ 0xA5A5A5A5u) + (now.unit.second * 1664525u + 1013904223u);
    shuffle_order(state, &seed);
    assign_blink_rates(state, &seed);
    compute_chunk_counts(state);

    state->last_hour = now.unit.hour;
    turn_off_all(state);
}

static uint16_t segments_should_be_active(entrop1c_state_t *state, uint8_t minute) {
    // 0-9 min => chunk0, 10-19 => chunk1, ... 50-59 => chunk5
    uint8_t chunk = minute / 10; if (chunk > 5) chunk = 5;
    return state->cumulative_counts[chunk];
}

static void apply_activation_and_blink(entrop1c_state_t *state, uint8_t subsecond, uint16_t active_target) {
    // First ensure only the first N in shuffled order are considered active
    for (uint16_t idx = 0; idx < state->num_segments; idx++) {
        uint16_t seg_index = state->order[idx];
        bool is_active = (idx < active_target);

        if (!is_active) {
            if (state->current_state[seg_index]) {
                clr_pix(state->seg_com[seg_index], state->seg_seg[seg_index]);
                state->current_state[seg_index] = 0;
            }
            continue;
        }

        // Blink per 4 Hz base tick: toggle state depending on configured rate
        // Use accumulator to approximate 1..4 toggles/sec on a 4 Hz update clock
        // For rate r toggles/sec, toggle on every (4/r) ticks with phase offset.
        uint8_t rate = state->blink_rate_hz[seg_index]; // 1..4
        uint8_t period_ticks = (uint8_t)(4 / rate);     // 4,2,1,1 for 1..4 Hz

        // Spread randomness with initial phase: add subsecond to accumulator modulo period
        uint8_t phase = state->tick_accum[seg_index];
        bool on;
        if (rate >= 4) {
            // 4 Hz: alternate every tick, use phase
            on = ((subsecond + phase) & 1) == 0;
        } else if (rate == 3) {
            // approximate 3 Hz: on 3 ticks, off 1 tick (75% duty), rotate phase
            on = ((subsecond + phase) % 4) != 3;
        } else if (rate == 2) {
            // 2 Hz: on for 1 tick, off for 1 tick
            on = (((subsecond + phase) & 1) == 0);
        } else {
            // 1 Hz: on 1 tick, off 3 ticks (25% duty) with phase
            on = ((subsecond + phase) % 4) == 0;
        }
        // Randomize initial state impact
        if (state->initial_state[seg_index]) on = !on;

        if (on) {
            if (!state->current_state[seg_index]) {
                set_pix(state->seg_com[seg_index], state->seg_seg[seg_index]);
                state->current_state[seg_index] = 1;
            }
        } else {
            if (state->current_state[seg_index]) {
                clr_pix(state->seg_com[seg_index], state->seg_seg[seg_index]);
                state->current_state[seg_index] = 0;
            }
        }
    }
}

bool entrop1c_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    (void) settings;
    entrop1c_state_t *state = (entrop1c_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE: {
            watch_date_time now = watch_rtc_get_date_time();

            // On hour rollover, clear and re-randomize
            if (now.unit.hour != state->last_hour) {
                watch_clear_display();
                uint32_t seed = (now.reg ^ 0xC3C3C3C3u) + (now.unit.second * 1103515245u + 12345u);
                shuffle_order(state, &seed);
                assign_blink_rates(state, &seed);
                compute_chunk_counts(state);
                memset(state->current_state, 0, state->num_segments);
                state->last_hour = now.unit.hour;
            }

            uint16_t active_target = segments_should_be_active(state, now.unit.minute);
            apply_activation_and_blink(state, event.subsecond & 0x03, active_target);

            break;
        }
        default:
            return movement_default_loop_handler(event, settings);
    }
    return true;
}

void entrop1c_face_resign(movement_settings_t *settings, void *context) {
    (void) settings;
    entrop1c_state_t *state = (entrop1c_state_t *)context;
    (void) state;
    movement_request_tick_frequency(1);
}


