/*******************************************************************************
 *
 * Copyright (C) 2022 NETINT Technologies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/

/*!*****************************************************************************
 *  \file   encode_utils.c
 *
 *  \brief  Video encoding utility functions shared by Libxcoder API examples
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_encode_utils.h"
#include "ni_log.h"
#include "ni_av_codec.h"
#include "ni_util.h"

/*!*****************************************************************************
 *  \brief  Set up hard coded demo ROI map
 *
 *  \param
 *
 *  \return none
 ******************************************************************************/
void set_demo_roi_map(ni_session_context_t *p_enc_ctx)
{
    ni_xcoder_params_t *p_param =
        (ni_xcoder_params_t *)(p_enc_ctx->p_session_config);
    uint32_t i, j, sumQp = 0;
    uint32_t mbWidth, mbHeight, numMbs;
    // mode 1: Set QP for center 1/3 of picture to highest - lowest quality
    // the rest to lowest - highest quality;
    // mode non-1: reverse of mode 1
    int importanceLevelCentre = p_param->roi_demo_mode == 1 ? 40 : 10;
    int importanceLevelRest = p_param->roi_demo_mode == 1 ? 10 : 40;
    int32_t width, height;

    if (!p_enc_ctx->roi_map)
    {
        p_enc_ctx->roi_map =
            (ni_enc_quad_roi_custom_map *)calloc(1, p_enc_ctx->roi_len);
    }
    if (!p_enc_ctx->roi_map)
    {
        return;
    }
    uint32_t roiMapBlockUnitSize = 64;   // HEVC
    uint32_t max_cu_size = 64;           // HEVC
    if (NI_CODEC_FORMAT_H264 == p_enc_ctx->codec_format)
    {
        max_cu_size = 16;
        roiMapBlockUnitSize = 16;
    }

    width = p_param->source_width;
    height = p_param->source_height;
    // AV1 non-8x8-aligned resolution is implicitly cropped due to Quadra HW limitation
    if (NI_CODEC_FORMAT_AV1 == p_enc_ctx->codec_format)
    {
      width = (width / 8) * 8;
      height = (height / 8) * 8;
    }

    mbWidth =
        ((width + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    mbHeight =
        ((height + max_cu_size - 1) & (~(max_cu_size - 1))) /
        roiMapBlockUnitSize;
    numMbs = mbWidth * mbHeight;

    // copy roi MBs QPs into custom map
    bool bIsCenter;
    // number of qp info (8x8) per mb or ctb
    uint32_t entryPerMb = (roiMapBlockUnitSize / 8) * (roiMapBlockUnitSize / 8);

    for (i = 0; i < numMbs; i++)
    {
        if ((i % mbWidth > mbWidth / 3) && (i % mbWidth < mbWidth * 2 / 3))
            bIsCenter = 1;
        else
            bIsCenter = 0;

        for (j = 0; j < entryPerMb; j++)
        {
            /*
              g_quad_roi_map[i*4+j].field.skip_flag = 0; // don't force
              skip mode g_quad_roi_map[i*4+j].field.roiAbsQp_flag = 1; //
              absolute QP g_quad_roi_map[i*4+j].field.qp_info = bIsCenter
              ? importanceLevelCentre : importanceLevelRest;
            */
            p_enc_ctx->roi_map[i * entryPerMb + j].field.ipcm_flag =
                0;   // don't force skip mode
            p_enc_ctx->roi_map[i * entryPerMb + j].field.roiAbsQp_flag =
                1;   // absolute QP
            p_enc_ctx->roi_map[i * entryPerMb + j].field.qp_info =
                bIsCenter ? importanceLevelCentre : importanceLevelRest;
        }
        sumQp += p_enc_ctx->roi_map[i * entryPerMb].field.qp_info;
    }
    p_enc_ctx->roi_avg_qp =
        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        (sumQp + (numMbs >> 1)) / numMbs;   // round off
}

// convert various reconfig and demo modes (stored in encoder configuration) to
// aux data and store them in frame
void prep_reconf_demo_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx, ni_frame_t *frame)
{
    ni_xcoder_params_t *api_param =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    ni_aux_data_t *aux_data = NULL;

    switch (api_param->reconf_demo_mode)
    {
        case XCODER_TEST_RECONF_BR:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_BITRATE, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf BR aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig BR %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_INTRAPRD:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_INTRAPRD, sizeof(int32_t));
                if (!aux_data)
                {
                    return;
                }
                int32_t intraprd = *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_TRACE,
                        "xcoder_send_frame: frame #%lu reconf "
                        "intraPeriod %d\n",
                        p_enc_ctx->frame_num,
                        intraprd);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_VUI_HRD:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                p_enc_ctx->enc_change_params->enable_option |=
                    NI_SET_CHANGE_PARAM_VUI_HRD_PARAM;
                p_enc_ctx->enc_change_params->colorDescPresent =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                p_enc_ctx->enc_change_params->colorPrimaries =
                    api_param->reconf_hash[p_ctx->reconfig_count][2];
                p_enc_ctx->enc_change_params->colorTrc =
                    api_param->reconf_hash[p_ctx->reconfig_count][3];
                p_enc_ctx->enc_change_params->colorSpace =
                    api_param->reconf_hash[p_ctx->reconfig_count][4];
                p_enc_ctx->enc_change_params->aspectRatioWidth =
                    api_param->reconf_hash[p_ctx->reconfig_count][5];
                p_enc_ctx->enc_change_params->aspectRatioHeight =
                    api_param->reconf_hash[p_ctx->reconfig_count][6];
                p_enc_ctx->enc_change_params->videoFullRange =
                    api_param->reconf_hash[p_ctx->reconfig_count][7];

                // frame reconf_len needs to be set here
                frame->reconf_len = sizeof(ni_encoder_change_params_t);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_LONG_TERM_REF:
            // the reconf file data line format for this is:
            // <frame-number>:useCurSrcAsLongtermPic,useLongtermRef where
            // values will stay the same on every frame until changed.
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_LONG_TERM_REF,
                    sizeof(ni_long_term_ref_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf LTR aux_data\n",
                        __func__);
                    return;
                }
                ni_long_term_ref_t *ltr = (ni_long_term_ref_t *)aux_data->data;
                ltr->use_cur_src_as_long_term_pic =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                ltr->use_long_term_ref =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][2];

                ni_log(NI_LOG_DEBUG,
                    "%s(): frame #%lu reconf LTR "
                    "use_cur_src_as_long_term_pic %u use_long_term_ref "
                    "%u\n",
                    __func__, p_enc_ctx->frame_num,
                    ltr->use_cur_src_as_long_term_pic, ltr->use_long_term_ref);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP:
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_REDUNDANT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_MIN_QP, sizeof(ni_rc_min_max_qp));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf max&min QP aux_data\n",
                        __func__);
                    return;
                }
                ni_rc_min_max_qp *qp_info = (ni_rc_min_max_qp *)aux_data->data;
                qp_info->minQpI     = api_param->reconf_hash[p_ctx->reconfig_count][1];
                qp_info->maxQpI     = api_param->reconf_hash[p_ctx->reconfig_count][2];
                qp_info->maxDeltaQp = api_param->reconf_hash[p_ctx->reconfig_count][3];
                qp_info->minQpPB    = api_param->reconf_hash[p_ctx->reconfig_count][4];
                qp_info->maxQpPB    = api_param->reconf_hash[p_ctx->reconfig_count][5];

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_INTERVAL:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_LTR_INTERVAL, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, "Error %s(): no mem for reconf LTR interval "
                                   "aux_data\n",
                                   __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconf LTR interval %d\n",
                               __func__, p_enc_ctx->frame_num,
                               *((int32_t *)aux_data->data));

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_INVALID_REF_FRAME:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_INVALID_REF_FRAME,
                    sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR, "Error %s(): no mem for reconf invalid ref "
                                   "frame aux_data\n",
                                   __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconf invalid ref frame %d\n",
                               __func__, p_enc_ctx->frame_num,
                               *((int32_t *)aux_data->data));

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_FRAMERATE:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_FRAMERATE, sizeof(ni_framerate_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf framerate aux_data\n",
                        __func__);
                    return;
                }
                ni_framerate_t *framerate = (ni_framerate_t *)aux_data->data;
                framerate->framerate_num =
                    (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                framerate->framerate_denom =
                    (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][2];

                ni_log(NI_LOG_DEBUG,
                    "%s(): frame #%lu reconfig framerate (%d/%d) by frame aux data\n",
                    __func__, p_enc_ctx->frame_num, framerate->framerate_num,
                    framerate->framerate_denom);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
                if (!aux_data)
                {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf maxFrameSize aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig maxFrameSize %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_CRF:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_CRF, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf crf aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu reconfig crf %d by frame aux data\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_CRF_FLOAT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_CRF_FLOAT, sizeof(float));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf crf aux_data\n",
                        __func__);
                    return;
                }
                float crf = (float)(api_param->reconf_hash[p_ctx->reconfig_count][1] +
                    (float)api_param->reconf_hash[p_ctx->reconfig_count][2] / 100.0);
                *((float *)aux_data->data) = crf;
                ni_log(NI_LOG_DEBUG,
                       "%s(): frame #%lu reconfig float type crf %f by frame "
                       "aux data\n", __func__, p_enc_ctx->frame_num, crf);
                p_ctx->reconfig_count++;
            }
            break;

        case XCODER_TEST_RECONF_VBV:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_VBV_MAX_RATE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf vbfMaxRate aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_VBV_BUFFER_SIZE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf vbvBufferSize aux_data\n",
                        __func__);
                    return;
                }
                *((int32_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][2];
                ni_log(NI_LOG_DEBUG,
                       "%s(): frame #%lu reconfig vbfMaxRate %d vbvBufferSize "
                       "%d by frame aux data\n",
                       __func__, p_enc_ctx->frame_num,
                       api_param->reconf_hash[p_ctx->reconfig_count][1],
                       api_param->reconf_hash[p_ctx->reconfig_count][2]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0]) {
                int maxFrameSizeRatio = api_param->reconf_hash[p_ctx->reconfig_count][1];
                if (maxFrameSizeRatio < 1) {
                    ni_log(NI_LOG_ERROR, "maxFrameSizeRatio %d cannot < 1\n",
                        maxFrameSizeRatio);
                    return;
                }
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_MAX_FRAME_SIZE, sizeof(int32_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf maxFrameSizeRatio aux_data\n",
                        __func__);
                    return;
                }

                int32_t bitrate, framerate_num, framerate_denom;
                uint32_t min_maxFrameSize, maxFrameSize;
                bitrate = (p_enc_ctx->target_bitrate > 0) ?  p_enc_ctx->target_bitrate : api_param->bitrate;

                if ((p_enc_ctx->framerate.framerate_num > 0) && (p_enc_ctx->framerate.framerate_denom > 0))
                {
                    framerate_num = p_enc_ctx->framerate.framerate_num;
                    framerate_denom = p_enc_ctx->framerate.framerate_denom;
                }
                else
                {
                    framerate_num = (int32_t) api_param->fps_number;
                    framerate_denom = (int32_t) api_param->fps_denominator;
                }

                min_maxFrameSize = ((uint32_t)bitrate / framerate_num * framerate_denom) / 8;
                maxFrameSize = min_maxFrameSize * maxFrameSizeRatio > NI_MAX_FRAME_SIZE ?
                               NI_MAX_FRAME_SIZE : min_maxFrameSize * maxFrameSizeRatio;
                *((int32_t *)aux_data->data) = maxFrameSize;
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu reconf "
                        "maxFrameSizeRatio %d maxFrameSize %d\n",
                        p_enc_ctx->frame_num, maxFrameSizeRatio, maxFrameSize);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_SLICE_ARG:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0]) {
                aux_data = ni_frame_new_aux_data(
                    frame, NI_FRAME_AUX_DATA_SLICE_ARG, sizeof(int16_t));
                if (!aux_data) {
                    ni_log(NI_LOG_ERROR,
                        "Error %s(): no mem for reconf sliceArg aux_data\n",
                        __func__);
                    return;
                }
                *((int16_t *)aux_data->data) =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_log2(p_enc_ctx, NI_LOG_TRACE,
                        "xcoder_send_frame: frame #%lu reconf "
                        "sliceArg %d\n",
                        p_enc_ctx->frame_num,
                        api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;

        case XCODER_TEST_FORCE_IDR_FRAME:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_force_idr_frame_type(p_enc_ctx);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu force IDR frame\n", __func__,
                               p_enc_ctx->frame_num);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_BR_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_reconfig_bitrate(p_enc_ctx,
                                    api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig BR %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_INTRAPRD_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0]) {
                int32_t intraprd =
                    api_param->reconf_hash[p_ctx->reconfig_count][1];
                ni_reconfig_intraprd(p_enc_ctx, intraprd);
                ni_log(NI_LOG_TRACE,
                        "xcoder_send_frame: frame #%lu API reconfig intraPeriod %d\n",
                        p_enc_ctx->frame_num,
                        intraprd);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_VUI_HRD_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_vui_hrd_t vui;
                vui.colorDescPresent =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                vui.colorPrimaries =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][2];
                vui.colorTrc =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][3];
                vui.colorSpace =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][4];
                vui.aspectRatioWidth =
                    (uint16_t)api_param->reconf_hash[p_ctx->reconfig_count][5];
                vui.aspectRatioHeight =
                    (uint16_t)api_param->reconf_hash[p_ctx->reconfig_count][6];
                vui.videoFullRange =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][7];

                ni_reconfig_vui(p_enc_ctx, &vui);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig VUI HRD "
                    "colorDescPresent %d colorPrimaries %d "
                    "colorTrc %d colorSpace %d aspectRatioWidth %d "
                    "aspectRatioHeight %d videoFullRange %d\n",
                    __func__, p_enc_ctx->frame_num,
                    api_param->reconf_hash[p_ctx->reconfig_count][1],
                    api_param->reconf_hash[p_ctx->reconfig_count][2],
                    api_param->reconf_hash[p_ctx->reconfig_count][3],
                    api_param->reconf_hash[p_ctx->reconfig_count][4],
                    api_param->reconf_hash[p_ctx->reconfig_count][5],
                    api_param->reconf_hash[p_ctx->reconfig_count][6],
                    api_param->reconf_hash[p_ctx->reconfig_count][7]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_long_term_ref_t ltr;
                ltr.use_cur_src_as_long_term_pic =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                ltr.use_long_term_ref =
                    (uint8_t)api_param->reconf_hash[p_ctx->reconfig_count][2];

                ni_set_ltr(p_enc_ctx, &ltr);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set LTR\n", __func__,
                               p_enc_ctx->frame_num);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API:
        case XCODER_TEST_RECONF_RC_MIN_MAX_QP_API_REDUNDANT:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_rc_min_max_qp qp_info;
                qp_info.minQpI = (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                qp_info.maxQpI = (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][2];
                qp_info.maxDeltaQp = (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][3];
                qp_info.minQpPB = (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][4];
                qp_info.maxQpPB = (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][5];
                ni_reconfig_min_max_qp(p_enc_ctx, &qp_info);
                ni_log(NI_LOG_DEBUG,
                    "%s(): frame %llu minQpI %d maxQpI %d maxDeltaQp %d minQpPB %d maxQpPB %d\n",
                    __func__, p_enc_ctx->frame_num,
                    qp_info.minQpI, qp_info.maxQpI, qp_info.maxDeltaQp, qp_info.minQpPB, qp_info.maxQpPB);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_LTR_INTERVAL_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_set_ltr_interval(p_enc_ctx,
                                    api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set LTR interval %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_INVALID_REF_FRAME_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_set_frame_ref_invalid(
                    p_enc_ctx, api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API set frame ref invalid "
                               "%d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_FRAMERATE_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_framerate_t framerate;
                framerate.framerate_num =
                    (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][1];
                framerate.framerate_denom =
                    (int32_t)api_param->reconf_hash[p_ctx->reconfig_count][2];
                ni_reconfig_framerate(p_enc_ctx, &framerate);
                ni_log(NI_LOG_DEBUG,
                    "%s(): frame #%lu API reconfig framerate (%d/%d)\n",
                    __func__, p_enc_ctx->frame_num,
                    api_param->reconf_hash[p_ctx->reconfig_count][1],
                    api_param->reconf_hash[p_ctx->reconfig_count][2]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_reconfig_max_frame_size(p_enc_ctx,
                                    api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig maxFrameSize %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_CRF_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_reconfig_crf(p_enc_ctx,
                                    api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig crf %d\n",
                               __func__, p_enc_ctx->frame_num,
                               api_param->reconf_hash[p_ctx->reconfig_count][1]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_CRF_FLOAT_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                float crf = (float)(api_param->reconf_hash[p_ctx->reconfig_count][1] +
                    (float)api_param->reconf_hash[p_ctx->reconfig_count][2] / 100.0);
                ni_reconfig_crf2(p_enc_ctx, crf);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig crf %f\n",
                               __func__, p_enc_ctx->frame_num, crf);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_VBV_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_reconfig_vbv_value(
                    p_enc_ctx, api_param->reconf_hash[p_ctx->reconfig_count][1],
                    api_param->reconf_hash[p_ctx->reconfig_count][2]);
                ni_log(NI_LOG_DEBUG, "%s(): frame #%lu API reconfig vbvMaxRate %d vbvBufferSize %d\n",
                       __func__, p_enc_ctx->frame_num,
                       api_param->reconf_hash[p_ctx->reconfig_count][1],
                       api_param->reconf_hash[p_ctx->reconfig_count][2]);
                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_MAX_FRAME_SIZE_RATIO_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0])
            {
                ni_reconfig_max_frame_size_ratio(
                    p_enc_ctx, api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu reconf maxFrameSizeRatio %d\n",
                        p_enc_ctx->frame_num, api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_SLICE_ARG_API:
            if (p_enc_ctx->frame_num ==
                api_param->reconf_hash[p_ctx->reconfig_count][0]) {
                ni_reconfig_slice_arg(
                    p_enc_ctx, api_param->reconf_hash[p_ctx->reconfig_count][1]);
                ni_log(NI_LOG_DEBUG,
                        "xcoder_send_frame: frame #%lu API reconfig sliceArg %d\n",
                        p_enc_ctx->frame_num,
                        api_param->reconf_hash[p_ctx->reconfig_count][1]);

                p_ctx->reconfig_count++;
            }
            break;
        case XCODER_TEST_RECONF_OFF:
        default:;
    }
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from input file
 *
 *  Note: For optimal performance, yuv_buf should be 4k aligned
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int encoder_send_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                      ni_session_data_io_t *p_in_data, void *yuv_buf,
                      int input_video_width, int input_video_height,
                      int is_last_input)
{
    int oneSent;
    ni_frame_t *p_in_frame = &p_in_data->data.frame;
    uint8_t enc_id = p_ctx->curr_enc_index;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data <===\n");

    if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data: Sequence Change - waiting "
               "for previous session to end\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (p_ctx->enc_eos_sent[enc_id] == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data: ALL data (incl. eos) sent "
                       "already!\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (p_ctx->enc_resend[enc_id])
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!p_ctx->enc_sos_sent[enc_id] ||
        p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_OPENING)
    {
        p_ctx->enc_sos_sent[enc_id] = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = 0;
    p_in_frame->force_key_frame = 0;

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    // reset encoder change data buffer
    memset(p_enc_ctx->enc_change_params, 0, sizeof(ni_encoder_change_params_t));

    // reset various aux data size
    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->sei_total_len = 0;
    p_in_frame->pts = p_ctx->pts[enc_id];

    // collect encode reconfig and demo info and save them as aux data in
    // the input frame struct.
    prep_reconf_demo_data(p_ctx, p_enc_ctx, p_in_frame);

    if (yuv_buf == NULL)
    {
        if (is_last_input)
        {
            p_in_frame->end_of_stream = 1;
            ni_log(NI_LOG_DEBUG, "encoder_send_data: read chunk size 0, eos!\n");
        }
        else
        {
            ni_log(NI_LOG_DEBUG, "encoder_send_data: exit to get next input\n");
            return NI_TEST_RETCODE_NEXT_INPUT;
        }
    }

send_frame:
    // simulate FFmpeg -re option, which controls process input rate to simualte real time environment
    if (p_ctx->read_framerate > 0)
    {
        uint64_t abs_time_ns;
        abs_time_ns = ni_gettime_ns();
        if ((abs_time_ns - p_ctx->start_time) < (uint64_t)(1000000000LL / p_ctx->read_framerate * p_ctx->num_frames_sent[enc_id]))
        {
           if (!p_ctx->enc_resend[enc_id])
               p_ctx->enc_resend[enc_id] = 2;
           if (p_ctx->enc_resend[enc_id] == 2)
               return NI_TEST_RETCODE_EAGAIN;
        }
        else
        {
            p_ctx->num_frames_sent[enc_id]++;
        }
    }

    oneSent = ni_enc_write_from_yuv_buffer(p_enc_ctx, p_in_frame, yuv_buf);
    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR,
                "Error: failed ni_device_session_write() for encoder\n");
        p_ctx->enc_resend[enc_id] = 1;
        return NI_TEST_RETCODE_FAILURE;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        p_ctx->enc_resend[enc_id] = 1;
        return NI_TEST_RETCODE_EAGAIN;
    } else
    {
        p_ctx->enc_resend[enc_id] = 0;

        p_ctx->enc_total_bytes_sent[enc_id] += p_in_frame->data_len[0] +
            p_in_frame->data_len[1] + p_in_frame->data_len[2] + p_in_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "encoder_send_data: total sent data size=%lu\n",
               p_ctx->enc_total_bytes_sent[enc_id]);

        ni_log(NI_LOG_DEBUG, "encoder_send_data: success\n");

        if (p_enc_ctx->ready_to_close)
        {
            if (p_enc_ctx->session_run_state != SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
            {
                p_ctx->enc_eos_sent[enc_id] = 1;
            }
        }

        if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_SEQ_CHANGE_OPENING)
        {
            p_enc_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
            ni_log(NI_LOG_DEBUG,
                   "encoder_send_data: session_run_state change to %d\n",
                   p_enc_ctx->session_run_state);
        }

        ni_pts_enqueue(p_ctx->enc_pts_queue[enc_id], p_ctx->pts[enc_id]);
        ++p_ctx->pts[enc_id];
    }

    ni_frame_wipe_aux_data(p_in_frame);
    return NI_TEST_RETCODE_SUCCESS;
}

