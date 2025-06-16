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
 *  \file   decode_utils.c
 *
 *  \brief  Video decoding utility functions shared by Libxcoder API examples
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_decode_utils.h"
#include "ni_log.h"
#include "ni_util.h"
#include "ni_defs.h"

static const uint8_t default_scaling_list_inter[] = {
    16, 16, 16, 16, 17, 18, 20, 24, 16, 16, 16, 17, 18, 20, 24, 25,
    16, 16, 17, 18, 20, 24, 25, 28, 16, 17, 18, 20, 24, 25, 28, 33,
    17, 18, 20, 24, 25, 28, 33, 41, 18, 20, 24, 25, 28, 33, 41, 54,
    20, 24, 25, 28, 33, 41, 54, 71, 24, 25, 28, 33, 41, 54, 71, 91};

static const uint8_t default_scaling_list_intra[] = {
    16, 16, 16, 16, 17, 18, 21, 24, 16, 16, 16, 16, 17, 19, 22, 25,
    16, 16, 17, 18, 20, 22, 25, 29, 16, 16, 18, 21, 24, 27, 31, 36,
    17, 17, 20, 24, 30, 35, 41, 47, 18, 19, 22, 27, 35, 44, 54, 65,
    21, 22, 25, 31, 41, 54, 70, 88, 24, 25, 29, 36, 47, 65, 88, 115};

static const uint8_t default_scaling4[2][16] = {
    {6, 13, 20, 28, 13, 20, 28, 32, 20, 28, 32, 37, 28, 32, 37, 42},
    {10, 14, 20, 24, 14, 20, 24, 27, 20, 24, 27, 30, 24, 27, 30, 34}};

static const uint8_t default_scaling8[2][64] = {
    {6,  10, 13, 16, 18, 23, 25, 27, 10, 11, 16, 18, 23, 25, 27, 29,
     13, 16, 18, 23, 25, 27, 29, 31, 16, 18, 23, 25, 27, 29, 31, 33,
     18, 23, 25, 27, 29, 31, 33, 36, 23, 25, 27, 29, 31, 33, 36, 38,
     25, 27, 29, 31, 33, 36, 38, 40, 27, 29, 31, 33, 36, 38, 40, 42},
    {9,  13, 15, 17, 19, 21, 22, 24, 13, 13, 17, 19, 21, 22, 24, 25,
     15, 17, 19, 21, 22, 24, 25, 27, 17, 19, 21, 22, 24, 25, 27, 28,
     19, 21, 22, 24, 25, 27, 28, 30, 21, 22, 24, 25, 27, 28, 30, 32,
     22, 24, 25, 27, 28, 30, 32, 33, 24, 25, 27, 28, 30, 32, 33, 35}};

static const uint8_t ni_zigzag_direct[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const uint8_t ni_zigzag_scan[16 + 1] = {
    0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4, 1 + 1 * 4, 2 + 0 * 4,
    3 + 0 * 4, 2 + 1 * 4, 1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
    3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

static const ni_rational_t vui_sar[] = {
    {0, 1},   {1, 1},    {12, 11}, {10, 11}, {16, 11}, {40, 33},
    {24, 11}, {20, 11},  {32, 11}, {80, 33}, {18, 11}, {15, 11},
    {64, 33}, {160, 99}, {4, 3},   {3, 2},   {2, 1},
};

static const uint8_t hevc_sub_width_c[] = {1, 2, 2, 1};

static const uint8_t hevc_sub_height_c[] = {1, 2, 1, 1};

static const uint8_t hevc_diag_scan4x4_x[16] = {
    0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 1, 2, 3, 2, 3, 3,
};

static const uint8_t hevc_diag_scan4x4_y[16] = {
    0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 3, 2, 3,
};

static const uint8_t hevc_diag_scan8x8_x[64] = {
    0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0,
    1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7, 2,
    3, 4, 5, 6, 7, 3, 4, 5, 6, 7, 4, 5, 6, 7, 5, 6, 7, 6, 7, 7,
};

static const uint8_t hevc_diag_scan8x8_y[64] = {
    0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 4, 3, 2, 1, 0, 5, 4, 3, 2, 1, 0, 6,
    5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 7,
    6, 5, 4, 3, 2, 7, 6, 5, 4, 3, 7, 6, 5, 4, 7, 6, 5, 7, 6, 7,
};

// find/copy next H.264 NAL unit (including start code) and its type;
// return NAL data size if found, 0 otherwise
uint64_t find_h264_next_nalu(ni_demo_context_t *p_ctx, uint8_t *p_dst, int *nal_type)
{
    uint64_t data_size;
    uint64_t i = p_ctx->curr_file_offset;

    if (i + 3 >= p_ctx->total_file_size)
    {
        ni_log(NI_LOG_DEBUG, "%s reaching end, curr_pos %llu, total input size %llu\n",
               __func__, (unsigned long long)p_ctx->curr_file_offset, (unsigned long long)p_ctx->total_file_size);

        if (p_ctx->loops_left > 1)
        {
            p_ctx->loops_left--;
            ni_log(NI_LOG_DEBUG, "input processed, %d loops left\n", p_ctx->loops_left);
            reset_data_buf_pos(p_ctx);
            i = p_ctx->curr_file_offset;
        } else {
            return 0;
        }
    }

    // search for start code 0x000001 or 0x00000001
    while ((p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x01) &&
           (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x00 || p_ctx->file_cache[i + 3] != 0x01))
    {
        i++;
        if (i + 3 > p_ctx->total_file_size)
        {
            return 0;
        }
    }

    // found start code, advance to NAL unit start depends on actual start code
    if (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
        p_ctx->file_cache[i + 2] != 0x01)
    {
        i++;
    }

    i += 3;

    // get the NAL type
    *nal_type = (p_ctx->file_cache[i] & 0x1f);

    // advance to the end of NAL, or stream
    while ((p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x00) &&
           (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x01))
    {
        i++;
        // if reaching the stream end
        if (i + 3 > p_ctx->total_file_size)
        {
            data_size = p_ctx->total_file_size - p_ctx->curr_file_offset;
            memcpy(p_dst, &p_ctx->file_cache[p_ctx->curr_file_offset], data_size);
            p_ctx->curr_file_offset = p_ctx->total_file_size;
            return data_size;
        }
    }

    data_size = i - p_ctx->curr_file_offset;
    memcpy(p_dst, &p_ctx->file_cache[p_ctx->curr_file_offset], data_size);
    p_ctx->curr_file_offset = i;
    return data_size;
}

// HRD parsing: return 0 if parsing ok, -1 otherwise
int h264_parse_hrd(ni_bitstream_reader_t *br, ni_h264_sps_t *sps)
{
    int cpb_count, i;

    cpb_count = (int)ni_bs_reader_get_ue(br) + 1;
    if (cpb_count > 32U)
    {
        ni_log(NI_LOG_ERROR, "parse_hrd invalid cpb_count %d\n", cpb_count);
        return -1;
    }

    ni_bs_reader_get_bits(br, 4);   // bit_rate_scale
    ni_bs_reader_get_bits(br, 4);   // cpb_size_scale
    for (i = 0; i < cpb_count; i++)
    {
        ni_bs_reader_get_ue(br);        // bit_rate_value_minus1
        ni_bs_reader_get_ue(br);        // cpb_size_value_minus1
        ni_bs_reader_get_bits(br, 1);   // cbr_flag
    }
    sps->initial_cpb_removal_delay_length =
        (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->cpb_removal_delay_length = (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->dpb_output_delay_length = (int)ni_bs_reader_get_bits(br, 5) + 1;
    sps->time_offset_length = ni_bs_reader_get_bits(br, 5);
    sps->cpb_cnt = cpb_count;
    return 0;
}

// VUI parsing: return 0 if parsing ok, -1 otherwise
int h264_parse_vui(ni_bitstream_reader_t *br, ni_h264_sps_t *sps)
{
    int ret = -1, aspect_ratio_info_present_flag;
    unsigned int aspect_ratio_idc;

    aspect_ratio_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (aspect_ratio_info_present_flag)
    {
        aspect_ratio_idc = ni_bs_reader_get_bits(br, 8);
        if (EXTENDED_SAR == aspect_ratio_idc)
        {
            sps->sar.num = ni_bs_reader_get_bits(br, 16);
            sps->sar.den = ni_bs_reader_get_bits(br, 16);
        } else if (aspect_ratio_idc < NI_NUM_PIXEL_ASPECT_RATIO)
        {
            sps->sar = ni_h264_pixel_aspect_list[aspect_ratio_idc];
        } else
        {
            ni_log(NI_LOG_ERROR, "parse_vui: illegal aspect ratio %u\n",
                           aspect_ratio_idc);
            goto end;
        }
    } else
    {
        sps->sar.num = sps->sar.den = 0;
    }

    if (ni_bs_reader_get_bits(br, 1))   // overscan_info_present_flag
    {
        ni_bs_reader_get_bits(br, 1);   // overscan_appropriate_flag
    }
    sps->video_signal_type_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->video_signal_type_present_flag)
    {
        ni_bs_reader_get_bits(br, 3);   // video_format
        sps->full_range =
            ni_bs_reader_get_bits(br, 1);   // video_full_range_flag

        sps->colour_description_present_flag = ni_bs_reader_get_bits(br, 1);
        if (sps->colour_description_present_flag)
        {
            sps->color_primaries = ni_bs_reader_get_bits(br, 8);
            sps->color_trc = ni_bs_reader_get_bits(br, 8);
            sps->colorspace = ni_bs_reader_get_bits(br, 8);
            if (sps->color_primaries < NI_COL_PRI_RESERVED0 ||
                sps->color_primaries >= NI_COL_PRI_NB)
            {
                sps->color_primaries = NI_COL_PRI_UNSPECIFIED;
            }
            if (sps->color_trc < NI_COL_TRC_RESERVED0 ||
                sps->color_trc >= NI_COL_TRC_NB)
            {
                sps->color_trc = NI_COL_TRC_UNSPECIFIED;
            }
            if (sps->colorspace < NI_COL_SPC_RGB ||
                sps->colorspace >= NI_COL_SPC_NB)
            {
                sps->colorspace = NI_COL_SPC_UNSPECIFIED;
            }
        }
    }

    if (ni_bs_reader_get_bits(br, 1))   // chroma_location_info_present_flag
    {
        ni_bs_reader_get_ue(br);   // chroma_sample_location_type_top_field
        ni_bs_reader_get_ue(br);   // chroma_sample_location_type_bottom_field
    }

    sps->timing_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->timing_info_present_flag)
    {
        unsigned num_units_in_tick = ni_bs_reader_get_bits(br, 32);
        unsigned time_scale = ni_bs_reader_get_bits(br, 32);
        if (!num_units_in_tick || !time_scale)
        {
            ni_log(NI_LOG_ERROR, "parse_vui: error num_units_in_tick/time_scale "
                           "(%u/%u)\n",
                           num_units_in_tick, time_scale);
            sps->timing_info_present_flag = 0;
        }
        sps->fixed_frame_rate_flag = ni_bs_reader_get_bits(br, 1);
    }

    sps->nal_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->nal_hrd_parameters_present_flag && h264_parse_hrd(br, sps) < 0)
    {
        ni_log(NI_LOG_ERROR, "parse_vui: nal_hrd_parameters_present and error "
                       "parse_hrd !\n");
        goto end;
    }

    sps->vcl_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->vcl_hrd_parameters_present_flag && h264_parse_hrd(br, sps) < 0)
    {
        ni_log(NI_LOG_ERROR, "parse_vui: vcl_hrd_parameters_present and error "
                       "parse_hrd !\n");
        goto end;
    }

    if (sps->nal_hrd_parameters_present_flag ||
        sps->vcl_hrd_parameters_present_flag)
    {
        ni_bs_reader_get_bits(br, 1);   // low_delay_hrd_flag
    }

    sps->pic_struct_present_flag = ni_bs_reader_get_bits(br, 1);

    sps->bitstream_restriction_flag = ni_bs_reader_get_bits(br, 1);
    if (sps->bitstream_restriction_flag)
    {
        ni_bs_reader_get_bits(br,
                              1);   // motion_vectors_over_pic_boundaries_flag
        ni_bs_reader_get_ue(br);    // max_bytes_per_pic_denom
        ni_bs_reader_get_ue(br);    // max_bits_per_mb_denom
        ni_bs_reader_get_ue(br);    // log2_max_mv_length_horizontal
        ni_bs_reader_get_ue(br);    // log2_max_mv_length_vertical
        sps->num_reorder_frames = ni_bs_reader_get_ue(br);
        sps->max_dec_frame_buffering = ni_bs_reader_get_ue(br);

        if (sps->num_reorder_frames > 16U)
        {
            ni_log(NI_LOG_ERROR, "parse_vui: clip illegal num_reorder_frames %d !\n",
                           sps->num_reorder_frames);
            sps->num_reorder_frames = 16;
            goto end;
        }
    }

    // everything is fine
    ret = 0;

end:
    return ret;
}

