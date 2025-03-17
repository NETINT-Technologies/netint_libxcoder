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
 *  \file   ni_xcoder_multithread_transcode.c
 *
 *  \brief  Multi-threaded video transcoding and filtering demo application 
 *          directly using Netint Libxcoder API
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_decode_utils.h"
#include "ni_encode_utils.h"
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
        "Multi-threaded video transcoding demo application directly using Netint Libxcoder version %s\n"
        "Usage: ni_xcoder_multithread_transcode [options]\n"
        "\n"
        "options:\n"
        "-h | --help                    Show this message.\n"
        "-v | --version                 Print version info.\n"
        "-i | --input                   (Required) Input file path.\n"
        "-o | --output                  (Required) Output file path.\n"
        "                               Can be specified multiple (max %d) times\n"
        "                               to run multiple encoding instances simultaneously.\n"
        "-m | --dec-codec               (Required) Decoder codec format. Must match the codec of input file.\n"
        "                               [a|avc, h|hevc, v|vp9]\n"
        "-n | --enc-codec               (Required) Encoder codec format.\n"
        "                               [a|avc, h|hevc, x|av1] (x is in ivf container format)\n"
        "-l | --loglevel                Set loglevel of this application and libxcoder API.\n"
        "                               [none, fatal, error, info, debug, trace]\n"
        "                               (Default: info)\n"
        "-c | --card                    Set card index to use.\n"
        "                               See `ni_rsrc_mon` for info of cards on system.\n"
        "                               (Default: 0)\n"
        "-r | --repeat                  To loop input X times. Must be a positive integer\n"
        "                               (Default: 1)\n"
        "-d | --decoder-params          Decoding params. See \"Decoding Parameters\" chapter in\n"
        "                               QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               (Default: \"\")\n"
        "-e | --encoder-params          Encoding params. See \"Encoding Parameters\" chapter in\n"
        "                               QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               Can be specified multiple (max %d) times,\n"
        "                               must match the number of -o specified.\n"
        "                               (Default: \"\")\n"
        "-g | --encoder-gop             Custom GOP for encoding. See \"Custom Gop Structure\" chapter in\n"
        "                               QuadraIntegration&ProgrammingGuide*.pdf for help.\n"
        "                               gopPresetIdx must be set to 0 to be in effect.\n"
        "                               (Default: \"\")\n"
        "-u | --user-data-sei-passthru  (No argument) Enable user data unregistered SEI passthrough when specified\n"
        "-f | --vf                      Video filter params. The only supported filters in this demo are:\n"
        "                               ni_quadra_scale - supported params [width, height, format]\n"
        "                               e.g. ni_quadra_scale=width=1280:height=720:format=yuv420p\n"
        "                               ni_quadra_drawbox - supported params [x, y, width, height]\n"
        "                               e.g. ni_quadra_drawbox=x=300:y=150:width=600:height=400\n"
        "                               (Default: \"\")\n"
        , NI_XCODER_REVISION, MAX_OUTPUT_FILES, MAX_OUTPUT_FILES);
}

