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
 *  \file   decode_utils.h
 *
 *  \brief  Video decoding utility functions shared by Libxcoder API examples
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_device_api.h"
#include "ni_av_codec.h"
#include "ni_bitstream.h"
#include "ni_generic_utils.h"
#include "ni_filter_utils.h"

typedef struct dec_send_param
{
    ni_demo_context_t *p_ctx;
    ni_session_context_t *p_dec_ctx;
    int input_width;
    int input_height;
    void *p_stream_info;
    ni_test_frame_list_t *frame_list;
} dec_send_param_t;

typedef struct dec_recv_param
{
    ni_demo_context_t *p_ctx;
    ni_session_context_t *p_dec_ctx;
    ni_session_context_t *p_sca_ctx;
    ni_session_context_t *p_crop_ctx;
    ni_session_context_t *p_pad_ctx;
    ni_session_context_t *p_ovly_ctx;
    ni_session_context_t *p_fmt_ctx;
    int xcoderGUID;
    int input_width;
    int input_height;
    ni_test_frame_list_t *frame_list;
    ni_scale_params_t *scale_params;
    ni_drawbox_params_t *drawbox_params;
} dec_recv_param_t;

/**
 * Sequence parameter set
 */
typedef struct _ni_h264_sps_t
{
    int width;
    int height;

    unsigned int sps_id;
    int profile_idc;
    int level_idc;
    int chroma_format_idc;
    int transform_bypass;     ///< qpprime_y_zero_transform_bypass_flag
    int log2_max_frame_num;   ///< log2_max_frame_num_minus4 + 4
    int poc_type;             ///< pic_order_cnt_type
    int log2_max_poc_lsb;     ///< log2_max_pic_order_cnt_lsb_minus4
    int delta_pic_order_always_zero_flag;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;   ///< num_ref_frames_in_pic_order_cnt_cycle
    int ref_frame_count;    ///< num_ref_frames
    int gaps_in_frame_num_allowed_flag;
    int mb_width;   ///< pic_width_in_mbs_minus1 + 1
    ///< (pic_height_in_map_units_minus1 + 1) * (2 - frame_mbs_only_flag)
    int mb_height;
    int frame_mbs_only_flag;
    int mb_aff;   ///< mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int crop;   ///< frame_cropping_flag

    unsigned int crop_left;     ///< frame_cropping_rect_left_offset
    unsigned int crop_right;    ///< frame_cropping_rect_right_offset
    unsigned int crop_top;      ///< frame_cropping_rect_top_offset
    unsigned int crop_bottom;   ///< frame_cropping_rect_bottom_offset
    int vui_parameters_present_flag;
    ni_rational_t sar;
    int video_signal_type_present_flag;
    int full_range;
    int colour_description_present_flag;
    ni_color_primaries_t color_primaries;
    ni_color_transfer_characteristic_t color_trc;
    ni_color_space_t colorspace;
    int timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    int fixed_frame_rate_flag;
    short offset_for_ref_frame[256];
    int bitstream_restriction_flag;
    int num_reorder_frames;
    unsigned int max_dec_frame_buffering;
    int scaling_matrix_present;
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[6][64];
    int nal_hrd_parameters_present_flag;
    int vcl_hrd_parameters_present_flag;
    int pic_struct_present_flag;
    int time_offset_length;
    int cpb_cnt;                            ///< See H.264 E.1.2
    int initial_cpb_removal_delay_length;   ///< initial_cpb_removal_delay_length_minus1 + 1
    int cpb_removal_delay_length;   ///< cpb_removal_delay_length_minus1 + 1
    int dpb_output_delay_length;    ///< dpb_output_delay_length_minus1 + 1
    int bit_depth_luma;             ///< bit_depth_luma_minus8 + 8
    int bit_depth_chroma;           ///< bit_depth_chroma_minus8 + 8
    int residual_color_transform_flag;   ///< residual_colour_transform_flag
    int constraint_set_flags;            ///< constraint_set[0-3]_flag
    uint8_t data[4096];
    size_t data_size;
} ni_h264_sps_t;