int h264_parse_scaling_list(ni_bitstream_reader_t *br, uint8_t *factors, int size,
                       const uint8_t *jvt_list, const uint8_t *fallback_list)
{
    int i, last = 8, next = 8;
    const uint8_t *scan = (size == 16 ? ni_zigzag_scan : ni_zigzag_direct);

    // matrix not written, we use the predicted one */
    if (!ni_bs_reader_get_bits(br, 1))
    {
        memcpy(factors, fallback_list, size * sizeof(uint8_t));
    } else
    {
        for (i = 0; i < size; i++)
        {
            if (next)
            {
                int v = ni_bs_reader_get_se(br);
                if (v < -128 || v > 127)
                {
                    ni_log(NI_LOG_ERROR, "delta scale %d is invalid\n", v);
                    return -1;
                }
                next = (last + v) & 0xff;
            }
            if (!i && !next)
            {   // matrix not written, we use the preset one
                memcpy(factors, jvt_list, size * sizeof(uint8_t));
                break;
            }
            last = (factors[scan[i]] = next ? next : last);
        }
    }
    return 0;
}

// SPS seq scaling matrices parsing: return 0 if parsing ok, -1 otherwise
int h264_parse_scaling_matrices(ni_bitstream_reader_t *br, const ni_h264_sps_t *sps,
                           uint8_t (*scaling_matrix4)[16], uint8_t (*scaling_matrix8)[64])
{
    int ret = 0;
    const uint8_t *fallback[4] = {default_scaling4[0], default_scaling4[1],
                                  default_scaling8[0], default_scaling8[1]};

    if (ni_bs_reader_get_bits(br, 1))   // scaling_matrix_present
    {
        // retrieve matrices
        ret |= h264_parse_scaling_list(br, scaling_matrix4[0], 16,
                                  default_scaling4[0], fallback[0]);   // Intra, Y
        ret |= h264_parse_scaling_list(br, scaling_matrix4[1], 16,
                                  default_scaling4[0], scaling_matrix4[0]);   // Intra, Cr
        ret |= h264_parse_scaling_list(br, scaling_matrix4[2], 16,
                                  default_scaling4[0], scaling_matrix4[1]);   // Intra, Cb
        ret |= h264_parse_scaling_list(br, scaling_matrix4[3], 16,
                                  default_scaling4[1], fallback[1]);   // Inter, Y
        ret |= h264_parse_scaling_list(br, scaling_matrix4[4], 16,
                                  default_scaling4[1], scaling_matrix4[3]);   // Inter, Cr
        ret |= h264_parse_scaling_list(br, scaling_matrix4[5], 16,
                                  default_scaling4[1], scaling_matrix4[4]);   // Inter, Cb
        ret |= h264_parse_scaling_list(br, scaling_matrix8[0], 64,
                                  default_scaling8[0], fallback[2]);   // Intra, Y
        ret |= h264_parse_scaling_list(br, scaling_matrix8[3], 64,
                                  default_scaling8[1], fallback[3]);   // Inter, Y
        if (sps->chroma_format_idc == 3)
        {
            ret |= h264_parse_scaling_list(br, scaling_matrix8[1], 64,
                                      default_scaling8[0], scaling_matrix8[0]); // Intra, Cr
            ret |= h264_parse_scaling_list(br, scaling_matrix8[4], 64,
                                      default_scaling8[1], scaling_matrix8[3]); // Inter, Cr
            ret |= h264_parse_scaling_list(br, scaling_matrix8[2], 64,
                                      default_scaling8[0], scaling_matrix8[1]); // Intra, Cb
            ret |= h264_parse_scaling_list(br, scaling_matrix8[5], 64,
                                      default_scaling8[1], scaling_matrix8[4]); // Inter, Cb
        }
        if (!ret)
        {
            ret = 1;
        }
    }
    return ret;
}

// SPS parsing: return 0 if parsing ok, -1 otherwise
int h264_parse_sps(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps)
{
    int ret = -1;
    ni_bitstream_reader_t br;
    int profile_idc, level_idc, constraint_set_flags = 0;
    uint32_t sps_id;
    int i, log2_max_frame_num_minus4;

    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    profile_idc = ni_bs_reader_get_bits(&br, 8);
    // from constraint_set0_flag to constraint_set5_flag
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 0;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 1;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 2;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 3;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 4;
    constraint_set_flags |= ni_bs_reader_get_bits(&br, 1) << 5;
    ni_bs_reader_skip_bits(&br, 2);   // reserved_zero_2bits
    level_idc = ni_bs_reader_get_bits(&br, 8);
    sps_id = ni_bs_reader_get_ue(&br);

    sps->sps_id = sps_id;
    sps->profile_idc = profile_idc;
    sps->constraint_set_flags = constraint_set_flags;
    sps->level_idc = level_idc;
    sps->full_range = -1;

    memset(sps->scaling_matrix4, 16, sizeof(sps->scaling_matrix4));
    memset(sps->scaling_matrix8, 16, sizeof(sps->scaling_matrix8));
    sps->scaling_matrix_present = 0;
    sps->colorspace = 2;   // NI_COL_SPC_UNSPECIFIED

    if (100 == profile_idc || 110 == profile_idc || 122 == profile_idc ||
        244 == profile_idc || 44 == profile_idc || 83 == profile_idc ||
        86 == profile_idc || 118 == profile_idc || 128 == profile_idc ||
        138 == profile_idc || 139 == profile_idc || 134 == profile_idc ||
        135 == profile_idc || 144 == profile_idc /* old High444 profile */)
    {
        sps->chroma_format_idc = ni_bs_reader_get_ue(&br);
        if (sps->chroma_format_idc > 3U)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: chroma_format_idc > 3 !\n");
            goto end;
        } else if (3 == sps->chroma_format_idc)
        {
            sps->residual_color_transform_flag = ni_bs_reader_get_bits(&br, 1);
            if (sps->residual_color_transform_flag)
            {
                ni_log(NI_LOG_ERROR, "parse_sps error: residual_color_transform not "
                               "supported !\n");
                goto end;
            }
        }
        sps->bit_depth_luma = (int)ni_bs_reader_get_ue(&br) + 8;
        sps->bit_depth_chroma = (int)ni_bs_reader_get_ue(&br) + 8;
        if (sps->bit_depth_luma != sps->bit_depth_chroma)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: different luma %d & chroma %d "
                           "bit depth !\n",
                           sps->bit_depth_luma, sps->bit_depth_chroma);
            goto end;
        }
        if (sps->bit_depth_luma < 8 || sps->bit_depth_luma > 12 ||
            sps->bit_depth_chroma < 8 || sps->bit_depth_chroma > 12)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: illegal luma/chroma bit depth "
                           "value (%d %d) !\n",
                           sps->bit_depth_luma, sps->bit_depth_chroma);
            goto end;
        }

        sps->transform_bypass = ni_bs_reader_get_bits(&br, 1);
        ret = h264_parse_scaling_matrices(&br, sps, sps->scaling_matrix4, sps->scaling_matrix8);
        if (ret < 0)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error scaling matrices parse failed !\n");
            goto end;
        }
        sps->scaling_matrix_present |= ret;
    }   // profile_idc
    else
    {
        sps->chroma_format_idc = 1;
        sps->bit_depth_luma = 8;
        sps->bit_depth_chroma = 8;
    }

    log2_max_frame_num_minus4 = ni_bs_reader_get_ue(&br);
    if (log2_max_frame_num_minus4 < MIN_LOG2_MAX_FRAME_NUM - 4 ||
        log2_max_frame_num_minus4 > MAX_LOG2_MAX_FRAME_NUM - 4)
    {
        ni_log(NI_LOG_ERROR, "parse_sps error: log2_max_frame_num_minus4 %d out of "
                       "range (0-12)!\n",
                       log2_max_frame_num_minus4);
        goto end;
    }
    sps->log2_max_frame_num = log2_max_frame_num_minus4 + 4;

    sps->poc_type = ni_bs_reader_get_ue(&br);
    if (0 == sps->poc_type)
    {
        uint32_t v = ni_bs_reader_get_ue(&br);
        if (v > 12)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: log2_max_poc_lsb %u out of range! "
                           "\n",
                           v);
            goto end;
        }
        sps->log2_max_poc_lsb = (int)v + 4;
    } else if (1 == sps->poc_type)
    {
        sps->delta_pic_order_always_zero_flag = ni_bs_reader_get_bits(&br, 1);
        sps->offset_for_non_ref_pic = ni_bs_reader_get_se(&br);
        sps->offset_for_top_to_bottom_field = ni_bs_reader_get_se(&br);
        sps->poc_cycle_length = ni_bs_reader_get_ue(&br);
        if ((unsigned)sps->poc_cycle_length >= 256)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: poc_cycle_length %d out of range! "
                           "\n",
                           sps->poc_cycle_length);
            goto end;
        }
        for (i = 0; i < sps->poc_cycle_length; i++)
        {
            sps->offset_for_ref_frame[i] = ni_bs_reader_get_se(&br);
        }
    } else if (2 != sps->poc_type)
    {
        ni_log(NI_LOG_ERROR, "parse_sps error: illegal PIC type %d!\n",
                       sps->poc_type);
        goto end;
    }
    sps->ref_frame_count = ni_bs_reader_get_ue(&br);
    sps->gaps_in_frame_num_allowed_flag = ni_bs_reader_get_bits(&br, 1);
    sps->mb_width = (int)ni_bs_reader_get_ue(&br) + 1;
    sps->mb_height = (int)ni_bs_reader_get_ue(&br) + 1;

    sps->frame_mbs_only_flag = ni_bs_reader_get_bits(&br, 1);
    sps->mb_height *= 2 - sps->frame_mbs_only_flag;

    if (!sps->frame_mbs_only_flag)
    {
        sps->mb_aff = ni_bs_reader_get_bits(&br, 1);
    } else
    {
        sps->mb_aff = 0;
    }

    sps->direct_8x8_inference_flag = ni_bs_reader_get_bits(&br, 1);

    sps->crop = ni_bs_reader_get_bits(&br, 1);
    if (sps->crop)
    {
        unsigned int crop_left = ni_bs_reader_get_ue(&br);
        unsigned int crop_right = ni_bs_reader_get_ue(&br);
        unsigned int crop_top = ni_bs_reader_get_ue(&br);
        unsigned int crop_bottom = ni_bs_reader_get_ue(&br);

        // no range checking
        int vsub = (sps->chroma_format_idc == 1) ? 1 : 0;
        int hsub =
            (sps->chroma_format_idc == 1 || sps->chroma_format_idc == 2) ? 1 :
                                                                           0;
        int step_x = 1 << hsub;
        int step_y = (2 - sps->frame_mbs_only_flag) << vsub;

        sps->crop_left = crop_left * step_x;
        sps->crop_right = crop_right * step_x;
        sps->crop_top = crop_top * step_y;
        sps->crop_bottom = crop_bottom * step_y;
    } else
    {
        sps->crop_left = sps->crop_right = sps->crop_top = sps->crop_bottom =
            sps->crop = 0;
    }

    // deduce real width/heigh
    sps->width = (int)(16 * sps->mb_width - sps->crop_left - sps->crop_right);
    sps->height = (int)(16 * sps->mb_height - sps->crop_top - sps->crop_bottom);

    sps->vui_parameters_present_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->vui_parameters_present_flag)
    {
        int ret1 = h264_parse_vui(&br, sps);
        if (ret1 < 0)
        {
            ni_log(NI_LOG_ERROR, "parse_sps error: parse_vui failed %d!\n", ret);
            goto end;
        }
    }

    // everything is fine
    ret = 0;

end:

    return ret;
}

int h264_parse_sei(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps,
                   int *sei_type, int *is_interlaced)
{
    ni_bitstream_reader_t br;
    *is_interlaced = 0;
    int ret = -1, dummy;
    int cpb_dpb_delays_present_flag = (sps->nal_hrd_parameters_present_flag ||
                                       sps->vcl_hrd_parameters_present_flag);
    //pic_struct_present_flag

    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);
    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    while (ni_bs_reader_get_bits_left(&br) > 16)
    {
        int next, size = 0;
        unsigned type = 0, tmp;

        do
        {
            if (ni_bs_reader_get_bits_left(&br) < 8)
            {
                ni_log(NI_LOG_ERROR, "parse_sei type parse error !\n");
                goto end;
            }
            tmp = ni_bs_reader_get_bits(&br, 8);
            type += tmp;
        } while (tmp == 0xFF);

        *sei_type = (int)type;
        do
        {
            if (ni_bs_reader_get_bits_left(&br) < 8)
            {
                ni_log(NI_LOG_ERROR, "parse_sei type %u size parse error !\n", type);
                goto end;
            }
            tmp = ni_bs_reader_get_bits(&br, 8);
            size += (int)tmp;
        } while (tmp == 0xFF);

        if (size > ni_bs_reader_get_bits_left(&br) / 8)
        {
            ni_log(NI_LOG_DEBUG, "parse_sei SEI type %u size %d truncated at %d\n",
                           type, size, ni_bs_reader_get_bits_left(&br));
            goto end;
        }
        next = ni_bs_reader_bits_count(&br) + 8 * size;

        switch (type)
        {
            case NI_H264_SEI_TYPE_PIC_TIMING:
                if (cpb_dpb_delays_present_flag)
                {
                    ni_bs_reader_get_bits(&br, sps->cpb_removal_delay_length);
                    ni_bs_reader_get_bits(&br, sps->dpb_output_delay_length);
                }
                if (sps->pic_struct_present_flag)
                {
                    dummy = ni_bs_reader_get_bits(&br, 4);
                    if (dummy < NI_H264_SEI_PIC_STRUCT_FRAME ||
                        dummy > NI_H264_SEI_PIC_STRUCT_FRAME_TRIPLING)
                    {
                        ni_log(NI_LOG_DEBUG,
                            "parse_sei pic_timing SEI invalid pic_struct: "
                            "%d\n",
                            dummy);
                        goto end;
                    }
                    if (dummy > NI_H264_SEI_PIC_STRUCT_FRAME)
                    {
                        *is_interlaced = 1;
                    }
                    goto success;
                }
                break;
            default:
                // skip all other SEI types
                ;
        }
        ni_bs_reader_skip_bits(&br, next - ni_bs_reader_bits_count(&br));
    }   // while in SEI

