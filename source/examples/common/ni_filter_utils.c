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
 *  \file   filter_utils.c
 *
 *  \brief  Video filtering utility functions shared by Libxcoder API examples
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_filter_utils.h"
#include "ni_log.h"
#include "ni_util.h"

int ni_scaler_params_set_value(ni_scale_params_t *params, const char *name, const char *value)
{
    if (!params || !name || !value)
    {
        ni_log(NI_LOG_ERROR, "Error: Null pointer received in ni_scaler_params_set_value\n");
        return -1;
    }

    if (!strcmp("width", name))
    {
        params->width = atoi(value);
    } else if (!strcmp("height", name))
    {
        params->height = atoi(value);
    } else if (!strcmp("format", name))
    {
        if (!strcmp(value, "yuv420p"))
        {
            params->format = GC620_I420;
        } else if (!strcmp(value, "yuv420p10le"))
        {
            params->format = GC620_I010;
        } else if (!strcmp(value, "nv12"))
        {
            params->format = GC620_NV12;
        } else if (!strcmp(value, "p010le"))
        {
            params->format = GC620_P010_MSB;
        } else
        {
            ni_log(NI_LOG_ERROR, "Error: pixel format %s not supported in scale\n", value);
            return -1;
        }
    } else
    {
        ni_log(NI_LOG_ERROR, "Error: invalid scale param name %s\n", name);
        return -1;
    }

    return 0;
}

int ni_drawbox_params_set_value(ni_drawbox_params_t *params, const char *name, const char *value)
{
    if (!params || !name || !value)
    {
        ni_log(NI_LOG_ERROR, "Error: Null pointer received in ni_drawbox_params_set_value\n");
        return -1;
    }

    if (!strcmp("width", name))
    {
        params->box_w = atoi(value);
    } else if (!strcmp("height", name))
    {
        params->box_h = atoi(value);
    } else if (!strcmp("x", name))
    {
        params->box_x = atoi(value);
    } else if (!strcmp("y", name))
    {
        params->box_y = atoi(value);
    } else
    {
        ni_log(NI_LOG_ERROR, "Error: invalid drawbox param name %s\n", name);
        return -1;
    }

    return 0;
}

int retrieve_filter_params(char filter_params[], ni_scale_params_t *scale_params, ni_drawbox_params_t *drawbox_params)
{
    char key[64], value[64];
    char *curr = filter_params, *colon_pos;
    int ret = 0;

    //reset all params to default values first
    memset(scale_params, 0, sizeof(ni_scale_params_t));
    memset(drawbox_params, 0, sizeof(ni_drawbox_params_t));
    scale_params->format = GC620_I420;

    if (strncmp("ni_quadra_scale", curr, 15) == 0)
    {
        scale_params->enabled = 1;
        curr += 16;
    }
    else if (strncmp("ni_quadra_drawbox", curr, 17) == 0)
    {
        drawbox_params->enabled = 1;
        curr += 18;
    }
    else
    {
        ni_log(NI_LOG_ERROR, "Error: invalid filter, must be one of [ni_quadra_scale, ni_quadra_drawbox]");
        return -1;
    }

    while (*curr)
    {
        colon_pos = strchr(curr, ':');

        if (colon_pos)
        {
            *colon_pos = '\0';
        }

        if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
            ni_param_get_key_value(curr, key, value))
        {
            ni_log(NI_LOG_ERROR, "Error: cannot retrieve key/value from filter param: %s\n", curr);
            ret = -1;
            break;
        }

        if (scale_params->enabled)
        {
            ret = ni_scaler_params_set_value(scale_params, key, value);
        } else
        {
            ret = ni_drawbox_params_set_value(drawbox_params, key, value);
        }
        if (ret != 0)
        {
            break;
        }

        if (colon_pos)
        {
            curr = colon_pos + 1;
        } else
        {
            curr += strlen(curr);
        }
    }
    return ret;
}