typedef struct _ni_h265_window_t
{
    unsigned int left_offset;
    unsigned int right_offset;
    unsigned int top_offset;
    unsigned int bottom_offset;
} ni_h265_window_t;

typedef struct VUI
{
    ni_rational_t sar;

    int overscan_info_present_flag;
    int overscan_appropriate_flag;

    int video_signal_type_present_flag;
    int video_format;
    int video_full_range_flag;
    int colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristic;
    uint8_t matrix_coeffs;

    int chroma_loc_info_present_flag;
    int chroma_sample_loc_type_top_field;
    int chroma_sample_loc_type_bottom_field;
    int neutra_chroma_indication_flag;

    int field_seq_flag;
    int frame_field_info_present_flag;

    int default_display_window_flag;
    ni_h265_window_t def_disp_win;

    int vui_timing_info_present_flag;
    uint32_t vui_num_units_in_tick;
    uint32_t vui_time_scale;
    int vui_poc_proportional_to_timing_flag;
    int vui_num_ticks_poc_diff_one_minus1;
    int vui_hrd_parameters_present_flag;

    int bitstream_restriction_flag;
    int tiles_fixed_structure_flag;
    int motion_vectors_over_pic_boundaries_flag;
    int restricted_ref_pic_lists_flag;
    int min_spatial_segmentation_idc;
    int max_bytes_per_pic_denom;
    int max_bits_per_min_cu_denom;
    int log2_max_mv_length_horizontal;
    int log2_max_mv_length_vertical;
} VUI;

typedef struct PTLCommon
{
    uint8_t profile_space;
    uint8_t tier_flag;
    uint8_t profile_idc;
    uint8_t profile_compatibility_flag[32];
    uint8_t progressive_source_flag;
    uint8_t interlaced_source_flag;
    uint8_t non_packed_constraint_flag;
    uint8_t frame_only_constraint_flag;
    uint8_t max_12bit_constraint_flag;
    uint8_t max_10bit_constraint_flag;
    uint8_t max_8bit_constraint_flag;
    uint8_t max_422chroma_constraint_flag;
    uint8_t max_420chroma_constraint_flag;
    uint8_t max_monochrome_constraint_flag;
    uint8_t intra_constraint_flag;
    uint8_t one_picture_only_constraint_flag;
    uint8_t lower_bit_rate_constraint_flag;
    uint8_t max_14bit_constraint_flag;
    uint8_t inbld_flag;
    uint8_t level_idc;
} PTLCommon;