success:
    ret = 0;

end:
    return ret;
}

// probe h.264 stream info; return 0 if stream can be decoded, -1 otherwise
int probe_h264_stream_info(ni_demo_context_t *p_ctx, ni_h264_sps_t *sps)
{
    int ret = -1;
    uint8_t *buf = NULL;
    uint8_t *p_buf;
    uint32_t nal_size, ep3_removed = 0, vcl_nal_count = 0;
    int nal_type = -1, sei_type = -1;
    int sps_parsed = 0, is_interlaced = 0;

    if (NULL == (buf = calloc(1, NI_MAX_TX_SZ)))
    {
        ni_log(NI_LOG_ERROR, "Error probe_h264_stream_info: allocate stream buf\n");
        goto end;
    }

    reset_data_buf_pos(p_ctx);
    // probe at most 100 VCL before stops
    while ((!sps_parsed || !is_interlaced) && vcl_nal_count < 100 &&
           (nal_size = find_h264_next_nalu(p_ctx, buf, &nal_type)) > 0)
    {
        ni_log(NI_LOG_DEBUG, "nal %d  nal_size %d\n", nal_type, nal_size);
        p_buf = buf;

        // skip the start code
        while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
               nal_size > 3)
        {
            p_buf++;
            nal_size--;
        }
        if (nal_size <= 3)
        {
            ni_log(NI_LOG_ERROR, "Error probe_h264_stream_info NAL has no header\n");
            continue;
        }

        p_buf += 3;
        nal_size -= 3;

        ep3_removed = ni_remove_emulation_prevent_bytes(p_buf, nal_size);
        nal_size -= ep3_removed;

        if (H264_NAL_SPS == nal_type && !sps_parsed)
        {
            if (vcl_nal_count > 0)
            {
                ni_log(NI_LOG_DEBUG,
                       "Warning: %s has %d slice NAL units ahead of SPS!\n",
                       __func__, vcl_nal_count);
            }

            if (h264_parse_sps(p_buf, nal_size, sps))
            {
                ni_log(NI_LOG_ERROR, "probe_h264_stream_info: parse_sps error\n");
                break;
            }
            sps_parsed = 1;
        } else if (H264_NAL_SEI == nal_type)
        {
            h264_parse_sei(p_buf, nal_size, sps, &sei_type, &is_interlaced);
        } else if (H264_NAL_SLICE == nal_type || H264_NAL_IDR_SLICE == nal_type)
        {
            vcl_nal_count++;
        }

        if (sps_parsed &&
            (sps->pic_struct_present_flag ||
             sps->nal_hrd_parameters_present_flag ||
             sps->vcl_hrd_parameters_present_flag) &&
            NI_H264_SEI_TYPE_PIC_TIMING == sei_type && is_interlaced)
        {
            ni_log(NI_LOG_ERROR,
                "probe_h264_stream_info interlaced NOT supported!\n");
            break;
        }
    }   // while for each NAL unit

    reset_data_buf_pos(p_ctx);

    ni_log(NI_LOG_DEBUG, "VCL NAL parsed: %d, SPS parsed: %s, is interlaced: %s\n",
                   vcl_nal_count, sps_parsed ? "Yes" : "No",
                   is_interlaced ? "Yes" : "No");
    if (sps_parsed && !is_interlaced)
    {
        ret = 0;
    } else
    {
        ni_log(NI_LOG_ERROR, "Input is either interlaced, or unable to determine, "
                       "probing failed.\n");
    }

    static const char csp[4][5] = {"Gray", "420", "422", "444"};
    ni_log(NI_LOG_DEBUG,
        "H.264 stream probed %d VCL NAL units, sps:%u "
        "profile:%d/%d poc %d ref:%d %dx%d [SAR: %d:%d] %s %s "
        "%" PRId32 "/%" PRId32 " %d bits max_reord:%d max_dec_buf:"
        "%d\n",
        vcl_nal_count, sps->sps_id, sps->profile_idc, sps->level_idc,
        sps->poc_type, sps->ref_frame_count, sps->width, sps->height,
        /*sps->crop_left, sps->crop_right, sps->crop_top, sps->crop_bottom,*/
        sps->sar.num, sps->sar.den,
        sps->vui_parameters_present_flag ? "VUI" : "no-VUI",
        csp[sps->chroma_format_idc],
        sps->timing_info_present_flag ? sps->num_units_in_tick : 0,
        sps->timing_info_present_flag ? sps->time_scale : 0,
        sps->bit_depth_luma,
        sps->bitstream_restriction_flag ? sps->num_reorder_frames : -1,
        sps->bitstream_restriction_flag ? sps->max_dec_frame_buffering : -1);

end:
    free(buf);
    buf = NULL;
    return ret;
}

// parse H.264 slice header to get frame_num; return 0 if success, -1 otherwise
int parse_h264_slice_header(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps,
                            int32_t *frame_num, unsigned int *first_mb_in_slice)
{
    ni_bitstream_reader_t br;
    uint8_t *p_buf = buf;
    unsigned int slice_type, pps_id;

    // skip the start code
    while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
           size_bytes > 3)
    {
        p_buf++;
        size_bytes--;
    }
    if (size_bytes <= 3)
    {
        ni_log(NI_LOG_ERROR, "Error parse_h264_slice_header slice has no header\n");
        return -1;
    }

    p_buf += 3;
    size_bytes -= 3;

    ni_bitstream_reader_init(&br, p_buf, 8 * size_bytes);

    // skip NAL header
    ni_bs_reader_skip_bits(&br, 8);

    *first_mb_in_slice = ni_bs_reader_get_ue(&br);
    slice_type = ni_bs_reader_get_ue(&br);
    if (slice_type > 9)
    {
        ni_log(NI_LOG_ERROR, "parse_h264_slice_header error: slice type %u too "
                       "large at %u\n",
                       slice_type, *first_mb_in_slice);
        return -1;
    }
    pps_id = ni_bs_reader_get_ue(&br);
    *frame_num = ni_bs_reader_get_bits(&br, sps->log2_max_frame_num);

    ni_log(NI_LOG_DEBUG, "parse_h264_slice_header slice type %u frame_num %d "
                   "pps_id %u size %d first_mb %u\n",
                   slice_type, *frame_num, pps_id, size_bytes,
                   *first_mb_in_slice);

    return 0;
}

/**
 * find/copy next H.265 NAL unit (including start code) and its type;
 * return NAL data size if found, 0 otherwise
*/
uint64_t find_h265_next_nalu(ni_demo_context_t *p_ctx, uint8_t *p_dst, int *nal_type)
{
    uint64_t data_size;
    uint64_t i = p_ctx->curr_file_offset;

    if (i + 3 >= p_ctx->total_file_size)
    {
        ni_log(NI_LOG_DEBUG, "%s reaching end, curr_pos %llu, total input size %llu\n",
               __func__, (unsigned long long)p_ctx->curr_file_offset, (unsigned long long)p_ctx->total_file_size);

        if (p_ctx->loops_left > 1)
        {
            p_ctx->loops_left--;
            ni_log(NI_LOG_DEBUG, "input processed, %d loops left\n", p_ctx->loops_left);
            reset_data_buf_pos(p_ctx);
            i = p_ctx->curr_file_offset;
        } else {
            return 0;
        }
    }

    // search for start code 0x000001 or 0x00000001
    while ((p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x01) &&
           (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x00 || p_ctx->file_cache[i + 3] != 0x01))
    {
        i++;
        if (i + 3 > p_ctx->total_file_size)
        {
            return 0;
        }
    }

    // found start code, advance to NAL unit start depends on actual start code
    if (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
        p_ctx->file_cache[i + 2] != 0x01)
    {
        i++;
    }

    i += 3;

    // get the NAL type
    *nal_type = (p_ctx->file_cache[i] & 0x7E) >> 1;

    // advance to the end of NAL, or stream
    while ((p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x00) &&
           (p_ctx->file_cache[i] != 0x00 || p_ctx->file_cache[i + 1] != 0x00 ||
            p_ctx->file_cache[i + 2] != 0x01))
    {
        i++;
        // if reaching the stream end
        if (i + 3 > p_ctx->total_file_size)
        {
            data_size = p_ctx->total_file_size - p_ctx->curr_file_offset;
            memcpy(p_dst, &p_ctx->file_cache[p_ctx->curr_file_offset], data_size);
            p_ctx->curr_file_offset = p_ctx->total_file_size;
            return data_size;
        }
    }

    data_size = i - p_ctx->curr_file_offset;
    memcpy(p_dst, &p_ctx->file_cache[p_ctx->curr_file_offset], data_size);
    p_ctx->curr_file_offset = i;
    return data_size;
}

void h265_decode_sublayer_hrd(ni_bitstream_reader_t *br, unsigned int nb_cpb,
                              int subpic_params_present)
{
    uint32_t i;

    for (i = 0; i < nb_cpb; i++)
    {
        ni_bs_reader_get_ue(br);   // bit_rate_value_minus1
        ni_bs_reader_get_ue(br);   // cpb_size_value_minus1

        if (subpic_params_present)
        {
            ni_bs_reader_get_ue(br);   // cpb_size_du_value_minus1
            ni_bs_reader_get_ue(br);   // bit_rate_du_value_minus1
        }
        ni_bs_reader_skip_bits(br, 1);   // cbr_flag
    }
}

int h265_decode_profile_tier_level(ni_bitstream_reader_t *br, PTLCommon *ptl)
{
    int i;

    if (ni_bs_reader_get_bits_left(br) < 2 + 1 + 5 + 32 + 4 + 43 + 1)
        return -1;

    ptl->profile_space = ni_bs_reader_get_bits(br, 2);
    ptl->tier_flag = ni_bs_reader_get_bits(br, 1);
    ptl->profile_idc = ni_bs_reader_get_bits(br, 5);

    for (i = 0; i < 32; i++)
    {
        ptl->profile_compatibility_flag[i] = ni_bs_reader_get_bits(br, 1);

        if (ptl->profile_idc == 0 && i > 0 &&
            ptl->profile_compatibility_flag[i])
            ptl->profile_idc = i;
    }
    ptl->progressive_source_flag = ni_bs_reader_get_bits(br, 1);
    ptl->interlaced_source_flag = ni_bs_reader_get_bits(br, 1);
    ptl->non_packed_constraint_flag = ni_bs_reader_get_bits(br, 1);
    ptl->frame_only_constraint_flag = ni_bs_reader_get_bits(br, 1);

#define check_profile_idc(idc)                                                 \
    ptl->profile_idc == (idc) || ptl->profile_compatibility_flag[idc]

    if (check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(6) ||
        check_profile_idc(7) || check_profile_idc(8) || check_profile_idc(9) ||
        check_profile_idc(10))
    {
        ptl->max_12bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_10bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_8bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_422chroma_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_420chroma_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->max_monochrome_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->intra_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->one_picture_only_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ptl->lower_bit_rate_constraint_flag = ni_bs_reader_get_bits(br, 1);

        if (check_profile_idc(5) || check_profile_idc(9) ||
            check_profile_idc(10))
        {
            ptl->max_14bit_constraint_flag = ni_bs_reader_get_bits(br, 1);
            ni_bs_reader_skip_bits(br, 33);   // XXX_reserved_zero_33bits[0..32]
        } else
        {
            ni_bs_reader_skip_bits(br, 34);   // XXX_reserved_zero_34bits[0..33]
        }
    } else if (check_profile_idc(2))
    {
        ni_bs_reader_skip_bits(br, 7);
        ptl->one_picture_only_constraint_flag = ni_bs_reader_get_bits(br, 1);
        ni_bs_reader_skip_bits(br, 35);   // XXX_reserved_zero_35bits[0..34]
    } else
    {
        ni_bs_reader_skip_bits(br, 43);   // XXX_reserved_zero_43bits[0..42]
    }

    if (check_profile_idc(1) || check_profile_idc(2) || check_profile_idc(3) ||
        check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(9))
        ptl->inbld_flag = ni_bs_reader_get_bits(br, 1);
    else
        ni_bs_reader_skip_bits(br, 1);
#undef check_profile_idc

    return 0;
}