/*******************************************************************************
 *  @brief  Send encoder input data, directly after receiving from decoder
 *
 *  @param  p_enc_ctx encoder context
 *          p_dec_ctx decoder context
 *          p_dec_out_data frame returned by decoder
 *          p_enc_in_data  frame to be sent to encoder
 *
 *  @return
 ******************************************************************************/
int encoder_send_data2(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_dec_out_data,
                       ni_session_data_io_t *p_enc_in_data,
                       int input_video_width, int input_video_height)
{
    int oneSent;
    int data_len_to_send;
    // frame pointer to data frame struct to be sent
    ni_frame_t *p_in_frame = &(p_enc_in_data->data.frame);
    ni_xcoder_params_t *api_params =
        (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int is_semiplanar = get_pixel_planar(p_enc_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
    int is_hwframe = p_enc_ctx->hw_action != NI_CODEC_HW_NONE;
    uint8_t enc_id = p_ctx->curr_enc_index;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data2 <===\n");

    if (p_ctx->enc_eos_sent[enc_id] == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data2: ALL data (incl. eos) sent "
                       "already!\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    // don't send new data before flushing completed
    if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_FLUSHING)
    {
        return NI_TEST_RETCODE_EAGAIN;
    }

//#define ENCODER_FLUSH_INJECT
// Note that this demo only supports in multi threads transcoding mode
// For examples: sudo ./xcoder -c 0 -i input.h265 -m h2h -b 8 -o output.h265 -t
#ifdef ENCODER_FLUSH_INJECT
    if ((p_enc_ctx->frame_num > 0 && p_enc_ctx->frame_num % 30 == 0) &&
        !p_dec_out_data->data.frame.end_of_stream)
    {
        // send the encoder flush command
        if (ni_device_session_flush(p_enc_ctx, NI_DEVICE_TYPE_ENCODER) == NI_RETCODE_SUCCESS)
        {
            // need to change the state
            p_enc_ctx->session_run_state = SESSION_RUN_STATE_FLUSHING;
            ni_log(NI_LOG_INFO, "encoder_send_data2 flush encoder successfully\n");
            return NI_TEST_RETCODE_EAGAIN;
        }
        else
        {
            ni_log(NI_LOG_ERROR, "Error: encoder_send_data2 flush encoder\n");
            return -1;
        }
    }
#endif

    // frame resend
    if (p_ctx->enc_resend[enc_id])
    {
        goto send_frame;
    }

    // copy input frame to a new frame struct and prep for the aux data
    p_in_frame->end_of_stream = p_dec_out_data->data.frame.end_of_stream;
    p_in_frame->ni_pict_type = 0;

    // reset encoder change data buffer
    memset(p_enc_ctx->enc_change_params, 0,
           sizeof(ni_encoder_change_params_t));

    // extra data starts with metadata header, and reset various aux data
    // size
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
    p_in_frame->roi_len = 0;
    p_in_frame->reconf_len = 0;
    p_in_frame->sei_total_len = 0;
    p_in_frame->force_pic_qp = 0;
    p_in_frame->pts = p_ctx->pts[enc_id];

    // collect encode reconfig and demo info and save them in the decode out
    // frame, to be used in the aux data prep and copy later
    prep_reconf_demo_data(p_ctx, p_enc_ctx, &(p_dec_out_data->data.frame));

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    bool alignment_2pass_wa = (
                   (api_params->cfg_enc_params.lookAheadDepth ||
                    api_params->cfg_enc_params.crf >= 0 ||
                    api_params->cfg_enc_params.crfFloat >= 0) &&
                   (p_enc_ctx->codec_format == NI_CODEC_FORMAT_H265 ||
                    p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1));
    ni_get_hw_yuv420p_dim(input_video_width, input_video_height,
                          p_enc_ctx->bit_depth_factor, is_semiplanar,
                          dst_stride, dst_height_aligned);

    if (alignment_2pass_wa && !is_hwframe) {
        if (is_semiplanar) {
            // for 2-pass encode output mismatch WA, need to extend (and
            // pad) CbCr plane height, because 1st pass assume input 32
            // align
            dst_height_aligned[1] = (((dst_height_aligned[0] + 31) / 32) * 32) / 2;
        } else {
            // for 2-pass encode output mismatch WA, need to extend (and
            // pad) Cr plane height, because 1st pass assume input 32 align
            dst_height_aligned[2] = (((dst_height_aligned[0] + 31) / 32) * 32) / 2;
        }
    }

    // ROI demo mode takes higher priority over aux data
    // Note: when ROI demo modes enabled, supply ROI map for the specified
    //       range frames, and 0 map for others
    if (api_params->roi_demo_mode && api_params->cfg_enc_params.roi_enable)
    {
        if (p_enc_ctx->frame_num > 90 && p_enc_ctx->frame_num < 300)
        {
            p_in_frame->roi_len = p_enc_ctx->roi_len;
        } else
        {
            p_in_frame->roi_len = 0;
        }
        // when ROI enabled, always have a data buffer for ROI
        // Note: this is handled separately from ROI through side/aux data
        p_in_frame->extra_data_len += p_enc_ctx->roi_len;
    }

    int should_send_sei_with_frame = ni_should_send_sei_with_frame(
        p_enc_ctx, p_in_frame->ni_pict_type, api_params);

    // data buffer for various SEI: HDR mastering display color volume, HDR
    // content light level, close caption, User data unregistered, HDR10+
    // etc.
    uint8_t mdcv_data[NI_MAX_SEI_DATA];
    uint8_t cll_data[NI_MAX_SEI_DATA];
    uint8_t cc_data[NI_MAX_SEI_DATA];
    uint8_t udu_data[NI_MAX_SEI_DATA];
    uint8_t hdrp_data[NI_MAX_SEI_DATA];

    // prep for auxiliary data (various SEI, ROI) in p_in_frame, based on
    // the data returned in decoded frame and also reconfig and demo modes
    // collected in prep_reconf_demo_data
    ni_enc_prep_aux_data(
        p_enc_ctx, p_in_frame, &(p_dec_out_data->data.frame),
        p_enc_ctx->codec_format, should_send_sei_with_frame, mdcv_data,
        cll_data, cc_data, udu_data, hdrp_data);

    p_in_frame->extra_data_len += p_in_frame->sei_total_len;

    // data layout requirement: leave space for reconfig data if at least
    // one of reconfig, SEI or ROI is present
    // Note: ROI is present when enabled, so use encode config flag instead
    //       of frame's roi_len as it can be 0 indicating a 0'd ROI map
    //       setting !
    if (p_in_frame->reconf_len || p_in_frame->sei_total_len ||
        (api_params->roi_demo_mode &&
         api_params->cfg_enc_params.roi_enable))
    {
        p_in_frame->extra_data_len += sizeof(ni_encoder_change_params_t);
    }

    if (!is_hwframe)
    {
        ni_encoder_sw_frame_buffer_alloc(api_params->cfg_enc_params.planar,
            p_in_frame, input_video_width, input_video_height, dst_stride,
            p_enc_ctx->codec_format == NI_CODEC_FORMAT_H264,
            (int)(p_in_frame->extra_data_len), alignment_2pass_wa);
        if (!p_in_frame->p_data[0])
        {
            ni_log(NI_LOG_ERROR, "Error: cannot allocate YUV frame buffer!");
            return NI_TEST_RETCODE_FAILURE;
        }
    } else
    {
        ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                    input_video_height,
                                    (int)(p_in_frame->extra_data_len));
        if (!p_in_frame->p_data[3])
        {
            ni_log(NI_LOG_ERROR, "Error: cannot allocate YUV frame buffer!");
            return NI_TEST_RETCODE_FAILURE;
        }
    }

    ni_log(NI_LOG_DEBUG,
        "p_dst alloc linesize = %d/%d/%d  src height=%d  "
        "dst height aligned = %d/%d/%d force_key_frame=%d, "
        "extra_data_len=%u"
        " sei_size=%u (hdr_content_light_level %u hdr_mastering_display_"
        "color_vol %u hdr10+ %u hrd %d) reconf_size=%u roi_size=%u "
        "force_pic_qp=%u udu_sei_size=%u "
        "use_cur_src_as_long_term_pic %u use_long_term_ref %u\n",
        dst_stride[0], dst_stride[1], dst_stride[2], input_video_height,
        dst_height_aligned[0], dst_height_aligned[1], dst_height_aligned[2],
        p_in_frame->force_key_frame, p_in_frame->extra_data_len,
        p_in_frame->sei_total_len,
        p_in_frame->sei_hdr_content_light_level_info_len,
        p_in_frame->sei_hdr_mastering_display_color_vol_len,
        p_in_frame->sei_hdr_plus_len, 0, /* hrd is 0 size for now */
        p_in_frame->reconf_len, p_in_frame->roi_len,
        p_in_frame->force_pic_qp, p_in_frame->sei_user_data_unreg_len,
        p_in_frame->use_cur_src_as_long_term_pic,
        p_in_frame->use_long_term_ref);

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS];
    int src_stride[NI_MAX_NUM_DATA_POINTERS];
    int src_height[NI_MAX_NUM_DATA_POINTERS];

    src_height[0] = p_dec_out_data->data.frame.video_height;
    src_height[1] = src_height[2] = src_height[0] / 2;
    src_height[3] = 0;

    src_stride[0] =
        (int)(p_dec_out_data->data.frame.data_len[0]) / src_height[0];
    src_stride[1] =
        (int)(p_dec_out_data->data.frame.data_len[1]) / src_height[1];
    src_stride[2] = src_stride[1];
    if (is_semiplanar)
    {
        src_height[2] = 0;
        src_stride[2] = 0;
    }
    src_stride[3] = 0;

    p_src[0] = p_dec_out_data->data.frame.p_data[0];
    p_src[1] = p_dec_out_data->data.frame.p_data[1];
    p_src[2] = p_dec_out_data->data.frame.p_data[2];
    p_src[3] = p_dec_out_data->data.frame.p_data[3];

    if (!is_hwframe)
    {   // YUV part of the encoder input data layout
        ni_copy_hw_yuv420p(
            (uint8_t **)(p_in_frame->p_data), p_src, input_video_width,
            input_video_height, p_enc_ctx->bit_depth_factor, is_semiplanar,
            ((ni_xcoder_params_t *)p_enc_ctx->p_session_config)
                ->cfg_enc_params.conf_win_right,
            dst_stride, dst_height_aligned, src_stride, src_height);
    } else
    {
        ni_copy_hw_descriptors((uint8_t **)(p_in_frame->p_data), p_src);
    }
    // auxiliary data part of the encoder input data layout
    ni_enc_copy_aux_data(p_enc_ctx, p_in_frame,
                         &(p_dec_out_data->data.frame),
                         p_enc_ctx->codec_format, mdcv_data, cll_data,
                         cc_data, udu_data, hdrp_data, is_hwframe,
                         is_semiplanar);

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;

    p_in_frame->start_of_stream = 0;
    if (!p_ctx->enc_sos_sent[enc_id])
    {
        p_ctx->enc_sos_sent[enc_id] = 1;
        p_in_frame->start_of_stream = 1;
    }

send_frame:
    data_len_to_send = (int)(p_in_frame->data_len[0] + p_in_frame->data_len[1] +
                    p_in_frame->data_len[2] + p_in_frame->data_len[3]);

    if (data_len_to_send > 0 || p_in_frame->end_of_stream)
    {
        oneSent = ni_device_session_write(p_enc_ctx, p_enc_in_data,
                                          NI_DEVICE_TYPE_ENCODER);
        p_in_frame->end_of_stream = 0;
    } else
    {
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: encoder_send_data2\n");
        p_ctx->enc_resend[enc_id] = 1;
        return NI_TEST_RETCODE_FAILURE;
    } else if (oneSent == 0)
    {
        if (p_ctx->enc_eos_sent[enc_id] == 0 && p_enc_ctx->ready_to_close)
        {
            p_ctx->enc_resend[enc_id] = 0;
            p_ctx->enc_eos_sent[enc_id] = 1;
        } else
        {
            p_ctx->enc_resend[enc_id] = 1;
            return NI_TEST_RETCODE_EAGAIN;
        }
    } else
    {
        p_ctx->enc_resend[enc_id] = 0;

        if (p_enc_ctx->ready_to_close)
        {
            p_ctx->enc_eos_sent[enc_id] = 1;
        }

        ni_pts_enqueue(p_ctx->enc_pts_queue[enc_id], p_ctx->pts[enc_id]);
        ++p_ctx->pts[enc_id];

        ni_log(NI_LOG_DEBUG, "encoder_send_data2: success\n");
    }

    return NI_TEST_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Send encoder input data, read from uploader instance hwframe
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int encoder_send_data3(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_in_data,
                       int input_video_width, int input_video_height, int eos)
{
    int oneSent;
    ni_frame_t *p_in_frame = &(p_in_data->data.frame);
    uint8_t enc_id = p_ctx->curr_enc_index;

    ni_log(NI_LOG_DEBUG, "===> encoder_send_data3 <===\n");

    if (p_ctx->enc_eos_sent[enc_id] == 1)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: ALL data (incl. eos) sent "
                       "already!\n");
        return 0;
    }

    if (p_ctx->enc_resend[enc_id])
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;
    if (!p_ctx->enc_sos_sent[enc_id])
    {
        p_ctx->enc_sos_sent[enc_id] = 1;
        p_in_frame->start_of_stream = 1;
    }
    p_in_frame->end_of_stream = eos;
    p_in_frame->force_key_frame = 0;
    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;
    p_in_frame->pts = p_ctx->pts[enc_id];
    if (eos)
    {
        ni_log(NI_LOG_DEBUG, "encoder_send_data3: read chunk size 0, eos!\n");
    }

    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

