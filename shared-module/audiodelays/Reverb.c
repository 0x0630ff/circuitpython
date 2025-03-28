// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Mark Komus
//
// SPDX-License-Identifier: MIT
#include "shared-bindings/audiodelays/Reverb.h"

#include <stdint.h>
#include "py/runtime.h"
#include <math.h>

void common_hal_audiodelays_reverb_construct(audiodelays_reverb_obj_t *self, mp_obj_t roomsize, mp_obj_t damp, mp_obj_t mix,
    uint32_t buffer_size, uint8_t bits_per_sample,
    bool samples_signed, uint8_t channel_count, uint32_t sample_rate) {

    // Basic settings every effect and audio sample has
    // These are the effects values, not the source sample(s)
    self->base.bits_per_sample = bits_per_sample; // Most common is 16, but 8 is also supported in many places
    self->base.samples_signed = samples_signed; // Are the samples we provide signed (common is true)
    self->base.channel_count = channel_count; // Channels can be 1 for mono or 2 for stereo
    self->base.sample_rate = sample_rate; // Sample rate for the effect, this generally needs to match all audio objects
    self->base.single_buffer = false;
    self->base.max_buffer_length = buffer_size;

    // To smooth things out as CircuitPython is doing other tasks most audio objects have a buffer
    // A double buffer is set up here so the audio output can use DMA on buffer 1 while we
    // write to and create buffer 2.
    // This buffer is what is passed to the audio component that plays the effect.
    // Samples are set sequentially. For stereo audio they are passed L/R/L/R/...
    self->buffer_len = buffer_size; // in bytes

    self->buffer[0] = m_malloc(self->buffer_len);
    if (self->buffer[0] == NULL) {
        common_hal_audiodelays_reverb_deinit(self);
        m_malloc_fail(self->buffer_len);
    }
    memset(self->buffer[0], 0, self->buffer_len);

    self->buffer[1] = m_malloc(self->buffer_len);
    if (self->buffer[1] == NULL) {
        common_hal_audiodelays_reverb_deinit(self);
        m_malloc_fail(self->buffer_len);
    }
    memset(self->buffer[1], 0, self->buffer_len);

    self->last_buf_idx = 1; // Which buffer to use first, toggle between 0 and 1

    // Initialize other values most effects will need.
    self->sample = NULL; // The current playing sample
    self->sample_remaining_buffer = NULL; // Pointer to the start of the sample buffer we have not played
    self->sample_buffer_length = 0; // How many samples do we have left to play (these may be 16 bit!)
    self->loop = false; // When the sample is done do we loop to the start again or stop (e.g. in a wav file)
    self->more_data = false; // Is there still more data to read from the sample or did we finish

    // The below section sets up the reverb effect's starting values. For a different effect this section will change
    if (roomsize == MP_OBJ_NULL) {
        roomsize = mp_obj_new_float(MICROPY_FLOAT_CONST(0.5));
    }
    synthio_block_assign_slot(roomsize, &self->roomsize, MP_QSTR_roomsize);
    common_hal_audiodelays_reverb_set_roomsize(self, roomsize);

    if (damp == MP_OBJ_NULL) {
        damp = mp_obj_new_float(MICROPY_FLOAT_CONST(0.5));
    }
    synthio_block_assign_slot(damp, &self->damp, MP_QSTR_damp);
    common_hal_audiodelays_reverb_set_damp(self, damp);

    if (mix == MP_OBJ_NULL) {
        mix = mp_obj_new_float(MICROPY_FLOAT_CONST(0.5));
    }
    synthio_block_assign_slot(mix, &self->mix, MP_QSTR_mix);
    common_hal_audiodelays_reverb_set_mix(self, mix);

    // Set up the comb filters * 2 for L/R (for now)
    self->combbuffersizes[0] = 1116 * 2;
    self->combbuffersizes[1] = 1188 * 2;
    self->combbuffersizes[2] = 1277 * 2;
    self->combbuffersizes[3] = 1356 * 2;
    self->combbuffersizes[4] = 1422 * 2;
    self->combbuffersizes[5] = 1491 * 2;
    self->combbuffersizes[6] = 1557 * 2;
    self->combbuffersizes[7] = 1617 * 2;
    for (uint32_t i = 0; i < 8; i++) {
        self->combbuffers[i] = m_malloc(self->combbuffersizes[i] * sizeof(uint16_t));
        if (self->combbuffers[i] == NULL) {
            common_hal_audiodelays_reverb_deinit(self);
            m_malloc_fail(self->combbuffersizes[i]);
        }
        memset(self->combbuffers[i], 0, self->combbuffersizes[i]);

        self->combbufferindex[i] = 0;
        self->combfitlers[i] = 0;
    }

    // Set up the allpass filters
    self->allpassbuffersizes[0] = 556 * 2;
    self->allpassbuffersizes[1] = 441 * 2;
    self->allpassbuffersizes[2] = 341 * 2;
    self->allpassbuffersizes[3] = 225 * 2;
    for (uint32_t i = 0; i < 4; i++) {
        self->allpassbuffers[i] = m_malloc(self->allpassbuffersizes[i] * sizeof(uint16_t));
        if (self->allpassbuffers[i] == NULL) {
            common_hal_audiodelays_reverb_deinit(self);
            m_malloc_fail(self->allpassbuffersizes[i]);
        }
        memset(self->allpassbuffers[i], 0, self->allpassbuffersizes[i]);

        self->allpassbufferindex[i] = 0;
    }
}

