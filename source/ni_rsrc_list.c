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
 * \file   ni_rsrc_list.c
 *
 * \brief  Application to query and print info about NETINT video processing
 *         devices on system
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#elif _WIN32
#include "ni_getopt.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "ni_rsrc_api.h"
#include "ni_util.h"

enum outFormat
{
  FMT_TEXT,
  FMT_FULL_TEXT,
  FMT_JSON
};

static const char *ni_codec_format_str[] = {"H.264", "H.265", "VP9", "JPEG",
                                            "AV1"};
static const char *ni_dec_name_str[] = {"h264_ni_quadra_dec", "h265_ni_quadra_dec",
                                        "vp9_ni_quadra_dec", "jpeg_ni_quadra_dec"};
static const char *ni_enc_name_str[] = {"h264_ni_quadra_enc", "h265_ni_quadra_enc", "empty",
                                        "jpeg_ni_quadra_enc", "av1_ni_quadra_enc"};


#define DYN_STR_BUF_CHUNK_SIZE 4096
typedef struct dyn_str_buf
{
    int str_len;   // Note: this does not include EOL char
    int buf_size;
    char *str_buf;   // Pointer to string
} dyn_str_buf_t;

/*!*****************************************************************************
 *  \brief     Accumulate string data in a dynamically sized buffer. This is
 *             useful to separate error messages from json and table output.
 *
 *  \param[in] *dyn_str_buf   pointer to structure holding dyn_str_buf info
 *  \param[in] *fmt           printf format specifier
 *  \param[in] ...            additional arguments
 *
 *  \return    0 for success, -1 for error
 ******************************************************************************/
int strcat_dyn_buf(dyn_str_buf_t *dyn_str_buf, const char *fmt, ...)
{
    int avail_buf;
    int formatted_len;
    int add_buf_size = 0;
    char *tmp_char_ptr = NULL;

    if (!dyn_str_buf)
    {
        fprintf(stderr, "ERROR: invalid param *dyn_str_buf\n");
        return -1;
    }

    if (!fmt)
    {
        return 0;
    }

    va_list vl, tmp_vl;
    va_start(vl, fmt);
    va_copy(tmp_vl, vl);

    // determine length of string to add
    formatted_len = vsnprintf(NULL, 0, fmt, tmp_vl);
    va_end(tmp_vl);

    // check if str_buf needs to be expanded in increments of chunk size
    avail_buf = dyn_str_buf->buf_size - dyn_str_buf->str_len;
    add_buf_size = (formatted_len + 1) > avail_buf ?
        ((formatted_len + 1 - avail_buf + DYN_STR_BUF_CHUNK_SIZE - 1) /
         DYN_STR_BUF_CHUNK_SIZE) *
            DYN_STR_BUF_CHUNK_SIZE :
        0;

    // realloc() to expand str_buf if necessary
    if (add_buf_size)
    {
        tmp_char_ptr = (char *)realloc(dyn_str_buf->str_buf,
                                       sizeof(char) * dyn_str_buf->buf_size +
                                           add_buf_size);
        if (!tmp_char_ptr)
        {
            fprintf(stderr, "ERROR: strcat_dyn_buf() failed realloc()\n");
            va_end(vl);
            return -1;
        }
        dyn_str_buf->str_buf = tmp_char_ptr;
        dyn_str_buf->buf_size += add_buf_size;
    }

    // concatenate string to buffer
    vsprintf(dyn_str_buf->str_buf + dyn_str_buf->str_len, fmt, vl);
    dyn_str_buf->str_len += formatted_len;

    va_end(vl);
    return 0;
}

void clear_dyn_str_buf(dyn_str_buf_t *dyn_str_buf)
{
    free(dyn_str_buf->str_buf);
    memset(dyn_str_buf, 0, sizeof(dyn_str_buf_t));
}