send_frame:
    oneSent =
        ni_device_session_write(p_enc_ctx, p_in_data, NI_DEVICE_TYPE_ENCODER);
    if (oneSent < 0)
    {
        ni_log(NI_LOG_ERROR,
               "Error: failed ni_device_session_write() for encoder\n");
        p_ctx->enc_resend[enc_id] = 1;
        return -1;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        p_ctx->enc_resend[enc_id] = 1;
        ni_log(NI_LOG_DEBUG, "NEEDED TO RESEND");
        return NI_TEST_RETCODE_EAGAIN;
    } else
    {
        p_ctx->enc_resend[enc_id] = 0;

        ni_log(NI_LOG_DEBUG, "encoder_send_data3: total sent data size=%u\n",
               p_in_frame->data_len[3]);

        ni_log(NI_LOG_DEBUG, "encoder_send_data3: success\n");

        if (p_enc_ctx->ready_to_close)
        {
            p_ctx->enc_eos_sent[enc_id] = 1;
        }

        ni_pts_enqueue(p_ctx->enc_pts_queue[enc_id], p_ctx->pts[enc_id]);
        ++p_ctx->pts[enc_id];
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Encoder session open
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_open_session(ni_session_context_t *p_enc_ctx, int dst_codec_format,
                         int iXcoderGUID, ni_xcoder_params_t *p_enc_params,
                         int width, int height, ni_pix_fmt_t pix_fmt,
                         bool check_zerocopy)
{
    int ret = 0;
    bool isrgba = false;

    p_enc_ctx->p_session_config = p_enc_params;
    p_enc_ctx->session_id = NI_INVALID_SESSION_ID;
    p_enc_ctx->codec_format = dst_codec_format;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_enc_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->hw_id = iXcoderGUID;

    // default: little endian
    p_enc_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;

    switch (pix_fmt)
    {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_NV12:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 1;
            break;
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_P010LE:
            p_enc_ctx->src_bit_depth = 10;
            p_enc_ctx->bit_depth_factor = 2;
            break;
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 4;
            isrgba = true;
            break;
        default:
            p_enc_ctx->src_bit_depth = 8;
            p_enc_ctx->bit_depth_factor = 1;
            pix_fmt = NI_PIX_FMT_YUV420P;
            break;
            //ni_log(NI_LOG_ERROR, "%s: Invalid pixel format %s\n", __func__,
            //        ni_pixel_format_name(pix_fmt));
            //return NI_RETCODE_INVALID_PARAM;
    }

    // original resolution this stream started with, this is used by encoder sequence change
    p_enc_ctx->ori_width = width;
    p_enc_ctx->ori_height = height;
    p_enc_ctx->ori_bit_depth_factor = p_enc_ctx->bit_depth_factor;
    p_enc_ctx->ori_pix_fmt = pix_fmt;
    p_enc_ctx->pixel_format = pix_fmt;

    int linesize_aligned = width;
    if (!isrgba)
    {
        if (linesize_aligned < NI_MIN_WIDTH)
        {
            p_enc_params->cfg_enc_params.conf_win_right +=
                (NI_MIN_WIDTH - width) / 2 * 2;
            linesize_aligned = NI_MIN_WIDTH;
        } else
        {
            linesize_aligned = ((width + 1) / 2) * 2;
            p_enc_params->cfg_enc_params.conf_win_right +=
                (linesize_aligned - width) / 2 * 2;
        }
    }
    p_enc_params->source_width = linesize_aligned;

    int height_aligned = height;
    if (!isrgba)
    {
        if (height_aligned < NI_MIN_HEIGHT)
        {
            p_enc_params->cfg_enc_params.conf_win_bottom +=
                (NI_MIN_HEIGHT - height) / 2 * 2;
            height_aligned = NI_MIN_HEIGHT;
        } else
        {
            height_aligned = ((height + 1) / 2) * 2;
            p_enc_params->cfg_enc_params.conf_win_bottom +=
                (height_aligned - height) / 2 * 2;
        }
    }
    p_enc_params->source_height = height_aligned;

    // default planar encoder input data
    p_enc_params->cfg_enc_params.planar = get_pixel_planar(pix_fmt);

    if (check_zerocopy)
    {

        // config linesize for zero copy (if input resolution is zero copy compatible)
        int src_stride[NI_MAX_NUM_DATA_POINTERS];

        // NOTE - FFmpeg / Gstreamer users should use linesize array in frame structure instead of src_stride in the following sample code
        src_stride[0] = width * p_enc_ctx->bit_depth_factor;

        if (isrgba)
            src_stride[1] = src_stride[2] = 0;
        else
        {
            bool isnv12frame = (p_enc_params->cfg_enc_params.planar == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR) ? true : false;
            src_stride[1] = isnv12frame ? src_stride[0] : src_stride[0] / 2;
            src_stride[2] = isnv12frame ? 0 : src_stride[0] / 2;
        }

        ni_encoder_frame_zerocopy_check(p_enc_ctx,
            p_enc_params, width, height,
            (const int *)src_stride, true);
    }

    ret = ni_device_session_open(p_enc_ctx, NI_DEVICE_TYPE_ENCODER);
    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
    } else
    {
        ni_log(NI_LOG_INFO, "Encoder device %d session open successful.\n", iXcoderGUID);
    }

    // set up ROI QP map for ROI demo modes if enabled
    if (p_enc_params->cfg_enc_params.roi_enable &&
        (1 == p_enc_params->roi_demo_mode || 2 == p_enc_params->roi_demo_mode))
    {
        set_demo_roi_map(p_enc_ctx);
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  Reopen or reconfig encoder upon sequence change
 *
 *  \param
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *          -1 - failure
 ******************************************************************************/
int encoder_reinit_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data)
{
    int ret = NI_TEST_RETCODE_SUCCESS;
    int new_stride, ori_stride;
    bool bIsSmallPicture = false;
    ni_frame_t *p_buffered_frame = &(p_in_data->data.frame);
    ni_xcoder_params_t *p_api_param = (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int new_width, new_height;
    ni_pix_fmt_t new_pix_fmt;
    bool is_semiplanar;
    bool is_rgba;
    int src_stride[NI_MAX_NUM_DATA_POINTERS];

    new_width = p_buffered_frame->video_width;
    new_height = p_buffered_frame->video_height;
    new_pix_fmt = p_buffered_frame->pixel_format;

    // check if resolution is zero copy compatible and set linesize according to new resolution
    is_semiplanar = (new_pix_fmt == NI_PIX_FMT_NV12 || new_pix_fmt == NI_PIX_FMT_P010LE);

    is_rgba = (new_pix_fmt == NI_PIX_FMT_ABGR || new_pix_fmt == NI_PIX_FMT_ARGB
                 || new_pix_fmt == NI_PIX_FMT_RGBA || new_pix_fmt == NI_PIX_FMT_BGRA);

    // NOTE - FFmpeg / Gstreamer users should use linesize array in frame structure instead of src_stride in the following sample code
    src_stride[0] = new_width * p_enc_ctx->bit_depth_factor;
    if (is_rgba)
    {
        src_stride[1] = 0;
        src_stride[2] = 0;
    }
    else
    {
        src_stride[1] = is_semiplanar ? src_stride[0] : src_stride[0] / 2;
        src_stride[2] = is_semiplanar ? 0 : src_stride[0] / 2;
    }

    if (ni_encoder_frame_zerocopy_check(p_enc_ctx,
        p_api_param, new_width, new_height,
        (const int *)src_stride, true) == NI_RETCODE_SUCCESS)
    {
        new_stride = p_api_param->luma_linesize; // new sequence is zero copy compatible
    }
    else
    {
        new_stride = NI_ALIGN(new_width * p_enc_ctx->bit_depth_factor, 128);
    }

    if (p_enc_ctx->ori_luma_linesize && p_enc_ctx->ori_chroma_linesize)
    {
        ori_stride = p_enc_ctx->ori_luma_linesize; // previous sequence was zero copy compatible
    }
    else
    {
        ori_stride = NI_ALIGN(p_enc_ctx->ori_width * p_enc_ctx->bit_depth_factor, 128);
    }

    if (p_api_param->cfg_enc_params.lookAheadDepth) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit 2-pass "
             "lookaheadDepth %d\n",
             p_api_param->cfg_enc_params.lookAheadDepth);
        if ((new_width < NI_2PASS_ENCODE_MIN_WIDTH) ||
           (new_height < NI_2PASS_ENCODE_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }
    else {
        if ((new_width < NI_MIN_WIDTH) ||
           (new_height < NI_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }

    if (p_api_param->cfg_enc_params.multicoreJointMode) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit multicore "
             "joint mode\n");
        if ((new_width < NI_MULTICORE_ENCODE_MIN_WIDTH) ||
           (new_height < NI_MULTICORE_ENCODE_MIN_HEIGHT)) {
          bIsSmallPicture = true;
        }
    }

    if (p_api_param->cfg_enc_params.crop_width || p_api_param->cfg_enc_params.crop_height) {
        ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit needs to close and re-open "
             "due to crop width x height\n");
        bIsSmallPicture = true;
    }

    ni_log(NI_LOG_DEBUG, "xcoder_encode_reinit resolution: %dx%d->%dx%d "
         "pix fmt: %d->%d bIsSmallPicture %d codec %d\n",
         ori_stride, p_enc_ctx->ori_height, new_stride, new_height,
         p_enc_ctx->ori_pix_fmt, new_pix_fmt, bIsSmallPicture,
         p_enc_ctx->codec_format);

    // fast sequence change without close / open only if new resolution < original resolution
    if (ori_stride*p_enc_ctx->ori_height < new_stride*new_height ||
        p_enc_ctx->ori_pix_fmt != new_pix_fmt ||
        bIsSmallPicture ||
        p_enc_ctx->codec_format == NI_CODEC_FORMAT_JPEG)
    {
        ni_log(NI_LOG_INFO, "XCoder encode sequence change by close / re-open session\n");
        encoder_close_session(p_enc_ctx, p_in_data, p_out_data);
        ret = encoder_open_session(p_enc_ctx, p_enc_ctx->codec_format,
                               p_enc_ctx->hw_id, p_api_param, new_width,
                               new_height, new_pix_fmt, true);
        if (NI_RETCODE_SUCCESS != ret)
        {
            ni_log(NI_LOG_ERROR, "Failed to Re-open Encoder Session upon Sequence Change (status = %d)\n", ret);
            return ret;
        }
        p_out_data->data.packet.end_of_stream = 0;
        p_in_data->data.frame.start_of_stream = 1;
        // clear crop parameters upon sequence change because cropping values may not be compatible to new resolution
        p_api_param->cfg_enc_params.crop_width = p_api_param->cfg_enc_params.crop_height = 0;
        p_api_param->cfg_enc_params.hor_offset = p_api_param->cfg_enc_params.ver_offset = 0;
    }
    else
    {
      if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1) {
          // AV1 8x8 alignment HW limitation is now worked around by FW cropping input resolution
          if (new_width % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
              ni_log(NI_LOG_ERROR,
                     "resolution change: AV1 Picture Width not aligned to %d - picture will be cropped\n",
                     NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);

          if (new_height % NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT)
              ni_log(NI_LOG_ERROR,
                     "resolution change: AV1 Picture Height not aligned to %d - picture will be cropped\n",
                     NI_PARAM_AV1_ALIGN_WIDTH_HEIGHT);
      }
      ni_log(NI_LOG_INFO, "XCoder encode sequence change by re-config session (fast path)\n");
      ret = encoder_sequence_change(p_enc_ctx, p_in_data, p_out_data, new_width, new_height, new_pix_fmt);
    }

    // this state is referenced when sending first frame after sequence change
    p_enc_ctx->session_run_state = SESSION_RUN_STATE_SEQ_CHANGE_OPENING;

    ni_log(NI_LOG_DEBUG, "%s: session_run_state change to %d \n", __func__,
           p_enc_ctx->session_run_state);

    return ret;
}

void write_av1_ivf_header(ni_demo_context_t *p_ctx, uint32_t width, uint32_t height, uint32_t frame_num,
                          uint32_t frame_denom, FILE *p_file)
{
    // write the global ivf start header
    if (!p_ctx->ivf_header_written[p_ctx->curr_enc_index] && p_file != NULL && !p_ctx->av1_output_obu)
    {
        uint8_t start_header[32] = {
            0x44, 0x4b, 0x49, 0x46, /* signature: 'DKIF' */
            0x00, 0x00,             /* version: 0 */
            0x20, 0x00,             /* length of header in bytes: 32 */
            0x41, 0x56, 0x30, 0x31, /* codec FourCC: AV01 */
            0x00, 0x07, /* width in pixels(little endian), default 1280 */
            0xd0, 0x02, /* height in pixels(little endian), default 720 */
            0x1e, 0x00, 0x00, 0x00, /* time base numerator, default 30 */
            0x01, 0x00, 0x00, 0x00, /* time base denominator, default 1 */
            0x00, 0x00, 0x00, 0x00, /* number of frames in file */
            0x00, 0x00, 0x00, 0x00  /* reserved */
        };

        if (width && height)
        {
            start_header[12] = width & 0xff;
            start_header[13] = ((width >> 8) & 0xff);
            start_header[14] = height & 0xff;
            start_header[15] = ((height >> 8) & 0xff);
        }

        if (frame_num && frame_denom)
        {
            start_header[16] = frame_num & 0xff;
            start_header[17] = ((frame_num >> 8) & 0xff);
            start_header[18] = ((frame_num >> 16) & 0xff);
            start_header[19] = ((frame_num >> 24) & 0xff);
            start_header[20] = frame_denom & 0xff;
            start_header[21] = ((frame_denom >> 8) & 0xff);
            start_header[22] = ((frame_denom >> 16) & 0xff);
            start_header[23] = ((frame_denom >> 24) & 0xff);
        }

        if (fwrite(start_header, sizeof(start_header), 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing ivf start header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }

        p_ctx->ivf_header_written[p_ctx->curr_enc_index] = 1;
    }
}

void write_av1_ivf_packet(ni_demo_context_t *p_ctx, ni_packet_t *p_out_pkt, uint32_t meta_size, FILE *p_file)
{
    int i;
    uint8_t enc_id = p_ctx->curr_enc_index;

    if (!p_file)
    {
        return;
    }

    // write ivf frame header
    if (!p_ctx->av1_output_obu)
    {
        uint32_t pts = p_ctx->av1_muxed_num_packets[enc_id];
        uint32_t pkt_size = p_out_pkt->data_len - meta_size;

        if (p_ctx->av1_seq_header_len[enc_id] > 0)
        {
            pkt_size += p_ctx->av1_seq_header_len[enc_id] - meta_size;
        }
        for (i = 0; i < p_out_pkt->av1_buffer_index; i++)
        {
            pkt_size += p_out_pkt->av1_data_len[i] - meta_size;
        }

        // ivf frame header
        // bytes 0-3: size of frame in bytes(not including the 12-byte header
        // byte 4-11: 64-bit pts (here pts=num_of_packets(32-bit), thus here only saves 32-bit
        uint8_t ivf_frame_header[12] = {((pkt_size & 0xff)),
                                        ((pkt_size >> 8) & 0xff),
                                        ((pkt_size >> 16) & 0xff),
                                        ((pkt_size >> 24) & 0xff),
                                        ((pts & 0xff)),
                                        ((pts >> 8) & 0xff),
                                        ((pts >> 16) & 0xff),
                                        ((pts >> 24) & 0xff),
                                        0x00, 0x00, 0x00, 0x00};
        if (fwrite(ivf_frame_header, 12, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing ivf frame header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
    }

    // write the leftover sequence header if there is any
    if (p_ctx->av1_seq_header_len[enc_id] > 0)
    {
        if (fwrite(p_ctx->p_av1_seq_header[enc_id] + meta_size,
                   p_ctx->av1_seq_header_len[enc_id] - meta_size, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing av1 sequence header fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
        ni_aligned_free(p_ctx->p_av1_seq_header[enc_id]);
        p_ctx->av1_seq_header_len[enc_id] = 0;
    }

    // write the leftover av1 packets
    for (i = 0; i < p_out_pkt->av1_buffer_index; i++)
    {
        if (fwrite((uint8_t *)p_out_pkt->av1_p_data[i] + meta_size,
                   p_out_pkt->av1_data_len[i] - meta_size, 1, p_file) != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: writing av1 packets fail!\n");
            ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
        }
    }

    // write the current packet
    if (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
               p_out_pkt->data_len - meta_size, 1, p_file) != 1)
    {
        ni_log(NI_LOG_ERROR, "Error: writing av1 packets fail!\n");
        ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
    }

    p_ctx->av1_muxed_num_packets[enc_id]++;
}

int write_av1_ivf_trailer(ni_demo_context_t *p_ctx, ni_packet_t *p_out_pkt, uint32_t meta_size, FILE *p_file)
{
    uint32_t muxed_num_packets = p_ctx->av1_muxed_num_packets[p_ctx->curr_enc_index];
    if (p_file)
    {
        // write the leftover packets
        if (p_out_pkt->av1_buffer_index > 0)
        {
            write_av1_ivf_packet(p_ctx, p_out_pkt, meta_size, p_file);
        }

        // update frame_count in ivf start header
        if (muxed_num_packets && !p_ctx->av1_output_obu)
        {
            uint8_t frame_cnt[4] = {
                (muxed_num_packets & 0xff),
                ((muxed_num_packets >> 8) & 0xff),
                ((muxed_num_packets >> 16) & 0xff),
                ((muxed_num_packets >> 24) & 0xff)};
            fseek(p_file, 24, SEEK_SET);
            if (fwrite(frame_cnt, 4, 1, p_file) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: failed to update frame_cnt in ivf "
                        "header!\n");
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                return -1;
            }
        }
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Receive output data from encoder
 *
 *  \param  p_in_data is passed in to specify new frame resolution upon sequence
 *          change
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *          -1 - failure
 ******************************************************************************/
int encoder_receive_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_out_data, int output_video_width,
                         int output_video_height, FILE *p_file, ni_session_data_io_t * p_in_data)
{
    int packet_size = NI_MAX_TX_SZ;
    int rc = 0;
    int end_flag = 0;
    int rx_size = 0;
    uint8_t enc_id = p_ctx->curr_enc_index;
    ni_packet_t *p_out_pkt = &(p_out_data->data.packet);
    int meta_size = p_enc_ctx->meta_size;
    ni_xcoder_params_t *p_api_param = (ni_xcoder_params_t *)p_enc_ctx->p_session_config;
    int pts = -1;
    int dts = -1;

    ni_log(NI_LOG_DEBUG, "===> encoder_receive_data <===\n");
    if (NI_INVALID_SESSION_ID == p_enc_ctx->session_id)
    {
        // keep-alive-thread timeout will set session_id to invalid, should exit
        ni_log(NI_LOG_ERROR,
               "encode session id invalid, the session should be closed\n");
        return NI_TEST_RETCODE_FAILURE;
    }

receive_data:
    rc = ni_packet_buffer_alloc(p_out_pkt, packet_size);
    if (rc != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: malloc packet failed, ret = %d!\n", rc);
        return NI_TEST_RETCODE_FAILURE;
    }

    rc = ni_device_session_read(p_enc_ctx, p_out_data, NI_DEVICE_TYPE_ENCODER);

    end_flag = p_out_pkt->end_of_stream;
    rx_size = rc;

    ni_log(NI_LOG_DEBUG, "encoder_receive_data: received data size=%d\n", rx_size);

    if (rx_size > meta_size)
    {
        if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1)
        {
            if (p_enc_ctx->pkt_num == 0)
            {
                write_av1_ivf_header(p_ctx, output_video_width, output_video_height,
                                     p_api_param->fps_number,
                                     p_api_param->fps_denominator, p_file);
                // store the sequence header for next packet writing
                p_ctx->p_av1_seq_header[enc_id] = (uint8_t *)p_out_pkt->p_data;
                p_ctx->av1_seq_header_len[enc_id] = p_out_pkt->data_len;
                p_out_pkt->p_buffer = NULL;
                p_out_pkt->p_data = NULL;
                p_out_pkt->buffer_size = 0;
                p_out_pkt->data_len = 0;
                p_enc_ctx->pkt_num = 1;
            } else
            {
                // store the av1 unshown frames for next packet writing
                if (!p_out_pkt->av1_show_frame)
                {
                    p_out_pkt->av1_p_buffer[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->p_buffer;
                    p_out_pkt->av1_p_data[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->p_data;
                    p_out_pkt->av1_buffer_size[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->buffer_size;
                    p_out_pkt->av1_data_len[p_out_pkt->av1_buffer_index] =
                        p_out_pkt->data_len;
                    p_out_pkt->av1_buffer_index++;
                    p_out_pkt->p_buffer = NULL;
                    p_out_pkt->p_data = NULL;
                    p_out_pkt->buffer_size = 0;
                    p_out_pkt->data_len = 0;
                    if (p_out_pkt->av1_buffer_index >= MAX_AV1_ENCODER_GOP_NUM)
                    {
                        ni_log(NI_LOG_ERROR, "Error: recv AV1 not shown frame "
                               "number %d >= %d\n", p_out_pkt->av1_buffer_index,
                               MAX_AV1_ENCODER_GOP_NUM);
                        return NI_TEST_RETCODE_FAILURE;
                    }
                } else
                {
                    ni_log(NI_LOG_DEBUG, "AV1 output packet "
                           "pts %lld  dts %lld\n", p_out_pkt->pts, p_out_pkt->dts);
                    write_av1_ivf_packet(p_ctx, p_out_pkt, p_enc_ctx->meta_size, p_file);
                    ni_packet_buffer_free_av1(p_out_pkt);
                }

                // recycle hw frame before next read
                if (p_enc_ctx->hw_action)
                {
                    // encoder only returns valid recycle index
                    // when there's something to recycle.
                    // This range is suitable for all memory bins
                    if (p_out_pkt->recycle_index > 0 &&
                        p_out_pkt->recycle_index <
                        NI_GET_MAX_HWDESC_FRAME_INDEX(p_enc_ctx->ddr_config))
                    {
                        ni_hw_frame_unref(p_out_pkt->recycle_index);
                        p_out_pkt->recycle_index = 0; //clear to not double count
                    }
                }
            }
        } else
        {
            if (p_file &&
                (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
                        p_out_pkt->data_len - meta_size, 1, p_file) != 1))
            {
                ni_log(NI_LOG_ERROR, "Error: writing data %u bytes error!\n",
                        p_out_pkt->data_len - meta_size);
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
            }
        }

        p_ctx->enc_total_bytes_received[enc_id] += rx_size - meta_size;

        // The first packet is the sequence head packet and it will be read before the first frame is sent
        if(p_ctx->num_packets_received[enc_id] != 0)
        {
            pts = p_out_pkt->pts;
            ni_pts_dequeue(p_ctx->enc_pts_queue[enc_id], &dts);
            ni_log(NI_LOG_DEBUG, "PTS: %d, DTS: %d\n", pts, dts);
        }

        (p_ctx->num_packets_received[enc_id])++;

        ni_log(NI_LOG_DEBUG, "Got:   Packets= %u\n", p_ctx->num_packets_received[enc_id]);
    } else if (rx_size != 0)
    {
        ni_log(NI_LOG_ERROR, "Error: received %d bytes, <= metadata size %d!\n",
                rx_size, meta_size);
        return NI_TEST_RETCODE_FAILURE;
    } else if (end_flag)
    {
        if (SESSION_RUN_STATE_SEQ_CHANGE_DRAINING ==
            p_enc_ctx->session_run_state)
        {
            // after sequence change completes, reset codec state
            ni_log(NI_LOG_INFO, "encoder_receive_data: sequence "
                   "change completed, return SEQ_CHANGE_DONE and will reopen "
                   "or reconfig codec!\n");
            rc = encoder_reinit_session(p_enc_ctx, p_in_data, p_out_data);
            ni_log(NI_LOG_TRACE, "encoder_receive_data: encoder_reinit_session ret %d\n", rc);
            if (rc == NI_RETCODE_SUCCESS)
            {
              return NI_TEST_RETCODE_SEQ_CHANGE_DONE;
            }
            else
            {
              return NI_TEST_RETCODE_FAILURE;
            }
        }

        if (p_enc_ctx->session_run_state == SESSION_RUN_STATE_FLUSHING)
        {
            // restart the encoder after sending flushing command
            rc = ni_device_session_restart(p_enc_ctx,
                                           p_in_data->data.frame.video_width,
                                           p_in_data->data.frame.video_height,
                                           NI_DEVICE_TYPE_ENCODER);
            ni_log(NI_LOG_INFO, "ni_device_session_restart() ret %d\n", rc);
            if (rc == NI_RETCODE_SUCCESS)
            {
              // need to reset the packet end_of_stream and run state
              p_out_pkt->end_of_stream = 0;
              p_enc_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
              return NI_TEST_RETCODE_EAGAIN;
            }
            else
            {
              return NI_TEST_RETCODE_FAILURE;
            }
        }

        if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1)
        {
            rc = write_av1_ivf_trailer(p_ctx, p_out_pkt, p_enc_ctx->meta_size, p_file);
            ni_packet_buffer_free_av1(p_out_pkt);
            if (rc < 0)
            {
                return NI_TEST_RETCODE_FAILURE;
            }
        }
        ni_log(NI_LOG_INFO, "Encoder Receiving done.\n");
        return NI_TEST_RETCODE_END_OF_STREAM;
    } else if (p_api_param->low_delay_mode && p_enc_ctx->frame_num >= p_enc_ctx->pkt_num) {
        ni_log(NI_LOG_DEBUG, "low delay mode and NO pkt, keep reading ..\n");
        ni_usleep(200);
        goto receive_data;
    } else { //rx_size == 0 && !end_flag
        ni_log(NI_LOG_DEBUG, "no packet received from encoder, return EAGAIN and retry\n");
        return NI_TEST_RETCODE_EAGAIN;
    }

    ni_log(NI_LOG_DEBUG, "encoder_receive_data: success\n");

    return NI_TEST_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  encoder session close
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_close_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data)
{
    int ret = 0;
    ni_device_session_close(p_enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);

    ni_log(NI_LOG_DEBUG, "encoder_close_session - close encoder blk_io_handle %d device_handle %d\n", p_enc_ctx->blk_io_handle, p_enc_ctx->device_handle);
#ifdef _WIN32
    ni_device_close(p_enc_ctx->device_handle);
#elif __linux__
    ni_device_close(p_enc_ctx->device_handle);
    ni_device_close(p_enc_ctx->blk_io_handle);
#endif

    if (p_enc_ctx->codec_format == NI_CODEC_FORMAT_AV1 &&
        p_out_data->data.packet.av1_buffer_index)
    {
        ni_packet_buffer_free_av1(&(p_out_data->data.packet));
    }
    ni_frame_buffer_free(&(p_in_data->data.frame));
    ni_packet_buffer_free(&(p_out_data->data.packet));
    return ret;
}

int encoder_sequence_change(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data,
                         int width, int height, ni_pix_fmt_t pix_fmt)
{
  ni_retcode_t ret = 0;
  int bit_depth;
  int bit_depth_factor;

  ni_log(NI_LOG_DEBUG, "XCoder encode sequence change (reconfig): session_run_state %d\n", p_enc_ctx->session_run_state);

  switch (pix_fmt)
  {
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_NV12:
      bit_depth = 8;
      bit_depth_factor = 1;
      break;
    case NI_PIX_FMT_YUV420P10LE:
    case NI_PIX_FMT_P010LE:
      bit_depth = 10;
      bit_depth_factor = 2;
      break;
    default:
      bit_depth = 8;
      bit_depth_factor = 1;
      break;
  }

  ret = ni_device_session_sequence_change(p_enc_ctx, width, height, bit_depth_factor, NI_DEVICE_TYPE_ENCODER);
  if (NI_RETCODE_SUCCESS != ret)
  {
    ni_log(NI_LOG_ERROR, "Failed to send Sequence Change to Encoder Session (status = %d)\n", ret);
    return ret;
  }

  // update session context
  p_enc_ctx->bit_depth_factor = bit_depth_factor;
  p_enc_ctx->src_bit_depth = bit_depth;
  // xcoder demo only support little endian (for 10-bit pixel format)
  p_enc_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
  p_enc_ctx->ready_to_close = 0;
  p_enc_ctx->frame_num = 0; // need to reset frame_num because pkt_num is set to 1 when header received after sequnce change, and low delay mode compares frame_num and pkt_num
  p_enc_ctx->pkt_num = 0; // also need to reset pkt_num because before header received, pkt_num > frame_num will also cause low delay mode stuck
  p_enc_ctx->pixel_format = p_in_data->data.frame.pixel_format;
  p_out_data->data.packet.end_of_stream = 0;
  p_in_data->data.frame.start_of_stream = 1;
  return ret;
}

int encoder_open(ni_demo_context_t *p_ctx,
                 ni_session_context_t *enc_ctx_list,
                 ni_xcoder_params_t *p_api_param_list,
                 int output_total, char p_enc_conf_params[][2048],
                 char p_enc_conf_gop[][2048],
                 ni_frame_t *p_ni_frame, int width, int height,
                 int fps_num, int fps_den, int bitrate,
                 int codec_format, ni_pix_fmt_t pix_fmt,
                 int aspect_ratio_idc, int xcoder_guid,
                 niFrameSurface1_t *p_surface, int multi_thread,
                 bool check_zerocopy)
{
    int i, ret = 0;
    int color_prim = NI_COL_PRI_UNSPECIFIED;
    int color_trc = NI_COL_TRC_UNSPECIFIED;
    int color_space = NI_COL_SPC_UNSPECIFIED;
    int sar_num = 0;
    int sar_den = 0;
    int video_full_range_flag = 0;

    if (p_ni_frame != NULL)
    {
        // open the encode session when the first frame arrives and the session
        // is not opened yet, with the source stream and user-configured encode
        // info both considered when constructing VUI in the stream headers
        color_prim = p_ni_frame->color_primaries;
        color_trc = p_ni_frame->color_trc;
        color_space = p_ni_frame->color_space;
        sar_num = p_ni_frame->sar_width;
        sar_den = p_ni_frame->sar_height;
        video_full_range_flag = p_ni_frame->video_full_range_flag;

        // calculate the source fps and set it as the default target fps, based
        // on the timing_info passed in from the decoded frame
        if (p_ni_frame->vui_num_units_in_tick && p_ni_frame->vui_time_scale)
        {
            if (NI_CODEC_FORMAT_H264 == p_ni_frame->src_codec)
            {
                if (0 == p_ni_frame->vui_time_scale % 2)
                {
                    fps_num = (int)(p_ni_frame->vui_time_scale / 2);
                    fps_den = (int)(p_ni_frame->vui_num_units_in_tick);
                } else
                {
                    fps_num = (int)(p_ni_frame->vui_time_scale);
                    fps_den = (int)(2 * p_ni_frame->vui_num_units_in_tick);
                }
            } else if (NI_CODEC_FORMAT_H265 == p_ni_frame->src_codec)
            {
                fps_num = p_ni_frame->vui_time_scale;
                fps_den = p_ni_frame->vui_num_units_in_tick;
            }
        }
    }

    for (i = 0; i < output_total; i++)
    {

        if(!p_ctx->enc_pts_queue[i])
        {
            p_ctx->enc_pts_queue[i] = (ni_pts_queue *)calloc(1, sizeof(ni_pts_queue));
            if(!p_ctx->enc_pts_queue[i])
            {
                ni_log(NI_LOG_ERROR, "Failed to allocate ni_pts_queue\n");
                return -1;
            }
        }

        // set up encoder p_config, using some info from source
        ret = ni_encoder_init_default_params(&p_api_param_list[i], fps_num,
                                             fps_den, bitrate, width, height,
                                             enc_ctx_list[i].codec_format);
        if (ret < 0)
        {
            ni_log(NI_LOG_ERROR, "Error encoder[%d] init default set up error\n", i);
            return -1;
        }

        // check and set ni_encoder_params from --xcoder-params
        // Note: the parameter setting has to be in this order so that user
        //       configured values can overwrite the source/default ones if
        //       desired.
        if (ni_retrieve_xcoder_params(p_enc_conf_params[i],
                                      &p_api_param_list[i], &enc_ctx_list[i]))
        {
            ni_log(NI_LOG_ERROR, "Error: encoder[%d] p_config parsing error\n", i);
            return -1;
        }

        if (ni_retrieve_xcoder_gop(p_enc_conf_gop[i],
                                   &p_api_param_list[i], &enc_ctx_list[i]))
        {
            ni_log(NI_LOG_ERROR, "Error: encoder[%d] p_config_gop parsing error\n", i);
            return -1;
        }

        // set async mode in enc_ctx if encoding is multi-threaded
        if (multi_thread)
        {
            ni_log(NI_LOG_INFO, "Encoder[%d] is multi-threaded, set async mode "
                   "in the session context!\n", i);
            enc_ctx_list[i].async_mode = 1;
            p_api_param_list[i].cfg_enc_params.enable_acq_limit = 1;
        }

        // check color primaries configuration
        if (color_prim != p_api_param_list[i].color_primaries &&
            NI_COL_PRI_UNSPECIFIED != p_api_param_list[i].color_primaries)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color primaries "
                   "%d to overwrite source %d\n",
                   i, p_api_param_list[i].color_primaries, color_prim);
            color_prim = p_api_param_list[i].color_primaries;
        }

        // check color transfer characteristic configuration
        if (color_trc != p_api_param_list[i].color_transfer_characteristic &&
            NI_COL_TRC_UNSPECIFIED != p_api_param_list[i].color_transfer_characteristic)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color trc %d to "
                   "overwrite source %d\n", i,
                   p_api_param_list[i].color_transfer_characteristic, color_trc);
            color_trc = p_api_param_list[i].color_transfer_characteristic;
        }

        // check color space configuration
        if (color_space != p_api_param_list[i].color_space &&
            NI_COL_SPC_UNSPECIFIED != p_api_param_list[i].color_space)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured color space %d "
                   "to overwrite source %d\n",
                   i, p_api_param_list[i].color_space, color_space);
            color_space = p_api_param_list[i].color_space;
        }

        // check video full range flag configuration
        if (p_api_param_list[i].video_full_range_flag >= 0)
        {
            ni_log(NI_LOG_DEBUG, "Encoder[%d] user-configured video full range "
                   "flag %d\n", i, p_api_param_list[i].video_full_range_flag);
            video_full_range_flag = p_api_param_list[i].video_full_range_flag;
        }

        // check aspect ratio indicator configuration
        if (aspect_ratio_idc > 0 && aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
        {
            sar_num = ni_h264_pixel_aspect_list[aspect_ratio_idc].num;
            sar_den = ni_h264_pixel_aspect_list[aspect_ratio_idc].den;
        } else if (p_api_param_list[i].sar_denom)
        {
            sar_num = p_api_param_list[i].sar_num;
            sar_den = p_api_param_list[i].sar_denom;
        }

        // check hwframe configuration
        if (p_surface != NULL)
        {
            //Items in this else condition could be improved by being handled in libxcoder
            enc_ctx_list[i].hw_action = NI_CODEC_HW_ENABLE;
            p_api_param_list[i].hwframes = 1;
            enc_ctx_list[i].sender_handle =
                (ni_device_handle_t)(int64_t)p_surface->device_handle;
            p_api_param_list[i].rootBufId = p_surface->ui16FrameIdx;
        }

        // VUI setting including color setting is done by specifying them in the
        // encoder config
        p_api_param_list[i].cfg_enc_params.colorDescPresent = 0;
        if ((color_prim != NI_COL_PRI_UNSPECIFIED) ||
            (color_space != NI_COL_SPC_UNSPECIFIED) ||
            (color_trc != NI_COL_TRC_UNSPECIFIED))
        {
            p_api_param_list[i].cfg_enc_params.colorDescPresent = 1;
        }
        p_api_param_list[i].cfg_enc_params.colorPrimaries = color_prim;
        p_api_param_list[i].cfg_enc_params.colorTrc = color_trc;
        p_api_param_list[i].cfg_enc_params.colorSpace = color_space;
        p_api_param_list[i].cfg_enc_params.videoFullRange = video_full_range_flag;
        p_api_param_list[i].cfg_enc_params.aspectRatioWidth = sar_num;
        p_api_param_list[i].cfg_enc_params.aspectRatioHeight = sar_den;

        ret = encoder_open_session(&enc_ctx_list[i], codec_format, xcoder_guid,
                                   &p_api_param_list[i], width, height,
                                   pix_fmt, check_zerocopy);
        if (ret != 0)
        {
            ni_log(NI_LOG_ERROR, "Error encoder[%d] open session failed!\n", i);
            return -1;
        }

        ni_init_pts_queue(p_ctx->enc_pts_queue[i]);
        p_ctx->pts[i] = 0;
        ni_prepare_pts_queue(p_ctx->enc_pts_queue[i], &p_api_param_list[i], p_ctx->pts[i]);
    }

    return ret;
}

