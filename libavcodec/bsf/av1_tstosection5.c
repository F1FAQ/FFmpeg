/*
 * AV1 MPEG-TS to Section 5 (Low Overhead) bitstream filter
 * Copyright (c) 2025 Jun Zhao <barryjzhao@tencent.com>
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

/**
 * @file
 * This bitstream filter converts AV1 from MPEG-TS start code format
 * to Section 5 (Low Overhead) format.
 *
 * If the input is already in Section 5 format, it passes through unchanged.
 *
 * Note: Per AOM AV1-MPEG2-TS spec section 3.6.2.1, emulation prevention bytes
 * should be handled, but for now we rely on the obu_size field for boundary
 * detection which makes emulation prevention optional in practice.
 */

#include "libavutil/mem.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "av1.h"
#include "av1_parse.h"

typedef struct AV1TsToSection5Context {
    AVPacket *buffer_pkt;

    uint8_t *output_buffer;
    size_t output_buffer_size;
    size_t output_buffer_capacity;
} AV1TsToSection5Context;

static int ensure_output_buffer(AV1TsToSection5Context *s, size_t required)
{
    if (s->output_buffer_capacity >= required)
        return 0;

    size_t new_capacity = FFMAX(required, s->output_buffer_capacity * 2);
    new_capacity = FFMAX(new_capacity, 4096);

    uint8_t *new_buffer = av_realloc(s->output_buffer,
                                      new_capacity + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!new_buffer)
        return AVERROR(ENOMEM);

    s->output_buffer = new_buffer;
    s->output_buffer_capacity = new_capacity;
    return 0;
}

static int convert_startcode_to_section5(AV1TsToSection5Context *s,
                                          const uint8_t *src, int src_size,
                                          void *logctx)
{
    AV1Packet pkt = { 0 };
    int ret, i;
    size_t total_size = 0;
    uint8_t *p;

    /* Parse start code format */
    ret = ff_av1_packet_split_startcode(&pkt, src, src_size, logctx);
    if (ret < 0)
        return ret;

    /* Calculate output size (Section 5 format without start codes) */
    for (i = 0; i < pkt.nb_obus; i++) {
        total_size += pkt.obus[i].raw_size;
    }

    /* Ensure output buffer capacity */
    ret = ensure_output_buffer(s, total_size);
    if (ret < 0) {
        ff_av1_packet_uninit(&pkt);
        return ret;
    }

    /* Write Section 5 format (no start codes) */
    p = s->output_buffer;
    for (i = 0; i < pkt.nb_obus; i++) {
        memcpy(p, pkt.obus[i].raw_data, pkt.obus[i].raw_size);
        p += pkt.obus[i].raw_size;
    }

    s->output_buffer_size = total_size;

    /* Fill padding */
    memset(s->output_buffer + total_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ff_av1_packet_uninit(&pkt);
    return 0;
}

static int av1_ts_to_section5_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    AV1TsToSection5Context *s = ctx->priv_data;
    int ret;

    ret = ff_bsf_get_packet_ref(ctx, s->buffer_pkt);
    if (ret < 0)
        return ret;

    /* If already Section 5 format, pass through (no-op) */
    if (!ff_av1_is_startcode_format(s->buffer_pkt->data, s->buffer_pkt->size)) {
        av_packet_move_ref(pkt, s->buffer_pkt);
        return 0;
    }

    /* Convert format */
    ret = convert_startcode_to_section5(s, s->buffer_pkt->data,
                                         s->buffer_pkt->size, ctx);
    if (ret < 0) {
        av_packet_unref(s->buffer_pkt);
        return ret;
    }

    /* Create output packet */
    ret = av_new_packet(pkt, s->output_buffer_size);
    if (ret < 0) {
        av_packet_unref(s->buffer_pkt);
        return ret;
    }

    memcpy(pkt->data, s->output_buffer, s->output_buffer_size);

    /* Copy metadata */
    ret = av_packet_copy_props(pkt, s->buffer_pkt);
    if (ret < 0) {
        av_packet_unref(pkt);
        av_packet_unref(s->buffer_pkt);
        return ret;
    }

    av_packet_unref(s->buffer_pkt);
    return 0;
}

static int av1_ts_to_section5_init(AVBSFContext *ctx)
{
    AV1TsToSection5Context *s = ctx->priv_data;

    s->buffer_pkt = av_packet_alloc();
    if (!s->buffer_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static void av1_ts_to_section5_flush(AVBSFContext *ctx)
{
    AV1TsToSection5Context *s = ctx->priv_data;
    av_packet_unref(s->buffer_pkt);
}

static void av1_ts_to_section5_close(AVBSFContext *ctx)
{
    AV1TsToSection5Context *s = ctx->priv_data;

    av_packet_free(&s->buffer_pkt);
    av_freep(&s->output_buffer);
}

static const enum AVCodecID av1_ts_to_section5_codec_ids[] = {
    AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_av1_tstosection5_bsf = {
    .p.name         = "av1_tstosection5",
    .p.codec_ids    = av1_ts_to_section5_codec_ids,
    .priv_data_size = sizeof(AV1TsToSection5Context),
    .init           = av1_ts_to_section5_init,
    .flush          = av1_ts_to_section5_flush,
    .close          = av1_ts_to_section5_close,
    .filter         = av1_ts_to_section5_filter,
};
