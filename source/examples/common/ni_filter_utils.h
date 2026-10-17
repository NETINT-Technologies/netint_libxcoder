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
 *  \file   filter_utils.h
 *
 *  \brief  Video filtering utility functions shared by Libxcoder API examples
 ******************************************************************************/
#pragma once

#include "ni_device_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ni_scale_params
{
    int enabled;
    int width;
    int height;
    int format;
} ni_scale_params_t;

typedef struct _ni_drawbox_params
{
    int enabled;
    int box_w;
    int box_h;
    int box_x;
    int box_y;
} ni_drawbox_params_t;

typedef struct _ni_ai_pre_params
{
    int enabled;
    char nb_file[FILE_NAME_LEN];   // path to the AI network binary (.nb) file
    int out_width;                 // 0 = same as input width
    int out_height;                // 0 = same as input height
    int initialized;               // set to 1 after the AI session is opened
} ni_ai_pre_params_t;

int ni_scaler_params_set_value(ni_scale_params_t *params, const char *name, const char *value);
int ni_drawbox_params_set_value(ni_drawbox_params_t *params, const char *name, const char *value);
int ni_ai_pre_params_set_value(ni_ai_pre_params_t *params, const char *name, const char *value);
int retrieve_filter_params(char filter_params[], ni_scale_params_t *scale_params,
                           ni_drawbox_params_t *drawbox_params, ni_ai_pre_params_t *ai_pre_params);

/*!*****************************************************************************
 *  \brief  Open an AI session, configure the network binary and create the
 *          hardware output frame pool used by the ai_pre filter.
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int ai_pre_filter_init(ni_session_context_t *p_ai_ctx, ni_network_data_t *p_network,
                       int iXcoderGUID, ni_ai_pre_params_t *params,
                       int in_width, int in_height, int in_gc620_format);

/*!*****************************************************************************
 *  \brief  Run one AI preprocess operation on a hardware frame. The output is a
 *          new hardware frame preserving the input pixel format.
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int ai_pre_filter(ni_session_context_t *p_ai_ctx, ni_network_data_t *p_network,
                  ni_frame_t *p_frame_in, ni_session_data_io_t *p_data_out,
                  ni_ai_pre_params_t *params, int in_gc620_format);

void init_scaler_params(ni_scaler_input_params_t *p_scaler_params, ni_scaler_opcode_t op, int in_rec_width,
                        int in_rec_height, int in_rec_x, int in_rec_y, int out_rec_x, int out_rec_y);
int scaler_session_open(ni_session_context_t *p_scaler_ctx, int iXcoderGUID, ni_scaler_opcode_t op);
int launch_scaler_operation(ni_session_context_t *p_scaler_ctx, int iXcoderGUID,
                            ni_frame_t *p_frame_in_up, ni_frame_t *p_frame_in_bg,
                            ni_session_data_io_t *p_data_out, ni_scaler_input_params_t scaler_params);

int drawbox_filter(ni_session_context_t *p_crop_ctx, ni_session_context_t *p_pad_ctx,
                   ni_session_context_t *p_overlay_ctx, ni_session_context_t *p_fmt_ctx,
                   ni_frame_t *p_frame_in, ni_session_data_io_t *p_data_out, ni_drawbox_params_t *p_box_params,
                   int iXcoderGUID, int input_format, int output_format);
int scale_filter(ni_session_context_t *p_ctx, ni_frame_t *p_frame_in,
                 ni_session_data_io_t *p_data_out, int iXcoderGUID,
                 int scale_width, int scale_height, int in_format, int out_format);

#ifdef __cplusplus
}
#endif