bool common_hal_audiodelays_reverb_deinited(audiodelays_reverb_obj_t *self) {
    if (self->buffer[0] == NULL) {
        return true;
    }
    return false;
}

void common_hal_audiodelays_reverb_deinit(audiodelays_reverb_obj_t *self) {
    if (common_hal_audiodelays_reverb_deinited(self)) {
        return;
    }
    self->buffer[0] = NULL;
    self->buffer[1] = NULL;
}

mp_obj_t common_hal_audiodelays_reverb_get_roomsize(audiodelays_reverb_obj_t *self) {
    return self->roomsize.obj;
}

void common_hal_audiodelays_reverb_set_roomsize(audiodelays_reverb_obj_t *self, mp_obj_t roomsize_obj) {
    synthio_block_assign_slot(roomsize_obj, &self->roomsize, MP_QSTR_roomsize);
}

int16_t audiodelays_reverb_get_roomsize_fixedpoint(mp_float_t n) {
    if (n > 1.0f) {
        n = 1.0f;
    } else if (n < 0.0f) {
        n = 0.0f;
    }

    return (int16_t)(n * 9175.04f) + 22937;     // 9175.04 = 0.28f in fixed point 22937 = 0.7f
}

mp_obj_t common_hal_audiodelays_reverb_get_damp(audiodelays_reverb_obj_t *self) {
    return self->damp.obj;
}

void common_hal_audiodelays_reverb_set_damp(audiodelays_reverb_obj_t *self, mp_obj_t damp) {
    synthio_block_assign_slot(damp, &self->damp, MP_QSTR_damp);
}

void audiodelays_reverb_get_damp_fixedpoint(mp_float_t n, int16_t *damp1, int16_t *damp2) {
    if (n > 1.0f) {
        n = 1.0f;
    } else if (n < 0.0f) {
        n = 0.0f;
    }

    *damp1 = (int16_t)(n * 13107.2f); // 13107.2 = 0.4f scaling factor
    *damp2 = (int16_t)(32768 - *damp1); // inverse of x1 damp2 = 1.0 - damp1
}

mp_obj_t common_hal_audiodelays_reverb_get_mix(audiodelays_reverb_obj_t *self) {
    return self->mix.obj;
}

void common_hal_audiodelays_reverb_set_mix(audiodelays_reverb_obj_t *self, mp_obj_t mix) {
    synthio_block_assign_slot(mix, &self->mix, MP_QSTR_mix);
}

void audiodelays_reverb_reset_buffer(audiodelays_reverb_obj_t *self,
    bool single_channel_output,
    uint8_t channel) {

    memset(self->buffer[0], 0, self->buffer_len);
    memset(self->buffer[1], 0, self->buffer_len);
}

bool common_hal_audiodelays_reverb_get_playing(audiodelays_reverb_obj_t *self) {
    return self->sample != NULL;
}

void common_hal_audiodelays_reverb_play(audiodelays_reverb_obj_t *self, mp_obj_t sample, bool loop) {
    audiosample_must_match(&self->base, sample);

    self->sample = sample;
    self->loop = loop;

    audiosample_reset_buffer(self->sample, false, 0);
    audioio_get_buffer_result_t result = audiosample_get_buffer(self->sample, false, 0, (uint8_t **)&self->sample_remaining_buffer, &self->sample_buffer_length);

    // Track remaining sample length in terms of bytes per sample
    self->sample_buffer_length /= (self->base.bits_per_sample / 8);
    // Store if we have more data in the sample to retrieve
    self->more_data = result == GET_BUFFER_MORE_DATA;

    return;
}

void common_hal_audiodelays_reverb_stop(audiodelays_reverb_obj_t *self) {
    // When the sample is set to stop playing do any cleanup here
    // For reverb we clear the sample but the reverb continues until the object reading our effect stops
    self->sample = NULL;
    return;
}

// cleaner sat16 by http://www.moseleyinstruments.com/
static int16_t sat16(int32_t n, int rshift) {
    // we should always round towards 0
    // to avoid recirculating round-off noise
    //
    // a 2s complement positive number is always
    // rounded down, so we only need to take
    // care of negative numbers
    if (n < 0) {
        n = n + (~(0xFFFFFFFFUL << rshift));
    }
    n = n >> rshift;
    if (n > 32767) {
        return 32767;
    }
    if (n < -32768) {
        return -32768;
    }
    return n;
}

