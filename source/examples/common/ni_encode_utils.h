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
 *  \file   encode_utils.h
 *
 *  \brief  Video encoding utility functions shared by Libxcoder API examples
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ni_generic_utils.h"
#include "ni_device_api.h"

typedef struct enc_send_param
{
    ni_demo_context_t *p_ctx;
    ni_session_context_t *p_enc_ctx;
    int output_width;
    int output_height;
    int output_total;
    ni_test_frame_list_t *frame_list;
} enc_send_param_t;

typedef struct enc_recv_param
{
    ni_demo_context_t *p_ctx;
    ni_session_context_t *p_enc_ctx;
    int output_width;
    int output_height;
    FILE **p_file;
    int output_total;
    ni_test_frame_list_t *frame_list;
} enc_recv_param_t;

void set_demo_roi_map(ni_session_context_t *p_enc_ctx);
void prep_reconf_demo_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx, ni_frame_t *frame);

int encoder_send_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                      ni_session_data_io_t *p_in_data, void *yuv_buf,
                      int input_video_width, int input_video_height,
                      int is_last_input);
int encoder_send_data2(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_dec_out_data,
                       ni_session_data_io_t *p_enc_in_data,
                       int input_video_width, int input_video_height);
int encoder_send_data3(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                       ni_session_data_io_t *p_in_data,
                       int input_video_width, int input_video_height, int eos);

int encoder_open_session(ni_session_context_t *p_enc_ctx, int dst_codec_format,
                         int iXcoderGUID, ni_xcoder_params_t *p_enc_params,
                         int width, int height, ni_pix_fmt_t pix_fmt,
                         bool check_zerocopy);
int encoder_reinit_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data);

void write_av1_ivf_header(ni_demo_context_t *p_ctx, uint32_t width, uint32_t height, uint32_t frame_num,
                          uint32_t frame_denom, FILE *p_file);
void write_av1_ivf_packet(ni_demo_context_t *p_ctx, ni_packet_t *p_out_pkt, uint32_t meta_size, FILE *p_file);
int write_av1_ivf_trailer(ni_demo_context_t *p_ctx, ni_packet_t *p_out_pkt, uint32_t meta_size, FILE *p_file);
int encoder_receive_data(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_out_data, int output_video_width,
                         int output_video_height, FILE *p_file, ni_session_data_io_t * p_in_data);
int encoder_close_session(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data);
int encoder_sequence_change(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_in_data,
                         ni_session_data_io_t *p_out_data,
                         int width, int height, ni_pix_fmt_t pix_fmt);

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
                 bool check_zerocopy);
int encoder_open2(ni_demo_context_t *p_ctx,
                 ni_session_context_t *enc_ctx_list,
                 ni_xcoder_params_t *p_api_param_list,
                 int output_total, char p_enc_conf_params[][2048],
                 char p_enc_conf_gop[][2048],
                 ni_frame_t *p_ni_frame, int width[], int height[],
                 int fps_num, int fps_den, int bitrate,
                 int codec_format, ni_pix_fmt_t pix_fmt[],
                 int aspect_ratio_idc, int xcoder_guid,
                 niFrameSurface1_t *p_surface[], int multi_thread,
                 bool check_zerocopy);
int encoder_receive(ni_demo_context_t *p_ctx,
                    ni_session_context_t *enc_ctx_list,
                    ni_session_data_io_t *in_frame,
                    ni_session_data_io_t *pkt, int width, int height,
                    int output_total, FILE **pfs_list);
int encoder_receive2(ni_demo_context_t *p_ctx,
                    ni_session_context_t *enc_ctx_list,
                    ni_session_data_io_t *in_frame,
                    ni_session_data_io_t *pkt, int width[], int height[],
                    int output_total, FILE **pfs_list);

void encoder_stat_report_and_close(ni_demo_context_t *p_ctx, ni_session_context_t *p_enc_ctx_list, int output_total);

void *encoder_send_thread(void *args);
void *encoder_receive_thread(void *args);

void ni_init_pts_queue(ni_pts_queue *q);
int ni_pts_queue_empty(ni_pts_queue *q);
int ni_pts_queue_full(ni_pts_queue *q);
int ni_pts_enqueue(ni_pts_queue *q, int64_t value);
int ni_pts_dequeue(ni_pts_queue *q, int64_t *value);
void ni_prepare_pts_queue(ni_pts_queue *q, ni_xcoder_params_t *enc_param, int pts_start);

#ifdef __cplusplus
}
#endif