typedef struct PTL
{
    PTLCommon general_ptl;
    PTLCommon sub_layer_ptl[HEVC_MAX_SUB_LAYERS];

    uint8_t sub_layer_profile_present_flag[HEVC_MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[HEVC_MAX_SUB_LAYERS];
} PTL;

typedef struct ScalingList
{
    /* This is a little wasteful, since sizeID 0 only needs 8 coeffs,
     * and size ID 3 only has 2 arrays, not 6. */
    uint8_t sl[4][6][64];
    uint8_t sl_dc[2][6];
} ScalingList;

typedef struct ShortTermRPS
{
    unsigned int num_negative_pics;
    int num_delta_pocs;
    int rps_idx_num_delta_pocs;
    int32_t delta_poc[32];
    uint8_t used[32];
} ShortTermRPS;

/**
 * HEVC Sequence parameter set
 */
typedef struct _ni_h265_sps_t
{
    unsigned vps_id;
    int chroma_format_idc;
    uint8_t separate_colour_plane_flag;

    ni_h265_window_t output_window;
    ni_h265_window_t pic_conf_win;

    int bit_depth;
    int bit_depth_chroma;
    int pixel_shift;
    int pix_fmt;

    unsigned int log2_max_poc_lsb;
    int pcm_enabled_flag;

    int max_sub_layers;
    struct
    {
        int max_dec_pic_buffering;
        int num_reorder_pics;
        int max_latency_increase;
    } temporal_layer[HEVC_MAX_SUB_LAYERS];
    uint8_t temporal_id_nesting_flag;

    VUI vui;
    PTL ptl;

    uint8_t scaling_list_enable_flag;
    ScalingList scaling_list;

    unsigned int nb_st_rps;
    ShortTermRPS st_rps[HEVC_MAX_SHORT_TERM_REF_PIC_SETS];

    uint8_t amp_enabled_flag;
    uint8_t sao_enabled;

    uint8_t long_term_ref_pics_present_flag;
    uint16_t lt_ref_pic_poc_lsb_sps[HEVC_MAX_LONG_TERM_REF_PICS];
    uint8_t used_by_curr_pic_lt_sps_flag[HEVC_MAX_LONG_TERM_REF_PICS];
    uint8_t num_long_term_ref_pics_sps;

    struct
    {
        uint8_t bit_depth;
        uint8_t bit_depth_chroma;
        unsigned int log2_min_pcm_cb_size;
        unsigned int log2_max_pcm_cb_size;
        uint8_t loop_filter_disable_flag;
    } pcm;
    uint8_t sps_temporal_mvp_enabled_flag;
    uint8_t sps_strong_intra_smoothing_enable_flag;

    unsigned int log2_min_cb_size;
    unsigned int log2_diff_max_min_coding_block_size;
    unsigned int log2_min_tb_size;
    unsigned int log2_max_trafo_size;
    unsigned int log2_ctb_size;
    unsigned int log2_min_pu_size;

    int max_transform_hierarchy_depth_inter;
    int max_transform_hierarchy_depth_intra;

    int sps_range_extension_flag;
    int transform_skip_rotation_enabled_flag;
    int transform_skip_context_enabled_flag;
    int implicit_rdpcm_enabled_flag;
    int explicit_rdpcm_enabled_flag;
    int extended_precision_processing_flag;
    int intra_smoothing_disabled_flag;
    int high_precision_offsets_enabled_flag;
    int persistent_rice_adaptation_enabled_flag;
    int cabac_bypass_alignment_enabled_flag;

    ///< coded frame dimension in various units
    int width;
    int height;
    int ctb_width;
    int ctb_height;
    int ctb_size;
    int min_cb_width;
    int min_cb_height;
    int min_tb_width;
    int min_tb_height;
    int min_pu_width;
    int min_pu_height;
    int tb_mask;

    int hshift[3];
    int vshift[3];

    int qp_bd_offset;

    uint8_t data[4096];
    int data_size;
} ni_h265_sps_t;

typedef struct _ni_vp9_header_info
{
    int profile;
    uint16_t header_length;
    uint16_t width;
    uint16_t height;
    struct
    {
        uint32_t den;
        uint32_t num;
    } timebase;
    uint32_t total_frames;
} ni_vp9_header_info_t;

typedef enum _ni_nalu_type
{
    H264_NAL_UNSPECIFIED = 0,
    H264_NAL_SLICE = 1,
    H264_NAL_DPA = 2,
    H264_NAL_DPB = 3,
    H264_NAL_DPC = 4,
    H264_NAL_IDR_SLICE = 5,
    H264_NAL_SEI = 6,
    H264_NAL_SPS = 7,
    H264_NAL_PPS = 8,
    H264_NAL_AUD = 9,
    H264_NAL_END_SEQUENCE = 10,
    H264_NAL_END_STREAM = 11,
    H264_NAL_FILLER_DATA = 12,
    H264_NAL_SPS_EXT = 13,
    H264_NAL_PREFIX = 14,
    H264_NAL_SUB_SPS = 15,
    H264_NAL_DPS = 16,
    H264_NAL_AUXILIARY_SLICE = 19,
} ni_nalu_type_t;

typedef enum _ni_hevc_nalu_type
{
    HEVC_NAL_TRAIL_N = 0,
    HEVC_NAL_TRAIL_R = 1,
    HEVC_NAL_TSA_N = 2,
    HEVC_NAL_TSA_R = 3,
    HEVC_NAL_STSA_N = 4,
    HEVC_NAL_STSA_R = 5,
    HEVC_NAL_RADL_N = 6,
    HEVC_NAL_RADL_R = 7,
    HEVC_NAL_RASL_N = 8,
    HEVC_NAL_RASL_R = 9,
    HEVC_NAL_IDR_W_RADL = 19,
    HEVC_NAL_IDR_N_LP = 20,
    HEVC_NAL_CRA_NUT = 21,
    HEVC_NAL_VPS = 32,
    HEVC_NAL_SPS = 33,
    HEVC_NAL_PPS = 34,
    HEVC_NAL_AUD = 35,
    HEVC_NAL_EOS_NUT = 36,
    HEVC_NAL_EOB_NUT = 37,
    HEVC_NAL_FD_NUT = 38,
    HEVC_NAL_SEI_PREFIX = 39,
    HEVC_NAL_SEI_SUFFIX = 40,
} ni_hevc_nalu_type;

uint64_t find_h264_next_nalu(ni_demo_context_t *p_ctx, uint8_t *p_dst, int *nal_type);
int h264_parse_hrd(ni_bitstream_reader_t *br, ni_h264_sps_t *sps);
int h264_parse_vui(ni_bitstream_reader_t *br, ni_h264_sps_t *sps);
int h264_parse_scaling_list(ni_bitstream_reader_t *br, uint8_t *factors, int size,
                       const uint8_t *jvt_list, const uint8_t *fallback_list);
int h264_parse_scaling_matrices(ni_bitstream_reader_t *br, const ni_h264_sps_t *sps,
                           uint8_t (*scaling_matrix4)[16], uint8_t (*scaling_matrix8)[64]);
int h264_parse_sps(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps);
int h264_parse_sei(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps,
                   int *sei_type, int *is_interlaced);
int probe_h264_stream_info(ni_demo_context_t *p_ctx, ni_h264_sps_t *sps);
int parse_h264_slice_header(uint8_t *buf, int size_bytes, ni_h264_sps_t *sps,
                            int32_t *frame_num, unsigned int *first_mb_in_slice);

uint64_t find_h265_next_nalu(ni_demo_context_t *p_ctx, uint8_t *p_dst, int *nal_type);
void h265_decode_sublayer_hrd(ni_bitstream_reader_t *br, unsigned int nb_cpb,
                              int subpic_params_present);
int h265_decode_profile_tier_level(ni_bitstream_reader_t *br, PTLCommon *ptl);
int h265_parse_ptl(ni_bitstream_reader_t *br, PTL *ptl, int max_num_sub_layers);
int h265_decode_hrd(ni_bitstream_reader_t *br, int common_inf_present, int max_sublayers);
void h265_set_default_scaling_list_data(ScalingList *sl);
int h265_scaling_list_data(ni_bitstream_reader_t *br, ScalingList *sl, ni_h265_sps_t *sps);
int h265_decode_short_term_rps(ni_bitstream_reader_t *br, ShortTermRPS *rps,
                               const ni_h265_sps_t *sps, int is_slice_header);
int h265_decode_vui(ni_bitstream_reader_t *br, int apply_defdispwin, ni_h265_sps_t *sps);
int h265_parse_sps(ni_h265_sps_t *sps, uint8_t *buf, int size_bytes);
int probe_h265_stream_info(ni_demo_context_t *p_ctx, ni_h265_sps_t *sps);

uint64_t find_vp9_next_packet(ni_demo_context_t *p_ctx, uint8_t *p_dst, ni_vp9_header_info_t *vp9_info);
int vp9_parse_header(ni_vp9_header_info_t *vp9_info, uint8_t *buf, int size_bytes);
int probe_vp9_stream_info(ni_demo_context_t *p_ctx, ni_vp9_header_info_t *vp9_info);

int decoder_send_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx,
                               ni_session_data_io_t *p_in_data,
                               int input_video_width, int input_video_height,
                               void *stream_info);
int decoder_receive_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx,
                         ni_session_data_io_t *p_out_data,
                         int output_video_width, int output_video_height,
                         FILE *p_file, int write_to_file,
                         int * p_rx_size);
int decoder_open_session(ni_session_context_t *p_dec_ctx, int iXcoderGUID,
                         ni_xcoder_params_t *p_dec_params);
void decoder_stat_report_and_close(ni_demo_context_t *p_ctx, ni_session_context_t *p_dec_ctx);

void *decoder_send_thread(void *args);
void *decoder_receive_thread(void *args);

#ifdef __cplusplus
}
#endif