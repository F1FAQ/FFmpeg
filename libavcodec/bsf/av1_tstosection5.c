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
 * to Section 5 (Low Overhead) format using the Coded Bitstream API.
 *
 * If the input is already in Section 5 format, it passes through unchanged.
 *
 * CBS parses OBUs to populate unit->content structures, which are required
 * by ff_cbs_write_packet() to correctly regenerate the bitstream with
 * Section 5 format (obu_has_size_field = 1).
 */

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_av1.h"
#include "av1_parse.h"

typedef struct AV1TsToSection5Context {
    CBSBSFContext common;
    AVPacket *buffer_pkt;
} AV1TsToSection5Context;

static int av1_tstosection5_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                            CodedBitstreamFragment *frag)
{
    /* Pass-through: no modification of the fragment content is needed.
     * The CBS writer will naturally write it in Section 5 format.
     */
    return 0;
}

static const CBSBSFType av1_tstosection5_type = {
    .codec_id        = AV_CODEC_ID_AV1,
    .fragment_name   = "temporal unit",
    .unit_name       = "OBU",
    .update_fragment = &av1_tstosection5_update_fragment,
};

static int av1_tstosection5_init(AVBSFContext *bsf)
{
    AV1TsToSection5Context *ctx = bsf->priv_data;

    ctx->buffer_pkt = av_packet_alloc();
    if (!ctx->buffer_pkt)
        return AVERROR(ENOMEM);

    return ff_cbs_bsf_generic_init(bsf, &av1_tstosection5_type);
}

static int av1_tstosection5_filter(AVBSFContext *bsf, AVPacket *pkt)
{
    AV1TsToSection5Context *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->common.fragment;
    int ret;

    /* Peek at the packet using the buffer */
    ret = ff_bsf_get_packet_ref(bsf, ctx->buffer_pkt);
    if (ret < 0)
        return ret;

    /* Check if it's already in Section 5 format (length delimited).
     * If so, pass it through without CBS overhead.
     */
    if (!av1_is_startcode_format(ctx->buffer_pkt->data, ctx->buffer_pkt->size)) {
        av_packet_move_ref(pkt, ctx->buffer_pkt);
        return 0;
    }

    /* It is startcode format. We need to convert it.
     * We can't use ff_cbs_bsf_generic_filter() directly because it calls ff_bsf_get_packet_ref
     * but we already have the packet in ctx->buffer_pkt.
     * So we manually invoke the CBS read/write cycle on buffer_pkt.
     */

    /* 1. Read the packet (CBS will parse it) */
    ret = ff_cbs_read_packet(ctx->common.input, frag, ctx->buffer_pkt);
    if (ret < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to read %s from packet.\n",
               ctx->common.type->fragment_name);
        goto fail;
    }

    if (frag->nb_units == 0) {
        av_log(bsf, AV_LOG_ERROR, "No %s found in packet.\n",
               ctx->common.type->unit_name);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    /* 2. Update fragment (no-op here, but good practice to call hook if we added it) */
    ret = ctx->common.type->update_fragment(bsf, ctx->buffer_pkt, frag);
    if (ret < 0)
        goto fail;

    /* 3. Write packet (CBS will write in Section 5 format) */
    ret = ff_cbs_write_packet(ctx->common.output, pkt, frag);
    if (ret < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to write %s into packet.\n",
               ctx->common.type->fragment_name);
        goto fail;
    }

    /* Copy side data / props from input to output */
    ret = av_packet_copy_props(pkt, ctx->buffer_pkt);
    if (ret < 0) {
        av_packet_unref(pkt);
        goto fail;
    }

    /* Success */
    ff_cbs_fragment_reset(frag);
    av_packet_unref(ctx->buffer_pkt);
    return 0;

fail:
    ff_cbs_fragment_reset(frag);
    av_packet_unref(ctx->buffer_pkt);
    return ret;
}

static void av1_tstosection5_flush(AVBSFContext *bsf)
{
    AV1TsToSection5Context *ctx = bsf->priv_data;

    av_packet_unref(ctx->buffer_pkt);
    ff_cbs_fragment_reset(&ctx->common.fragment);
    ff_cbs_flush(ctx->common.input);
    ff_cbs_flush(ctx->common.output);
}

static void av1_tstosection5_close(AVBSFContext *bsf)
{
    AV1TsToSection5Context *ctx = bsf->priv_data;
    av_packet_free(&ctx->buffer_pkt);
    ff_cbs_bsf_generic_close(bsf);
}

static const enum AVCodecID av1_tstosection5_codec_ids[] = {
    AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_av1_tstosection5_bsf = {
    .p.name         = "av1_tstosection5",
    .p.codec_ids    = av1_tstosection5_codec_ids,
    .priv_data_size = sizeof(AV1TsToSection5Context),
    .init           = av1_tstosection5_init,
    .flush          = av1_tstosection5_flush,
    .close          = av1_tstosection5_close,
    .filter         = av1_tstosection5_filter,
};
