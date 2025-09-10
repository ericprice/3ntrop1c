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

// Bit-packed structure to save memory
typedef struct {
    // Segment coordinates - pack COM (2 bits) and SEG (5 bits) into single byte
    uint8_t seg_packed[96];  // bits 7-6: COM (0-2), bits 5-0: SEG (0-23)
    uint8_t num_segments;
    
    // Randomized order - use uint8_t since we have max 96 segments
    uint8_t order[96];
    
    // Blink configuration packed into 4 bits per segment (rate: 2 bits, accum: 2 bits)
    // 96 segments * 4 bits = 384 bits = 48 bytes
    uint8_t blink_config[48];  // Each byte holds 2 segments' configs
    
    // Binary states packed as bits: initial (96 bits = 12 bytes), current (96 bits = 12 bytes)
    uint8_t initial_state[12];  // 96 bits packed
    uint8_t current_state[12];  // 96 bits packed
    
    // Hour scheduling - can use uint8_t for counts < 256
    uint8_t chunk_counts[6];
    uint8_t cumulative_counts[6];
    
    uint8_t last_hour;
    bool segments_initialized;
    uint8_t current_freq;  // Track current frequency to avoid redundant switches
} entrop1c_state_t;

// Helper macros for bit operations
#define GET_BIT(array, idx) ((array[(idx)/8] >> ((idx)%8)) & 1)
#define SET_BIT(array, idx) (array[(idx)/8] |= (1 << ((idx)%8)))
#define CLEAR_BIT(array, idx) (array[(idx)/8] &= ~(1 << ((idx)%8)))
#define PACK_SEG(com, seg) (((com) << 6) | (seg))
#define UNPACK_COM(packed) (((packed) >> 6) & 0x03)
#define UNPACK_SEG(packed) ((packed) & 0x3F)

void entrop1c_face_setup(uint8_t watch_face_index, void ** context_ptr);
void entrop1c_face_activate(void *context);
bool entrop1c_face_loop(movement_event_t event, void *context);
void entrop1c_face_resign(void *context);

#define entrop1c_face ((const watch_face_t){ \
    entrop1c_face_setup, \
    entrop1c_face_activate, \
    entrop1c_face_loop, \
    entrop1c_face_resign, \
    NULL, \
})

#endif // N3TROP1C_FACE_H_