static void print_text(ni_device_t *p_device)
{
    if (!p_device)
    {
        ni_log(NI_LOG_INFO, "WARNING: NULL parameter passed in!\n");
        return;
    }

    dyn_str_buf_t output_buf = {0};
    ni_device_info_t *p_dev_info = NULL;
    for (int xcoder_index_1 = 0;
         xcoder_index_1 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
         xcoder_index_1++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_ENCODER][xcoder_index_1];

        strcat_dyn_buf(&output_buf, "Device #%d:\n", xcoder_index_1);
        strcat_dyn_buf(&output_buf, "  Serial number: %.*s\n",
               (int)sizeof(p_dev_info->serial_number),
               p_dev_info->serial_number);
        strcat_dyn_buf(&output_buf, "  Model number: %.*s\n",
               (int)sizeof(p_dev_info->model_number),
               p_dev_info->model_number);
        strcat_dyn_buf(&output_buf, "  Last ran firmware loader version: %.8s\n",
               p_dev_info->fl_ver_last_ran);
        strcat_dyn_buf(&output_buf, "  NOR flash firmware loader version: %.8s\n",
               p_dev_info->fl_ver_nor_flash);
        strcat_dyn_buf(&output_buf, "  Current firmware revision: %.8s\n",
               p_dev_info->fw_rev);
        strcat_dyn_buf(&output_buf, "  NOR flash firmware revision: %.8s\n",
               p_dev_info->fw_rev_nor_flash);
        strcat_dyn_buf(&output_buf, "  F/W & S/W compatibility: %s\n",
               p_dev_info->fw_ver_compat_warning ?
               "no, possible missing features" : "yes");
        strcat_dyn_buf(&output_buf, "  F/W branch: %s\n",
               p_dev_info->fw_branch_name);
        strcat_dyn_buf(&output_buf, "  F/W commit time: %s\n",
               p_dev_info->fw_commit_time);
        strcat_dyn_buf(&output_buf, "  F/W commit hash: %s\n",
               p_dev_info->fw_commit_hash);
        strcat_dyn_buf(&output_buf, "  F/W build time: %s\n",
               p_dev_info->fw_build_time);
        strcat_dyn_buf(&output_buf, "  F/W build id: %s\n",p_dev_info->fw_build_id);
        strcat_dyn_buf(&output_buf, "  DeviceID: %s\n", p_dev_info->dev_name);
        strcat_dyn_buf(&output_buf, "  PixelFormats: yuv420p, yuv420p10le, nv12, p010le"
               ", ni_quadra\n");

        for (size_t dev_type = NI_DEVICE_TYPE_DECODER;
             dev_type != NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            for (int xcoder_index_2 = 0;
                 xcoder_index_2 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
                 xcoder_index_2++)
            {
                if (strcmp(p_dev_info->dev_name,
                           p_device->xcoders[dev_type][xcoder_index_2].dev_name)
                    == 0 && p_dev_info->module_id >= 0)
                {
                    const ni_device_info_t *p_device_info =
                                            &(p_device->xcoders[dev_type][xcoder_index_2]);
                    int i;

                    if (!p_device_info)
                    {
                        ni_log(NI_LOG_ERROR, "ERROR: Cannot print device info!\n");
                    } else
                    {
                        strcat_dyn_buf(&output_buf, " %s #%d\n",
                            GET_XCODER_DEVICE_TYPE_STR(p_device_info->device_type),
                            p_device_info->module_id);
                        strcat_dyn_buf(&output_buf, "  H/W ID: %d\n", p_device_info->hw_id);
                        strcat_dyn_buf(&output_buf, "  MaxNumInstances: %d\n",
                            p_device_info->max_instance_cnt);

                        if (NI_DEVICE_TYPE_SCALER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Capabilities:\n");
                            strcat_dyn_buf(&output_buf,
                                            "    Operations: Crop (ni_quadra_crop), Scale (ni_quadra_scale), Pad "
                                            "(ni_quadra_pad), Overlay (ni_quadra_overlay)\n"
                                            "                Drawbox (ni_quadra_drawbox), Rotate (ni_quadra_rotate), XStack (ni_quadra_xstack)\n");
                        } else if (NI_DEVICE_TYPE_AI == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Capabilities:\n");
                            strcat_dyn_buf(&output_buf,
                                "    Operations: ROI (ni_quadra_roi), Background Replace (ni_quadra_bg)\n");
                        } else if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type ||
                                NI_DEVICE_TYPE_ENCODER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Max4KFps: %d\n", p_device_info->max_fps_4k);
                            for (i = 0; i < EN_CODEC_MAX; i++)
                            {
                                if (EN_INVALID != p_device_info->dev_cap[i].supports_codec)
                                {
                                    strcat_dyn_buf(&output_buf, "  %s ",
                                        ni_codec_format_str[p_device_info->dev_cap[i]
                                                                .supports_codec]);
                                    strcat_dyn_buf(&output_buf, "(%s) Capabilities:\n",
                                        NI_DEVICE_TYPE_DECODER ==
                                                p_device_info->device_type ?
                                            ni_dec_name_str[p_device_info->dev_cap[i]
                                                                .supports_codec] :
                                            ni_enc_name_str[p_device_info->dev_cap[i]
                                                                .supports_codec]);
                                    strcat_dyn_buf(&output_buf, "    MaxResolution: %dx%d\n",
                                        p_device_info->dev_cap[i].max_res_width,
                                        p_device_info->dev_cap[i].max_res_height);
                                    strcat_dyn_buf(&output_buf, "    MinResolution: %dx%d\n",
                                        p_device_info->dev_cap[i].min_res_width,
                                        p_device_info->dev_cap[i].min_res_height);

                                    // no profile for JPEG encode, or level for JPEG
                                    if (! (NI_DEVICE_TYPE_ENCODER == p_device_info->device_type &&
                                        EN_JPEG == p_device_info->dev_cap[i].supports_codec))
                                    {
                                        strcat_dyn_buf(&output_buf, "    Profiles: %s\n",
                                            p_device_info->dev_cap[i].profiles_supported);
                                    }
                                    if (EN_JPEG != p_device_info->dev_cap[i].supports_codec)
                                    {
                                        strcat_dyn_buf(&output_buf, "    Level: %s\n",
                                            p_device_info->dev_cap[i].level);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (output_buf.str_buf)
        printf("%s", output_buf.str_buf);
    clear_dyn_str_buf(&output_buf);
}

static void print_full_text(ni_device_t *p_device)
{
    if (!p_device)
    {
        ni_log(NI_LOG_INFO, "WARNING: NULL parameter passed in!\n");
        return;
    }

    dyn_str_buf_t output_buf = {0};
    ni_device_info_t *p_dev_info = NULL;
    for (int xcoder_index_1 = 0;
         xcoder_index_1 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
         xcoder_index_1++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_ENCODER][xcoder_index_1];
        strcat_dyn_buf(&output_buf, "Device #%d:\n", xcoder_index_1);
        strcat_dyn_buf(&output_buf, "  Serial number: %.*s\n",
               (int)sizeof(p_dev_info->serial_number),
               p_dev_info->serial_number);
        strcat_dyn_buf(&output_buf, "  Model number: %.*s\n",
               (int)sizeof(p_dev_info->model_number),
               p_dev_info->model_number);
        strcat_dyn_buf(&output_buf, "  Last ran firmware loader version: %.8s\n",
               p_dev_info->fl_ver_last_ran);
        strcat_dyn_buf(&output_buf, "  NOR flash firmware loader version: %.8s\n",
               p_dev_info->fl_ver_nor_flash);
        strcat_dyn_buf(&output_buf, "  Current firmware revision: %.8s\n",
               p_dev_info->fw_rev);
        strcat_dyn_buf(&output_buf, "  NOR flash firmware revision: %.8s\n",
               p_dev_info->fw_rev_nor_flash);
        strcat_dyn_buf(&output_buf, "  F/W & S/W compatibility: %s\n",
               p_dev_info->fw_ver_compat_warning ?
               "no, possible missing features" : "yes");
        strcat_dyn_buf(&output_buf, "  F/W branch: %s\n",
               p_dev_info->fw_branch_name);
        strcat_dyn_buf(&output_buf, "  F/W commit time: %s\n",
               p_dev_info->fw_commit_time);
        strcat_dyn_buf(&output_buf, "  F/W commit hash: %s\n",
               p_dev_info->fw_commit_hash);
        strcat_dyn_buf(&output_buf, "  F/W build time: %s\n",
               p_dev_info->fw_build_time);
        strcat_dyn_buf(&output_buf, "  F/W build id: %s\n",p_dev_info->fw_build_id);
        strcat_dyn_buf(&output_buf, "  DeviceID: %s\n", p_dev_info->dev_name);

        for (size_t dev_type = NI_DEVICE_TYPE_DECODER;
             dev_type != NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            for (int xcoder_index_2 = 0;
                 xcoder_index_2 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
                 xcoder_index_2++)
            {
                if (strcmp(p_dev_info->dev_name,
                           p_device->xcoders[dev_type][xcoder_index_2].dev_name)
                    == 0 && p_dev_info->module_id >= 0)
                {
                    const ni_device_info_t *p_device_info =
                                            &(p_device->xcoders[dev_type][xcoder_index_2]);
                    int i;

                    if (!p_device_info)
                    {
                        ni_log(NI_LOG_ERROR, "ERROR: Cannot print device info!\n");
                    } else
                    {
                        strcat_dyn_buf(&output_buf, " %s #%d\n",
                            GET_XCODER_DEVICE_TYPE_STR(p_device_info->device_type),
                            p_device_info->module_id);
                        strcat_dyn_buf(&output_buf, "  H/W ID: %d\n", p_device_info->hw_id);
                        strcat_dyn_buf(&output_buf, "  MaxNumInstances: %d\n",
                            p_device_info->max_instance_cnt);

                        if (NI_DEVICE_TYPE_SCALER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Capabilities:\n");
                            strcat_dyn_buf(&output_buf,
                                "    Operations: Crop (ni_quadra_crop), Scale (ni_quadra_scale), Pad "
                                "(ni_quadra_pad), Overlay (ni_quadra_overlay)\n"
                                "                Drawbox (ni_quadra_drawbox), Rotate (ni_quadra_rotate), XStack (ni_quadra_xstack)\n"
                                "                Delogo (ni_quadra_delogo), Merge (ni_quadra_merge), Flip (ni_quadra_flip)\n"
                                "                Drawtext (ni_quadra_drawtext)\n");
                        } else if (NI_DEVICE_TYPE_AI == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Capabilities:\n");
                            strcat_dyn_buf(&output_buf, "    Cores: 2\n");
                            strcat_dyn_buf(&output_buf, "    Computing Power: int8 18 tops\n");
                            strcat_dyn_buf(&output_buf,
                                "    Operations: ROI (ni_quadra_roi), Background Replace (ni_quadra_bg),  Ai Pre-processing (ni_quadra_ai_pre)\n"
                                "                Background Remove (ni_quadra_bgr), Hvsplus (ni_quadra_hvsplus)\n");
                        } else if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type ||
                                NI_DEVICE_TYPE_ENCODER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "  Max4KFps: %d\n", p_device_info->max_fps_4k);
                            for (i = 0; i < EN_CODEC_MAX; i++)
                            {
                                if (EN_INVALID != p_device_info->dev_cap[i].supports_codec)
                                {
                                    strcat_dyn_buf(&output_buf, "  %s ",
                                        ni_codec_format_str[p_device_info->dev_cap[i]
                                                                .supports_codec]);
                                    strcat_dyn_buf(&output_buf, "(%s) Capabilities:\n",
                                        NI_DEVICE_TYPE_DECODER ==
                                                p_device_info->device_type ?
                                            ni_dec_name_str[p_device_info->dev_cap[i]
                                                                .supports_codec] :
                                            ni_enc_name_str[p_device_info->dev_cap[i]
                                                                .supports_codec]);
                                    if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type)
                                    {
                                        switch (p_device_info->dev_cap[i].supports_codec)
                                        {
                                            case EN_H264:
                                            case EN_H265:
                                            case EN_VP9:
                                                strcat_dyn_buf(&output_buf, "    PixelFormats: "
                                                    "yuv420p, yuv420p10le, nv12, p010le, ni_quad\n");
                                                break;
                                            case EN_JPEG:
                                                strcat_dyn_buf(&output_buf, "    PixelFormats: "
                                                    "yuvj420p, ni_quad\n");
                                                break;
                                            case EN_AV1:
                                            default:
                                                break;
                                        }
                                    }
                                    else
                                    {
                                        switch (p_device_info->dev_cap[i].supports_codec)
                                        {
                                            case EN_H264:
                                            case EN_H265:
                                            case EN_AV1:
                                                strcat_dyn_buf(&output_buf, "    PixelFormats: "
                                                    "yuv420p, yuvj420p, yuv420p10le, nv12, p010le, ni_quad\n");
                                                break;
                                            case EN_JPEG:
                                                strcat_dyn_buf(&output_buf, "    PixelFormats: "
                                                    "yuvj420p, ni_quad\n");
                                                break;
                                            case EN_VP9:
                                            default:
                                                break;
                                        }
                                    }
                                    strcat_dyn_buf(&output_buf, "    MaxResolution: %dx%d\n",
                                        p_device_info->dev_cap[i].max_res_width,
                                        p_device_info->dev_cap[i].max_res_height);
                                    strcat_dyn_buf(&output_buf, "    MinResolution: %dx%d\n",
                                        p_device_info->dev_cap[i].min_res_width,
                                        p_device_info->dev_cap[i].min_res_height);

                                    // no profile for JPEG encode, or level for JPEG
                                    if (! (NI_DEVICE_TYPE_ENCODER == p_device_info->device_type &&
                                        EN_JPEG == p_device_info->dev_cap[i].supports_codec))
                                    {
                                        strcat_dyn_buf(&output_buf, "    Profiles: %s\n",
                                            p_device_info->dev_cap[i].profiles_supported);
                                    }
                                    if (EN_JPEG != p_device_info->dev_cap[i].supports_codec)
                                    {
                                        strcat_dyn_buf(&output_buf, "    Level: %s\n",
                                            p_device_info->dev_cap[i].level);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (output_buf.str_buf)
        printf("%s", output_buf.str_buf);
    clear_dyn_str_buf(&output_buf);
}

static void print_json(ni_device_t *p_device)
{
    if (!p_device)
    {
        ni_log(NI_LOG_INFO, "WARNING: NULL parameter passed in!\n");
        return;
    }

    dyn_str_buf_t output_buf = {0};
    ni_device_info_t *p_dev_info = NULL;
    strcat_dyn_buf(&output_buf,"{\n");
    for (int xcoder_index_1 = 0;
         xcoder_index_1 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
         xcoder_index_1++)
    {
        p_dev_info = &p_device->xcoders[NI_DEVICE_TYPE_ENCODER][xcoder_index_1];
        strcat_dyn_buf(&output_buf, " \"Device #%d\": [\n", xcoder_index_1);
        strcat_dyn_buf(&output_buf, "\t{\n"
                                    "\t\t\"Serial number\": \"%.*s\",\n"
                                    "\t\t\"Model number:\": \"%.*s\",\n"
                                    "\t\t\"Last ran firmware loader version\": \"%.8s\",\n"
                                    "\t\t\"NOR flash firmware loader version\": \"%.8s\",\n"
                                    "\t\t\"Current firmware revision\": \"%.8s\",\n"
                                    "\t\t\"NOR flash firmware revision\": \"%.8s\",\n"
                                    "\t\t\"F/W & S/W compatibility\": \"%s\",\n"
                                    "\t\t\"F/W branch\":  \"%s\",\n"
                                    "\t\t\"F/W commit time\":  \"%s\",\n"
                                    "\t\t\"F/W commit hash\":  \"%s\",\n"
                                    "\t\t\"F/W build time\":  \"%s\",\n"
                                    "\t\t\"F/W build id\":  \"%s\",\n"
                                    "\t\t\"DeviceID\":  \"%s\",\n"
                                    , (int)sizeof(p_dev_info->serial_number), p_dev_info->serial_number,
                                    (int)sizeof(p_dev_info->model_number), p_dev_info->model_number,
                                    p_dev_info->fl_ver_last_ran, p_dev_info->fl_ver_nor_flash,
                                    p_dev_info->fw_rev, p_dev_info->fw_rev_nor_flash,
                                    p_dev_info->fw_ver_compat_warning ? "no, possible missing features" : "yes",
                                    p_dev_info->fw_branch_name, p_dev_info->fw_commit_time,
                                    p_dev_info->fw_commit_hash, p_dev_info->fw_build_time,
                                    p_dev_info->fw_build_id, p_dev_info->dev_name);
        for (size_t dev_type = NI_DEVICE_TYPE_DECODER;
             dev_type != NI_DEVICE_TYPE_XCODER_MAX; dev_type++)
        {
            for (int xcoder_index_2 = 0;
                 xcoder_index_2 < p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];
                 xcoder_index_2++)
            {
                if (strcmp(p_dev_info->dev_name,
                           p_device->xcoders[dev_type][xcoder_index_2].dev_name)
                    == 0 && p_dev_info->module_id >= 0)
                {
                    const ni_device_info_t *p_device_info =
                                            &(p_device->xcoders[dev_type][xcoder_index_2]);
                    int i;

                    if (!p_device_info)
                    {
                        ni_log(NI_LOG_ERROR, "ERROR: Cannot print device info!\n");
                    } else
                    {
                        strcat_dyn_buf(&output_buf, "\t\t\"%s #%d\": [\n",
                            GET_XCODER_DEVICE_TYPE_STR(p_device_info->device_type),
                            p_device_info->module_id);
                        strcat_dyn_buf(&output_buf, "\t\t\t{\n");
                        strcat_dyn_buf(&output_buf, "\t\t\t\t\"H/W ID\": %d,\n"
                                                    "\t\t\t\t\"MaxNumInstances\": %d,\n"
                                                    , p_device_info->hw_id,
                                                    p_device_info->max_instance_cnt);
                        if (NI_DEVICE_TYPE_SCALER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\"Capabilities\": [\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t{\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"Operations\": "
                                                        "\"Crop (ni_quadra_crop), Scale (ni_quadra_scale), Pad "
                                                        "(ni_quadra_pad), Overlay (ni_quadra_overlay), "
                                                        "Drawbox (ni_quadra_drawbox), Rotate (ni_quadra_rotate), "
                                                        "XStack (ni_quadra_xstack), Delogo (ni_quadra_delogo), "
                                                        "Merge (ni_quadra_merge), Flip (ni_quadra_flip), "
                                                        "Drawtext (ni_quadra_drawtext)\"\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t}\n\t\t\t\t]\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t}\n\t\t],\n");
                        } else if (NI_DEVICE_TYPE_AI == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\"Capabilities\": [\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t{\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"Cores\": 2,\n"
                                                        "\t\t\t\t\t\t\"Computing Power\": \"int8 18 tops\",\n"
                                                        "\t\t\t\t\t\t\"Operations\": "
                                                        "\"ROI (ni_quadra_roi), Background Replace (ni_quadra_bg), "
                                                        "Ai Pre-processing (ni_quadra_ai_pre), "
                                                        "Background Remove (ni_quadra_bgr), Hvsplus (ni_quadra_hvsplus)\"\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\t}\n\t\t\t\t]\n");
                            strcat_dyn_buf(&output_buf, "\t\t\t}\n\t\t]\n");
                        } else if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type ||
                                NI_DEVICE_TYPE_ENCODER == p_device_info->device_type)
                        {
                            strcat_dyn_buf(&output_buf, "\t\t\t\t\"Max4KFps\": %d,\n",
                                                        p_device_info->max_fps_4k);
                            for (i = 0; i < EN_CODEC_MAX; i++)
                            {
                                if (EN_INVALID != p_device_info->dev_cap[i].supports_codec)
                                {
                                    strcat_dyn_buf(&output_buf, "\t\t\t\t\"%s (%s) Capabilities\": [\n",
                                        ni_codec_format_str[p_device_info->dev_cap[i].supports_codec],
                                        NI_DEVICE_TYPE_DECODER == p_device_info->device_type ?
                                            ni_dec_name_str[p_device_info->dev_cap[i].supports_codec] :
                                            ni_enc_name_str[p_device_info->dev_cap[i].supports_codec]);
                                    strcat_dyn_buf(&output_buf, "\t\t\t\t\t{\n");
                                    if (NI_DEVICE_TYPE_DECODER == p_device_info->device_type)
                                    {
                                        switch (p_device_info->dev_cap[i].supports_codec)
                                        {
                                            case EN_H264:
                                            case EN_H265:
                                            case EN_VP9:
                                                strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"PixelFormats\": "
                                                    "\"yuv420p, yuv420p10le, nv12, p010le, ni_quad\",\n");
                                                break;
                                            case EN_JPEG:
                                                strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"PixelFormats\": "
                                                    "\"yuvj420p, ni_quad\",\n");
                                                break;
                                            case EN_AV1:
                                            default:
                                                break;
                                        }
                                    }
                                    else
                                    {
                                        switch (p_device_info->dev_cap[i].supports_codec)
                                        {
                                            case EN_H264:
                                            case EN_H265:
                                            case EN_AV1:
                                                strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"PixelFormats\": "
                                                    "\"yuv420p, yuvj420p, yuv420p10le, nv12, p010le, ni_quad\",\n");
                                                break;
                                            case EN_JPEG:
                                                strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"PixelFormats\": "
                                                    "\"yuvj420p, ni_quad\",\n");
                                                break;
                                            case EN_VP9:
                                            default:
                                                break;
                                        }
                                    }
                                    strcat_dyn_buf(&output_buf, "\t\t\t\t\t\t\"MaxResolution\": \"%dx%d\",\n"
                                                                "\t\t\t\t\t\t\"MinResolution\": \"%dx%d\""
                                                                ,p_device_info->dev_cap[i].max_res_width,
                                                                p_device_info->dev_cap[i].max_res_height,
                                                                p_device_info->dev_cap[i].min_res_width,
                                                                p_device_info->dev_cap[i].min_res_height);
                                    // no profile for JPEG encode, or level for JPEG
                                    if (! (NI_DEVICE_TYPE_ENCODER == p_device_info->device_type &&
                                        EN_JPEG == p_device_info->dev_cap[i].supports_codec))
                                    {
                                        strcat_dyn_buf(&output_buf, ",\n\t\t\t\t\t\t\"Profiles\": \"%s\"",
                                            p_device_info->dev_cap[i].profiles_supported);
                                    }
                                    if (EN_JPEG != p_device_info->dev_cap[i].supports_codec)
                                    {
                                        strcat_dyn_buf(&output_buf, ",\n\t\t\t\t\t\t\"Level\": \"%s\"",
                                            p_device_info->dev_cap[i].level);
                                    }
                                    if ((NI_DEVICE_TYPE_DECODER == p_device_info->device_type &&
                                        EN_VP9 == p_device_info->dev_cap[i].supports_codec) ||
                                        (NI_DEVICE_TYPE_ENCODER == p_device_info->device_type &&
                                        EN_AV1 == p_device_info->dev_cap[i].supports_codec))
                                    {
                                        strcat_dyn_buf(&output_buf, "\n\t\t\t\t\t}\n\t\t\t\t]\n");
                                    }
                                    else
                                    {
                                        strcat_dyn_buf(&output_buf, "\n\t\t\t\t\t}\n\t\t\t\t],\n");
                                    }
                                }
                            }
                            strcat_dyn_buf(&output_buf, "\t\t\t}\n\t\t],\n");
                        }
                    }
                }
            }
        }
        if (xcoder_index_1 == p_device->xcoder_cnt[NI_DEVICE_TYPE_ENCODER] - 1)
            strcat_dyn_buf(&output_buf, "\t}\n ]\n");
        else
            strcat_dyn_buf(&output_buf, "\t}\n ],\n");
    }
    strcat_dyn_buf(&output_buf,"}\n");
    if (output_buf.str_buf)
        printf("%s", output_buf.str_buf);
    clear_dyn_str_buf(&output_buf);
}


int32_t main(int argc, char *argv[])
{
    int opt;
    bool list_uninitialized = false;
    ni_log_level_t log_level = NI_LOG_INFO;
    int printFormat = FMT_TEXT;

    // arg handling
    while ((opt = getopt(argc, argv, "ahvlo:")) != -1)
    {
        switch (opt)
        {
            case 'a':
                list_uninitialized = true;
                break;
            case 'h':
                // help message
                printf("-------- ni_rsrc_list v%s --------\n"
                       "Display information for NETINT hardware.\n"
                       "\n"
                       "-a  Print includes info for uninitialized cards.\n"
                       "-h  Display this help and exit.\n"
                       "-v  Print version info.\n"
                       "-o  Output format. [text, full, json]\n"
                       "    Default: text\n"
                       "-l  Set loglevel of libxcoder API.\n"
                       "    [none, fatal, error, info, debug, trace]\n"
                       "    Default: info\n",
                       NI_XCODER_REVISION);
                return 0;
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                return 0;
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                } else {
                    fprintf(stderr, "FATAL: invalid log level selected: %s\n",
                            optarg);
                    exit(1);
                }
                break;
            case 'o':
                // Output print format
                if (!strcmp(optarg, "text"))
                {
                    printFormat = FMT_TEXT;
                }
                else if (!strcmp(optarg, "full"))
                {
                    printFormat = FMT_FULL_TEXT;
                }
                else if (!strcmp(optarg, "json"))
                {
                    printFormat = FMT_JSON;
                }
                else
                {
                    fprintf(stderr, "Error: unknown selection for outputFormat: %s\n", optarg);
                    return 1;
                }
                break;
            default:
                fprintf(stderr, "FATAL: invalid arg '%c'\n", opt);
                return 1;
        }
    }

    ni_log_set_level(log_level);
    ni_device_t *device = NULL;
    device = (ni_device_t *)malloc(sizeof(ni_device_t));
    if (!device)
    {
        ni_log(NI_LOG_ERROR, "ERROR %s() failed to malloc memory: %s\n",
               __func__, strerror(NI_ERRNO));
        return 1;
    }
    memset(device, 0, sizeof(ni_device_t));

    if (NI_RETCODE_SUCCESS != ni_rsrc_list_all_devices2(device, list_uninitialized))
    {
        free(device);
        return 1;
    }

    switch(printFormat)
    {
        case FMT_TEXT:
            print_text(device);
            break;
        case FMT_FULL_TEXT:
            print_full_text(device);
            break;
        case FMT_JSON:
            print_json(device);
            break;
        default:
            break;
    }
    free(device);
    return 0;
}