int encoder_receive(ni_demo_context_t *p_ctx,
                    ni_session_context_t *enc_ctx_list,
                    ni_session_data_io_t *in_frame,
                    ni_session_data_io_t *pkt, int width, int height,
                    int output_total, FILE **pfs_list)
{
    int i, recycle_index;
    int recv_fin_flag = NI_TEST_RETCODE_SUCCESS;
    uint32_t prev_num_pkt[MAX_OUTPUT_FILES] = {0};
    ni_session_data_io_t *p_out_pkt = pkt;

    for (i = 0; i < output_total; i++)
    {
        if (enc_ctx_list[i].codec_format == NI_CODEC_FORMAT_AV1)
        {
            p_out_pkt = &pkt[i];
        }
        p_out_pkt->data.packet.end_of_stream = 0;
        prev_num_pkt[i] = p_ctx->num_packets_received[i];

        p_ctx->curr_enc_index = i;
        recv_fin_flag = encoder_receive_data(p_ctx, &enc_ctx_list[i], p_out_pkt, width,
                                             height, pfs_list[i], in_frame);

        recycle_index = p_out_pkt->data.packet.recycle_index;
        if (prev_num_pkt[i] < p_ctx->num_packets_received[i] &&
            enc_ctx_list[i].hw_action && recycle_index > 0 &&
            recycle_index < NI_GET_MAX_HWDESC_FRAME_INDEX(enc_ctx_list[i].ddr_config))
        {
            //encoder only returns valid recycle index
            //when there's something to recycle.
            //This range is suitable for all memory bins
            ni_hw_frame_unref(recycle_index);
        } else
        {
            ni_log(NI_LOG_DEBUG, "enc %d recv, prev_num_pkt %u "
                   "number_of_packets_list %u recycle_index %u\n", i,
                   prev_num_pkt[i], p_ctx->num_packets_received[i], recycle_index);
        }

        if (prev_num_pkt[i] < p_ctx->num_packets_received[i] &&
            enc_ctx_list[i].codec_format == NI_CODEC_FORMAT_AV1 &&
            p_ctx->num_packets_received[i] > 1)
        {
            // For low delay mode encoding, only one packet is received for one
            // frame sent. For non low delay mode, there will be multiple
            // packets received for one frame sent. So we need to read out all
            // the packets encoded.
            ni_xcoder_params_t *p_api_param =
                (ni_xcoder_params_t *)enc_ctx_list[i].p_session_config;
            if (!p_api_param->low_delay_mode)
            {
                i--;
                continue;
            }
        }

        p_ctx->enc_eos_received[i] = p_out_pkt->data.packet.end_of_stream;

        if (recv_fin_flag < 0)
        {
            ni_log(NI_LOG_DEBUG, "enc %d error, quit !\n", i);
            break;
        } else if (recv_fin_flag == NI_TEST_RETCODE_EAGAIN)
        {
            ni_usleep(100);
        }
    }

    return recv_fin_flag;
}