int h265_parse_ptl(ni_bitstream_reader_t *br, PTL *ptl, int max_num_sub_layers)
{
    int i;
    if (h265_decode_profile_tier_level(br, &ptl->general_ptl) < 0 ||
        ni_bs_reader_get_bits_left(br) <
            8 + (8 * 2 * (max_num_sub_layers - 1 > 0)))
    {
        ni_log(NI_LOG_ERROR, "PTL information too short\n");
        return -1;
    }

    ptl->general_ptl.level_idc = ni_bs_reader_get_bits(br, 8);

    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        ptl->sub_layer_profile_present_flag[i] = ni_bs_reader_get_bits(br, 1);
        ptl->sub_layer_level_present_flag[i] = ni_bs_reader_get_bits(br, 1);
    }

    if (max_num_sub_layers - 1 > 0)
        for (i = max_num_sub_layers - 1; i < 8; i++)
            ni_bs_reader_skip_bits(br, 2);   // reserved_zero_2bits[i]
    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        if (ptl->sub_layer_profile_present_flag[i] &&
            h265_decode_profile_tier_level(br, &ptl->sub_layer_ptl[i]) < 0)
        {
            ni_log(NI_LOG_ERROR, "PTL information for sublayer %i too short\n",
                   i);
            return -1;
        }
        if (ptl->sub_layer_level_present_flag[i])
        {
            if (ni_bs_reader_get_bits_left(br) < 8)
            {
                ni_log(NI_LOG_ERROR,
                       "Not enough data for sublayer %i level_idc\n", i);
                return -1;
            } else
                ptl->sub_layer_ptl[i].level_idc = ni_bs_reader_get_bits(br, 8);
        }
    }

    return 0;
}

int h265_decode_hrd(ni_bitstream_reader_t *br, int common_inf_present, int max_sublayers)
{
    int nal_params_present = 0, vcl_params_present = 0;
    int subpic_params_present = 0;
    int i;

    if (common_inf_present)
    {
        nal_params_present = ni_bs_reader_get_bits(br, 1);
        vcl_params_present = ni_bs_reader_get_bits(br, 1);

        if (nal_params_present || vcl_params_present)
        {
            subpic_params_present = ni_bs_reader_get_bits(br, 1);

            if (subpic_params_present)
            {
                ni_bs_reader_skip_bits(br, 8);   // tick_divisor_minus2
                ni_bs_reader_skip_bits(
                    br, 5);   // du_cpb_removal_delay_increment_length_minus1
                ni_bs_reader_skip_bits(
                    br, 1);   // sub_pic_cpb_params_in_pic_timing_sei_flag
                ni_bs_reader_skip_bits(
                    br, 5);   // dpb_output_delay_du_length_minus1
            }

            ni_bs_reader_skip_bits(br, 4);   // bit_rate_scale
            ni_bs_reader_skip_bits(br, 4);   // cpb_size_scale

            if (subpic_params_present)
                ni_bs_reader_skip_bits(br, 4);   // cpb_size_du_scale

            ni_bs_reader_skip_bits(
                br, 5);   // initial_cpb_removal_delay_length_minus1
            ni_bs_reader_skip_bits(br,
                                   5);   // au_cpb_removal_delay_length_minus1
            ni_bs_reader_skip_bits(br, 5);   // dpb_output_delay_length_minus1
        }
    }

    for (i = 0; i < max_sublayers; i++)
    {
        int low_delay = 0;
        unsigned int nb_cpb = 1;
        int fixed_rate = ni_bs_reader_get_bits(br, 1);

        if (!fixed_rate)
            fixed_rate = ni_bs_reader_get_bits(br, 1);

        if (fixed_rate)
            ni_bs_reader_get_ue(br);   // elemental_duration_in_tc_minus1
        else
            low_delay = ni_bs_reader_get_bits(br, 1);

        if (!low_delay)
        {
            nb_cpb = ni_bs_reader_get_ue(br) + 1;
            if (nb_cpb < 1 || nb_cpb > 32)
            {
                ni_log(NI_LOG_ERROR, "nb_cpb %d invalid\n", nb_cpb);
                return -1;
            }
        }

        if (nal_params_present)
            h265_decode_sublayer_hrd(br, nb_cpb, subpic_params_present);
        if (vcl_params_present)
            h265_decode_sublayer_hrd(br, nb_cpb, subpic_params_present);
    }
    return 0;
}

void h265_set_default_scaling_list_data(ScalingList *sl)
{
    int matrixId;

    for (matrixId = 0; matrixId < 6; matrixId++)
    {
        // 4x4 default is 16
        memset(sl->sl[0][matrixId], 16, 16);
        sl->sl_dc[0][matrixId] = 16;   // default for 16x16
        sl->sl_dc[1][matrixId] = 16;   // default for 32x32
    }
    memcpy(sl->sl[1][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][5], default_scaling_list_inter, 64);
}

int h265_scaling_list_data(ni_bitstream_reader_t *br, ScalingList *sl, ni_h265_sps_t *sps)
{
    uint8_t scaling_list_pred_mode_flag;
    int32_t scaling_list_dc_coef[2][6];
    int size_id, matrix_id, pos;
    int i;

    for (size_id = 0; size_id < 4; size_id++)
        for (matrix_id = 0; matrix_id < 6;
             matrix_id += ((size_id == 3) ? 3 : 1))
        {
            scaling_list_pred_mode_flag = ni_bs_reader_get_bits(br, 1);
            if (!scaling_list_pred_mode_flag)
            {
                int delta = ni_bs_reader_get_ue(br);
                /* Only need to handle non-zero delta. Zero means default,
                 * which should already be in the arrays. */
                if (delta)
                {
                    // Copy from previous array.
                    delta *= (size_id == 3) ? 3 : 1;
                    if (matrix_id < delta)
                    {
                        ni_log(NI_LOG_ERROR,
                               "Invalid delta in scaling list data: %d.\n",
                               delta);
                        return -1;
                    }

                    memcpy(sl->sl[size_id][matrix_id],
                           sl->sl[size_id][matrix_id - delta],
                           size_id > 0 ? 64 : 16);
                    if (size_id > 1)
                        sl->sl_dc[size_id - 2][matrix_id] =
                            sl->sl_dc[size_id - 2][matrix_id - delta];
                }
            } else
            {
                int32_t next_coef, coef_num;
                int32_t scaling_list_delta_coef;

                next_coef = 8;
                coef_num = 1 << (4 + (size_id << 1));
                if (coef_num >= 64)
                    coef_num = 64;
                if (size_id > 1)
                {
                    scaling_list_dc_coef[size_id - 2][matrix_id] =
                        ni_bs_reader_get_se(br) + 8;
                    next_coef = scaling_list_dc_coef[size_id - 2][matrix_id];
                    sl->sl_dc[size_id - 2][matrix_id] = next_coef;
                }
                for (i = 0; i < coef_num; i++)
                {
                    if (size_id == 0)
                        pos =
                            4 * hevc_diag_scan4x4_y[i] + hevc_diag_scan4x4_x[i];
                    else
                        pos =
                            8 * hevc_diag_scan8x8_y[i] + hevc_diag_scan8x8_x[i];

                    scaling_list_delta_coef = ni_bs_reader_get_se(br);
                    next_coef =
                        (next_coef + 256U + scaling_list_delta_coef) % 256;
                    sl->sl[size_id][matrix_id][pos] = next_coef;
                }
            }
        }

    if (sps->chroma_format_idc == 3)
    {
        for (i = 0; i < 64; i++)
        {
            sl->sl[3][1][i] = sl->sl[2][1][i];
            sl->sl[3][2][i] = sl->sl[2][2][i];
            sl->sl[3][4][i] = sl->sl[2][4][i];
            sl->sl[3][5][i] = sl->sl[2][5][i];
        }
        sl->sl_dc[1][1] = sl->sl_dc[0][1];
        sl->sl_dc[1][2] = sl->sl_dc[0][2];
        sl->sl_dc[1][4] = sl->sl_dc[0][4];
        sl->sl_dc[1][5] = sl->sl_dc[0][5];
    }

    return 0;
}

int h265_decode_short_term_rps(ni_bitstream_reader_t *br, ShortTermRPS *rps,
                               const ni_h265_sps_t *sps, int is_slice_header)
{
    uint8_t rps_predict = 0;
    int32_t delta_poc;
    int k0 = 0;
    int k1 = 0;
    int32_t k = 0;
    int i;

    if (rps != sps->st_rps && sps->nb_st_rps)
        rps_predict = ni_bs_reader_get_bits(br, 1);

    if (rps_predict)
    {
        const ShortTermRPS *rps_ridx;
        int32_t delta_rps;
        int32_t abs_delta_rps;
        uint8_t use_delta_flag = 0;
        uint8_t delta_rps_sign;

        if (is_slice_header)
        {
            unsigned int delta_idx = ni_bs_reader_get_ue(br) + 1;
            if (delta_idx > sps->nb_st_rps)
            {
                ni_log(NI_LOG_ERROR,
                       "Invalid value of delta_idx in slice header RPS: %d > "
                       "%d.\n",
                       delta_idx, sps->nb_st_rps);
                return -1;
            }
            rps_ridx = &sps->st_rps[sps->nb_st_rps - delta_idx];
            rps->rps_idx_num_delta_pocs = rps_ridx->num_delta_pocs;
        } else
            rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];

        delta_rps_sign = ni_bs_reader_get_bits(br, 1);
        abs_delta_rps = (int)(ni_bs_reader_get_ue(br) + 1);
        if (abs_delta_rps < 1 || abs_delta_rps > 32768)
        {
            ni_log(NI_LOG_ERROR, "Invalid value of abs_delta_rps: %d\n",
                   abs_delta_rps);
            return -1;
        }
        delta_rps = (1 - (delta_rps_sign << 1)) * abs_delta_rps;
        for (i = 0; i <= rps_ridx->num_delta_pocs; i++)
        {
            int used = rps->used[k] = ni_bs_reader_get_bits(br, 1);

            if (!used)
                use_delta_flag = ni_bs_reader_get_bits(br, 1);

            if (used || use_delta_flag)
            {
                if (i < rps_ridx->num_delta_pocs)
                    delta_poc = delta_rps + rps_ridx->delta_poc[i];
                else
                    delta_poc = delta_rps;
                rps->delta_poc[k] = delta_poc;
                if (delta_poc < 0)
                    k0++;
                else
                    k1++;
                k++;
            }
        }

        if (k >= (sizeof(rps->used) / sizeof(rps->used[0])))
        {
            ni_log(NI_LOG_ERROR, "Invalid num_delta_pocs: %d\n", k);
            return -1;
        }

        rps->num_delta_pocs = k;
        rps->num_negative_pics = k0;
        // sort in increasing order (smallest first)
        if (rps->num_delta_pocs != 0)
        {
            int used, tmp;
            for (i = 1; i < rps->num_delta_pocs; i++)
            {
                delta_poc = rps->delta_poc[i];
                used = rps->used[i];
                for (k = i - 1; k >= 0; k--)
                {
                    tmp = rps->delta_poc[k];
                    if (delta_poc < tmp)
                    {
                        rps->delta_poc[k + 1] = tmp;
                        rps->used[k + 1] = rps->used[k];
                        rps->delta_poc[k] = delta_poc;
                        rps->used[k] = used;
                    }
                }
            }
        }
        if ((rps->num_negative_pics >> 1) != 0)
        {
            int used;
            k = rps->num_negative_pics - 1;
            // flip the negative values to largest first
            for (i = 0; i < (int)(rps->num_negative_pics >> 1); i++)
            {
                delta_poc = rps->delta_poc[i];
                used = rps->used[i];
                rps->delta_poc[i] = rps->delta_poc[k];
                rps->used[i] = rps->used[k];
                rps->delta_poc[k] = delta_poc;
                rps->used[k] = used;
                k--;
            }
        }
    } else
    {
        int prev, nb_positive_pics;
        rps->num_negative_pics = ni_bs_reader_get_ue(br);
        nb_positive_pics = ni_bs_reader_get_ue(br);

        if (rps->num_negative_pics >= HEVC_MAX_REFS ||
            nb_positive_pics >= HEVC_MAX_REFS)
        {
            ni_log(NI_LOG_ERROR, "Too many refs in a short term RPS.\n");
            return -1;
        }

        rps->num_delta_pocs = (int)(rps->num_negative_pics + nb_positive_pics);
        if (rps->num_delta_pocs)
        {
            prev = 0;
            for (i = 0; i < (int)rps->num_negative_pics; i++)
            {
                delta_poc = ni_bs_reader_get_ue(br) + 1;
                if (delta_poc < 1 || delta_poc > 32768)
                {
                    ni_log(NI_LOG_ERROR, "Invalid value of delta_poc: %d\n",
                           delta_poc);
                    return -1;
                }
                prev -= delta_poc;
                rps->delta_poc[i] = prev;
                rps->used[i] = ni_bs_reader_get_bits(br, 1);
            }
            prev = 0;
            for (i = 0; i < nb_positive_pics; i++)
            {
                delta_poc = ni_bs_reader_get_ue(br) + 1;
                if (delta_poc < 1 || delta_poc > 32768)
                {
                    ni_log(NI_LOG_ERROR, "Invalid value of delta_poc: %d\n",
                           delta_poc);
                    return -1;
                }
                prev += delta_poc;
                rps->delta_poc[rps->num_negative_pics + i] = prev;
                rps->used[rps->num_negative_pics + i] =
                    ni_bs_reader_get_bits(br, 1);
            }
        }
    }
    return 0;
}

