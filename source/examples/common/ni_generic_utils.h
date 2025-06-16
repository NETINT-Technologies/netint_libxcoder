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
 *  \file   generic_utils.h
 *
 *  \brief  Miscellaneous utility functions shared by Libxcoder API examples
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include "ni_device_api.h"

#ifdef _WIN32
#define open  _open
#define close _close
#define read  _read
#define write _write
#define lseek _lseek
#endif

#define FILE_NAME_LEN    256
#define MAX_INPUT_FILES  3
#define MAX_OUTPUT_FILES 4

#define NI_TEST_RETCODE_FAILURE -1
#define NI_TEST_RETCODE_SUCCESS 0
#define NI_TEST_RETCODE_END_OF_STREAM 1
#define NI_TEST_RETCODE_EAGAIN 2
#define NI_TEST_RETCODE_NEXT_INPUT 3
#define NI_TEST_RETCODE_SEQ_CHANGE_DONE 4

#define NI_ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

// max YUV frame size
#define MAX_YUV_FRAME_SIZE (7680 * 4320 * 3)

// H264
#define MAX_LOG2_MAX_FRAME_NUM (12 + 4)
#define MIN_LOG2_MAX_FRAME_NUM 4
#define EXTENDED_SAR 255
#define QP_MAX_NUM (51 + 6 * 6)   // The maximum supported qp
#define NI_MAX_BUFFERED_FRAME 45

#define HEVC_MAX_SUB_LAYERS 7
#define HEVC_MAX_SHORT_TERM_REF_PIC_SETS 64
#define HEVC_MAX_LONG_TERM_REF_PICS 32
#define HEVC_MAX_SPS_COUNT 16
#define HEVC_MAX_REFS 16
#define HEVC_MAX_LOG2_CTB_SIZE 6

// pts and dts
#define NI_MAX_PTS_QUEUE_SIZE 1024

typedef enum
{
    NI_SW_PIX_FMT_NONE = -1,       /* invalid format       */
    NI_SW_PIX_FMT_YUV444P,         /* 8-bit YUV444 planar  */
    NI_SW_PIX_FMT_YUV444P10LE,     /* 10-bit YUV444 planar */
} ni_sw_pix_fmt_t;

typedef struct _ni_pix_fmt_name
{
    const char *name;
    ni_pix_fmt_t pix_fmt;
} ni_pix_fmt_name_t;

typedef struct _ni_gc620_pix_fmt
{
  ni_pix_fmt_t pix_fmt_ni;
  int pix_fmt_gc620;
} ni_gc620_pix_fmt_t;

// simplistic ref counted HW frame
typedef struct _ni_hwframe_ref_t
{
    int ref_cnt;
    niFrameSurface1_t surface;
} ni_hwframe_ref_t;

typedef struct _ni_test_frame_list
{
    ni_session_data_io_t frames[NI_MAX_BUFFERED_FRAME];
    int head;
    int tail;
} ni_test_frame_list_t;

typedef struct _ni_pts_queue
{
    int data[NI_MAX_PTS_QUEUE_SIZE];
    int front;
    int rear;
    int size;
} ni_pts_queue;

typedef struct _ni_demo_context
{
    uint8_t end_all_threads;

    // read input files
    uint8_t *file_cache;
    uint64_t curr_file_offset;
    uint64_t total_file_size;
    uint64_t loops_left;

    // target fps at which YUV input is read
    int read_framerate;

    // bookkeeping counters
    uint8_t curr_enc_index;
    uint64_t dec_total_bytes_sent;
    uint64_t dec_total_bytes_received;
    uint64_t enc_total_bytes_sent[MAX_OUTPUT_FILES];
    uint64_t enc_total_bytes_received[MAX_OUTPUT_FILES];
    uint64_t num_frames_received;
    uint64_t num_frames_sent[MAX_OUTPUT_FILES];
    uint64_t num_packets_received[MAX_OUTPUT_FILES];
    uint64_t reconfig_count;
    uint64_t start_time;

    // av1-specific variables
    uint32_t av1_muxed_num_packets[MAX_OUTPUT_FILES];
    uint8_t *p_av1_seq_header[MAX_OUTPUT_FILES];
    uint32_t av1_seq_header_len[MAX_OUTPUT_FILES];
    uint8_t av1_output_obu;
    uint8_t ivf_header_written[MAX_OUTPUT_FILES];

    // trackers for current state of decoder/encoder
    int dec_sos_sent;
    int dec_eos_sent;
    int dec_eos_received;
    int enc_resend[MAX_OUTPUT_FILES];
    int enc_sos_sent[MAX_OUTPUT_FILES];
    int enc_eos_sent[MAX_OUTPUT_FILES];
    int enc_eos_received[MAX_OUTPUT_FILES];

    // pts and dts
    ni_pts_queue *enc_pts_queue[MAX_OUTPUT_FILES];
    int pts[MAX_OUTPUT_FILES];
} ni_demo_context_t;