void encoder_stat_report_and_close(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx_list, int output_total)
{
    int i;
    int nb_recycled;
    uint64_t current_time;

    current_time = ni_gettime_ns();

    nb_recycled = scan_and_clean_hwdescriptors();

    for (i = 0; i < output_total; i++)
    {
        ni_log(NI_LOG_ERROR, "Encoder %d closing, Got:  Packets=%u  FPS=%.2f  Total bytes %llu\n",
               (int)i, p_ctx->num_packets_received[i],
               (float)p_enc_ctx_list[i].frame_num / (float)(current_time - p_ctx->start_time) * (float)1000000000,
               p_ctx->enc_total_bytes_received[i]);

        ni_device_session_close(&p_enc_ctx_list[i], 1, NI_DEVICE_TYPE_ENCODER);
    }

    ni_log(NI_LOG_DEBUG, "Cleanup recycled %d internal buffers\n",
           nb_recycled);
}

void *encoder_send_thread(void *args)
{
    enc_send_param_t *p_enc_send_param = args;
    ni_demo_context_t *p_ctx = p_enc_send_param->p_ctx;
    ni_session_context_t *p_enc_ctx_list = p_enc_send_param->p_enc_ctx;
    ni_test_frame_list_t *frame_list = p_enc_send_param->frame_list;
    ni_session_data_io_t *p_dec_frame = NULL;
    ni_session_data_io_t enc_in_frame = {0};
    ni_frame_t *p_ni_frame = NULL;
    niFrameSurface1_t *p_surface;
    int i, ret = 0;

    ni_log(NI_LOG_INFO, "%s start\n", __func__);

    for (;;)
    {
        while (frame_list_is_empty(frame_list) && !p_ctx->end_all_threads)
        {
            ni_usleep(100);
        }

        if (p_ctx->end_all_threads)
        {
            break;
        }

        p_dec_frame = &frame_list->frames[frame_list->head];
        p_ni_frame = &p_dec_frame->data.frame;

        for (i = 0; i < p_enc_send_param->output_total; i++)
        {
            p_ctx->curr_enc_index = i;
            ret = encoder_send_data2(p_ctx, &p_enc_ctx_list[i], p_dec_frame,
                                     &enc_in_frame,
                                     p_enc_send_param->output_width,
                                     p_enc_send_param->output_height);
            if (ret < 0)   //Error
            {
                if (p_enc_ctx_list[i].hw_action)
                {
                    //pre close cleanup will clear it out
                    p_surface = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                    ni_hw_frame_ref(p_surface);
                } else
                {
                    ni_decoder_frame_buffer_free(p_ni_frame);
                }
                frame_list_drain(frame_list);
                ni_log(NI_LOG_ERROR, "Error: encoder send frame failed\n");
                break;
            } else if (ret == NI_TEST_RETCODE_EAGAIN) {
                ni_usleep(100);
                i--;  //resend frame
                continue;
            } else if (p_enc_ctx_list[0].hw_action && !p_ctx->enc_eos_sent[i])
            {
                p_surface = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                ni_hw_frame_ref(p_surface);
            }
        }

        if (ret < 0)
        {
            break;
        } else if (p_enc_ctx_list[0].hw_action)
        {
            ni_frame_wipe_aux_data(p_ni_frame);   //reuse buffer
        } else
        {
            ni_decoder_frame_buffer_free(p_ni_frame);
        }
        frame_list_drain(frame_list);

        if (p_enc_send_param->p_ctx->enc_eos_sent[0])   // eos
        {
            ni_log(NI_LOG_INFO, "%s EOS sent\n", __func__);
            break;
        }
    }

    ni_frame_buffer_free(&enc_in_frame.data.frame);

    // Broadcast all codec threads to quit on exception such as NVMe IO
    if (ret < 0)
    {
        p_ctx->end_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "%s exit\n", __func__);
    return (void *)(long)ret;
}

