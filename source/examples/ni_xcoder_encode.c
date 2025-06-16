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
 *  \file   ni_xcoder_encode.c
 *
 *  \brief  Video encoding demo application directly using Netint Libxcoder API
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_encode_utils.h"
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
        "Video encoding demo application directly using Netint Libxcoder version %s\n"
        "Usage: ni_xcoder_encode [options]\n"
        "\n"
        "options:\n"
        "-h | --help                    Show this message.\n"
        "-v | --version                 Print version info.\n"
        "-i | --input                   (Required) Input file path.\n"
        "                               Can be specified multiple (max %d) times\n"
        "                               to concatenate inputs with different resolution together (sequence change)\n"
        "-o | --output                  (Required) Output file path.\n"
        "                               Can be specified multiple (max %d) times\n"
        "                               to run multiple encoding instances simultaneously.\n"
        "-m | --enc-codec               (Required) Encoder codec format.\n"
        "                               [a|avc, h|hevc, x|av1, o|obu]\n"
        "                               (x is in ivf container format, o is output raw AV1 OBU only)\n"
        "-l | --loglevel                Set loglevel of this application and libxcoder API.\n"
        "                               [none, fatal, error, info, debug, trace]\n"
        "                               (Default: info)\n"
        "-c | --card                    Set card index to use.\n"
        "                               See `ni_rsrc_mon` for info of cards on system.\n"
        "                               (Default: 0)\n"
        "-r | --repeat                  To loop input X times. Must be a positive integer\n"
        "                               (Default: 1)\n"
        "-k | --readframerate           Read input at specified frame rate.\n"
        "-p | --pix_fmt                 Indicate the pixel format of the input.\n"
        "                               [yuv420p, yuv420p10le, nv12, p010le, rgba, bgra, argb, abgr, bgr0, yuv444p]\n"
        "                               (Default: yuv420p)\n"
        "-s | --size                    (Required) Resolution of input file in format WIDTHxHEIGHT.\n"
        "                               (eg. '1920x1080')\n"
        "-e | --encoder-params          Encoding params. See \"Encoding Parameters\" chapter in\n"
        "                               QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               Can be specified multiple (max %d) times,\n"
        "                               must match the number of -o specified.\n"
        "                               (Default: \"\")\n"
        "-g | --encoder-gop             Custom GOP for encoding. See \"Custom Gop Structure\" chapter in\n"
        "                               QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               gopPresetIdx must be set to 0 to be in effect.\n"
        "                               (Default: \"\")\n"
        "-u | --hwupload                (No argument) When enabled, upload raw frame to device first before encoding\n"
        "                               Multiple input files and yuv444p format input are not supported in this mode\n"
        , NI_XCODER_REVISION, MAX_INPUT_FILES, MAX_OUTPUT_FILES, MAX_OUTPUT_FILES);
}

