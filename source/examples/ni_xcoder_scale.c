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
 *  \file   ni_xcoder_scale.c
 *
 *  \brief  Video scaling demo application directly using Netint Libxcoder API
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_filter_utils.h"
#include "ni_log.h"
#include "ni_util.h"

#ifdef _WIN32
#include "ni_getopt.h"
#elif __linux__ || __APPLE__
#include <getopt.h>
#include <unistd.h>
#endif

static void print_usage(void)
{
    ni_log(NI_LOG_ERROR,
        "Video scaling demo application directly using Netint Libxcoder version %s\n"
        "Usage: ni_xcoder_scale [options]\n"
        "\n"
        "options:\n"
        "-h | --help                    Show this message.\n"
        "-v | --version                 Print version info.\n"
        "-i | --input                   (Required) Input file path.\n"
        "-o | --output                  (Required) Output file path.\n"
        "                               Can be specified multiple (max %d) times\n"
        "                               to run multiple scaling instances simultaneously.\n"
        "-l | --loglevel                Set loglevel of this application and libxcoder API.\n"
        "                               [none, fatal, error, info, debug, trace]\n"
        "                               (Default: info)\n"
        "-c | --card                    Set card index to use.\n"
        "                               See `ni_rsrc_mon` for info of cards on system.\n"
        "                               (Default: 0)\n"
        "-r | --repeat                  To loop input X times. Must be a positive integer\n"
        "                               (Default: 1)\n"
        "-p | --pix_fmt                 Indicate the pixel format of the input.\n"
        "                               [yuv420p, yuv420p10le, nv12, p010le, rgba, bgra, argb, abgr, bgr0]\n"
        "                               (Default: yuv420p)\n"
        "-s | --size                    (Required) Resolution of input file in format WIDTHxHEIGHT.\n"
        "                               (eg. '1920x1080')\n"
        "-f | --vf                      Video filter params. The only supported filter in this demo is:\n"
        "                               ni_quadra_scale - supported params [width, height, format]\n"
        "                               e.g. ni_quadra_scale=width=1280:height=720:format=yuv420p\n"
        "                               (Default: \"\")\n"
        , NI_XCODER_REVISION, MAX_OUTPUT_FILES);
}