void *encoder_receive_thread(void *args)
{
    enc_recv_param_t *p_enc_recv_param = args;
    ni_demo_context_t *p_ctx = p_enc_recv_param->p_ctx;
    ni_session_context_t *p_enc_ctx = p_enc_recv_param->p_enc_ctx;
    ni_session_data_io_t out_packet[MAX_OUTPUT_FILES] = {0};
    int i, ret = 0;
    int end_of_all_streams = 0;
    uint64_t current_time, previous_time = p_ctx->start_time;

    ni_log(NI_LOG_INFO, "encoder_receive_thread start\n");

    while (!end_of_all_streams && ret >= 0 && !p_ctx->end_all_threads)
    {
        ret = encoder_receive(p_ctx, p_enc_ctx,
                              &p_enc_recv_param->frame_list->frames[p_enc_recv_param->frame_list->head],
                              out_packet, p_enc_recv_param->output_width,
                              p_enc_recv_param->output_height, p_enc_recv_param->output_total,
                              p_enc_recv_param->p_file);
        for (i = 0; ret >= 0 && i < p_enc_recv_param->output_total; i++)
        {
            if (!p_ctx->enc_eos_received[i])
            {
                ni_log(NI_LOG_DEBUG, "enc %d continues to read!\n", i);
                end_of_all_streams = 0;
                break;
            } else
            {
                ni_log(NI_LOG_DEBUG, "enc %d eos !\n", i);
                end_of_all_streams = 1;
            }
        }

        current_time = ni_gettime_ns();
        if (current_time - previous_time >= (uint64_t)1000000000) {
            for (i = 0; i < p_enc_recv_param->output_total; i++)
            {
                ni_log(NI_LOG_INFO, "Encoder %d stats: received %u packets, fps %.2f, total bytes %u\n",
                        i, p_ctx->num_packets_received[i],
                        (float)p_enc_ctx[i].frame_num / (float)(current_time - p_ctx->start_time) * (float)1000000000,
                        p_ctx->enc_total_bytes_received[i]);
            }
            previous_time = current_time;
        }
    }

    for (i = 0; i < p_enc_recv_param->output_total; i++)
    {
        ni_packet_buffer_free(&out_packet[i].data.packet);
    }

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (ret < 0)
    {
        p_ctx->end_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "%s exit\n", __func__);
    return (void *)(long)ret;
}

