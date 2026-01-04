/*
 * AV1 common parsing code
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

#ifndef AVCODEC_AV1_PARSE_H
#define AVCODEC_AV1_PARSE_H

#include <limits.h>
#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/error.h"
#include "libavutil/intmath.h"
#include "libavutil/macros.h"

#include "av1.h"
#include "get_bits.h"
#include "leb.h"
#include "libavutil/intreadwrite.h"

// OBU header fields + max leb128 length
#define MAX_OBU_HEADER_SIZE (2 + 8)

typedef struct AV1OBU {
    /** Size of escaped payload */
    int size;
    const uint8_t *data;

    /**
     * Size, in bits, of just the data, excluding the trailing_one_bit and
     * any trailing padding.
     */
    int size_bits;

    /** Size of entire OBU, escaped, including header */
    int escaped_size;
    const uint8_t *escaped_data;

    /** Size of entire OBU, unescaped, including header */
    int raw_size;
    const uint8_t *raw_data;

    int type;

    int temporal_id;
    int spatial_id;
} AV1OBU;

typedef struct AV1Buffer {
    AVBufferRef *ref;
    int buf_size;
} AV1Buffer;

/** An input packet split into OBUs */
typedef struct AV1Packet {
    AV1OBU *obus;
    AV1Buffer buf;
    int nb_obus;
    int obus_allocated;
    unsigned obus_allocated_size;
} AV1Packet;

/**
 * Extract an OBU from a raw bitstream.
 *
 * @note This function does not copy or store any bitstream data. All
 *       the pointers in the AV1OBU structure will be valid as long
 *       as the input buffer also is.
 */
int ff_av1_extract_obu(AV1OBU *obu, AV1Buffer *buf, const uint8_t *data,
                       int length, int ts, void *logctx);

/**
 * Split an input packet into OBUs.
 * Supports both Section 5 (length delimited) and MPEG-TS start code formats.
 * Automatically detects the format and handles it accordingly.
 *
 * @note This function does not copy or store any bitstream data. All
 *       the pointers in the AV1Packet structure will be valid as
 *       long as the input buffer also is.
 */
int ff_av1_packet_split(AV1Packet *pkt, const uint8_t *buf, int length,
                        void *logctx);

/**
 * Check if AV1 data is in MPEG-TS start code format (0x000001 prefix).
 * MPEG-TS format: 0x000001 followed by valid OBU header (bit 7 = 0, obu_forbidden_bit = 0).
 * Section 5 format: 0x000001 in payload data, byte 3 can be any value.
 */
static inline int av1_is_startcode_format(const uint8_t *buf, int size)
{
    if (size >= 4 && AV_RB24(buf) == 0x000001) {
        /* MPEG-TS format has valid OBU header after start code (bit 7 = 0) */
        return !(buf[3] & 0x80);
    }
    return 0;
}

/**
 * Free all the allocated memory in the packet.
 */
void ff_av1_packet_uninit(AV1Packet *pkt);

static inline int parse_obu_header(const uint8_t *buf, int buf_size,
                                   int64_t *obu_size, int *start_pos, int *type,
                                   int *temporal_id, int *spatial_id)
{
    int i = 0;
    int extension_flag, has_size_flag;
    int64_t size;

    if (buf_size <= 0)
        return AVERROR_INVALIDDATA;

    if (buf[0] & 0x80) // obu_forbidden_bit
        return AVERROR_INVALIDDATA;

    *type      = (buf[0] >> 3) & 0xF;
    extension_flag = (buf[0] >> 2) & 1;
    has_size_flag  = (buf[0] >> 1) & 1;
    // obu_reserved_1bit is ignored

    i = 1;

    if (extension_flag) {
        if (i >= buf_size)
            return AVERROR_INVALIDDATA;
        *temporal_id = (buf[i] >> 5) & 0x7;
        *spatial_id  = (buf[i] >> 3) & 0x3;
        i++;
    } else {
        *temporal_id = *spatial_id = 0;
    }

    if (has_size_flag) {
        uint64_t leb = 0;
        int leb_i;
        for (leb_i = 0; leb_i < 8; leb_i++) {
            uint8_t byte;
            if (i >= buf_size)
                return AVERROR_INVALIDDATA;
            byte = buf[i++];
            leb |= (uint64_t)(byte & 0x7f) << (leb_i * 7);
            if (!(byte & 0x80))
                break;
        }
        if (leb_i == 8) // Too long
            return AVERROR_INVALIDDATA;
        *obu_size = leb;
    } else {
        *obu_size = buf_size - 1 - extension_flag;
    }

    *start_pos = i;

    if (*obu_size > INT_MAX - *start_pos)
        return AVERROR_INVALIDDATA;

    size = *obu_size + *start_pos;

    return (int)size;
}

static inline int get_obu_bit_length(const uint8_t *buf, int size, int type)
{
    int v;

    /* There are no trailing bits on these */
    if (type == AV1_OBU_TILE_GROUP ||
        type == AV1_OBU_TILE_LIST ||
        type == AV1_OBU_FRAME) {
        if (size > INT_MAX / 8)
            return AVERROR(ERANGE);
        else
            return size * 8;
    }

    while (size > 0 && buf[size - 1] == 0)
        size--;

    if (!size)
        return 0;

    v = buf[size - 1];

    if (size > INT_MAX / 8)
        return AVERROR(ERANGE);
    size *= 8;

    /* Remove the trailing_one_bit and following trailing zeros */
    if (v)
        size -= ff_ctz(v) + 1;

    return size;
}

AVRational ff_av1_framerate(int64_t ticks_per_frame, int64_t units_per_tick,
                            int64_t time_scale);

#endif /* AVCODEC_AV1_PARSE_H */