typedef struct uploader_param
{
    ni_demo_context_t *p_ctx;
    ni_session_context_t *p_upl_ctx;
    ni_session_context_t *p_sca_ctx;
    ni_session_data_io_t *p_swin_frame;
    ni_session_data_io_t *p_scale_frame;
    int input_video_width;
    int input_video_height;
    int pfs;
    void *yuv_buf;
    unsigned long *p_total_bytes_sent;
    int pool_size;
    ni_test_frame_list_t *frame_list;
} uploader_param_t;

void print_version(void);

int is_ni_enc_pix_fmt(ni_pix_fmt_t pix_fmt);
ni_pix_fmt_t ni_pixel_format_search(const char *name);
const char *ni_pixel_format_name(ni_pix_fmt_t pix_fmt);
int ni_to_gc620_pix_fmt(ni_pix_fmt_t pix_fmt);
ni_pix_fmt_t gc620_to_ni_pix_fmt(int pix_fmt);
ni_pixel_planar_format get_pixel_planar(ni_pix_fmt_t pix_fmt);

bool frame_list_is_empty(ni_test_frame_list_t *list);
bool frame_list_is_full(ni_test_frame_list_t *list);
bool uploader_frame_list_full(ni_test_frame_list_t *list, ni_pix_fmt_t pix_fmt);
int frame_list_length(ni_test_frame_list_t *list);
int frame_list_enqueue(ni_test_frame_list_t *list);
int frame_list_drain(ni_test_frame_list_t *list);
void hwframe_list_release(ni_test_frame_list_t *list);

uint64_t get_total_file_size(FILE* fp);
int read_and_cache_file(ni_demo_context_t *ctx, char *filename);
void reset_data_buf_pos(ni_demo_context_t *p_ctx);
void rewind_data_buf_pos_by(ni_demo_context_t *p_ctx, uint64_t nb_bytes);
int frame_read_buffer_size(int w, int h, ni_pix_fmt_t pix_fmt,
                           ni_sw_pix_fmt_t sw_pix_fmt);
uint32_t read_next_chunk_from_file(ni_demo_context_t *p_ctx, FILE *pfs, uint8_t *p_dst, uint32_t to_read);
int read_yuv_from_file(ni_demo_context_t *p_ctx, FILE *pfs, void *yuv_buf, int width, int height,
                       ni_pix_fmt_t pix_fmt, ni_sw_pix_fmt_t sw_pix_fmt,
                       int *eos, ni_session_run_state_t run_state);

int convert_yuv_444p_to_420p(ni_session_data_io_t *p_frame,
                             void *yuv_buf, int width, int height,
                             ni_sw_pix_fmt_t sw_pix_fmt, int mode,
                             ni_codec_format_t codec_format);

int write_rawvideo_data(FILE *p_file, int input_aligned_width, int input_aligned_height,
                        int output_width, int output_height, int format, ni_frame_t *p_out_frame);

int scan_and_clean_hwdescriptors(void);
void ni_hw_frame_ref(const niFrameSurface1_t *p_surface);
void ni_hw_frame_unref(uint16_t hwframe_index);


int hwdl_frame(ni_session_context_t *p_ctx,
               ni_session_data_io_t *p_session_data, ni_frame_t *p_src_frame,
               int output_format);
int upload_send_data_get_desc(ni_demo_context_t *p_ctx, ni_session_context_t *p_upl_ctx,
                              ni_session_data_io_t *p_swin_data,   //intermediate for swf
                              ni_session_data_io_t *p_in_data,
                              int input_video_width, int input_video_height,
                              void *yuv_buf);
int uploader_open_session(ni_session_context_t *p_upl_ctx, int iXcoderGUID,
                          int width, int height, ni_pix_fmt_t pix_fmt,
                          int is_p2p, int pool_size);
niFrameSurface1_t *hwupload_frame(ni_demo_context_t *p_ctx,
                                  ni_session_context_t *p_upl_ctx,
                                  ni_session_context_t *p_sca_ctx,
                                  ni_session_data_io_t *p_sw_data,
                                  ni_session_data_io_t *p_hw_data,
                                  ni_session_data_io_t *p_scale_data,
                                  ni_pix_fmt_t pix_fmt, int width,
                                  int height, FILE *pfs, void *yuv_buf, int *eos);

#ifdef __cplusplus
}
#endif