/*!*****************************************************************************
 *  \brief  Init scaler params here - both user setting params and fixed params.
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
void init_scaler_params(ni_scaler_input_params_t *p_scaler_params, ni_scaler_opcode_t op, int in_rec_width,
                        int in_rec_height, int in_rec_x, int in_rec_y, int out_rec_x, int out_rec_y)
{
    p_scaler_params->op = op;
    // input_format/width/height, output_format/width/height should be assigned by users for all ops
    if (op == NI_SCALER_OPCODE_CROP)
    {
        // fixed numbers
        p_scaler_params->out_rec_width = 0;
        p_scaler_params->out_rec_height = 0;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;
        p_scaler_params->rgba_color = 0;

        // params set by user
        p_scaler_params->in_rec_width = in_rec_width;
        p_scaler_params->in_rec_height = in_rec_height;
        p_scaler_params->in_rec_x = in_rec_x;
        p_scaler_params->in_rec_y = in_rec_y;
    } else if (op == NI_SCALER_OPCODE_SCALE)
    {
        // fixed params
        p_scaler_params->in_rec_width = 0;
        p_scaler_params->in_rec_height = 0;
        p_scaler_params->in_rec_x = 0;
        p_scaler_params->in_rec_y = 0;

        p_scaler_params->out_rec_width = 0;
        p_scaler_params->out_rec_height = 0;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;

        p_scaler_params->rgba_color = 0;
    } else if (op == NI_SCALER_OPCODE_PAD)
    {
        // fixed params
        p_scaler_params->in_rec_width = p_scaler_params->input_width;
        p_scaler_params->in_rec_height = p_scaler_params->input_height;
        p_scaler_params->in_rec_x = 0;
        p_scaler_params->in_rec_y = 0;

        p_scaler_params->out_rec_width = p_scaler_params->input_width;
        p_scaler_params->out_rec_height = p_scaler_params->input_height;

        /*
            Scaler uses BGRA color, or ARGB in little-endian
            ui32RgbaColor = (s->rgba_color[3] << 24) | (s->rgba_color[0] << 16) |
                            (s->rgba_color[1] << 8) | s->rgba_color[2];
            here p_scaler_params->rgba_color = ui32RgbaColor;
        */
        p_scaler_params->rgba_color =
            4278190080;   // now padding color is black

        // params set by user
        p_scaler_params->out_rec_x = out_rec_x;
        p_scaler_params->out_rec_y = out_rec_y;
    } else if (op == NI_SCALER_OPCODE_OVERLAY)
    {
        // fixed params
        // set the in_rec params to the w/h of overlay(the upper) frames
        p_scaler_params->in_rec_width = p_scaler_params->input_width;
        p_scaler_params->in_rec_height = p_scaler_params->input_height;

        // the output w/h is the main frame's w/h (main frame is the lower/background frame)
        p_scaler_params->out_rec_width = p_scaler_params->output_width;
        p_scaler_params->out_rec_height = p_scaler_params->output_height;
        p_scaler_params->out_rec_x = 0;
        p_scaler_params->out_rec_y = 0;
        p_scaler_params->rgba_color = 0;

        // params set by user
        p_scaler_params->in_rec_x = in_rec_x;
        p_scaler_params->in_rec_y = in_rec_y;
    }
}