int h265_decode_vui(ni_bitstream_reader_t *br, int apply_defdispwin, ni_h265_sps_t *sps)
{
    VUI backup_vui, *vui = &sps->vui;
    ni_bitstream_reader_t br_backup;
    int sar_present, alt = 0;

    sar_present = ni_bs_reader_get_bits(br, 1);
    if (sar_present)
    {
        uint8_t sar_idx = ni_bs_reader_get_bits(br, 8);
        if (sar_idx < (sizeof(vui_sar) / sizeof(vui_sar[0])))
            vui->sar = vui_sar[sar_idx];
        else if (sar_idx == 255)
        {
            vui->sar.num = ni_bs_reader_get_bits(br, 16);
            vui->sar.den = ni_bs_reader_get_bits(br, 16);
        } else
        {
            ni_log(NI_LOG_ERROR, "Unknown SAR Index: %u.\n", sar_idx);
        }
    }

    vui->overscan_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->overscan_info_present_flag)
        vui->overscan_appropriate_flag = ni_bs_reader_get_bits(br, 1);

    vui->video_signal_type_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->video_signal_type_present_flag)
    {
        vui->video_format = ni_bs_reader_get_bits(br, 3);
        vui->video_full_range_flag = ni_bs_reader_get_bits(br, 1);
        vui->colour_description_present_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->video_full_range_flag && sps->pix_fmt == NI_PIX_FMT_YUV420P)
            sps->pix_fmt = NI_PIX_FMT_YUV420P;
        if (vui->colour_description_present_flag)
        {
            vui->colour_primaries = ni_bs_reader_get_bits(br, 8);
            vui->transfer_characteristic = ni_bs_reader_get_bits(br, 8);
            vui->matrix_coeffs = ni_bs_reader_get_bits(br, 8);

            if (vui->colour_primaries >= NI_COL_PRI_NB)
            {
                vui->colour_primaries = NI_COL_PRI_UNSPECIFIED;
            }
            if (vui->transfer_characteristic >= NI_COL_TRC_NB)
            {
                vui->transfer_characteristic = NI_COL_TRC_UNSPECIFIED;
            }
            if (vui->matrix_coeffs >= NI_COL_SPC_NB)
            {
                vui->matrix_coeffs = NI_COL_SPC_UNSPECIFIED;
            }
            if (vui->matrix_coeffs == NI_COL_SPC_RGB)
            {
                if (sps->pix_fmt)
                {
                    ni_log(NI_LOG_ERROR,
                           "Invalid format, only support yuv420p\n");
                    return -1;
                }
            }
        }
    }

    vui->chroma_loc_info_present_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->chroma_loc_info_present_flag)
    {
        vui->chroma_sample_loc_type_top_field = ni_bs_reader_get_ue(br);
        vui->chroma_sample_loc_type_bottom_field = ni_bs_reader_get_ue(br);
    }

    vui->neutra_chroma_indication_flag = ni_bs_reader_get_bits(br, 1);
    vui->field_seq_flag = ni_bs_reader_get_bits(br, 1);
    vui->frame_field_info_present_flag = ni_bs_reader_get_bits(br, 1);

    // Backup context in case an alternate header is detected
    memcpy(&br_backup, br, sizeof(br_backup));
    memcpy(&backup_vui, vui, sizeof(backup_vui));
    vui->default_display_window_flag = ni_bs_reader_get_bits(br, 1);

    if (vui->default_display_window_flag)
    {
        int vert_mult = hevc_sub_height_c[sps->chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
        vui->def_disp_win.left_offset = ni_bs_reader_get_ue(br) * horiz_mult;
        vui->def_disp_win.right_offset = ni_bs_reader_get_ue(br) * horiz_mult;
        vui->def_disp_win.top_offset = ni_bs_reader_get_ue(br) * vert_mult;
        vui->def_disp_win.bottom_offset = ni_bs_reader_get_ue(br) * vert_mult;

        if (apply_defdispwin)
        {
            ni_log(NI_LOG_DEBUG,
                   "discarding vui default display window, "
                   "original values are l:%u r:%u t:%u b:%u\n",
                   vui->def_disp_win.left_offset,
                   vui->def_disp_win.right_offset, vui->def_disp_win.top_offset,
                   vui->def_disp_win.bottom_offset);

            vui->def_disp_win.left_offset = vui->def_disp_win.right_offset =
                vui->def_disp_win.top_offset = vui->def_disp_win.bottom_offset =
                    0;
        }
    }

timing_info:
    vui->vui_timing_info_present_flag = ni_bs_reader_get_bits(br, 1);

    if (vui->vui_timing_info_present_flag)
    {
        if (ni_bs_reader_get_bits_left(br) < 66 && !alt)
        {
            // The alternate syntax seem to have timing info located
            // at where def_disp_win is normally located
            ni_log(NI_LOG_INFO,
                   "Strange VUI timing information, retrying...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(br, &br_backup, sizeof(br_backup));
            alt = 1;
            goto timing_info;
        }
        vui->vui_num_units_in_tick = ni_bs_reader_get_bits(br, 32);
        vui->vui_time_scale = ni_bs_reader_get_bits(br, 32);
        if (alt)
        {
            ni_log(NI_LOG_INFO, "Retry got %u/%ufps\n", vui->vui_time_scale,
                   vui->vui_num_units_in_tick);
        }
        vui->vui_poc_proportional_to_timing_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->vui_poc_proportional_to_timing_flag)
            vui->vui_num_ticks_poc_diff_one_minus1 = ni_bs_reader_get_ue(br);
        vui->vui_hrd_parameters_present_flag = ni_bs_reader_get_bits(br, 1);
        if (vui->vui_hrd_parameters_present_flag)
            h265_decode_hrd(br, 1, sps->max_sub_layers);
    }

    vui->bitstream_restriction_flag = ni_bs_reader_get_bits(br, 1);
    if (vui->bitstream_restriction_flag)
    {
        if (ni_bs_reader_get_bits_left(br) < 8 && !alt)
        {
            ni_log(NI_LOG_INFO,
                   "Strange VUI bitstream restriction information, retrying"
                   " from timing information...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(br, &br_backup, sizeof(br_backup));
            alt = 1;
            goto timing_info;
        }
        vui->tiles_fixed_structure_flag = ni_bs_reader_get_bits(br, 1);
        vui->motion_vectors_over_pic_boundaries_flag =
            ni_bs_reader_get_bits(br, 1);
        vui->restricted_ref_pic_lists_flag = ni_bs_reader_get_bits(br, 1);
        vui->min_spatial_segmentation_idc = ni_bs_reader_get_ue(br);
        vui->max_bytes_per_pic_denom = ni_bs_reader_get_ue(br);
        vui->max_bits_per_min_cu_denom = ni_bs_reader_get_ue(br);
        vui->log2_max_mv_length_horizontal = ni_bs_reader_get_ue(br);
        vui->log2_max_mv_length_vertical = ni_bs_reader_get_ue(br);
    }

    if (ni_bs_reader_get_bits_left(br) < 1 && !alt)
    {
        ni_log(NI_LOG_INFO,
               "Overread in VUI, retrying from timing information...\n");
        memcpy(vui, &backup_vui, sizeof(backup_vui));
        memcpy(br, &br_backup, sizeof(br_backup));
        alt = 1;
        goto timing_info;
    }
    return 0;
}

int h265_parse_sps(ni_h265_sps_t *sps, uint8_t *buf, int size_bytes)
{
    ni_h265_window_t *ow;
    int ret = 0;
    int log2_diff_max_min_transform_block_size;
    int bit_depth_chroma, start, vui_present, sublayer_ordering_info;
    int i;

    ni_bitstream_reader_t br;
    uint32_t sps_id;
    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);

    ni_bs_reader_skip_bits(&br, 16);   // skip NAL header

    sps->vps_id = ni_bs_reader_get_bits(&br, 4);

    sps->max_sub_layers = (int)ni_bs_reader_get_bits(&br, 3) + 1;
    if (sps->max_sub_layers > HEVC_MAX_SUB_LAYERS)
    {
        ni_log(NI_LOG_ERROR, "sps_max_sub_layers out of range: %d\n",
               sps->max_sub_layers);
        return -1;
    }

    sps->temporal_id_nesting_flag = ni_bs_reader_get_bits(&br, 1);

    if ((ret = h265_parse_ptl(&br, &sps->ptl, sps->max_sub_layers)) < 0)
        return ret;

    sps_id = ni_bs_reader_get_ue(&br);
    if (sps_id >= HEVC_MAX_SPS_COUNT)
    {
        ni_log(NI_LOG_ERROR, "SPS id out of range: %d\n", sps_id);
        return -1;
    }

    sps->chroma_format_idc = ni_bs_reader_get_ue(&br);
    if (sps->chroma_format_idc > 3U)
    {
        ni_log(NI_LOG_ERROR, "chroma_format_idc %d is invalid\n",
               sps->chroma_format_idc);
        return -1;
    }

    if (sps->chroma_format_idc == 3)
        sps->separate_colour_plane_flag = ni_bs_reader_get_bits(&br, 1);

    if (sps->separate_colour_plane_flag)
        sps->chroma_format_idc = 0;

    sps->width = (int)ni_bs_reader_get_ue(&br);
    sps->height = (int)ni_bs_reader_get_ue(&br);

    if (ni_bs_reader_get_bits(&br, 1))
    {   // pic_conformance_flag
        int vert_mult = hevc_sub_height_c[sps->chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
        sps->pic_conf_win.left_offset = ni_bs_reader_get_ue(&br) * horiz_mult;
        sps->pic_conf_win.right_offset = ni_bs_reader_get_ue(&br) * horiz_mult;
        sps->pic_conf_win.top_offset = ni_bs_reader_get_ue(&br) * vert_mult;
        sps->pic_conf_win.bottom_offset = ni_bs_reader_get_ue(&br) * vert_mult;

        sps->output_window = sps->pic_conf_win;
    }

    sps->bit_depth = (int)(ni_bs_reader_get_ue(&br) + 8);
    bit_depth_chroma = (int)(ni_bs_reader_get_ue(&br) + 8);
    if (sps->chroma_format_idc && bit_depth_chroma != sps->bit_depth)
    {
        ni_log(NI_LOG_ERROR,
               "Luma bit depth (%d) is different from chroma bit depth (%d), "
               "this is unsupported.\n",
               sps->bit_depth, bit_depth_chroma);
        return -1;
    }
    sps->bit_depth_chroma = bit_depth_chroma;
    if (((sps->bit_depth != 8) && (sps->bit_depth != 10)) ||
        (sps->chroma_format_idc != 1))
    {
        ni_log(NI_LOG_ERROR,
               "only support 8bit/10bit yuv420p, bit_depth %d, "
               "chroma_format_idc %d\n",
               sps->bit_depth, sps->chroma_format_idc);
        return -1;
    }
    sps->pix_fmt = 0;
    sps->hshift[0] = sps->vshift[0] = 0;
    sps->hshift[2] = sps->hshift[1] = 1;
    sps->vshift[2] = sps->vshift[1] = 1;
    sps->pixel_shift = sps->bit_depth > 8;

    sps->log2_max_poc_lsb = ni_bs_reader_get_ue(&br) + 4;
    if (sps->log2_max_poc_lsb > 16)
    {
        ni_log(NI_LOG_ERROR,
               "log2_max_pic_order_cnt_lsb_minus4 out range: %d\n",
               sps->log2_max_poc_lsb - 4);
        return -1;
    }

    sublayer_ordering_info = ni_bs_reader_get_bits(&br, 1);
    start = sublayer_ordering_info ? 0 : sps->max_sub_layers - 1;
    for (i = start; i < sps->max_sub_layers; i++)
    {
        sps->temporal_layer[i].max_dec_pic_buffering =
            (int)(ni_bs_reader_get_ue(&br) + 1);
        sps->temporal_layer[i].num_reorder_pics = (int)ni_bs_reader_get_ue(&br);
        sps->temporal_layer[i].max_latency_increase =
            (int)(ni_bs_reader_get_ue(&br) - 1);
        if (sps->temporal_layer[i].num_reorder_pics >
            sps->temporal_layer[i].max_dec_pic_buffering - 1)
        {
            ni_log(NI_LOG_ERROR, "sps_max_num_reorder_pics out of range: %d\n",
                   sps->temporal_layer[i].num_reorder_pics);
            sps->temporal_layer[i].max_dec_pic_buffering =
                sps->temporal_layer[i].num_reorder_pics + 1;
        }
    }

    if (!sublayer_ordering_info)
    {
        for (i = 0; i < start; i++)
        {
            sps->temporal_layer[i].max_dec_pic_buffering =
                sps->temporal_layer[start].max_dec_pic_buffering;
            sps->temporal_layer[i].num_reorder_pics =
                sps->temporal_layer[start].num_reorder_pics;
            sps->temporal_layer[i].max_latency_increase =
                sps->temporal_layer[start].max_latency_increase;
        }
    }

    sps->log2_min_cb_size = ni_bs_reader_get_ue(&br) + 3;
    sps->log2_diff_max_min_coding_block_size = ni_bs_reader_get_ue(&br);
    sps->log2_min_tb_size = ni_bs_reader_get_ue(&br) + 2;
    log2_diff_max_min_transform_block_size = ni_bs_reader_get_ue(&br);
    sps->log2_max_trafo_size =
        log2_diff_max_min_transform_block_size + sps->log2_min_tb_size;

    if (sps->log2_min_cb_size < 3 || sps->log2_min_cb_size > 30)
    {
        ni_log(NI_LOG_ERROR, "Invalid value %d for log2_min_cb_size",
               sps->log2_min_cb_size);
        return -1;
    }

    if (sps->log2_diff_max_min_coding_block_size > 30)
    {
        ni_log(NI_LOG_ERROR,
               "Invalid value %d for log2_diff_max_min_coding_block_size",
               sps->log2_diff_max_min_coding_block_size);
        return -1;
    }

    if (sps->log2_min_tb_size >= sps->log2_min_cb_size ||
        sps->log2_min_tb_size < 2)
    {
        ni_log(NI_LOG_ERROR, "Invalid value for log2_min_tb_size");
        return -1;
    }

    if (log2_diff_max_min_transform_block_size < 0 ||
        log2_diff_max_min_transform_block_size > 30)
    {
        ni_log(NI_LOG_ERROR,
               "Invalid value %d for log2_diff_max_min_transform_block_size",
               log2_diff_max_min_transform_block_size);
        return -1;
    }

    sps->max_transform_hierarchy_depth_inter = ni_bs_reader_get_ue(&br);
    sps->max_transform_hierarchy_depth_intra = ni_bs_reader_get_ue(&br);

    sps->scaling_list_enable_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->scaling_list_enable_flag)
    {
        h265_set_default_scaling_list_data(&sps->scaling_list);

        if (ni_bs_reader_get_bits(&br, 1))
        {
            ret = h265_scaling_list_data(&br, &sps->scaling_list, sps);
            if (ret < 0)
                return ret;
        }
    }

    sps->amp_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    sps->sao_enabled = ni_bs_reader_get_bits(&br, 1);

    sps->pcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->pcm_enabled_flag)
    {
        sps->pcm.bit_depth = ni_bs_reader_get_bits(&br, 4) + 1;
        sps->pcm.bit_depth_chroma = ni_bs_reader_get_bits(&br, 4) + 1;
        sps->pcm.log2_min_pcm_cb_size = ni_bs_reader_get_ue(&br) + 3;
        sps->pcm.log2_max_pcm_cb_size =
            sps->pcm.log2_min_pcm_cb_size + ni_bs_reader_get_ue(&br);
        if ((sps->pcm.bit_depth > sps->bit_depth) ||
            (sps->pcm.bit_depth_chroma > sps->bit_depth))
        {
            ni_log(NI_LOG_ERROR,
                   "PCM bit depth (%d, %d) is greater than normal bit depth "
                   "(%d)\n",
                   sps->pcm.bit_depth, sps->pcm.bit_depth_chroma,
                   sps->bit_depth);
            return -1;
        }

        sps->pcm.loop_filter_disable_flag = ni_bs_reader_get_bits(&br, 1);
    }

    sps->nb_st_rps = ni_bs_reader_get_ue(&br);
    if (sps->nb_st_rps > HEVC_MAX_SHORT_TERM_REF_PIC_SETS)
    {
        ni_log(NI_LOG_ERROR, "Too many short term RPS: %d.\n", sps->nb_st_rps);
        return -1;
    }
    for (i = 0; i < (int)sps->nb_st_rps; i++)
    {
        if ((ret = h265_decode_short_term_rps(&br, &sps->st_rps[i], sps, 0)) <
            0)
            return ret;
    }

    sps->long_term_ref_pics_present_flag = ni_bs_reader_get_bits(&br, 1);
    if (sps->long_term_ref_pics_present_flag)
    {
        sps->num_long_term_ref_pics_sps = ni_bs_reader_get_ue(&br);
        if (sps->num_long_term_ref_pics_sps > HEVC_MAX_LONG_TERM_REF_PICS)
        {
            ni_log(NI_LOG_ERROR, "Too many long term ref pics: %d.\n",
                   sps->num_long_term_ref_pics_sps);
            return -1;
        }
        for (i = 0; i < sps->num_long_term_ref_pics_sps; i++)
        {
            sps->lt_ref_pic_poc_lsb_sps[i] =
                ni_bs_reader_get_bits(&br, (int)sps->log2_max_poc_lsb);
            sps->used_by_curr_pic_lt_sps_flag[i] =
                ni_bs_reader_get_bits(&br, 1);
        }
    }

    sps->sps_temporal_mvp_enabled_flag = ni_bs_reader_get_bits(&br, 1);
    sps->sps_strong_intra_smoothing_enable_flag = ni_bs_reader_get_bits(&br, 1);
    sps->vui.sar = (ni_rational_t){0, 1};
    vui_present = ni_bs_reader_get_bits(&br, 1);
    if (vui_present)
        h265_decode_vui(&br, 0, sps);

    if (ni_bs_reader_get_bits(&br, 1))
    {   // sps_extension_flag
        sps->sps_range_extension_flag = ni_bs_reader_get_bits(&br, 1);
        ni_bs_reader_skip_bits(
            &br, 7);   //sps_extension_7bits = ni_bs_reader_get_bits(br, 7);
        if (sps->sps_range_extension_flag)
        {
            sps->transform_skip_rotation_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            sps->transform_skip_context_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            sps->implicit_rdpcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);

            sps->explicit_rdpcm_enabled_flag = ni_bs_reader_get_bits(&br, 1);

            sps->extended_precision_processing_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->extended_precision_processing_flag)
                ni_log(
                    NI_LOG_INFO,
                    "extended_precision_processing_flag not yet implemented\n");

            sps->intra_smoothing_disabled_flag = ni_bs_reader_get_bits(&br, 1);
            sps->high_precision_offsets_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->high_precision_offsets_enabled_flag)
                ni_log(NI_LOG_INFO,
                       "high_precision_offsets_enabled_flag not yet "
                       "implemented\n");

            sps->persistent_rice_adaptation_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);

            sps->cabac_bypass_alignment_enabled_flag =
                ni_bs_reader_get_bits(&br, 1);
            if (sps->cabac_bypass_alignment_enabled_flag)
                ni_log(NI_LOG_INFO,
                       "cabac_bypass_alignment_enabled_flag not yet "
                       "implemented\n");
        }
    }

    ow = &sps->output_window;
    if (ow->left_offset >= INT32_MAX - ow->right_offset ||
        ow->top_offset >= INT32_MAX - ow->bottom_offset ||
        ow->left_offset + ow->right_offset >= (uint32_t)sps->width ||
        ow->top_offset + ow->bottom_offset >= (uint32_t)sps->height)
    {
        ni_log(NI_LOG_INFO, "Invalid cropping offsets: %u/%u/%u/%u\n",
               ow->left_offset, ow->right_offset, ow->top_offset,
               ow->bottom_offset);
        ni_log(NI_LOG_INFO, "Displaying the whole video surface.\n");
        memset(ow, 0, sizeof(*ow));
        memset(&sps->pic_conf_win, 0, sizeof(sps->pic_conf_win));
    }

    // Inferred parameters
    sps->log2_ctb_size =
        sps->log2_min_cb_size + sps->log2_diff_max_min_coding_block_size;
    sps->log2_min_pu_size = sps->log2_min_cb_size - 1;

    if (sps->log2_ctb_size > HEVC_MAX_LOG2_CTB_SIZE)
    {
        ni_log(NI_LOG_ERROR, "CTB size out of range: 2^%d\n",
               sps->log2_ctb_size);
        return -1;
    }
    if (sps->log2_ctb_size < 4)
    {
        ni_log(
            NI_LOG_ERROR,
            "log2_ctb_size %d differs from the bounds of any known profile\n",
            sps->log2_ctb_size);
        return -1;
    }

    sps->ctb_width =
        (sps->width + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_height =
        (sps->height + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_size = sps->ctb_width * sps->ctb_height;

    sps->min_cb_width = sps->width >> sps->log2_min_cb_size;
    sps->min_cb_height = sps->height >> sps->log2_min_cb_size;
    sps->min_tb_width = sps->width >> sps->log2_min_tb_size;
    sps->min_tb_height = sps->height >> sps->log2_min_tb_size;
    sps->min_pu_width = sps->width >> sps->log2_min_pu_size;
    sps->min_pu_height = sps->height >> sps->log2_min_pu_size;
    sps->tb_mask = (1 << (sps->log2_ctb_size - sps->log2_min_tb_size)) - 1;

    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);

    if ((sps->width & ((1U << sps->log2_min_cb_size) - 1)) ||
        (sps->height & ((1U << sps->log2_min_cb_size) - 1)))
    {
        ni_log(NI_LOG_ERROR, "Invalid coded frame dimensions.\n");
        return -1;
    }

    if (sps->max_transform_hierarchy_depth_inter >
        (int)(sps->log2_ctb_size - sps->log2_min_tb_size))
    {
        ni_log(NI_LOG_ERROR,
               "max_transform_hierarchy_depth_inter out of range: %d\n",
               sps->max_transform_hierarchy_depth_inter);
        return -1;
    }
    if (sps->max_transform_hierarchy_depth_intra >
        (int)(sps->log2_ctb_size - sps->log2_min_tb_size))
    {
        ni_log(NI_LOG_ERROR,
               "max_transform_hierarchy_depth_intra out of range: %d\n",
               sps->max_transform_hierarchy_depth_intra);
        return -1;
    }
    if ((sps->log2_max_trafo_size > sps->log2_ctb_size) &&
        (sps->log2_max_trafo_size > 5))
    {
        ni_log(NI_LOG_ERROR, "max transform block size out of range: %d\n",
               sps->log2_max_trafo_size);
        return -1;
    }
    if (ni_bs_reader_get_bits_left(&br) < 0)
    {
        ni_log(NI_LOG_ERROR, "Overread SPS by %d bits\n",
               -ni_bs_reader_get_bits_left(&br));
        return -1;
    }

    return 0;
}