void ni_init_pts_queue(ni_pts_queue *q)
{
    q->front = 0;
    q->rear = 0;
    q->size = 0;
}
int ni_pts_queue_empty(ni_pts_queue *q)
{
    return q->size == 0;
}
int ni_pts_queue_full(ni_pts_queue *q)
{
    return q->size == NI_MAX_PTS_QUEUE_SIZE;
}
int ni_pts_enqueue(ni_pts_queue *q, int value)
{
    if(ni_pts_queue_full(q))
    {
        ni_log(NI_LOG_ERROR, "Failed to enqueue, ni_pts_queue is full\n");
        return 0;
    }

    q->data[q->rear] = value;
    q->rear = (q->rear + 1) % NI_MAX_PTS_QUEUE_SIZE;
    ++q->size;
    return 1;
}
int ni_pts_dequeue(ni_pts_queue *q, int *value)
{
    if(ni_pts_queue_empty(q))
    {
        ni_log(NI_LOG_ERROR, "Failed to dequeue, ni_pts_queue is empty\n");
    }
    *value = q->data[q->front];
    q->front = (q->front + 1) % NI_MAX_PTS_QUEUE_SIZE;
    --q->size;
    return 1;
}

void ni_prepare_pts_queue(ni_pts_queue *q, ni_xcoder_params_t *enc_param, int pts_start)
{
    int dtsOffset = 0;

    switch (enc_param->cfg_enc_params.gop_preset_index)
    {
    /* dtsOffset is the max number of non-reference frames in a GOP
     * (derived from x264/5 algo) In case of IBBBP the first dts of the I
     * frame should be input_pts-(3*ticks_per_frame) In case of IBP the
     * first dts of the I frame should be input_pts-(1*ticks_per_frame)
     * thus we ensure pts>dts in all cases
     * */
    case 1:
    case 9:
    case 10:
        dtsOffset = 0;
        break;
    /* ts requires dts/pts of I fraem not same when there are B frames in
     * streams */
    case 3:
    case 4:
    case 7:
        dtsOffset = 1;
        break;
    case 5:
        dtsOffset = 2;
        break;
    case -1: // adaptive GOP
    case 8:
        dtsOffset = 3;
        break;
    default:
        dtsOffset = 7;
        break;
    }

    if (enc_param->cfg_enc_params.custom_gop_params.custom_gop_size)
    {
        int dts_offset;
        dtsOffset = 0;
        int has_b_frame = 0;
        for (int idx = 0;
             idx < enc_param->cfg_enc_params.custom_gop_params.custom_gop_size;
             idx++)
        {
            if (enc_param->cfg_enc_params.custom_gop_params.pic_param[idx].poc_offset < idx + 1)
            {
                dts_offset = (idx + 1) - enc_param->cfg_enc_params.custom_gop_params.pic_param[idx].poc_offset;
                if (dtsOffset < dts_offset)
                {
                    dtsOffset = dts_offset;
                }
            }

            if (!has_b_frame &&
                (enc_param->cfg_enc_params.custom_gop_params.pic_param[idx].pic_type == PIC_TYPE_B))
            {
                has_b_frame = 1;
            }
        }

        if (has_b_frame && !dtsOffset)
        {
            dtsOffset = 1;
        }
    }

    for (int i = 0; i < dtsOffset; ++i)
    {
        ni_pts_enqueue(q, i - dtsOffset + pts_start);
    }
}
