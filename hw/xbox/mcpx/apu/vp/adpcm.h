/*
 * ADPCM decoder from the ADPCM-XQ project: https://github.com/dbry/adpcm-xq
 *
 *                        Copyright (c) David Bryant
 *                           All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Conifer Software nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ADPCM_DECODE_H
#define ADPCM_DECODE_H

/********************************* 4-bit ADPCM decoder ********************************/

/* Decode the block of ADPCM data into PCM. This requires no context because ADPCM blocks
 * are indeppendently decodable. This assumes that a single entire block is always decoded;
 * it must be called multiple times for multiple blocks and cannot resume in the middle of a
 * block.
 *
 * Parameters:
 *  outbuf          destination for interleaved PCM samples
 *  inbuf           source ADPCM block
 *  inbufsize       size of source ADPCM block
 *  channels        number of channels in block (must be determined from other context)
 *
 * Input and output storage must not overlap, including when input is borrowed
 * directly from guest RAM.
 *
 * Returns number of converted composite samples (total samples divided by number of channels)
 */

/* step table */
static const uint16_t adpcm_step_table[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,
    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,
    73,    80,    88,    97,    107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

/* step index tables */
static const int adpcm_index_table[] = {
    /* adpcm data size is 4 */
    -1, -1, -1, -1, 2, 4, 6, 8
};

static inline int adpcm_decode_block (int16_t *restrict outbuf, const uint8_t *restrict inbuf, size_t inbufsize, int channels)
{
    #define CLIP(data, min, max) \
    if ((data) > (max)) data = max; \
    else if ((data) < (min)) data = min;

    int samples = 1, chunks;
    int32_t pcmdata[2];
    int8_t index[2];

    if (inbufsize < (uint32_t) channels * 4)
        return 0;

    for (int ch = 0; ch < channels; ch++) {
        *outbuf++ = pcmdata[ch] = (int16_t) (inbuf [0] | (inbuf [1] << 8));
        index[ch] = inbuf [2];

        if (index [ch] < 0 || index [ch] > 88 || inbuf [3])     // sanitize the input a little...
            return 0;

        inbufsize -= 4;
        inbuf += 4;
    }

    chunks = inbufsize / (channels * 4);
    samples += chunks * 8;

    while (chunks--) {

        for (int ch = 0; ch < channels; ++ch) {

            for (int i = 0; i < 4; ++i) {
                int step = adpcm_step_table [index [ch]], delta = step >> 3;

                if (*inbuf & 1) delta += (step >> 2);
                if (*inbuf & 2) delta += (step >> 1);
                if (*inbuf & 4) delta += step;
                if (*inbuf & 8) delta = -delta;

                pcmdata[ch] += delta;
                index[ch] += adpcm_index_table [*inbuf & 0x7];
                CLIP(index[ch], 0, 88);
                CLIP(pcmdata[ch], -32768, 32767);
                outbuf [i * 2 * channels] = pcmdata[ch];

                step = adpcm_step_table [index [ch]], delta = step >> 3;

                if (*inbuf & 0x10) delta += (step >> 2);
                if (*inbuf & 0x20) delta += (step >> 1);
                if (*inbuf & 0x40) delta += step;
                if (*inbuf & 0x80) delta = -delta;

                pcmdata[ch] += delta;
                index[ch] += adpcm_index_table [(*inbuf >> 4) & 0x7];
                CLIP(index[ch], 0, 88);
                CLIP(pcmdata[ch], -32768, 32767);
                outbuf [(i * 2 + 1) * channels] = pcmdata[ch];

                inbuf++;
            }

            outbuf++;
        }

        outbuf += channels * 7;
    }

    return samples;
}

/* Exact nibble magnitudes: retain each separately truncated shift from the
 * reference decoder. 1,424 read-only bytes replace three conditional shift additions per nibble. */
#define ADPCM_DELTA(s, c) (((s) >> 3) + (((c) & 1) ? (s) >> 2 : 0) + \
                          (((c) & 2) ? (s) >> 1 : 0) + (((c) & 4) ? (s) : 0))
#define ADPCM_DELTA_ROW(s) { ADPCM_DELTA(s, 0), ADPCM_DELTA(s, 1), \
    ADPCM_DELTA(s, 2), ADPCM_DELTA(s, 3), ADPCM_DELTA(s, 4), \
    ADPCM_DELTA(s, 5), ADPCM_DELTA(s, 6), ADPCM_DELTA(s, 7) }