// probe h.265 stream info; return 0 if stream can be decoded, -1 otherwise
int probe_h265_stream_info(ni_demo_context_t *p_ctx, ni_h265_sps_t *sps)
{
    int ret = -1;
    uint8_t *buf = NULL;
    uint8_t *p_buf;
    uint32_t nal_size, ep3_removed = 0, vcl_nal_count = 0;
    int nal_type = -1;
    int sps_parsed = 0;

    if (NULL == (buf = calloc(1, NI_MAX_TX_SZ)))
    {
        ni_log(NI_LOG_ERROR,
               "Error probe_h265_stream_info: allocate stream buf failed\n");
        goto end;
    }

    reset_data_buf_pos(p_ctx);
    // probe at most 100 VCL before stops
    while ((!sps_parsed) && vcl_nal_count < 100 &&
           (nal_size = find_h265_next_nalu(p_ctx, buf, &nal_type)) > 0)
    {
        p_buf = buf;

        // skip the start code
        while (!(p_buf[0] == 0x00 && p_buf[1] == 0x00 && p_buf[2] == 0x01) &&
               (nal_size > 3))
        {
            p_buf++;
            nal_size--;
        }
        if (nal_size <= 3)
        {
            ni_log(NI_LOG_ERROR,
                   "Error probe_h265_stream_info NAL has no header\n");
            continue;
        }

        p_buf += 3;   // skip start code
        nal_size -= 3;
        ep3_removed = ni_remove_emulation_prevent_bytes(p_buf, nal_size);
        nal_size -= ep3_removed;
        ni_log(NI_LOG_DEBUG, "nal %d  nal_size %d\n", nal_type, nal_size);

        if (HEVC_NAL_SPS == nal_type && !sps_parsed)
        {
            if (vcl_nal_count > 0)
            {
                ni_log(NI_LOG_INFO,
                       "Warning: %s has %d slice NAL units ahead of SPS!\n",
                       __func__, vcl_nal_count);
            }

            if (h265_parse_sps(sps, p_buf, nal_size))
            {
                ni_log(NI_LOG_ERROR,
                       "probe_h265_stream_info: parse_sps error\n");
                break;
            }
            sps_parsed = 1;
        } else if (nal_type < 32)
        {
            vcl_nal_count++;
        }
    }

    reset_data_buf_pos(p_ctx);
    if (sps_parsed)
    {
        ret = 0;
    } else
    {
        ni_log(NI_LOG_ERROR, "probing failed.\n");
    }

end:
    free(buf);
    buf = NULL;
    return ret;
}