audioio_get_buffer_result_t audiodelays_reverb_get_buffer(audiodelays_reverb_obj_t *self, bool single_channel_output, uint8_t channel,
    uint8_t **buffer, uint32_t *buffer_length) {

    if (!single_channel_output) {
        channel = 0;
    }

    // Switch our buffers to the other buffer
    self->last_buf_idx = !self->last_buf_idx;

    // 16 bit samples we need a 16 bit pointer
    int16_t *word_buffer = (int16_t *)self->buffer[self->last_buf_idx];
    uint32_t length = self->buffer_len / (self->base.bits_per_sample / 8);

    // Loop over the entire length of our buffer to fill it, this may require several calls to get data from the sample
    while (length != 0) {
        // Check if there is no more sample to play, we will either load more data, reset the sample if loop is on or clear the sample
        if (self->sample_buffer_length == 0) {
            if (!self->more_data) { // The sample has indicated it has no more data to play
                if (self->loop && self->sample) { // If we are supposed to loop reset the sample to the start
                    audiosample_reset_buffer(self->sample, false, 0);
                } else { // If we were not supposed to loop the sample, stop playing it but we still need to play the reverb
                    self->sample = NULL;
                }
            }
            if (self->sample) {
                // Load another sample buffer to play
                audioio_get_buffer_result_t result = audiosample_get_buffer(self->sample, false, 0, (uint8_t **)&self->sample_remaining_buffer, &self->sample_buffer_length);
                // Track length in terms of words.
                self->sample_buffer_length /= (self->base.bits_per_sample / 8);
                self->more_data = result == GET_BUFFER_MORE_DATA;
            }
        }

        // Determine how many bytes we can process to our buffer, the less of the sample we have left and our buffer remaining
        uint32_t n;
        if (self->sample == NULL) {
            n = MIN(length, SYNTHIO_MAX_DUR * self->base.channel_count);
        } else {
            n = MIN(MIN(self->sample_buffer_length, length), SYNTHIO_MAX_DUR * self->base.channel_count);
        }

        // get the effect values we need from the BlockInput. These may change at run time so you need to do bounds checking if required
        shared_bindings_synthio_lfo_tick(self->base.sample_rate, n / self->base.channel_count);
        mp_float_t damp = synthio_block_slot_get_limited(&self->damp, MICROPY_FLOAT_CONST(0.0), MICROPY_FLOAT_CONST(1.0));
        int16_t damp1, damp2;
        audiodelays_reverb_get_damp_fixedpoint(damp, &damp1, &damp2);

        mp_float_t roomsize = synthio_block_slot_get_limited(&self->roomsize, MICROPY_FLOAT_CONST(0.0), MICROPY_FLOAT_CONST(1.0));
        int16_t feedback = audiodelays_reverb_get_roomsize_fixedpoint(roomsize);

        // If we have no sample keep the reverb reverbing
        if (self->sample == NULL) {
            // Since we have no sample we can just iterate over the our entire remaining buffer and finish
            for (uint32_t i = 0; i < n; i++) {
                int16_t word = 0;
                word_buffer[i] = word;
            }
        } else {
            // we have a sample to play
            int16_t *sample_src = (int16_t *)self->sample_remaining_buffer;

            for (uint32_t i = 0; i < n; i++) {
                int32_t sample_word = sample_src[i];
                int32_t word;
                int16_t input, bufout, output;
                int32_t sum;

                input = sat16(sample_word * 8738, 17);
                sum = 0;

                for (uint32_t j = 0; j < 8; j++) {
                    bufout = self->combbuffers[j][self->combbufferindex[j]];
                    sum += bufout;
                    self->combfitlers[j] = sat16(bufout * damp2 + self->combfitlers[j] * damp1, 15);
                    self->combbuffers[j][self->combbufferindex[j]] = sat16(input + sat16(self->combfitlers[j] * feedback, 15), 0);
                    if (++self->combbufferindex[j] >= self->combbuffersizes[j]) {
                        self->combbufferindex[j] = 0;
                    }
                }

                output = sat16(sum * 31457, 17); // 31457 = 0.96f

                for (uint32_t j = 0; j < 4; j++) {
                    bufout = self->allpassbuffers[j][self->allpassbufferindex[j]];
                    self->allpassbuffers[j][self->allpassbufferindex[j]] = output + (bufout >> 1); // bufout >> 1 same as bufout*0.5f
                    output = sat16(bufout - output, 1);
                    if (++self->allpassbufferindex[j] >= self->allpassbuffersizes[j]) {
                        self->allpassbufferindex[j] = 0;
                    }
                }

                word = output * 30;

                word = synthio_mix_down_sample(word, SYNTHIO_MIX_DOWN_SCALE(2));
                word_buffer[i] = (int16_t)word;
            }

            // Update the remaining length and the buffer positions based on how much we wrote into our buffer
            length -= n;
            word_buffer += n;
            self->sample_remaining_buffer += (n * (self->base.bits_per_sample / 8));
            self->sample_buffer_length -= n;
        }
    }

    // Finally pass our buffer and length to the calling audio function
    *buffer = (uint8_t *)self->buffer[self->last_buf_idx];
    *buffer_length = self->buffer_len;

    // Reverb always returns more data but some effects may return GET_BUFFER_DONE or GET_BUFFER_ERROR (see audiocore/__init__.h)
    return GET_BUFFER_MORE_DATA;
}