static const uint16_t adpcm_delta_table[89][8] = {
    ADPCM_DELTA_ROW(7), ADPCM_DELTA_ROW(8), ADPCM_DELTA_ROW(9), ADPCM_DELTA_ROW(10),
    ADPCM_DELTA_ROW(11), ADPCM_DELTA_ROW(12), ADPCM_DELTA_ROW(13), ADPCM_DELTA_ROW(14),
    ADPCM_DELTA_ROW(16), ADPCM_DELTA_ROW(17), ADPCM_DELTA_ROW(19), ADPCM_DELTA_ROW(21),
    ADPCM_DELTA_ROW(23), ADPCM_DELTA_ROW(25), ADPCM_DELTA_ROW(28), ADPCM_DELTA_ROW(31),
    ADPCM_DELTA_ROW(34), ADPCM_DELTA_ROW(37), ADPCM_DELTA_ROW(41), ADPCM_DELTA_ROW(45),
    ADPCM_DELTA_ROW(50), ADPCM_DELTA_ROW(55), ADPCM_DELTA_ROW(60), ADPCM_DELTA_ROW(66),
    ADPCM_DELTA_ROW(73), ADPCM_DELTA_ROW(80), ADPCM_DELTA_ROW(88), ADPCM_DELTA_ROW(97),
    ADPCM_DELTA_ROW(107), ADPCM_DELTA_ROW(118), ADPCM_DELTA_ROW(130), ADPCM_DELTA_ROW(143),
    ADPCM_DELTA_ROW(157), ADPCM_DELTA_ROW(173), ADPCM_DELTA_ROW(190), ADPCM_DELTA_ROW(209),
    ADPCM_DELTA_ROW(230), ADPCM_DELTA_ROW(253), ADPCM_DELTA_ROW(279), ADPCM_DELTA_ROW(307),
    ADPCM_DELTA_ROW(337), ADPCM_DELTA_ROW(371), ADPCM_DELTA_ROW(408), ADPCM_DELTA_ROW(449),
    ADPCM_DELTA_ROW(494), ADPCM_DELTA_ROW(544), ADPCM_DELTA_ROW(598), ADPCM_DELTA_ROW(658),
    ADPCM_DELTA_ROW(724), ADPCM_DELTA_ROW(796), ADPCM_DELTA_ROW(876), ADPCM_DELTA_ROW(963),
    ADPCM_DELTA_ROW(1060), ADPCM_DELTA_ROW(1166), ADPCM_DELTA_ROW(1282), ADPCM_DELTA_ROW(1411),
    ADPCM_DELTA_ROW(1552), ADPCM_DELTA_ROW(1707), ADPCM_DELTA_ROW(1878), ADPCM_DELTA_ROW(2066),
    ADPCM_DELTA_ROW(2272), ADPCM_DELTA_ROW(2499), ADPCM_DELTA_ROW(2749), ADPCM_DELTA_ROW(3024),
    ADPCM_DELTA_ROW(3327), ADPCM_DELTA_ROW(3660), ADPCM_DELTA_ROW(4026), ADPCM_DELTA_ROW(4428),
    ADPCM_DELTA_ROW(4871), ADPCM_DELTA_ROW(5358), ADPCM_DELTA_ROW(5894), ADPCM_DELTA_ROW(6484),
    ADPCM_DELTA_ROW(7132), ADPCM_DELTA_ROW(7845), ADPCM_DELTA_ROW(8630), ADPCM_DELTA_ROW(9493),
    ADPCM_DELTA_ROW(10442), ADPCM_DELTA_ROW(11487), ADPCM_DELTA_ROW(12635), ADPCM_DELTA_ROW(13899),
    ADPCM_DELTA_ROW(15289), ADPCM_DELTA_ROW(16818), ADPCM_DELTA_ROW(18500), ADPCM_DELTA_ROW(20350),
    ADPCM_DELTA_ROW(22385), ADPCM_DELTA_ROW(24623), ADPCM_DELTA_ROW(27086), ADPCM_DELTA_ROW(29794),
    ADPCM_DELTA_ROW(32767),
};
#undef ADPCM_DELTA_ROW
#undef ADPCM_DELTA

/* Xbox blocks expose 64 samples per channel (the generic decoder also emits
 * the unused 65th). Decode the prefix needed for predictor state, but write
 * only the requested window directly as stereo 24-bit VP sample steps.
 * No input or output pointer survives the call; their storage must not overlap. */
static unsigned adpcm_decode_window(int32_t (*restrict out)[2],
                                    const uint8_t *restrict in,
                                    size_t bytes, unsigned channels,
                                    unsigned start, unsigned count)
{
    if ((channels != 1 && channels != 2) || bytes != 36 * channels ||
        start >= 64 || count > 64 - start) {
        return 0;
    }
    int predictor[2], index[2];
    for (unsigned ch = 0; ch < channels; ch++) {
        predictor[ch] = (int16_t)(in[ch * 4] | (in[ch * 4 + 1] << 8));
        index[ch] = in[ch * 4 + 2];
        if (index[ch] > 88 || in[ch * 4 + 3]) {
            return 0;
        }
    }
    if (!count) {
        return 0;
    }
    for (unsigned ch = 0; ch < channels; ch++) {
        int pcm = predictor[ch], step_index = index[ch];
        unsigned sample = 0;
        if (!start) {
            out[0][ch] = pcm * 256;
            if (channels == 1) out[0][1] = out[0][0];
        }
        const uint8_t *data = in + channels * 4 + ch * 4;
        while (sample + 1 < start + count) {
            for (unsigned byte = 0; byte < 4; byte++) {
                unsigned value = data[byte];
                for (unsigned half = 0; half < 2; half++) {
                    if (sample + 1 >= start + count) break;
                    unsigned code = value & 15;
                    int delta = adpcm_delta_table[step_index][code & 7];
                    pcm += code & 8 ? -delta : delta;
                    step_index += adpcm_index_table[code & 7];
                    if (step_index > 88) step_index = 88;
                    else if (step_index < 0) step_index = 0;
                    if (pcm > 32767) pcm = 32767;
                    else if (pcm < -32768) pcm = -32768;
                    sample++;
                    if (sample >= start) {
                        out[sample - start][ch] = pcm * 256;
                        if (channels == 1) out[sample - start][1] = out[sample - start][0];
                    }
                    value >>= 4;
                }
                if (sample + 1 >= start + count) break;
            }
            if (sample + 1 >= start + count) break;
            data += channels * 4;
        }
    }
    return count;
}

#endif