uint64_t find_vp9_next_packet(ni_demo_context_t *p_ctx, uint8_t *p_dst, ni_vp9_header_info_t *vp9_info)
{
    uint64_t data_size;
    uint64_t i = p_ctx->curr_file_offset ? p_ctx->curr_file_offset : vp9_info->header_length;
    if (i + 12 >= p_ctx->total_file_size)
    {
        ni_log(NI_LOG_DEBUG, "%s reaching end, curr_pos %llu, total input size %llu\n",
               __func__, (unsigned long long)p_ctx->curr_file_offset, (unsigned long long)p_ctx->total_file_size);

        if (p_ctx->loops_left > 1)
        {
            p_ctx->loops_left--;
            ni_log(NI_LOG_DEBUG, "input processed, %d loops left\n", p_ctx->loops_left);
            reset_data_buf_pos(p_ctx);
            i = vp9_info->header_length;
        } else {
            return 0;
        }
    }
    /** packet structure:
     * bytes 0-3: size of frame in bytes (not including the 12-byte header)
     * bytes 4-11: 64-bit presentation timestamp
     * bytes 12.. frame data
     */
    data_size =
        ((p_ctx->file_cache[i]) + (p_ctx->file_cache[i + 1] << 8) +
         (p_ctx->file_cache[i + 2] << 16) + (p_ctx->file_cache[i + 3] << 24));
    ni_log(NI_LOG_DEBUG, "vp9 packet data_size %u\n", data_size);
    i += 12;

    if (i + data_size > p_ctx->total_file_size)
    {
        data_size = p_ctx->total_file_size - i;
        memcpy(p_dst, &p_ctx->file_cache[i], data_size);
        p_ctx->curr_file_offset = p_ctx->total_file_size;
        return data_size;
    }

    memcpy(p_dst, &p_ctx->file_cache[i], data_size);
    p_ctx->curr_file_offset = i + data_size;   // point to the start of data packet
    return data_size;
}

int vp9_parse_header(ni_vp9_header_info_t *vp9_info, uint8_t *buf, int size_bytes)
{
    ni_bitstream_reader_t br;
    ni_bitstream_reader_init(&br, buf, 8 * size_bytes);

    ni_bs_reader_skip_bits(&br, 32);   // skip signature
    ni_bs_reader_skip_bits(&br, 16);   // skip version

    vp9_info->header_length = ni_bs_reader_get_bits(&br, 8);
    vp9_info->header_length |= ni_bs_reader_get_bits(&br, 8) << 8;

    ni_bs_reader_skip_bits(&br, 32);   // skip codec fucc

    vp9_info->width = ni_bs_reader_get_bits(&br, 8);
    vp9_info->width |= ni_bs_reader_get_bits(&br, 8) << 8;

    vp9_info->height = ni_bs_reader_get_bits(&br, 8);
    vp9_info->height |= ni_bs_reader_get_bits(&br, 8) << 8;

    vp9_info->timebase.den = ni_bs_reader_get_bits(&br, 8);
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->timebase.den |= ni_bs_reader_get_bits(&br, 8) << 24;

    vp9_info->timebase.num = ni_bs_reader_get_bits(&br, 8);
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->timebase.num |= ni_bs_reader_get_bits(&br, 8) << 24;

    vp9_info->total_frames = ni_bs_reader_get_bits(&br, 8);
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 8;
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 16;
    vp9_info->total_frames |= ni_bs_reader_get_bits(&br, 8) << 24;

    if (vp9_info->header_length != 32)
    {
        ni_log(NI_LOG_ERROR, "Parse faled: header_length %d != 32\n",
               vp9_info->header_length);
        return -1;
    }
    ni_bs_reader_skip_bits(&br, 32);   // unused bytes
    // here we skip frame header(12 bytes) to get profile
    ni_bs_reader_skip_bits(&br, 8 * 12);
    if (ni_bs_reader_get_bits(&br, 2) != 0x2)   // frame marker
    {
        ni_log(NI_LOG_ERROR, "Invalid frame marker\n");
        return -1;
    }
    int profile = 0;
    profile = ni_bs_reader_get_bits(&br, 1);
    profile |= ni_bs_reader_get_bits(&br, 1) << 1;
    if ((profile != 0) && (profile != 2))
    {
        ni_log(
            NI_LOG_ERROR,
            "Only support profile0(yuv420,8bit) and profile2(yuv420, 10bit)\n");
        return -1;
    }
    vp9_info->profile = profile;
    return 0;
}

// probe vp9 stream info; return 0 if stream can be decoded, -1 otherwise
int probe_vp9_stream_info(ni_demo_context_t *p_ctx, ni_vp9_header_info_t *vp9_info)
{
    int ret = -1;
    uint8_t *buf = NULL;

    if (NULL == (buf = calloc(1, 64)))
    {
        ni_log(NI_LOG_ERROR,
               "Error probe_vp9_stream_info: allocate stream buf failed\n");
        goto end;
    }

    reset_data_buf_pos(p_ctx);
    uint32_t size_bytes = 64;
    if (32 + 12 + 1 >= p_ctx->total_file_size)
    {
        ni_log(NI_LOG_ERROR, "No frame data probed!\n");
        goto end;
    } else
    {
        if (size_bytes > p_ctx->total_file_size)
            size_bytes = p_ctx->total_file_size;
        memcpy(buf, &p_ctx->file_cache[p_ctx->curr_file_offset], size_bytes);
    }

    ret = vp9_parse_header(vp9_info, buf, size_bytes);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Failed to parse vp9 header info\n");
        goto end;
    }
    reset_data_buf_pos(p_ctx);
    // packets data starts after ivf file header
    p_ctx->curr_file_offset += vp9_info->header_length;

end:
    free(buf);
    buf = NULL;
    return ret;
}

/*!*****************************************************************************
 *  \brief  Send decoder input data
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int decoder_send_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx,
                               ni_session_data_io_t *p_in_data,
                               int input_video_width, int input_video_height,
                               void *stream_info)
{
    static uint8_t tmp_buf[NI_MAX_TX_SZ];
    uint8_t *tmp_buf_ptr = tmp_buf;
    int packet_size;
    uint32_t frame_pkt_size = 0, nal_size;
    int nal_type = -1;
    int tx_size = 0;
    uint32_t send_size = 0;
    int new_packet = 0;
    int32_t frame_num = -1, curr_frame_num;
    unsigned int first_mb_in_slice = 0;
    ni_packet_t *p_in_pkt = &(p_in_data->data.packet);

    ni_log(NI_LOG_DEBUG, "===> decoder_send_data <===\n");

    if (p_ctx->dec_eos_sent)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: ALL data (incl. eos) sent "
                       "already!\n");
        return NI_TEST_RETCODE_SUCCESS;
    }

    if (0 == p_in_pkt->data_len)
    {
        memset(p_in_pkt, 0, sizeof(ni_packet_t));

        if (NI_CODEC_FORMAT_H264 == p_dec_ctx->codec_format)
        {
            ni_h264_sps_t *sps;
            sps = (ni_h264_sps_t *)stream_info;
            // send whole encoded packet which ends with a slice NAL
            while ((nal_size = find_h264_next_nalu(p_ctx, tmp_buf_ptr, &nal_type)) > 0)
            {
                frame_pkt_size += nal_size;
                tmp_buf_ptr += nal_size;
                ni_log(NI_LOG_DEBUG, "%s nal %d  nal_size %d\n", __func__,
                       nal_type, nal_size);

                if (H264_NAL_SLICE == nal_type ||
                    H264_NAL_IDR_SLICE == nal_type)
                {
                    if (!parse_h264_slice_header(tmp_buf_ptr - nal_size,
                                                 nal_size, sps, &curr_frame_num,
                                                 &first_mb_in_slice))
                    {
                        if (-1 == frame_num)
                        {
                            // first slice, continue to check
                            frame_num = curr_frame_num;
                        } else if (curr_frame_num != frame_num ||
                                   0 == first_mb_in_slice)
                        {
                            // this slice has diff. frame_num or first_mb_in_slice addr is
                            // 0: not the same frame and return
                            rewind_data_buf_pos_by(p_ctx, nal_size);
                            frame_pkt_size -= nal_size;
                            break;
                        }
                        // this slice is in the same frame, so continue to check and see
                        // if there is more
                    } else
                    {
                        ni_log(NI_LOG_ERROR,
                               "decoder_send_data: parse_slice_header error "
                               "NAL type %d size %u, continue\n",
                               nal_type, nal_size);
                    }
                } else if (-1 != frame_num)
                {
                    // already got a slice and this is non-slice NAL: return
                    rewind_data_buf_pos_by(p_ctx, nal_size);
                    frame_pkt_size -= nal_size;
                    break;
                }
                // otherwise continue until a slice is found
            }   // while there is still NAL
        } else if (NI_CODEC_FORMAT_H265 == p_dec_ctx->codec_format)
        {
            while ((nal_size = find_h265_next_nalu(p_ctx, tmp_buf_ptr, &nal_type)) > 0)
            {
                frame_pkt_size += nal_size;
                tmp_buf_ptr += nal_size;
                ni_log(NI_LOG_DEBUG, "%s nal_type %d nal_size %d\n", __func__,
                       nal_type, nal_size);

                if (nal_type >= 0 && nal_type <= 23)   // vcl units
                {
                    ni_log(NI_LOG_DEBUG, "%s send vcl_nal %d nal_size %d\n",
                           __func__, nal_type, nal_size);
                    break;
                }
            }
        } else if (NI_CODEC_FORMAT_VP9 == p_dec_ctx->codec_format)
        {
            while ((packet_size = find_vp9_next_packet(p_ctx, tmp_buf_ptr, stream_info)) > 0)
            {
                frame_pkt_size += packet_size;
                ni_log(NI_LOG_DEBUG, "%s vp9 packet_size %d\n", __func__,
                       packet_size);
                break;
            }
        } else {
            ni_log(NI_LOG_ERROR, "Error: Unsupported codec format %u", p_dec_ctx->codec_format);
            return NI_TEST_RETCODE_FAILURE;
        }
        ni_log(NI_LOG_DEBUG, "decoder_send_data * frame_pkt_size %d\n",
                       frame_pkt_size);

        p_in_pkt->p_data = NULL;
        send_size = frame_pkt_size + p_dec_ctx->prev_size;
        if (send_size > 0)
        {
            ni_packet_buffer_alloc(p_in_pkt, (int)send_size);
        }
        p_in_pkt->data_len = send_size;
        new_packet = 1;
    } else
    {
        send_size = p_in_pkt->data_len;
    }

    p_in_pkt->start_of_stream = 0;
    if (!p_ctx->dec_sos_sent)
    {
        p_in_pkt->start_of_stream = 1;
        p_ctx->dec_sos_sent = 1;
    }
    p_in_pkt->end_of_stream = 0;
    p_in_pkt->video_width = input_video_width;
    p_in_pkt->video_height = input_video_height;

    if (send_size == 0)
    {
        if (p_ctx->curr_file_offset)
        {
            p_in_pkt->end_of_stream = 1;
            ni_log(NI_LOG_ERROR, "Sending eos\n");
        }
    } else
    {
        if (new_packet)
        {
            ni_packet_copy(p_in_pkt->p_data, tmp_buf, frame_pkt_size,
                           p_dec_ctx->p_leftover, &p_dec_ctx->prev_size);
        }
    }

    tx_size =
        ni_device_session_write(p_dec_ctx, p_in_data, NI_DEVICE_TYPE_DECODER);

    if (tx_size < 0)
    {
        // Error
        ni_log(NI_LOG_ERROR, "Error: sending data error. rc:%d\n", tx_size);
        return NI_TEST_RETCODE_FAILURE;
    } else if (tx_size == 0 && !p_dec_ctx->ready_to_close)
    {
        ni_log(NI_LOG_DEBUG, "0 byte sent this time, return EAGAIN to retry.\n");
        return NI_TEST_RETCODE_EAGAIN;
    }

    p_ctx->dec_total_bytes_sent += tx_size;

    if (p_dec_ctx->ready_to_close)
    {
        p_ctx->dec_eos_sent = 1;
    }

    if (tx_size > 0)
    {
        ni_log(NI_LOG_DEBUG, "decoder_send_data: reset packet_buffer.\n");
        ni_packet_buffer_free(p_in_pkt);
    }

    return NI_TEST_RETCODE_SUCCESS;
}

/*!*****************************************************************************
 *  \brief  Receive decoded output data from decoder
 *
 *  \param
 *
 *  \return 0: got YUV frame;  1: end-of-stream;  2: got nothing
 ******s************************************************************************/