int main(int argc, char *argv[])
{
    int i, ret = 0;
    ni_demo_context_t ctx = {0};
    char in_filename[FILE_NAME_LEN] = {0};
    char out_filename[MAX_OUTPUT_FILES][FILE_NAME_LEN] = {0};
    FILE *output_fp[MAX_OUTPUT_FILES] = {0};
    int o_index = 0, e_index = 0, g_index = 0;
    int output_total = 0;
    int input_width = 0, input_height = 0, output_width = 0, output_height = 0, bit_depth = 8;
    int dec_codec_format = -1, enc_codec_format = -1;
    ni_log_level_t log_level;
    int xcoderGUID = 0;
    char *n;
    char dec_conf_params[2048] = {0};
    char filter_conf_params[2048] = {0};
    char enc_conf_params[MAX_OUTPUT_FILES][2048] = {0};
    char enc_gop_params[MAX_OUTPUT_FILES][2048] = {0};
    int user_data_sei_passthru = 0;
    ni_h264_sps_t avc_sps = {0};
    ni_h265_sps_t hevc_sps = {0};
    ni_vp9_header_info_t vp9_info = {0};
    ni_xcoder_params_t *p_dec_api_param = NULL;
    ni_xcoder_params_t *p_enc_api_param = NULL;
    ni_session_context_t dec_ctx = {0};
    ni_session_context_t enc_ctx[MAX_OUTPUT_FILES] = {0};
    ni_session_context_t sca_ctx = {0};
    ni_session_context_t crop_ctx = {0};
    ni_session_context_t pad_ctx = {0};
    ni_session_context_t ovly_ctx = {0};
    ni_session_context_t fmt_ctx = {0};
    void *p_stream_info = NULL;
    int fps_num = 30, fps_den = 1, bitrate = 200000;
    ni_frame_t *p_ni_frame = NULL;
    niFrameSurface1_t *p_hwframe = NULL;
    ni_scale_params_t scale_params = {0};
    ni_drawbox_params_t drawbox_params = {0};
    ni_test_frame_list_t frame_list = {0};
    ni_pthread_t dec_send_tid, dec_recv_tid, enc_send_tid, enc_recv_tid;
    dec_send_param_t dec_send_param = {0};
    dec_recv_param_t dec_recv_param = {0};
    enc_send_param_t enc_send_param = {0};
    enc_recv_param_t enc_recv_param = {0};
    ni_pix_fmt_t enc_pix_fmt = NI_PIX_FMT_YUV420P;

    int opt;
    int opt_index;
    const char *opt_string = "hvi:o:m:n:l:c:r:d:e:g:uf:";
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"dec-codec", required_argument, NULL, 'n'},
        {"enc-codec", required_argument, NULL, 'm'},
        {"loglevel", required_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"repeat", required_argument, NULL, 'r'},
        {"decoder-params", required_argument, NULL, 'd'},
        {"encoder-params", required_argument, NULL, 'e'},
        {"encoder-gop", required_argument, NULL, 'g'},
        {"user-data-sei-passthru", no_argument, NULL, 'u'},
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
                strcpy(in_filename, optarg);
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
                    dec_codec_format = NI_CODEC_FORMAT_H264;
                }
                else if (strcmp(optarg, "h") == 0 || strcmp(optarg, "hevc") == 0)
                {
                    dec_codec_format = NI_CODEC_FORMAT_H265;
                }
                else if (strcmp(optarg, "v") == 0 || strcmp(optarg, "vp9") == 0)
                {
                    dec_codec_format = NI_CODEC_FORMAT_VP9;
                }
                else 
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -m | --dec-codec option\n"
                        "Must be one of [a|avc, h|hevc, v|vp9]\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'n':
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
                else 
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -n | --enc-codec option\n"
                        "Must be one of [a|avc, h|hevc, x|av1]\n", optarg);
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
                if (n == optarg || *n != '\0' || ctx.loops_left <= 0)
                {
                    ni_log(NI_LOG_ERROR, "Error: Invalid value \"%s\" for -r | --repeat option\n"
                        "Must be a positive integer\n", optarg);
                    ret = -1;
                    goto end;
                }
                break;
            case 'd':
                strcpy(dec_conf_params, optarg);
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
                user_data_sei_passthru = 1;
                break;
            case 'f':
                strcpy(filter_conf_params, optarg);
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

    if (dec_codec_format == -1) {
        ni_log(NI_LOG_ERROR, "Error: Missing decoder codec argument (-m | --dec-codec)\n");
        ret = -1;
        goto end;
    }

    if (enc_codec_format == -1) {
        ni_log(NI_LOG_ERROR, "Error: Missing encoder codec argument (-n | --enc-codec)\n");
        ret = -1;
        goto end;
    }

    ret = read_and_cache_file(&ctx, in_filename);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: Read input file failure\n");
        goto end;
    }

    output_total = o_index;
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

    p_dec_api_param = malloc(sizeof(ni_xcoder_params_t));
    if (!p_dec_api_param)
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate p_dec_api_param\n");
        ret = -1;
        goto end;
    }

    p_enc_api_param = calloc(output_total, sizeof(ni_xcoder_params_t));
    if (!p_enc_api_param)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to allocate memory for p_enc_api_param\n");
        ret = -1;
        goto end;
    }

    if (dec_codec_format == NI_CODEC_FORMAT_H264)
    {
        ret = probe_h264_stream_info(&ctx, &avc_sps);
        if (ret)
        {
            ni_log(NI_LOG_ERROR,
                "ERROR: Failed to probe input file as H.264, file format not supported!\n");
            goto end;
        }

        bit_depth = avc_sps.bit_depth_luma;
        input_width = avc_sps.width;
        input_height = avc_sps.height;
        ni_log(NI_LOG_DEBUG, "Using probed H.264 source info: %d bits, resolution %dx%d\n",
            bit_depth, input_width, input_height);
    } else if (dec_codec_format == NI_CODEC_FORMAT_H265)
    {
        ret = probe_h265_stream_info(&ctx, &hevc_sps);
        if (ret)
        {
            ni_log(NI_LOG_ERROR,
                "ERROR: Failed to probe input file as H.265, file format not supported!\n");
            goto end;
        }
        bit_depth = hevc_sps.bit_depth_chroma;
        input_width = (int)(hevc_sps.width -
                    (hevc_sps.pic_conf_win.left_offset +
                    hevc_sps.pic_conf_win.right_offset));
        input_height = (int)(hevc_sps.height -
                    (hevc_sps.pic_conf_win.top_offset +
                        hevc_sps.pic_conf_win.bottom_offset));
        ni_log(NI_LOG_INFO, "Using probed H.265 source info: %d bits, resolution %dx%d\n",
            bit_depth, input_width, input_height);
    } else if (dec_codec_format == NI_CODEC_FORMAT_VP9)
    {
        ret = probe_vp9_stream_info(&ctx, &vp9_info);
        if (ret)
        {
            ni_log(NI_LOG_ERROR,
                    "ERROR: Failed to probe input file as VP9, file format not supported!\n");
            goto end;
        }
        bit_depth = vp9_info.profile ? 10 : 8;
        input_width = vp9_info.width;
        input_height = vp9_info.height;
        ni_log(NI_LOG_INFO,
            "Using probed VP9 source info: %d bits, resolution %dx%d, timebase %u/%u\n",
            bit_depth, input_width, input_height, 
            vp9_info.timebase.den, vp9_info.timebase.num);
    }
    
    // set up decoder config params with some hard-coded values
    ret = ni_decoder_init_default_params(p_dec_api_param, 25, 1, 200000, input_width, input_height);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to init default decoder config\n");
        goto end;
    }

    ret = ni_device_session_context_init(&dec_ctx);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to init decoder context\n");
        goto end;
    }

    dec_ctx.codec_format = dec_codec_format;
    dec_ctx.src_bit_depth = bit_depth;
    dec_ctx.bit_depth_factor = 1;
    if (10 == dec_ctx.src_bit_depth)
    {
        dec_ctx.bit_depth_factor = 2;
    }
    if ((NI_CODEC_FORMAT_H264 == dec_codec_format) || (NI_CODEC_FORMAT_H265 == dec_codec_format))
        dec_ctx.enable_user_data_sei_passthru = user_data_sei_passthru;

    // check and set ni_decoder_params from --xcoder-params
    ret = ni_retrieve_decoder_params(dec_conf_params, p_dec_api_param, &dec_ctx);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: decoder config params parsing error\n");
        goto end;
    }

    // Decode, use all the parameters specified by user
    ret = decoder_open_session(&dec_ctx, xcoderGUID, p_dec_api_param);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to open decoder session\n");
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

    if (dec_codec_format == NI_CODEC_FORMAT_H264)
        p_stream_info = &avc_sps;
    else if (dec_codec_format == NI_CODEC_FORMAT_H265)
        p_stream_info = &hevc_sps;
    else if (dec_codec_format == NI_CODEC_FORMAT_VP9)
    {
        p_stream_info = &vp9_info;
        fps_num = vp9_info.timebase.den;
        fps_den = vp9_info.timebase.num;
    }

    output_width = input_width;
    output_height = input_height;

    if (filter_conf_params[0])
    {
        if (!dec_ctx.hw_action) {
            ni_log(NI_LOG_ERROR, "Error: filters are only supported when hwframe is enabled (out=hw)\n");
            ret = -1;
            goto end;
        }
        ret = retrieve_filter_params(filter_conf_params, &scale_params, &drawbox_params);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: failed to parse filter parameters: %s\n", filter_conf_params);
            goto end;
        }
    }

    if (scale_params.enabled)
    {
        ret = ni_device_session_context_init(&sca_ctx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to init scale context\n");
            goto end;
        }
        output_width = scale_params.width;
        output_height = scale_params.height;
    }
    else if (drawbox_params.enabled)
    {
        ret = ni_device_session_context_init(&crop_ctx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to init crop context\n");
            goto end;
        }
        ret = ni_device_session_context_init(&pad_ctx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to init pad context\n");
            goto end;
        }
        ret = ni_device_session_context_init(&ovly_ctx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to init overlay context\n");
            goto end;
        }
        ret = ni_device_session_context_init(&fmt_ctx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to init format context\n");
            goto end;
        }
    }

    ni_log(NI_LOG_INFO, "Starting to transcode: HWFrames %d, video resolution %dx%d -> %dx%d\n", 
        dec_ctx.hw_action, input_width, input_height, output_width, output_height);

    ctx.start_time = ni_gettime_ns();

    // init and create decoding thread
    dec_send_param.p_ctx = &ctx;
    dec_send_param.p_dec_ctx = &dec_ctx;
    dec_send_param.input_width = input_width;
    dec_send_param.input_height = input_height;
    dec_send_param.p_stream_info = p_stream_info;
    dec_send_param.frame_list = &frame_list;

    dec_recv_param.p_ctx = &ctx;
    dec_recv_param.p_dec_ctx = &dec_ctx;
    dec_recv_param.p_sca_ctx = &sca_ctx;
    dec_recv_param.p_crop_ctx = &crop_ctx;
    dec_recv_param.p_pad_ctx = &pad_ctx;
    dec_recv_param.p_ovly_ctx = &ovly_ctx;
    dec_recv_param.p_fmt_ctx = &fmt_ctx;
    dec_recv_param.xcoderGUID = xcoderGUID;
    dec_recv_param.input_width = input_width;
    dec_recv_param.input_height = input_height;
    dec_recv_param.frame_list = &frame_list;
    dec_recv_param.scale_params = &scale_params;
    dec_recv_param.drawbox_params = &drawbox_params;

    if (ni_pthread_create(&dec_send_tid, NULL, decoder_send_thread,
                            &dec_send_param))
    {
        ni_log(NI_LOG_ERROR,
                "Error: create decoder send thread failed in transcode "
                "mode\n");
        return -1;
    }
    if (ni_pthread_create(&dec_recv_tid, NULL, decoder_receive_thread,
                            &dec_recv_param))
    {
        ni_log(NI_LOG_ERROR,
                "Error: create decoder receive thread failed in "
                "transcode mode\n");
        return -1;
    }

    // polling the first received decoded frame
    while (frame_list_is_empty(&frame_list) && !ctx.end_all_threads)
    {
        ni_usleep(100);
    }

    if (!ctx.end_all_threads)
    {
        p_ni_frame = &frame_list.frames[frame_list.head].data.frame;
        p_hwframe = dec_ctx.hw_action == NI_CODEC_HW_ENABLE ?
                    (niFrameSurface1_t *)p_ni_frame->p_data[3] : NULL;

        if (scale_params.enabled)
            enc_pix_fmt = gc620_to_ni_pix_fmt(scale_params.format);
        else if (drawbox_params.enabled)
            enc_pix_fmt = NI_PIX_FMT_YUV420P;
        else
            enc_pix_fmt = dec_ctx.pixel_format;
        
        ret = encoder_open(enc_ctx, p_enc_api_param, output_total, enc_conf_params, enc_gop_params,
                           p_ni_frame, output_width, output_height, fps_num, fps_den, bitrate, 
                           enc_codec_format, enc_pix_fmt, p_ni_frame->aspect_ratio_idc, 
                           xcoderGUID, p_hwframe, 1, false);
        if (ret != 0)
        {
            goto end;
        }

        // init and create encoding thread
        enc_send_param.p_ctx = &ctx;
        enc_send_param.p_enc_ctx = enc_ctx;
        enc_send_param.output_width = output_width;
        enc_send_param.output_height = output_height;
        enc_send_param.output_total = output_total;
        enc_send_param.frame_list = &frame_list;

        enc_recv_param.p_ctx = &ctx;
        enc_recv_param.p_enc_ctx = enc_ctx;
        enc_recv_param.output_width = output_width;
        enc_recv_param.output_height = output_height;
        enc_recv_param.p_file = output_fp;
        enc_recv_param.output_total = output_total;
        enc_recv_param.frame_list = &frame_list;

        if (ni_pthread_create(&enc_send_tid, NULL, encoder_send_thread,
                                &enc_send_param))
        {
            ni_log(NI_LOG_ERROR, "Error: create encoder send thread failed "
                    "in transcode mode\n");
            return -1;
        }
        if (ni_pthread_create(&enc_recv_tid, NULL, encoder_receive_thread,
                                &enc_recv_param))
        {
            ni_log(NI_LOG_ERROR, "Error: create encoder recieve thread "
                    "failed in transcode mode\n");
            return -1;
        }

        ni_pthread_join(enc_send_tid, NULL);
        ni_pthread_join(enc_recv_tid, NULL);
    }
    ni_pthread_join(dec_send_tid, NULL);
    ni_pthread_join(dec_recv_tid, NULL);

    decoder_stat_report_and_close(&ctx, &dec_ctx);
    encoder_stat_report_and_close(&ctx, enc_ctx, output_total);

end:
    hwframe_list_release(&frame_list);

    ni_device_session_context_clear(&dec_ctx);
    for (i = 0; i < output_total; i++)
    {
        ni_device_session_context_clear(&enc_ctx[i]);
        if (output_fp[i] != NULL)
        {
            fclose(output_fp[i]);
        }
    }
    if (scale_params.enabled)
    {
        ni_device_session_context_clear(&sca_ctx);
    }
    if (drawbox_params.enabled)
    {
        ni_device_session_context_clear(&crop_ctx);
        ni_device_session_context_clear(&pad_ctx);
        ni_device_session_context_clear(&ovly_ctx);
        ni_device_session_context_clear(&fmt_ctx);
    }

    free(ctx.file_cache);
    free(p_dec_api_param);
    free(p_enc_api_param);

    return ret;
}