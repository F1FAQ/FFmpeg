/*
 * AV1 helper functions for muxers
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFORMAT_AV1_H
#define AVFORMAT_AV1_H

#include <stdint.h>

#include "avio.h"

typedef struct AV1SequenceParameters {
    uint8_t profile;
    uint8_t level;
    uint8_t tier;
    uint8_t bitdepth;
    uint8_t monochrome;
    uint8_t chroma_subsampling_x;
    uint8_t chroma_subsampling_y;
    uint8_t chroma_sample_position;
    uint8_t color_description_present_flag;
    uint8_t color_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t color_range;
    uint8_t initial_display_delay_present;
    uint8_t initial_display_delay_minus_1;
} AV1SequenceParameters;

/**
 * Filter out AV1 OBUs not meant to be present in ISOBMFF sample data and write
 * the resulting bitstream to the provided AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the filtered bitstream shall be
 *           written; may be NULL, in which case nothing is written.
 * @param buf input data buffer
 * @param size size of the input data buffer
 *
 * @return the amount of bytes written (or would have been written in case
 *         pb had been supplied) in case of success, a negative AVERROR
 *         code in case of failure
 * @note   One can use NULL for pb to just get the output size.
 */
int ff_av1_filter_obus(AVIOContext *pb, const uint8_t *buf, int size);

/**
 * Filter out AV1 OBUs not meant to be present in ISOBMFF sample data and return
 * the result in a data buffer, avoiding allocations and copies if possible.
 *
 * @param in input data buffer
 * @param out pointer to pointer for the returned buffer. In case of success,
 *            it is independently allocated if and only if `*out` differs from in.
 * @param size size of the input data buffer. The size of the resulting output
 *             data buffer will be written here
 * @param offset offset of the returned data inside `*out`: It runs from
 *               `*out + offset` (inclusive) to `*out + offset + size`
 *               (exclusive); is zero if `*out` is independently allocated.
 *
 * @return 0 in case of success, a negative AVERROR code in case of failure.
 *         On failure, *out and *size are unchanged
 * @note *out will be treated as uninitialized on input and will not be freed.
 */
int ff_av1_filter_obus_buf(const uint8_t *in, uint8_t **out,
                           int *size, int *offset);

/**
 * Parses a Sequence Header from the the provided buffer.
 *
 * @param seq pointer to the AV1SequenceParameters where the parsed values will
 *            be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of failure
 */
int ff_av1_parse_seq_header(AV1SequenceParameters *seq, const uint8_t *buf, int size);

/**
 * Writes AV1 extradata (Sequence Header and Metadata OBUs) to the provided
 * AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the av1C box shall be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 * @param write_seq_header If 1, Sequence Header OBU will be written inside the
 *           av1C box. Otherwise, Sequence Header OBU will be omitted.
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of failure
 */
int ff_isom_write_av1c(AVIOContext *pb, const uint8_t *buf, int size, int write_seq_header);

/**
 * Check if the packet starts with a Temporal Delimiter OBU.
 *
 * @param buf  Input buffer (Section 5 or start code format)
 * @param size Buffer size
 * @return 1 if starts with TD OBU, 0 otherwise
 */
int ff_av1_is_temporal_unit_start(const uint8_t *buf, int size);

/**
 * Check if the AV1 bitstream is in MPEG-TS start code format.
 *
 * Detects the MPEG-TS start code format where each OBU is prefixed with
 * a 3-byte start code (0x000001) and followed by an OBU header.
 * The obu_forbidden_bit (bit 7 of the byte after start code) must be 0
 * to distinguish real start codes from payload data that happens to contain 0x000001.
 *
 * @param buf  Input buffer
 * @param size Buffer size
 * @return 1 if in start code format, 0 otherwise
 */
int ff_av1_is_startcode_format(const uint8_t *buf, int size);

/**
 * Convert AV1 Section 5 (low overhead) format to MPEG-TS start code format.
 *
 * @param buf      Input buffer (Section 5 format)
 * @param size     Input buffer size
 * @param out_buf  Output buffer (caller must av_free)
 * @param out_size Output buffer size
 * @param add_td   If non-zero and no TD OBU present, add one at the start
 * @return 0 on success, negative AVERROR on failure
 */
int ff_av1_section5_to_startcode(const uint8_t *buf, int size,
                                  uint8_t **out_buf, int *out_size,
                                  int add_td);

#endif /* AVFORMAT_AV1_H */