int decoder_receive_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx,
                         ni_session_data_io_t *p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *p_file, int write_to_file,
                         int * p_rx_size)
{
    int rc = NI_RETCODE_FAILURE;
    int rx_size = 0;
    bool b_is_hwframe = p_dec_ctx->hw_action;
    ni_frame_t *p_out_frame = &(p_out_data->data.frame);
    ni_session_data_io_t hwdl_session_data = {0};
    int width, height;
    // In decoder session read function it will allocate the actual YUV
    // transfer size for the very first read. And the pixel format of session
    // context would be set as well. So it does not matter with the planar
    // format for the first call of this function.
    int is_planar = get_pixel_planar(p_dec_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_PLANAR;

    ni_log(NI_LOG_DEBUG,
           "===> decoder_receive_data hwframe %d pixel_format %d <===\n",
           b_is_hwframe, p_dec_ctx->pixel_format);

    if (p_ctx->dec_eos_received)
    {
        ni_log(NI_LOG_DEBUG, "decoder_receive_data eos received already, Done!\n");
        rc = NI_TEST_RETCODE_EAGAIN;
        goto end;
    }

    // prepare memory buffer for receiving decoded frame
    width = p_dec_ctx->actual_video_width > 0 ?
        (int)(p_dec_ctx->actual_video_width) :
        output_video_width;
    height = p_dec_ctx->active_video_height > 0 ?
        (int)(p_dec_ctx->active_video_height) :
        output_video_height;

    // allocate memory only after resolution is known (for buffer pool set up)
    int alloc_mem = (p_dec_ctx->active_video_width > 0 &&
                             p_dec_ctx->active_video_height > 0 ?
                         1 :
                         0);
    if (!b_is_hwframe)
    {
        rc = ni_decoder_frame_buffer_alloc(
            p_dec_ctx->dec_fme_buf_pool, &(p_out_data->data.frame), alloc_mem,
            width, height, p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264,
            p_dec_ctx->bit_depth_factor, is_planar);
        if (NI_RETCODE_SUCCESS != rc)
        {
            rc = NI_TEST_RETCODE_FAILURE;
            goto end;
        }
        rx_size = ni_device_session_read(p_dec_ctx, p_out_data,
                                         NI_DEVICE_TYPE_DECODER);
    } else
    {
        rc = ni_frame_buffer_alloc(
            &(p_out_data->data.frame), width, height,
            p_dec_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
            p_dec_ctx->bit_depth_factor,
            3 /*3 is max supported hwframe output count per frame*/, is_planar);
        if (NI_RETCODE_SUCCESS != rc)
        {
            rc = NI_TEST_RETCODE_FAILURE;
            goto end;
        }
        rx_size = ni_device_session_read_hwdesc(p_dec_ctx, p_out_data,
                                                NI_DEVICE_TYPE_DECODER);
    }

    // the actual pix_fmt is known and updated in ctx only after the first
    // frame is decoded, so check/update it here again to be used below
    is_planar = get_pixel_planar(p_dec_ctx->pixel_format) ==
        NI_PIXEL_PLANAR_FORMAT_PLANAR;

    if (rx_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: receiving data error. rc:%d\n", rx_size);
        if (!b_is_hwframe)
        {
            ni_decoder_frame_buffer_free(p_out_frame);
        } else
        {
            ni_frame_buffer_free(p_out_frame);
        }
        rc = NI_TEST_RETCODE_FAILURE;
        goto end;
    } else if (rx_size > 0)
    {
        p_ctx->num_frames_received++;
        ni_log(NI_LOG_DEBUG, "Got frame # %" PRIu64 " bytes %d\n",
                       p_dec_ctx->frame_num, rx_size);

        ni_dec_retrieve_aux_data(p_out_frame);

        if (p_file && write_to_file) {
            if (p_dec_ctx->hw_action == NI_CODEC_HW_ENABLE)
            {
                rc = hwdl_frame(p_dec_ctx, &hwdl_session_data, p_out_frame, p_dec_ctx->pixel_format);
                if (rc <= 0) {
                    rc = NI_TEST_RETCODE_FAILURE;
                    goto end;
                }
                rc = write_rawvideo_data(p_file, p_dec_ctx->active_video_width, p_dec_ctx->active_video_height,
                                         output_video_width, output_video_height, p_dec_ctx->pixel_format,
                                         &hwdl_session_data.data.frame);
                ni_frame_buffer_free(&hwdl_session_data.data.frame);
            } else
            {
                rc = write_rawvideo_data(p_file, p_dec_ctx->active_video_width, p_dec_ctx->active_video_height,
                                         output_video_width, output_video_height, p_dec_ctx->pixel_format, p_out_frame);
            }

            if (rc < 0) {
                goto end;
            }
        }
    } else // rx_size == 0 means no decoded frame is available now
    {
        ni_log(NI_LOG_DEBUG, "No data received from decoder, return EAGAIN and retry\n");
        if (!p_out_frame->end_of_stream)
        {
            if (!b_is_hwframe)
            {
                ni_decoder_frame_buffer_free(p_out_frame);
            } else
            {
                ni_frame_buffer_free(p_out_frame);
            }
        }
        rc = NI_TEST_RETCODE_EAGAIN;
    }

    p_ctx->dec_total_bytes_received += rx_size;
    *p_rx_size = rx_size;

    if (p_out_frame->end_of_stream)
    {
        ni_log(NI_LOG_INFO, "Decoder Receiving done.\n");
        p_ctx->dec_eos_received = 1;
        rc = NI_TEST_RETCODE_END_OF_STREAM;
    }

    ni_log(NI_LOG_DEBUG, "decoder_receive_data: success\n");

end:
    ni_log(NI_LOG_DEBUG, "decoder_receive_data: rc %d rx_size %d\n", rc, rx_size);

    return rc;
}

/*!*****************************************************************************
 *  \brief  decoder session open
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int decoder_open_session(ni_session_context_t *p_dec_ctx, int iXcoderGUID,
                         ni_xcoder_params_t *p_dec_params)
{
    int ret = 0;

    // default is little_endian
    p_dec_ctx->src_endian = NI_FRAME_LITTLE_ENDIAN;
    p_dec_ctx->p_session_config = p_dec_params;
    p_dec_ctx->session_id = NI_INVALID_SESSION_ID;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_dec_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_dec_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_dec_ctx->hw_id = iXcoderGUID;

    if (p_dec_params->dec_input_params.hwframes)
    {
        p_dec_ctx->hw_action = NI_CODEC_HW_ENABLE;
    } else
    {
        p_dec_ctx->hw_action = NI_CODEC_HW_NONE;
    }

    ret = ni_device_session_open(p_dec_ctx, NI_DEVICE_TYPE_DECODER);

    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_decoder_session_open() failure!\n");
        return -1;
    } else
    {
        ni_log(NI_LOG_INFO, "Decoder device %d session open successful.\n", iXcoderGUID);
        return 0;
    }
}

void decoder_stat_report_and_close(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx)
{
    uint64_t current_time;

    current_time = ni_gettime_ns();

    ni_log(NI_LOG_ERROR, "Decoder Closing, Got:  Frames=%u  FPS=%.2f  Total bytes %llu\n",
           p_ctx->num_frames_received,
           (float)p_ctx->num_frames_received / (float)(current_time - p_ctx->start_time) * (float)1000000000,
           p_ctx->dec_total_bytes_received);

    ni_device_session_close(p_dec_ctx, 1, NI_DEVICE_TYPE_DECODER);
}

void *decoder_send_thread(void *args)
{
    dec_send_param_t *p_dec_send_param = args;
    ni_demo_context_t *p_ctx = p_dec_send_param->p_ctx;
    ni_session_context_t *p_dec_ctx = p_dec_send_param->p_dec_ctx;
    ni_test_frame_list_t *frame_list = p_dec_send_param->frame_list;
    ni_session_data_io_t in_pkt = {0};
    int retval = 0;

    ni_log(NI_LOG_INFO, "decoder_send_thread start: decoder_low_delay %d\n",
           p_dec_ctx->decoder_low_delay);
    while (1)
    {
        // Do not send packet to decoder if the output frame list is full.
        // Try to consume one decoded frame before sending new packets
        while (frame_list_is_full(frame_list) && !p_ctx->end_all_threads)
        {
            ni_usleep(100);
        }

        if (p_ctx->end_all_threads)
        {
            break;
        }

        retval = decoder_send_data(p_ctx, p_dec_ctx, &in_pkt, p_dec_send_param->input_width,
                                   p_dec_send_param->input_height, p_dec_send_param->p_stream_info);
        if (retval < 0)   // Error
        {
            ni_log(NI_LOG_ERROR, "Error: decoder send packet failed\n");
            break;
        } else if (p_dec_send_param->p_ctx->dec_eos_sent)   //eos
        {
            ni_log(NI_LOG_INFO, "decoder_send_thread reached eos\n");
            break;
        } else if (retval == NI_TEST_RETCODE_EAGAIN) {
            ni_usleep(100);
        }
    }

    ni_packet_buffer_free(&in_pkt.data.packet);

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (retval < 0)
    {
        p_ctx->end_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "decoder_send_thread exit\n");
    return (void *)(long)retval;
}

void *decoder_receive_thread(void *args)
{
    dec_recv_param_t *p_dec_recv_param = args;
    ni_demo_context_t *p_ctx = p_dec_recv_param->p_ctx;
    ni_session_context_t *p_dec_ctx = p_dec_recv_param->p_dec_ctx;
    ni_test_frame_list_t *frame_list = p_dec_recv_param->frame_list;
    ni_scale_params_t *scale_params = p_dec_recv_param->scale_params;
    ni_drawbox_params_t *drawbox_params = p_dec_recv_param->drawbox_params;
    ni_session_data_io_t filter_out_frame = {0};
    ni_session_data_io_t *p_out_frame = NULL;
    ni_frame_t *p_ni_frame = NULL;
    niFrameSurface1_t *p_hwframe;
    int retval = 0;
    int rx_size = 0;
    uint64_t current_time, previous_time = p_ctx->start_time;

    ni_log(NI_LOG_INFO, "decoder_receive_thread start\n");

    for (;;)
    {
        while (frame_list_is_full(frame_list) && !p_ctx->end_all_threads)
        {
            ni_usleep(100);
        }

        if (p_ctx->end_all_threads)
        {
            break;
        }

        p_out_frame = &frame_list->frames[frame_list->tail];
        p_ni_frame = &p_out_frame->data.frame;
        retval = decoder_receive_data(
            p_ctx, p_dec_ctx, p_out_frame, p_dec_recv_param->input_width,
            p_dec_recv_param->input_height, NULL, 0 /* no save to file */, &rx_size);
        if (retval < 0) // Error
        {
            if (!p_dec_ctx->hw_action)
            {
                ni_decoder_frame_buffer_free(p_ni_frame);
            } else
            {
                ni_frame_buffer_free(p_ni_frame);
            }
            ni_log(
                NI_LOG_ERROR,
                "Error: decoder_receive_thread break in transcode mode!\n");
            break;
        } else if (p_ni_frame->end_of_stream)
        {
            frame_list_enqueue(frame_list);
            ni_log(NI_LOG_INFO, "decoder_receive_thread reach eos\n");
            retval = 0;
            break;
        } else if (retval == NI_TEST_RETCODE_EAGAIN)
        {
            if (!p_dec_ctx->hw_action)
            {
                ni_decoder_frame_buffer_free(p_ni_frame);
            } else
            {
                ni_frame_buffer_free(p_ni_frame);
            }
            ni_usleep(100);
        } else
        {
            if (scale_params->enabled)
            {
                p_hwframe = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                ni_hw_frame_ref(p_hwframe);
                scale_filter(p_dec_recv_param->p_sca_ctx, p_ni_frame, &filter_out_frame, p_dec_recv_param->xcoderGUID,
                             scale_params->width, scale_params->height, ni_to_gc620_pix_fmt(p_dec_ctx->pixel_format),
                             scale_params->format);
                ni_hw_frame_unref(p_hwframe->ui16FrameIdx);
                ni_frame_buffer_free(p_ni_frame);
                memcpy(p_out_frame, &filter_out_frame, sizeof(ni_session_data_io_t));
                memset(&filter_out_frame, 0, sizeof(ni_session_data_io_t));
            }
            else if (drawbox_params->enabled)
            {
                p_hwframe = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                ni_hw_frame_ref(p_hwframe);
                drawbox_filter(p_dec_recv_param->p_crop_ctx, p_dec_recv_param->p_pad_ctx, p_dec_recv_param->p_ovly_ctx,
                               p_dec_recv_param->p_fmt_ctx, p_ni_frame, &filter_out_frame, drawbox_params,
                               p_dec_recv_param->xcoderGUID, ni_to_gc620_pix_fmt(p_dec_ctx->pixel_format), GC620_I420);
                ni_hw_frame_unref(p_hwframe->ui16FrameIdx);
                ni_frame_buffer_free(p_ni_frame);
                memcpy(p_out_frame, &filter_out_frame, sizeof(ni_session_data_io_t));
                memset(&filter_out_frame, 0, sizeof(ni_session_data_io_t));
            }
            if (p_dec_ctx->hw_action)
            {
                uint16_t current_hwframe_index = ((niFrameSurface1_t *)p_ni_frame->p_data[3])->ui16FrameIdx;
                ni_log(NI_LOG_DEBUG, "decoder recv:%d, tail:%d\n", current_hwframe_index, frame_list->tail);
            }
            frame_list_enqueue(frame_list);
        }

        current_time = ni_gettime_ns();
        if (current_time - previous_time >= (uint64_t)1000000000) {
            ni_log(NI_LOG_INFO, "Decoder stats: received %u frames, fps %.2f, total bytes %u\n",
                   p_ctx->num_frames_received,
                   (float)p_ctx->num_frames_received / (float)(current_time - p_ctx->start_time) * (float)1000000000,
                   p_ctx->dec_total_bytes_received);
            previous_time = current_time;
        }
    }

    ni_frame_buffer_free(&filter_out_frame.data.frame);

    // Broadcast all codec threads to quit on exception such as NVMe IO.
    if (retval < 0)
    {
        p_ctx->end_all_threads = 1;
    }

    ni_log(NI_LOG_TRACE, "decoder_receive_thread exit\n");
    return (void *)(long)retval;
}