int main(int argc, char *argv[])
{
    int i, ret = 0;
    ni_demo_context_t ctx = {0};
    char in_filename[MAX_INPUT_FILES][FILE_NAME_LEN] = {0};
    char out_filename[MAX_OUTPUT_FILES][FILE_NAME_LEN] = {0};
    FILE* input_fp[MAX_INPUT_FILES] = {0};
    FILE* output_fp[MAX_OUTPUT_FILES] = {0};
    int i_index = 0, s_index = 0, o_index = 0, e_index = 0, g_index = 0;
    int input_total = 0, output_total = 0;
    int enc_codec_format = -1;
    ni_log_level_t log_level;
    int xcoderGUID = 0;
    char *n;
    ni_pix_fmt_t pix_fmt = NI_PIX_FMT_YUV420P;
    ni_sw_pix_fmt_t sw_pix_fmt = NI_SW_PIX_FMT_NONE;
    int video_width[MAX_INPUT_FILES] = {0};
    int video_height[MAX_INPUT_FILES] = {0};
    char enc_conf_params[MAX_OUTPUT_FILES][2048] = {0};
    char enc_gop_params[MAX_OUTPUT_FILES][2048] = {0};
    ni_xcoder_params_t *p_enc_api_param = NULL;
    int hwupload = 0;
    int first_frame_uploaded = 0;
    void *yuv_buf = NULL;
    int send_rc = NI_TEST_RETCODE_SUCCESS, receive_rc = NI_TEST_RETCODE_SUCCESS;
    ni_session_context_t enc_ctx[MAX_OUTPUT_FILES] = {0};
    ni_session_context_t upl_ctx = {0};
    ni_session_context_t sca_ctx = {0};
    ni_session_data_io_t in_frame = {0};
    ni_session_data_io_t out_packet[MAX_OUTPUT_FILES] = {0};
    ni_session_data_io_t sw_pix_frame[2] = {0};
    ni_session_data_io_t scale_frame = {0};
    ni_session_data_io_t swin_frame = {0};
    ni_session_data_io_t *p_in_frame;
    uint64_t current_time, previous_time;
    int end_of_all_streams = 0, read_size = 0, eos = 0;
    niFrameSurface1_t *p_hwframe = NULL;

    int opt;
    int opt_index;
    const char *opt_string = "hvi:o:m:l:c:r:k:p:s:e:g:u";
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"enc-codec", required_argument, NULL, 'm'},
        {"loglevel", required_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"repeat", required_argument, NULL, 'r'},
        {"readframerate", required_argument, NULL, 'k'},
        {"pix_fmt", required_argument, NULL, 'p'},
        {"size", required_argument, NULL, 's'},
        {"encoder-params", required_argument, NULL, 'e'},
        {"encoder-gop", required_argument, NULL, 'g'},
        {"hwupload", no_argument, NULL, 'u'},
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
                if (i_index == MAX_INPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of input files cannot exceed %d\n", MAX_INPUT_FILES);
                    ret = -1;
                    goto end;
                }
                strcpy(in_filename[i_index], optarg);
                i_index++;
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

                strcpy(out_filename[o_index], optarg);
                o_index++;
                break;
            case 'm':
                // Accept both upper and lower case
                for (i = 0; i < strlen(optarg); i++)
                {
                    optarg[i] = (char)tolower((unsigned char)optarg[i]);
                }
                if (strcmp(optarg, "a") == 0 || strcmp(optarg, "avc") == 0)
                {
                    enc_codec_format = NI_CODEC_FORMAT_H264;
                }
                else if (strcmp(optarg, "h") == 0 || strcmp(optarg, "hevc") == 0)
                {
                    enc_codec_format = NI_CODEC_FORMAT_H265;
                }
                else if (strcmp(optarg, "x") == 0 || strcmp(optarg, "av1") == 0)
                {
                    enc_codec_format = NI_CODEC_FORMAT_AV1;
                }
                else if (strcmp(optarg, "o") == 0 || strcmp(optarg, "obu") == 0)
                {
                    enc_codec_format = NI_CODEC_FORMAT_AV1;
                    ctx.av1_output_obu = 1;
                }
                else
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -m | --dec-codec option\n"
                           "Must be one of [a|avc, h|hevc, x|av1, o|obu]\n", optarg);
                    ret = -1;
                    goto end;
                }
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
            case 'k':
                ctx.read_framerate = (int)strtol(optarg, &n, 10);
                if (n == optarg || *n != '\0' || ctx.read_framerate < 0)
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -k | --readframerate option\n"
                           "Must be a non-negative integer\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'p':
                pix_fmt = ni_pixel_format_search(optarg);
                if (pix_fmt == NI_PIX_FMT_NONE)
                {
                    if (!strcmp(optarg, "yuv444p"))
                    {
                        sw_pix_fmt = NI_SW_PIX_FMT_YUV444P;
                    }
                    else
                    {
                        ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -p | --pix_fmt option\n"
                               "Must be one of [yuv420p, yuv420p10le, nv12, p010le, rgba, gbra, argb, abgr, bgr0, yuv444p]\n",
                               optarg);
                        ret = -1;
                        goto end;
                    }
                }
                break;
            case 's':
                if (s_index == MAX_INPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of input resolutions cannot exceed %d\n", MAX_INPUT_FILES);
                    ret = -1;
                    goto end;
                }
                video_width[s_index] = (int)strtol(optarg, &n, 10);
                video_height[s_index] = atoi(n + 1);
                if ((*n != 'x') || (video_width[s_index] <= 0 || video_height[s_index] <= 0))
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -s | --size option\n"
                           "Must be in format [WIDTHxHEIGHT] (e.g. 1920x1080)\n", optarg);
                    ret = -1;
                    goto end;
                }
                s_index++;
                break;
            case 'e':
                if (e_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of encoder config cannot exceed %d\n", MAX_OUTPUT_FILES);
                    ret = -1;
                    goto end;
                }
                strcpy(enc_conf_params[e_index], optarg);
                e_index++;
                break;
            case 'g':
                if (g_index == MAX_OUTPUT_FILES)
                {
                    ni_log(NI_LOG_ERROR, "Error: number of encoder gop settings cannot exceed %d\n", MAX_OUTPUT_FILES);
                    ret = -1;
                    goto end;
                }
                strcpy(enc_gop_params[g_index], optarg);
                g_index++;
                break;
            case 'u':
                hwupload = 1;
                break;
            default:
                print_usage();
                ret = -1;
                goto end;
        }
    }

    if (i_index == 0) {
        ni_log(NI_LOG_ERROR, "Error: Missing input file argument (-i | --input)\n");
        ret = -1;
        goto end;
    }

    if (o_index == 0) {
        ni_log(NI_LOG_ERROR, "Error: Missing output file argument (-o | --output)\n");
        ret = -1;
        goto end;
    }

    if (s_index != i_index) {
        ni_log(NI_LOG_ERROR, "Error: Number of input resolution specified does not match number of input files\n");
        ret = -1;
        goto end;
    }

    if (enc_codec_format == -1) {
        ni_log(NI_LOG_ERROR, "Error: Missing encoder codec argument (-m | --enc-codec)\n");
        ret = -1;
        goto end;
    }

    input_total = i_index;
    i_index = 0;
    if (ctx.loops_left > 1 && input_total > 1)
    {
        ni_log(NI_LOG_ERROR, "Error: multiple input files not supported when loops %u greater than 1\n", ctx.loops_left);
        ret = -1;
        goto end;
    }

    output_total = o_index;
    if (sw_pix_fmt == NI_SW_PIX_FMT_YUV444P && output_total != 2)
    {
        ni_log(NI_LOG_ERROR, "Error: Must indicate 2 output files for yuv444p encoding\n");
        ret = -1;
        goto end;
    }

    // Check for features not supported with hwupload
    if (hwupload && ctx.read_framerate)
    {
        ni_log(NI_LOG_ERROR, "Error: -k | --readframerate option is not supported in hwupload mode\n");
        ret = -1;
        goto end;
    }
    if (hwupload && input_total > 1)
    {
        ni_log(NI_LOG_ERROR, "Error: multiple input (sequence change) is not supported in hwupload mode\n");
        ret = -1;
        goto end;
    }
    if (hwupload && sw_pix_fmt == NI_SW_PIX_FMT_YUV444P)
    {
        ni_log(NI_LOG_ERROR, "Error: yuv444p input is not supported in hwupload mode\n");
        ret = -1;
        goto end;
    }

    // Only in hwupload mode, the pixel formats not directly supported by encoder
    // will be converted to yuv420p using 2D engine
    if (!hwupload && !is_ni_enc_pix_fmt(pix_fmt) && sw_pix_fmt == NI_SW_PIX_FMT_NONE)
    {
        ni_log(NI_LOG_ERROR, "Error: pixel format %s is only supported in hwupload mode\n",
               ni_pixel_format_name(pix_fmt));
        ret = -1;
        goto end;
    }

    if (ni_posix_memalign(&yuv_buf, sysconf(_SC_PAGESIZE), MAX_YUV_FRAME_SIZE))
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate YUV data buffer\n");
        ret = -1;
        goto end;
    }

    for (i = 0; i < input_total; i++)
    {
        if (!in_filename[i][0])
        {
            ni_log(NI_LOG_ERROR, "Error: invalid input file %d\n", i);
            ret = -1;
            goto end;
        }

        input_fp[i] = fopen(in_filename[i], "rb");

        if (!input_fp[i])
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to open input %s\n", in_filename[i]);
            ret = -1;
            goto end;
        }
        ni_log(NI_LOG_INFO, "Opened input file: %s\n", in_filename[i]);
    }

    ctx.total_file_size = get_total_file_size(input_fp[0]);
    ctx.curr_file_offset = 0;

    for (i = 0; i < output_total; i++)
    {
        if (strcmp(out_filename[i], "null") != 0 &&
            strcmp(out_filename[i], "/dev/null") != 0)
        {
            output_fp[i] = fopen(out_filename[i], "wb");
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

    p_enc_api_param = calloc(output_total, sizeof(ni_xcoder_params_t));
    if (!p_enc_api_param)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for p_enc_api_param\n");
        ret = -1;
        goto end;
    }

    for (i = 0; i < output_total; i++)
    {
        if (ni_device_session_context_init(&enc_ctx[i]) < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: init encoder %d context error\n", i);
            ret = -1;
            goto end;
        }
    }

    for (i = 0; i < output_total; i++)
    {
        enc_ctx[i].codec_format = enc_codec_format;
    }

    ctx.start_time = ni_gettime_ns();
    previous_time = ctx.start_time;

    if (hwupload)
    {
        if (ni_device_session_context_init(&upl_ctx) < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: init uploader context error\n");
            ret = -1;
            goto end;
        }

        if (ni_device_session_context_init(&sca_ctx) < 0)
        {
            ni_log(NI_LOG_ERROR, "Error: init scale context error\n");
            ret = -1;
            goto end;
        }

        ni_log(NI_LOG_INFO, "Starting hwupload + encoding mode: video resolution %dx%d\n",
               video_width[0], video_height[0]);
        // buffers by downstream entity like encoders
        ret = uploader_open_session(&upl_ctx, xcoderGUID, video_width[0] < NI_MIN_WIDTH ? NI_MIN_WIDTH : video_width[0],
                                    video_height[0] < NI_MIN_HEIGHT ? NI_MIN_HEIGHT : video_height[0], pix_fmt, 0, 3);
        if (ret != 0)
        {
            goto end;
        }

        p_hwframe = hwupload_frame(&ctx, &upl_ctx, &sca_ctx, &swin_frame, &in_frame, &scale_frame, pix_fmt,
                                   video_width[0], video_height[0], input_fp[0], yuv_buf, &eos);
        if (p_hwframe == NULL)
        {
            ret = -1;
            goto end;
        }

        ret = encoder_open(&ctx, &enc_ctx[0], p_enc_api_param, output_total, enc_conf_params, enc_gop_params,
                           NULL, video_width[0], video_height[0], 30, 1, 200000, enc_codec_format,
                           is_ni_enc_pix_fmt(pix_fmt) ? pix_fmt : NI_PIX_FMT_YUV420P,
                           0, xcoderGUID, p_hwframe, 0, false);
        if (ret != 0)
        {
            goto end;
        }

        do
        {
            receive_rc = encoder_receive(&ctx, enc_ctx, &in_frame, out_packet,
                                         video_width[0], video_height[0], output_total, output_fp);
        }
        while (receive_rc == NI_TEST_RETCODE_EAGAIN);

        if (receive_rc == NI_TEST_RETCODE_SUCCESS)
        {
            ni_log(NI_LOG_INFO, "Got encoded sequence header packet\n");
        }
        else
        {
            ni_log(NI_LOG_ERROR, "Failed to get encoded sequence header packet, retcode %d\n", receive_rc);
            ret = receive_rc;
            goto end;
        }

        while (!end_of_all_streams &&
            (send_rc == NI_TEST_RETCODE_SUCCESS || receive_rc== NI_TEST_RETCODE_SUCCESS ||
             (send_rc == NI_TEST_RETCODE_EAGAIN && receive_rc == NI_TEST_RETCODE_EAGAIN)))
        {
            if (first_frame_uploaded && !eos)
            {
                p_hwframe = hwupload_frame(&ctx, &upl_ctx, &sca_ctx, &swin_frame, &in_frame, &scale_frame, pix_fmt,
                                           video_width[0], video_height[0], input_fp[0], yuv_buf, &eos);
                if (upl_ctx.status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
                {
                    ni_log(NI_LOG_DEBUG, "No space to write to, try to read a packet\n");
                    p_in_frame = &in_frame;
                    goto receive_pkt;
                } else if (p_hwframe == NULL)
                {
                    ret = NI_TEST_RETCODE_FAILURE;
                    break;
                }
            }

            // Sending
            p_in_frame = is_ni_enc_pix_fmt(pix_fmt) ? &in_frame : &scale_frame;
            for (i = 0; i < output_total; i++)
            {
                ctx.curr_enc_index = i;
                send_rc = encoder_send_data3(&ctx, &enc_ctx[i], p_in_frame, video_width[0], video_height[0], eos);
                first_frame_uploaded = 1;   //since first frame read before while-loop
                if (send_rc < 0)   //Error
                {
                    ni_log(NI_LOG_ERROR, "enc %d send error, quit !\n", i);
                    ni_hw_frame_ref(p_hwframe);
                    end_of_all_streams = 1;
                    break;
                }
                //track in array with unique index, free when enc read finds
                //this must be implemented in application space for complete
                //tracking of hwframes
                if (!ctx.enc_resend[i])
                {
                    //successful read means there is recycle to check
                    ni_hw_frame_ref(p_hwframe);
                } else
                {
                    ni_log(NI_LOG_DEBUG, "enc %d need to re-send !\n", i);
                    ni_usleep(500);
                    i--;
                    continue;
                }
            }
            if (end_of_all_streams)
                break;

receive_pkt:
            receive_rc = encoder_receive(&ctx, enc_ctx, p_in_frame, out_packet,
                                         video_width[0], video_height[0], output_total, output_fp);
            for (i = 0; receive_rc >= 0 && i < output_total; i++)
            {
                if (!ctx.enc_eos_received[i])
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
                for (i = 0; i < output_total; i++)
                {
                    ni_log(NI_LOG_INFO, "Encoder %d stats: received %u packets, fps %.2f, total bytes %u\n",
                           i,  ctx.num_packets_received[i],
                           (float)enc_ctx[i].frame_num / (float)(current_time - ctx.start_time) * (float)1000000000,
                           ctx.enc_total_bytes_received);
                }
                previous_time = current_time;
            }
        }

        ni_device_session_close(&upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
        if (!is_ni_enc_pix_fmt(pix_fmt))
        {   //Uploading rgba requires scaler conversion so close the session too
            ni_device_session_close(&sca_ctx, 1, NI_DEVICE_TYPE_SCALER);
        }
    }
    else
    {
        in_frame.data.frame.pixel_format = pix_fmt;
        ret = encoder_open(&ctx, enc_ctx, p_enc_api_param, output_total,
                        enc_conf_params, enc_gop_params, NULL, video_width[0],
                        video_height[0], 30, 1, 200000,
                        enc_codec_format, pix_fmt, 0, xcoderGUID, NULL,
                        0, (sw_pix_fmt != NI_SW_PIX_FMT_NONE) ? false : true);
                        // zero copy is not supported for YUV444P
        if (ret != 0)
        {
            goto end;
        }

        do
        {
            receive_rc = encoder_receive(&ctx, enc_ctx, &in_frame, out_packet,
                                         video_width[0], video_height[0], output_total, output_fp);
        }
        while (receive_rc == NI_TEST_RETCODE_EAGAIN);

        if (receive_rc == NI_TEST_RETCODE_SUCCESS)
        {
            ni_log(NI_LOG_INFO, "Got encoded sequence header packet\n");
        }
        else
        {
            ni_log(NI_LOG_ERROR, "Failed to get encoded sequence header packet, retcode %d\n", receive_rc);
            ret = receive_rc;
            goto end;
        }

        ni_log(NI_LOG_INFO, "Starting to encode: video resolution %dx%d\n", video_width[0], video_height[0]);

        while (!end_of_all_streams &&
            (send_rc == NI_TEST_RETCODE_SUCCESS || receive_rc == NI_TEST_RETCODE_SUCCESS ||
             (send_rc == NI_TEST_RETCODE_EAGAIN && receive_rc == NI_TEST_RETCODE_EAGAIN)))
        {
            read_size = read_yuv_from_file(&ctx, input_fp[i_index], yuv_buf,
                                        video_width[i_index], video_height[i_index],
                                        pix_fmt, sw_pix_fmt, &eos,
                                        enc_ctx[0].session_run_state);
            if (read_size < 0)
            {
                break;
            }

            // YUV444P reading
            if (sw_pix_fmt != NI_SW_PIX_FMT_NONE)
            {
                ret = convert_yuv_444p_to_420p(&sw_pix_frame[0], eos ? NULL : yuv_buf,
                                            video_width[i_index], video_height[i_index],
                                            sw_pix_fmt, 0, enc_codec_format);
                if (ret < 0)
                {
                    break;
                }
            }

            for (i = 0; i < output_total; i++)
            {
                if (sw_pix_fmt != NI_SW_PIX_FMT_NONE)
                {
                    ctx.curr_enc_index = i;
                    send_rc = encoder_send_data3(&ctx, &enc_ctx[i], &sw_pix_frame[i],
                                                video_width[i_index], video_height[i_index], eos);
                } else
                {
                    ctx.curr_enc_index = i;
                    send_rc = encoder_send_data(&ctx, &enc_ctx[i],
                                                &in_frame, eos ? NULL : yuv_buf,
                                                video_width[i_index], video_height[i_index],
                                                i_index == input_total - 1);
                }

                if (send_rc == NI_TEST_RETCODE_EAGAIN)
                {
                    // retry send to same encoder session
                    i--;
                    continue;
                } else if (send_rc == NI_TEST_RETCODE_NEXT_INPUT) // next input (will trigger sequence change)
                {
                    i_index++;
                    ctx.total_file_size = get_total_file_size(input_fp[i_index]);
                    ctx.curr_file_offset = 0;
                    send_rc = NI_TEST_RETCODE_SUCCESS;
                }
            }

            receive_rc = encoder_receive(&ctx, enc_ctx, &in_frame, out_packet,
                                        video_width[0], video_height[0], output_total, output_fp);
            for (i = 0; receive_rc >= 0 && i < output_total; i++)
            {
                if (!ctx.enc_eos_received[i])
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
                for (i = 0; i < output_total; i++)
                {
                    ni_log(NI_LOG_INFO, "Encoder %d stats: received %u packets, fps %.2f, total bytes %u\n",
                        i,  ctx.num_packets_received[i],
                        (float)enc_ctx[i].frame_num / (float)(current_time - ctx.start_time) * (float)1000000000,
                        ctx.enc_total_bytes_received);
                    previous_time = current_time;
                }
            }
        }
    }

    encoder_stat_report_and_close(&ctx, enc_ctx, output_total);

end:
    ni_frame_buffer_free(&in_frame.data.frame);
    ni_frame_buffer_free(&swin_frame.data.frame);
    for (i = 0; i < output_total; i++)
    {
        ni_packet_buffer_free(&out_packet[i].data.packet);
    }
    for (i = 0; i < sizeof(sw_pix_frame)/sizeof(ni_session_data_io_t); i++)
    {
        ni_frame_buffer_free(&sw_pix_frame[i].data.frame);
    }

    ni_device_session_context_clear(&upl_ctx);
    ni_device_session_context_clear(&sca_ctx);
    for (i = 0; i < output_total; i++)
    {
        ni_device_session_context_clear(&enc_ctx[i]);
        if (output_fp[i] != NULL)
        {
            fclose(output_fp[i]);
        }
    }

    for (i = 0; i < input_total; i++)
    {
        if (input_fp[i])
        {
            fclose(input_fp[i]);
        }
    }

    ni_aligned_free(yuv_buf);
    free(p_enc_api_param);

    for(i = 0; i < MAX_OUTPUT_FILES; ++i)
    {
        free(ctx.enc_pts_queue[i]);
        ctx.enc_pts_queue[i] = NULL;
    }

    return ret;
}