int main(int argc, char *argv[])
{
    int i, ret = 0;
    ni_demo_context_t ctx = {0};
    char in_filename[FILE_NAME_LEN] = {0};
    char out_filename[MAX_OUTPUT_FILES][FILE_NAME_LEN] = {0};
    FILE *input_fp = NULL;
    FILE *output_fp[MAX_OUTPUT_FILES] = {0};
    int o_index = 0, f_index = 0;
    int output_total = 0;
    int input_width = 0, input_height = 0;
    int bit_depth_factor[MAX_OUTPUT_FILES] = {0};
    int output_width[MAX_OUTPUT_FILES] = {0};
    int output_height[MAX_OUTPUT_FILES] = {0};
    int chunk_size = 0, eos = 0;
    void *yuv_buf = NULL;
    ni_log_level_t log_level;
    int xcoderGUID = 0;
    char *n;
    char filter_conf_params[MAX_OUTPUT_FILES][2048] = {0};
    ni_session_context_t upl_ctx = {0};
    ni_session_context_t sca_ctx[MAX_OUTPUT_FILES] = {0};
    uint64_t current_time, previous_time;
    ni_session_data_io_t sw_in_frame = {0};
    ni_session_data_io_t hw_in_frame = {0};
    ni_session_data_io_t scaled_frame[MAX_OUTPUT_FILES] = {0};
    ni_session_data_io_t download_frame[MAX_OUTPUT_FILES] = {0};
    niFrameSurface1_t *p_hwframe = NULL;
    ni_scale_params_t scale_params[MAX_OUTPUT_FILES] = {0};
    ni_drawbox_params_t drawbox_params = {0};
    ni_pix_fmt_t pix_fmt = NI_PIX_FMT_YUV420P;

    int opt;
    int opt_index;
    const char *opt_string = "hvi:o:l:c:r:p:s:f:";
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"loglevel", required_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"repeat", required_argument, NULL, 'r'},
        {"pix_fmt", required_argument, NULL, 'p'},
        {"size", required_argument, NULL, 's'},
        {"vf", required_argument, NULL, 'f'},
        {NULL, 0, NULL, 0},
    };

    while ((opt = getopt_long(argc, argv, opt_string, long_options, &opt_index)) != -1)
    {
        switch (opt)
        {
            case 'h':
                print_usage();
                ret = 0;
                goto end;
            case 'v':
                print_version();
                ret = 0;
                goto end;
            case 'i':
                ni_strcpy(in_filename, FILE_NAME_LEN, optarg);
                break;
            case 'o':
                if (o_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of output files cannot exceed %d\n", MAX_OUTPUT_FILES);
                    ret = -1;
                    goto end;
                }

                for (i = 0; i < o_index; i++)
                {
                    if (0 == strcmp(out_filename[i], optarg))
                    {
                        ni_log(NI_LOG_ERROR, "Error: output file names must be unique: %s\n", optarg);
                        ret = -1;
                        goto end;
                    }
                }

                ni_strcpy(out_filename[o_index], FILE_NAME_LEN, optarg);
                o_index++;
                break;
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                }
                else
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -l | --loglevel option\n"
                           "Must be one of [none, fatal, error, info, debug, trace]\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'c':
                xcoderGUID = (int)strtol(optarg, &n, 10);
                if (n == optarg || *n != '\0' || xcoderGUID < 0)
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -c | --card option\n"
                           "Must be a non-negative integer\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'r':
                ctx.loops_left = strtol(optarg, &n, 10);
                if (n == optarg || *n != '\0' || !(ctx.loops_left >= 1))
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -r | --repeat option\n"
                           "Must be a positive integer\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'p':
                pix_fmt = ni_pixel_format_search(optarg);
                if (pix_fmt == NI_PIX_FMT_NONE)
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -p | --pix_fmt option\n"
                            "Must be one of [yuv420p, yuv420p10le, nv12, p010le, rgba, gbra, argb, abgr, bgr0, yuv444p]\n",
                            optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 's':
                input_width = (int)strtol(optarg, &n, 10);
                input_height = atoi(n + 1);
                if ((*n != 'x') || (input_width <= 0 || input_height <= 0))
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -s | --size option\n"
                           "Must be in format [WIDTHxHEIGHT] (e.g. 1920x1080)\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'f':
                if (f_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of filter config cannot exceed %d\n", MAX_OUTPUT_FILES);
                    ret = -1;
                    goto end;
                }
                ni_strcpy(filter_conf_params[f_index], sizeof(filter_conf_params[f_index]), optarg);
                f_index++;
                break;
            default:
                print_usage();
                ret = -1;
                goto end;
        }
    }

    if (!in_filename[0]) {
        ni_log(NI_LOG_ERROR, "Error: Missing input file argument (-i | --input)\n");
        ret = -1;
        goto end;
    }

    if (o_index == 0) {
        ni_log(NI_LOG_ERROR, "Error: Missing output file argument (-o | --output)\n");
        ret = -1;
        goto end;
    }

    if (ni_posix_memalign(&yuv_buf, sysconf(_SC_PAGESIZE), MAX_YUV_FRAME_SIZE))
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate YUV data buffer\n");
        ret = -1;
        goto end;
    }

    ni_fopen(&(input_fp), in_filename, "rb");
    if (!input_fp)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to open input %s\n", in_filename);
        ret = -1;
        goto end;
    }
    ni_log(NI_LOG_INFO, "Opened input file: %s\n", in_filename);

    ctx.total_file_size = get_total_file_size(input_fp);
    ctx.curr_file_offset = 0;

    output_total = o_index;
    for (i = 0; i < output_total; i++)
    {
        if (strcmp(out_filename[i], "null") != 0 &&
            strcmp(out_filename[i], "/dev/null") != 0)
        {
            output_fp[i] = NULL;
            ni_fopen(&(output_fp[i]), out_filename[i], "wb");
            if (!output_fp[i])
            {
                ni_log(NI_LOG_ERROR, "Error: Failed to open %s\n", out_filename[i]);
                ret = -1;
                goto end;
            }
            ni_log(NI_LOG_INFO, "Opened output file: %s\n", out_filename[i]);
        } else
        {
            output_fp[i] = NULL;
            ni_log(NI_LOG_INFO, "Note: Requested NULL output for index %d, no output file will be generated\n", i);
        }
    }

    for (i = 0; i < output_total; i++)
    {
        output_width[i] = input_width;
        output_height[i] = input_height;
    }

    if (ni_device_session_context_init(&upl_ctx) < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: init uploader context error\n");
        ret = -1;
        goto end;
    }

    for (i = 0; i < output_total; i++)
    {
        if (filter_conf_params[i][0])
        {
            ret = retrieve_filter_params(filter_conf_params[i], &scale_params[i], &drawbox_params);
            if (ret)
            {
                ni_log(NI_LOG_ERROR, "Error: failed to parse filter parameters: %s\n", filter_conf_params[i]);
                goto end;
            }
        }

        if (!scale_params[i].enabled)
        {
            ni_log(NI_LOG_ERROR, "Error: scale parameters not set for output #%d\n", i);
            ret = -1;
            goto end;
        }
        else
        {
            ret = ni_device_session_context_init(&sca_ctx[i]);
            if (ret)
            {
                ni_log(NI_LOG_ERROR, "Error: Failed to init scale context\n");
                goto end;
            }
            output_width[i] = scale_params[i].width;
            output_height[i] = scale_params[i].height;
            bit_depth_factor[i] = 1;
            if (scale_params[i].format == GC620_I010 || scale_params[i].format == GC620_P010_MSB)
            {
                bit_depth_factor[i] = 2;
            }
        }
    }

    ret = uploader_open_session(&upl_ctx, xcoderGUID, input_width < NI_MIN_WIDTH ? NI_MIN_WIDTH : input_width,
                                input_height < NI_MIN_HEIGHT ? NI_MIN_HEIGHT : input_height, pix_fmt, 0, 3);
    if (ret != 0)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to open uploader session\n");
        goto end;
    }

    for (i = 0; i < output_total; i++)
    {
        ni_log(NI_LOG_INFO, "(Task %d) Starting to scale: video resolution %dx%d -> %dx%d\n",
               i, input_width, input_height, output_width[i], output_height[i]);
    }

    ctx.start_time = ni_gettime_ns();
    previous_time = ctx.start_time;

    while (!eos)
    {
        chunk_size = read_yuv_from_file(&ctx, input_fp, yuv_buf, input_width, input_height, pix_fmt,
                                        NI_SW_PIX_FMT_NONE, &eos, upl_ctx.session_run_state);
        if (chunk_size < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: read yuv file error\n");
            ret = -1;
            break;
        }

        ret = upload_send_data_get_desc(&ctx, &upl_ctx, &sw_in_frame, &hw_in_frame, input_width,
                                        input_height, eos ? NULL : yuv_buf);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: upload frame error\n");
            break;
        }

        p_hwframe = (niFrameSurface1_t *)hw_in_frame.data.frame.p_data[3];
        ni_hw_frame_ref(p_hwframe);
        for (i = 0; i < output_total; i++)
        {
            scale_filter(&sca_ctx[i], &hw_in_frame.data.frame, &scaled_frame[i], xcoderGUID,
                         scale_params[i].width, scale_params[i].height,
                         ni_to_gc620_pix_fmt(pix_fmt), scale_params[i].format);
            if (ret)
            {
                ni_log(NI_LOG_ERROR, "Error: Failed to run scale filter\n");
                goto end;
            }

            p_hwframe = (niFrameSurface1_t *)scaled_frame[i].data.frame.p_data[3];
            p_hwframe->ui16width = scale_params[i].width;
            p_hwframe->ui16height = scale_params[i].height;
            p_hwframe->bit_depth = bit_depth_factor[i];
            if (scale_params[i].format == GC620_I420 || scale_params[i].format == GC620_I010)
                p_hwframe->encoding_type = NI_PIXEL_PLANAR_FORMAT_PLANAR;
            else
                p_hwframe->encoding_type = NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
            ni_hw_frame_ref(p_hwframe);
            ret = hwdl_frame(&sca_ctx[i], &download_frame[i], &scaled_frame[i].data.frame,
                             gc620_to_ni_pix_fmt(scale_params[i].format));
            if (ret <= 0)
            {
                ni_log(NI_LOG_ERROR, "Error: Failed to download output frame\n");
                goto end;
            }
            ni_hw_frame_unref(p_hwframe->ui16FrameIdx);

            ret = write_rawvideo_data(output_fp[i], (output_width[i] * bit_depth_factor[i] + 127) / 128 * 128,
                                      (output_height[i] + 1) / 2 * 2, output_width[i], output_height[i],
                                      gc620_to_ni_pix_fmt(scale_params[i].format), &download_frame[i].data.frame);
        }
        p_hwframe = (niFrameSurface1_t *)hw_in_frame.data.frame.p_data[3];
        ni_hw_frame_unref(p_hwframe->ui16FrameIdx);

        current_time = ni_gettime_ns();
        if (current_time - previous_time >= (uint64_t)1000000000)
        {
            ni_log(NI_LOG_INFO, "Scaler stats: processed %u frames, fps %.2f\n",
                   ctx.num_frames_received,
                   (float)ctx.num_frames_received / (float)(current_time - ctx.start_time) * (float)1000000000);
            previous_time = current_time;
        }
    }

    ni_device_session_close(&upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
    for (i = 0; i < output_total; i++)
    {
        ni_device_session_close(&sca_ctx[i], 1, NI_DEVICE_TYPE_SCALER);
    }

end:
    ni_frame_buffer_free(&sw_in_frame.data.frame);
    ni_frame_buffer_free(&hw_in_frame.data.frame);
    for (i = 0; i < output_total; i++)
    {
        ni_frame_buffer_free(&scaled_frame[i].data.frame);
        ni_frame_buffer_free(&download_frame[i].data.frame);
    }

    ni_device_session_context_clear(&upl_ctx);
    for (i = 0; i < output_total; i++)
    {
        ni_device_session_context_clear(&sca_ctx[i]);
        if (output_fp[i] != NULL)
        {
            fclose(output_fp[i]);
        }
    }

    if (input_fp)
    {
        fclose(input_fp);
    }

    ni_aligned_free(yuv_buf);

    for(i = 0; i < MAX_OUTPUT_FILES; ++i)
    {
        free(ctx.enc_pts_queue[i]);
        ctx.enc_pts_queue[i] = NULL;
    }

    return ret;
}
