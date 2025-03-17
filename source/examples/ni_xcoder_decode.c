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
 *  \file   ni_xcoder_decode.c
 *
 *  \brief  Video decoding demo application directly using Netint Libxcoder API
 ******************************************************************************/

#include "ni_generic_utils.h"
#include "ni_decode_utils.h"
#include "ni_log.h"
#include "ni_util.h"

#ifdef _WIN32
#include "ni_getopt.h"
#elif __linux__ || __APPLE__
#include <getopt.h>
#endif

static void print_usage(void)
{
    ni_log(NI_LOG_ERROR, 
        "Video decoding demo application directly using Netint Libxcoder version %s\n"
        "Usage: ni_xcoder_decode [options]\n"
        "\n"
        "options:\n"
        "-h | --help                    Show this message.\n"
        "-v | --version                 Print version info.\n"
        "-i | --input                   (Required) Input file path.\n"
        "-o | --output                  (Required) Output file path.\n"
        "-m | --dec-codec               (Required) Decoder codec format. Must match the codec of input file.\n"
        "                               [a|avc, h|hevc, v|vp9]\n"
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
        , NI_XCODER_REVISION);
}

int main(int argc, char *argv[])
{
    int i, ret = 0;
    ni_demo_context_t ctx = {0};
    char in_filename[FILE_NAME_LEN] = {0};
    char out_filename[FILE_NAME_LEN] = {0};
    FILE *output_fp = NULL;
    int video_width, video_height, bit_depth;
    int dec_codec_format = -1;
    ni_log_level_t log_level;
    int xcoderGUID = 0;
    char *n;
    char dec_conf_params[2048] = {0};
    ni_h264_sps_t avc_sps = {0};
    ni_h265_sps_t hevc_sps = {0};
    ni_vp9_header_info_t vp9_info = {0};
    int send_rc = NI_TEST_RETCODE_SUCCESS, receive_rc = NI_TEST_RETCODE_SUCCESS, rx_size = 0;
    ni_xcoder_params_t *p_dec_api_param = NULL;
    ni_session_context_t dec_ctx = {0};
    ni_frame_t *p_ni_frame;
    niFrameSurface1_t * p_hwframe;
    void *p_stream_info = NULL;
    ni_session_data_io_t in_pkt = {0};
    ni_session_data_io_t out_frame = {0};
    uint64_t current_time, previous_time;

    int opt;
    int opt_index;
    const char *opt_string = "hvi:o:m:l:c:r:d:";
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"dec-codec", required_argument, NULL, 'm'},
        {"loglevel", required_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"repeat", required_argument, NULL, 'r'},
        {"decoder-params", required_argument, NULL, 'd'},
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
                strcpy(out_filename, optarg);
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

    if (!out_filename[0]) {
        ni_log(NI_LOG_ERROR, "Error: Missing output file argument (-o | --output)\n");
        ret = -1;
        goto end;
    }

    if (dec_codec_format == -1) {
        ni_log(NI_LOG_ERROR, "Error: Missing decoder codec argument (-m | --dec-codec)\n");
        ret = -1;
        goto end;
    }

    p_dec_api_param = malloc(sizeof(ni_xcoder_params_t));
    if (!p_dec_api_param)
    {
        ni_log(NI_LOG_ERROR, "Error: failed to allocate p_dec_api_param\n");
        ret = -1;
        goto end;
    }

    ret = read_and_cache_file(&ctx, in_filename);
    if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: Read input file failure\n");
        goto end;
    }

    if (strcmp(out_filename, "null") != 0 &&
        strcmp(out_filename, "/dev/null") != 0)
    {
        output_fp = fopen(out_filename, "wb");
        if (!output_fp)
        {
            ni_log(NI_LOG_ERROR, "Error: Failed to open %s\n", out_filename);
            ret = -1;
            goto end;
        }
        ni_log(NI_LOG_INFO, "Opened output file: %s\n", out_filename);
    } else
    {
        output_fp = NULL;
        ni_log(NI_LOG_INFO, "Note: Requested NULL output, no output file will be generated\n");
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
        video_width = avc_sps.width;
        video_height = avc_sps.height;
        ni_log(NI_LOG_DEBUG, "Using probed H.264 source info: %d bits, resolution %dx%d\n",
               bit_depth, video_width, video_height);
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
        video_width = (int)(hevc_sps.width -
                      (hevc_sps.pic_conf_win.left_offset +
                       hevc_sps.pic_conf_win.right_offset));
        video_height = (int)(hevc_sps.height -
                       (hevc_sps.pic_conf_win.top_offset +
                        hevc_sps.pic_conf_win.bottom_offset));
        ni_log(NI_LOG_INFO, "Using probed H.265 source info: %d bits, resolution %dx%d\n",
               bit_depth, video_width, video_height);
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
        video_width = vp9_info.width;
        video_height = vp9_info.height;
        ni_log(NI_LOG_INFO,
               "Using probed VP9 source info: %d bits, resolution %dx%d, timebase %u/%u\n",
               bit_depth, video_width, video_height, 
               vp9_info.timebase.den, vp9_info.timebase.num);
    }
    
    // set up decoder config params with some hard-coded values
    ret = ni_decoder_init_default_params(p_dec_api_param, 25, 1, 200000, video_width, video_height);
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

    ni_log(NI_LOG_INFO, "Starting to decode: HWFrames %d\n", dec_ctx.hw_action);

    if (dec_codec_format == NI_CODEC_FORMAT_H264)
        p_stream_info = &avc_sps;
    else if (dec_codec_format == NI_CODEC_FORMAT_H265)
        p_stream_info = &hevc_sps;
    else if (dec_codec_format == NI_CODEC_FORMAT_VP9)
        p_stream_info = &vp9_info;

    ctx.start_time = ni_gettime_ns();
    previous_time = ctx.start_time;

    while (send_rc == NI_TEST_RETCODE_SUCCESS || receive_rc == NI_TEST_RETCODE_SUCCESS ||
           (send_rc == NI_TEST_RETCODE_EAGAIN && receive_rc == NI_TEST_RETCODE_EAGAIN))
    {
        // Sending
        send_rc = decoder_send_data(&ctx, &dec_ctx, &in_pkt, video_width, 
                                    video_height, p_stream_info);
        if (send_rc < 0)
        {
            ni_log(NI_LOG_ERROR,
                   "Error: decoder_send_data() failed, rc: %d\n",
                   send_rc);
            break;
        }

        // Receiving
        do
        {
            rx_size = 0;
            receive_rc = decoder_receive_data(&ctx, &dec_ctx, &out_frame, video_width, video_height,
                                              output_fp, 1, &rx_size);

            if (dec_ctx.hw_action == NI_CODEC_HW_ENABLE)
            {
                if (receive_rc != NI_TEST_RETCODE_EAGAIN &&
                    receive_rc != NI_TEST_RETCODE_END_OF_STREAM)
                {
                    p_ni_frame = &out_frame.data.frame;
                    p_hwframe = (niFrameSurface1_t *)p_ni_frame->p_data[3];
                    ni_log(NI_LOG_DEBUG, "decoder_receive_data HW decode-only. recycle HW frame idx %u\n", p_hwframe->ui16FrameIdx);
                    ni_hwframe_buffer_recycle2(p_hwframe);
                    ni_frame_buffer_free(p_ni_frame);
                }
            }
            else
            {
                ni_decoder_frame_buffer_free(&(out_frame.data.frame));
            }

            // Error or eos
            if (receive_rc < 0 || out_frame.data.frame.end_of_stream)
            {
                break;
            }

            current_time = ni_gettime_ns();
            if (current_time - previous_time >= (uint64_t)1000000000) 
            {
                ni_log(NI_LOG_INFO, "Decoder stats: received %u frames, fps %.2f, total bytes %u\n", 
                       ctx.num_frames_received, 
                       (float)ctx.num_frames_received / (float)(current_time - ctx.start_time) * (float)1000000000,
                       ctx.dec_total_bytes_received);
                previous_time = current_time;
            }
            
        } while (!dec_ctx.decoder_low_delay && rx_size > 0); //drain consecutive outputs

        // Error or eos
        if (receive_rc < 0 || out_frame.data.frame.end_of_stream)
        {
            break;
        }
    }

    decoder_stat_report_and_close(&ctx, &dec_ctx);

end:
    ni_packet_buffer_free(&(in_pkt.data.packet));
    if (dec_ctx.hw_action == NI_CODEC_HW_ENABLE)
    {
        ni_frame_buffer_free(&out_frame.data.frame);
    } else 
    {
        ni_decoder_frame_buffer_free(&out_frame.data.frame);
    }

    ni_device_session_context_clear(&dec_ctx);

    free(p_dec_api_param);
    free(ctx.file_cache);
    if (output_fp != NULL)
    {
        fclose(output_fp);
    }
    
    return ret;
}