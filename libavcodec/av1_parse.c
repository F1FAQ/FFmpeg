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

#include "config.h"

#include "libavutil/mem.h"

#include "av1.h"
#include "av1_parse.h"
#include "bytestream.h"
#include "startcode.h"

int ff_av1_extract_obu(AV1OBU *obu, AV1Buffer *buf, const uint8_t *src,
                       int length, int ts, void *logctx)
{
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;
    int i = ts ? 0 : length, si, di;
    int len;
    uint8_t *dst;

#define STARTCODE_TEST                                              \
    if (i < length - 2 && src[i + 1] == 0 &&                        \
       (src[i + 2] == 3 || src[i + 2] == 1)) {                      \
        if (src[i + 2] == 1) {                                      \
            /* startcode, so we must be past the end */             \
            length = i;                                             \
        }                                                           \
        break;                                                      \
    }

    for (; i < length; i++) {
        i += ff_startcode_find_candidate_c(src + i, length - i);
        STARTCODE_TEST
    }

    if (i >= length - 1) { // no escaped 0
        si = parse_obu_header(src, length, &obu_size, &start_pos,
                               &type, &temporal_id, &spatial_id);

        if (si < 0)
            return si;

        /* Clamp si to actual buffer length to prevent overflow */
        if (si > length)
            si = length;

        obu->type         = type;
        obu->temporal_id  = temporal_id;
        obu->spatial_id   = spatial_id;

        obu->data         = src + start_pos;
        obu->size         = obu_size;

        if (obu->size > length - start_pos)
            obu->size = length - start_pos;

        obu->escaped_data =
        obu->raw_data     = src;
        obu->escaped_size =
        obu->raw_size     = si;

        goto end;
    } else if (i > length)

        i = length;

    av_assert0(buf);

    if (buf->buf_size + length > buf->ref->size)
        return AVERROR(ENOMEM);

    dst = &buf->ref->data[buf->buf_size];

    memcpy(dst, src, i);
    si = di = i;
    while (si + 2 < length) {
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0 && src[si + 2] != 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si       += 3;
                continue;
            } else // next start code
                goto nsc;
        }

        dst[di++] = src[si++];
    }
    while (si < length)
        dst[di++] = src[si++];

nsc:
    memset(dst + di, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    len = parse_obu_header(dst, di, &obu_size, &start_pos,
                           &type, &temporal_id, &spatial_id);
    if (len < 0)
        return len;

    obu->type        = type;
    obu->temporal_id = temporal_id;
    obu->spatial_id  = spatial_id;

    obu->data     = dst + start_pos;
    obu->size     = obu_size;
    if (obu->size > di - start_pos)
        obu->size = di - start_pos;

    obu->escaped_data = dst;
    obu->escaped_size = di;
    obu->raw_data = src;
    obu->raw_size = si;
    buf->buf_size += di;

end:
    av_log(logctx, AV_LOG_DEBUG,
           "obu_type: %d, temporal_id: %d, spatial_id: %d, payload size: %d\n",
           obu->type, obu->temporal_id, obu->spatial_id, obu->size);

    return si;
}

static void alloc_buffer(AV1Buffer *buf, unsigned int size)
{
    int min_size = size;

    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        goto fail;
    size += AV_INPUT_BUFFER_PADDING_SIZE;

    if (buf->ref && buf->ref->size >= size && av_buffer_is_writable(buf->ref)) {
        memset(buf->ref->data + min_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        return;
    }

    size = FFMIN(size + size / 16 + 32, INT_MAX);

    av_buffer_unref(&buf->ref);
    buf->ref = av_buffer_allocz(size);

    return;
fail:
    av_buffer_unref(&buf->ref);
}

int ff_av1_packet_split(AV1Packet *pkt, const uint8_t *buf, int length, void *logctx)
{
    GetByteContext bc;
    int consumed, ts;
    int is_startcode_format = 0;

    // Detect format at the beginning: check for start code format (0x000001)
    if (av1_is_startcode_format(buf, length)) {
        is_startcode_format = 1;
    }

    bytestream2_init(&bc, buf, length);
    alloc_buffer(&pkt->buf, length);

    if (!pkt->buf.ref)
        return AVERROR(ENOMEM);

    pkt->buf.buf_size = 0;
    pkt->nb_obus = 0;

    while (bytestream2_get_bytes_left(&bc) > 0) {
        AV1OBU *obu;

        if (pkt->obus_allocated < pkt->nb_obus + 1) {
            int new_size = pkt->obus_allocated + 1;
            AV1OBU *tmp;

            if (new_size >= INT_MAX / sizeof(*tmp))
                return AVERROR(ENOMEM);
            tmp = av_fast_realloc(pkt->obus, &pkt->obus_allocated_size, new_size * sizeof(*tmp));
            if (!tmp)
                return AVERROR(ENOMEM);

            pkt->obus = tmp;
            memset(pkt->obus + pkt->obus_allocated, 0, sizeof(*pkt->obus));
            pkt->obus_allocated = new_size;
        }
        obu = &pkt->obus[pkt->nb_obus];

        // Skip start code if in start code format
        if (is_startcode_format) {
            if (av1_is_startcode_format(bc.buffer, bytestream2_get_bytes_left(&bc))) {
                bytestream2_skip(&bc, 3);
            }
            ts = 1;
        } else {
            ts = 0;
        }

        consumed = ff_av1_extract_obu(obu, &pkt->buf, bc.buffer, bytestream2_get_bytes_left(&bc), ts, logctx);
        if (consumed < 0)
            return consumed;

        bytestream2_skip(&bc, consumed);

        obu->size_bits = get_obu_bit_length(obu->data, obu->size, obu->type);

        if (obu->size_bits < 0 ||
            (obu->size_bits == 0 && (obu->type != AV1_OBU_TEMPORAL_DELIMITER &&
                                     obu->type != AV1_OBU_PADDING))) {
            av_log(logctx, AV_LOG_ERROR, "Invalid OBU of type %d, skipping.\n", obu->type);
            continue;
        }

        pkt->nb_obus++;
    }

    return 0;
}

void ff_av1_packet_uninit(AV1Packet *pkt)
{
    av_freep(&pkt->obus);
    pkt->obus_allocated = pkt->obus_allocated_size = 0;
    av_buffer_unref(&pkt->buf.ref);
    pkt->buf.buf_size = 0;
}

AVRational ff_av1_framerate(int64_t ticks_per_frame, int64_t units_per_tick,
                            int64_t time_scale)
{
    AVRational fr;

    if (ticks_per_frame && units_per_tick && time_scale &&
        ticks_per_frame < INT64_MAX / units_per_tick    &&
        av_reduce(&fr.den, &fr.num, units_per_tick * ticks_per_frame,
                  time_scale, INT_MAX))
        return fr;

    return (AVRational){ 0, 1 };
}