/*!*****************************************************************************
 *  \brief  open scaler session
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int scaler_session_open(ni_session_context_t *p_scaler_ctx, int iXcoderGUID, ni_scaler_opcode_t op)
{
    int ret = 0;

    p_scaler_ctx->session_id = NI_INVALID_SESSION_ID;

    p_scaler_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_scaler_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_scaler_ctx->hw_id = iXcoderGUID;
    p_scaler_ctx->device_type = NI_DEVICE_TYPE_SCALER;
    p_scaler_ctx->scaler_operation = op;
    p_scaler_ctx->keep_alive_timeout = NI_DEFAULT_KEEP_ALIVE_TIMEOUT;

    ret = ni_device_session_open(p_scaler_ctx, NI_DEVICE_TYPE_SCALER);

    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_scaler_session_open() failure!\n");
        return -1;
    } else
    {
#ifdef _WIN32
        ni_log(NI_LOG_INFO, "Scaler session open: device_handle %p, session_id %u.\n",
               p_scaler_ctx->device_handle, p_scaler_ctx->session_id);
#else
        ni_log(NI_LOG_INFO, "Scaler session open: device_handle %d, session_id %u.\n",
               p_scaler_ctx->device_handle, p_scaler_ctx->session_id);
#endif
        return 0;
    }
}

/*!*****************************************************************************
 *  \brief  Launch scaler operation and get the result hw frame
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int launch_scaler_operation(ni_session_context_t *p_scaler_ctx, int iXcoderGUID,
                            ni_frame_t *p_frame_in_up, ni_frame_t *p_frame_in_bg,
                            ni_session_data_io_t *p_data_out, ni_scaler_input_params_t scaler_params)
{
    int ret = 0;
    niFrameSurface1_t *frame_surface_up;
    niFrameSurface1_t *frame_surface_bg;
    niFrameSurface1_t *frame_surface_output;

    if (p_scaler_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        // Open scaler session
        if (0 != scaler_session_open(p_scaler_ctx, iXcoderGUID, scaler_params.op))
        {
            ni_log(NI_LOG_ERROR, "Error: scaler open session error\n");
            return -1;
        }

        // init scaler hwframe pool
        if (0 != ni_scaler_frame_pool_alloc(p_scaler_ctx, scaler_params))
        {
            ni_log(NI_LOG_ERROR, "Error: init filter hwframe pool\n");
            return -1;
        }
    }

    // allocate a ni_frame_t structure on the host PC
    ret = ni_frame_buffer_alloc_hwenc(&p_data_out->data.frame,
                                      scaler_params.output_width,
                                      scaler_params.output_height, 0);
    if (ret != 0)
    {
        return -1;
    }

    // out_frame retrieved from decoder
    frame_surface_up = (niFrameSurface1_t *)(p_frame_in_up->p_data[3]);
    frame_surface_bg = (niFrameSurface1_t *)(p_frame_in_bg->p_data[3]);

    // Allocate scaler input frame
    ret = ni_scaler_input_frame_alloc(p_scaler_ctx, scaler_params, frame_surface_up);
    if (ret != 0)
    {
        return -1;
    }

    // Allocate scaler destination frame.
    ret = ni_scaler_dest_frame_alloc(p_scaler_ctx, scaler_params, frame_surface_bg);
    if (ret != 0)
    {
        return -1;
    }

    // Retrieve hardware frame info from 2D engine and put it in the ni_frame_t structure.
    ret = ni_device_session_read_hwdesc(p_scaler_ctx, p_data_out, NI_DEVICE_TYPE_SCALER);
    frame_surface_output = (niFrameSurface1_t *)(p_data_out->data.frame.p_data[3]);
    ni_log(NI_LOG_DEBUG, "%s: output FID %u \n", __func__,
           frame_surface_output->ui16FrameIdx);
    if (ret < 0)
    {
        ni_frame_buffer_free(p_frame_in_up);
        ni_frame_buffer_free(p_frame_in_bg);
        ni_frame_buffer_free(&p_data_out->data.frame);
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  Use crop->pad->overlay to simulate a drawbox filter.
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int drawbox_filter(ni_session_context_t *p_crop_ctx, ni_session_context_t *p_pad_ctx,
                   ni_session_context_t *p_overlay_ctx, ni_session_context_t *p_fmt_ctx,
                   ni_frame_t *p_frame_in, ni_session_data_io_t *p_data_out, ni_drawbox_params_t *p_box_params,
                   int iXcoderGUID, int input_format, int output_format)
{
    // set default box params
    int box_width = 80;
    int box_height = 60;
    int box_x = 160;
    int box_y = 100;
    int line_width = 4;
    if (p_box_params)
    {
        box_width = ((int)((p_box_params->box_w + 1) / 2) * 2);
        box_height = ((int)((p_box_params->box_h + 1) / 2) * 2);
        box_x = p_box_params->box_x;
        box_y = p_box_params->box_y;
    }

    /*
        To simulate a drawbox filter, we need to
        1. Crop at the box set position
        2. Padding the crop output frame for fixed fixels (depends on the line_width of the box)
           Recycle the crop hwFrame
        3. Overlay the padding result on the original frame
           Recycle the padding hwFrame
        4. if format-change is needed, launch a scale operation and do format change
           Recycle the overlay hwFrame
    */

    int ret = 0;
    niFrameSurface1_t *p_surface_in;
    p_surface_in = (niFrameSurface1_t *)(p_frame_in->p_data[3]);

    ni_scaler_input_params_t crop_params = {0};
    crop_params.input_format = input_format;
    crop_params.input_width = p_surface_in->ui16width;
    crop_params.input_height = p_surface_in->ui16height;
    crop_params.output_format = GC620_I420;
    crop_params.output_width = box_width;
    crop_params.output_height = box_height;
    init_scaler_params(&crop_params, NI_SCALER_OPCODE_CROP, box_width,
                       box_height, box_x, box_y, 0, 0);
    ni_session_data_io_t crop_data = {0};
    ret = launch_scaler_operation(p_crop_ctx, iXcoderGUID, p_frame_in,
                                  p_frame_in, &crop_data, crop_params);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", crop_params.op);
        return -1;
    }
    niFrameSurface1_t *crop_frame_surface =
        (niFrameSurface1_t *)(crop_data.data.frame.p_data[3]);
    ni_hw_frame_ref(crop_frame_surface);

    ni_scaler_input_params_t pad_params = {0};
    pad_params.input_format = input_format;
    pad_params.input_width = crop_params.output_width;
    pad_params.input_height = crop_params.output_height;
    pad_params.output_format = GC620_I420;
    pad_params.output_width = crop_params.output_width + line_width * 2;
    pad_params.output_height = crop_params.output_height + line_width * 2;
    init_scaler_params(&pad_params, NI_SCALER_OPCODE_PAD, 0, 0, 0, 0,
                       line_width, line_width);
    ni_session_data_io_t pad_data = {0};
    ret = launch_scaler_operation(p_pad_ctx, iXcoderGUID, &crop_data.data.frame,
                                  &crop_data.data.frame, &pad_data, pad_params);
    // recycle HwFrameIdx first, then free the frame
    ni_hw_frame_unref(crop_frame_surface->ui16FrameIdx);
    ni_frame_buffer_free(&(crop_data.data.frame));
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", pad_params.op);
        return -1;
    }
    niFrameSurface1_t *pad_frame_surface =
        (niFrameSurface1_t *)(pad_data.data.frame.p_data[3]);
    ni_hw_frame_ref(pad_frame_surface);

    ni_scaler_input_params_t overlay_params = {0};
    overlay_params.input_format = input_format;
    overlay_params.input_width = pad_params.output_width;
    overlay_params.input_height = pad_params.output_height;
    overlay_params.output_format = GC620_I420;
    overlay_params.output_width = p_surface_in->ui16width;
    overlay_params.output_height = p_surface_in->ui16height;
    int overlay_x = box_x - line_width;
    int overlay_y = box_y - line_width;
    init_scaler_params(&overlay_params, NI_SCALER_OPCODE_OVERLAY, 0, 0,
                       overlay_x, overlay_y, 0, 0);
    ni_session_data_io_t ovly_data = {0};
    if (output_format == GC620_I420)
        ret = launch_scaler_operation(p_overlay_ctx, iXcoderGUID,
                                      &pad_data.data.frame, p_frame_in,
                                      p_data_out, overlay_params);
    else
        ret = launch_scaler_operation(p_overlay_ctx, iXcoderGUID,
                                      &pad_data.data.frame, p_frame_in,
                                      &ovly_data, overlay_params);
    // recycle HwFrameIdx first, then free the frame
    ni_hw_frame_unref(pad_frame_surface->ui16FrameIdx);
    ni_frame_buffer_free(&(pad_data.data.frame));
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", overlay_params.op);
        return -1;
    }

    if (output_format != GC620_I420)   // use scale filter to do format change
    {
        niFrameSurface1_t *ovly_frame_surface =
            (niFrameSurface1_t *)(ovly_data.data.frame.p_data[3]);
        ni_hw_frame_ref(ovly_frame_surface);
        ovly_frame_surface->ui16width = overlay_params.output_width;
        ovly_frame_surface->ui16height = overlay_params.output_height;
        ret = scale_filter(p_fmt_ctx, &(ovly_data.data.frame), p_data_out,
                           iXcoderGUID, overlay_params.output_width,
                           overlay_params.output_height, GC620_I420,
                           output_format);
        ni_hw_frame_unref(ovly_frame_surface->ui16FrameIdx);
        ni_frame_buffer_free(&ovly_data.data.frame);
        if (ret != 0)
        {
            ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation 0\n");
            return -1;
        }
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Do a scale and/or format-change operation.
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int scale_filter(ni_session_context_t *p_ctx, ni_frame_t *p_frame_in,
                 ni_session_data_io_t *p_data_out, int iXcoderGUID,
                 int scale_width, int scale_height, int in_format, int out_format)
{
    int ret;
    niFrameSurface1_t *p_surface_in;
    ni_scaler_input_params_t scale_params;

    p_surface_in = (niFrameSurface1_t *)p_frame_in->p_data[3];
    scale_params.output_format = out_format;   // rgba or bgrp or yuv420p;
    scale_params.output_width = scale_width;
    scale_params.output_height = scale_height;
    scale_params.input_format = in_format;
    scale_params.input_width = p_surface_in ? p_surface_in->ui16width : 0;
    scale_params.input_height = p_surface_in ? p_surface_in->ui16height : 0;
    init_scaler_params(&scale_params, NI_SCALER_OPCODE_SCALE, 0, 0, 0, 0, 0, 0);

    ret = launch_scaler_operation(p_ctx, iXcoderGUID, p_frame_in, p_frame_in,
                                  p_data_out, scale_params);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Failed to lauch scaler operation %d\n", scale_params.op);
        return -1;
    }

    return ret;
}