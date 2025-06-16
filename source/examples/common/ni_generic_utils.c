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
 *  \file   generic_utils.c
 *
 *  \brief  Miscellaneous utility functions shared by Libxcoder API examples
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <sys/types.h>
#endif

#include "ni_generic_utils.h"
#include "ni_filter_utils.h"
#include "ni_log.h"
#include "ni_util.h"

static const ni_pix_fmt_name_t g_ni_pix_fmt_name_list[] = {
    {"yuv420p",     NI_PIX_FMT_YUV420P},     /* 8-bit YUV420 planar       */
    {"yuv420p10le", NI_PIX_FMT_YUV420P10LE}, /* 10-bit YUV420 planar      */
    {"nv12",        NI_PIX_FMT_NV12},        /* 8-bit YUV420 semi-planar  */
    {"p010le",      NI_PIX_FMT_P010LE},      /* 10-bit YUV420 semi-planar */
    {"rgba",        NI_PIX_FMT_RGBA},        /* 32-bit RGBA packed        */
    {"bgra",        NI_PIX_FMT_BGRA},        /* 32-bit BGRA packed        */
    {"argb",        NI_PIX_FMT_ARGB},        /* 32-bit ARGB packed        */
    {"abgr",        NI_PIX_FMT_ABGR},        /* 32-bit ABGR packed        */
    {"bgr0",        NI_PIX_FMT_BGR0},        /* 32-bit RGB packed         */
    {"null",        NI_PIX_FMT_NONE},        /* invalid format            */
};

static const ni_gc620_pix_fmt_t g_ni_gc620_pix_fmt_list[] = {
    {NI_PIX_FMT_NV12, GC620_NV12},
    {NI_PIX_FMT_YUV420P, GC620_I420},
    {NI_PIX_FMT_P010LE, GC620_P010_MSB},
    {NI_PIX_FMT_YUV420P10LE, GC620_I010},
    {NI_PIX_FMT_YUYV422, GC620_YUYV},
    {NI_PIX_FMT_UYVY422, GC620_UYVY},
    {NI_PIX_FMT_NV16, GC620_NV16},
    {NI_PIX_FMT_RGBA, GC620_RGBA8888},
    {NI_PIX_FMT_BGR0, GC620_BGRX8888},
    {NI_PIX_FMT_BGRA, GC620_BGRA8888},
    {NI_PIX_FMT_ABGR, GC620_ABGR8888},
    {NI_PIX_FMT_ARGB, GC620_ARGB8888},
    {NI_PIX_FMT_BGRP, GC620_RGB888_PLANAR},
};

void print_version(void)
{
    ni_log(NI_LOG_ERROR,
           "Release ver: %s\n"
           "API ver:     %s\n"
           "Date:        %s\n"
           "ID:          %s\n",
           NI_XCODER_REVISION, LIBXCODER_API_VERSION,
           NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
}

int is_ni_enc_pix_fmt(ni_pix_fmt_t pix_fmt)
{
    return pix_fmt == NI_PIX_FMT_YUV420P || pix_fmt == NI_PIX_FMT_NV12 ||
           pix_fmt == NI_PIX_FMT_YUV420P10LE || pix_fmt == NI_PIX_FMT_P010LE ||
           pix_fmt == NI_PIX_FMT_RGBA || pix_fmt == NI_PIX_FMT_BGRA ||
           pix_fmt == NI_PIX_FMT_ARGB || pix_fmt == NI_PIX_FMT_ABGR;
}

ni_pix_fmt_t ni_pixel_format_search(const char *name)
{
    int i;

    for (i = 0; i < sizeof(g_ni_pix_fmt_name_list)/sizeof(ni_pix_fmt_name_t); i++)
    {
        if (!strcmp(name, g_ni_pix_fmt_name_list[i].name))
        {
            return g_ni_pix_fmt_name_list[i].pix_fmt;
        }
    }

    return NI_PIX_FMT_NONE;
}

const char *ni_pixel_format_name(ni_pix_fmt_t pix_fmt)
{
    int i;

    for (i = 0; i < sizeof(g_ni_pix_fmt_name_list)/sizeof(ni_pix_fmt_name_t); i++)
    {
        if (pix_fmt == g_ni_pix_fmt_name_list[i].pix_fmt)
        {
            return g_ni_pix_fmt_name_list[i].name;
        }
    }

    return NULL;
}

int ni_to_gc620_pix_fmt(ni_pix_fmt_t pix_fmt)
{
    int i;

    for (i = 0; i < sizeof(g_ni_gc620_pix_fmt_list)/sizeof(ni_gc620_pix_fmt_t); i++)
    {
        if (g_ni_gc620_pix_fmt_list[i].pix_fmt_ni == pix_fmt)
        {
            return g_ni_gc620_pix_fmt_list[i].pix_fmt_gc620;
        }
    }

    return -1;
}

ni_pix_fmt_t gc620_to_ni_pix_fmt(int pix_fmt)
{
    int i;

    for (i = 0; i < sizeof(g_ni_gc620_pix_fmt_list)/sizeof(ni_gc620_pix_fmt_t); i++)
    {
        if (g_ni_gc620_pix_fmt_list[i].pix_fmt_gc620 == pix_fmt)
        {
            return g_ni_gc620_pix_fmt_list[i].pix_fmt_ni;
        }
    }

    return -1;
}

ni_pixel_planar_format get_pixel_planar(ni_pix_fmt_t pix_fmt)
{
    ni_pixel_planar_format ret = -1;
    switch (pix_fmt)
    {
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            ret = NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
            break;
        case NI_PIX_FMT_8_TILED4X4:
        case NI_PIX_FMT_10_TILED4X4:
            ret = NI_PIXEL_PLANAR_FORMAT_TILED4X4;
            break;
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_ABGR: /* 32-bit ABGR packed        */
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_BGR0:
            ret = NI_PIXEL_PLANAR_FORMAT_PLANAR;
            break;
        default:
            break;
    }

    return ret;
}

bool frame_list_is_empty(ni_test_frame_list_t *list)
{
    return (list->head == list->tail);
}

bool frame_list_is_full(ni_test_frame_list_t *list)
{
    return (list->head == ((list->tail + 1) % NI_MAX_BUFFERED_FRAME));
}

bool uploader_frame_list_full(ni_test_frame_list_t *list, ni_pix_fmt_t pix_fmt)
{
    // There are two types of pixel formats for hw uploading. One is those
    // supported by the NI encoder such as yuv420p nv12 etc. The other is those
    // unsupported by the NI encoder such as rgba bgr0 etc. Such formats should
    // be restricted by the hw scaler pool size in number because they have to
    // be converted into the formats as former.
    if (is_ni_enc_pix_fmt(pix_fmt))
    {
        return frame_list_is_full(list);
    } else
    {
        return frame_list_length(list) >=
            (NI_MAX_FILTER_POOL_SIZE < NI_MAX_BUFFERED_FRAME ?
             NI_MAX_FILTER_POOL_SIZE : NI_MAX_BUFFERED_FRAME);
    }
}

int frame_list_length(ni_test_frame_list_t *list)
{
    return ((list->tail - list->head + NI_MAX_BUFFERED_FRAME) %
            NI_MAX_BUFFERED_FRAME);
}

int frame_list_enqueue(ni_test_frame_list_t *list)
{
    if (frame_list_is_full(list))
    {
        return -1;
    }
    list->tail = (list->tail + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

int frame_list_drain(ni_test_frame_list_t *list)
{
    if (frame_list_is_empty(list))
    {
        return -1;
    }
    list->head = (list->head + 1) % NI_MAX_BUFFERED_FRAME;
    return 0;
}

//Applies only to hwframe where recycling HW frame to FW is needed
//Loop through unsent frames to set in tracking list for cleanup
void hwframe_list_release(ni_test_frame_list_t *list)
{
    int i;

    // store the unsent frames in the tracker to be cleared out by scan at end
    while (!frame_list_is_empty(list))
    {
        ni_frame_t *p_frame = &list->frames[list->head].data.frame;
        niFrameSurface1_t *p_surface = (niFrameSurface1_t *)p_frame->p_data[3];
        ni_hw_frame_ref(p_surface);
        frame_list_drain(list);
    }

    for (i = 0; i < NI_MAX_BUFFERED_FRAME; i++)
    {
        ni_frame_buffer_free(&list->frames[i].data.frame);
    }
}

uint64_t get_total_file_size(FILE *fp)
{
    uint64_t total_file_size;
    fseek(fp, 0, SEEK_END);
    total_file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return total_file_size;
}

int read_and_cache_file(ni_demo_context_t *ctx, char *filename)
{
    FILE *fp;
    uint64_t total_file_size;
    size_t read_chunk = 4096;
    size_t read_rc;
    uint64_t file_size_left;

    fp = fopen(filename, "rb");
    if (!fp)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to open file %s\n", filename);
        return -1;
    }
    total_file_size = get_total_file_size(fp);
    ctx->total_file_size = total_file_size;
    file_size_left = total_file_size;

    //try to allocate memory for input file buffer, quit if failure
    if (total_file_size > 0 && !(ctx->file_cache = malloc(total_file_size)))
    {
        ni_log(NI_LOG_ERROR,
                "Error: Failed to allocate memory of size %llu for file cache\n",
                (unsigned long long)total_file_size);
        fclose(fp);
        return -1;
    }

    ctx->curr_file_offset = 0;
    ni_log(NI_LOG_INFO, "Reading %llu bytes into memory\n", (unsigned long long)total_file_size);

    while (file_size_left)
    {
        if (read_chunk > file_size_left)
        {
            read_chunk = file_size_left;
        }
        read_rc = fread(ctx->file_cache + ctx->curr_file_offset, read_chunk, 1, fp);
        if (read_rc != 1)
        {
            ni_log(NI_LOG_ERROR, "Error: Failure when reading file, bytes left to read %llu\n",
                   file_size_left);
            fclose(fp);
            return -1;
        } else
        {
            file_size_left -= read_chunk;
            ctx->curr_file_offset += read_chunk;
        }
    }

    ctx->curr_file_offset = 0;
    fclose(fp);
    return 0;
}

// reset input data buffer position to the start
void reset_data_buf_pos(ni_demo_context_t *p_ctx)
{
    p_ctx->curr_file_offset = 0;
}

// rewind input data buffer position by a number of bytes, if possible
void rewind_data_buf_pos_by(ni_demo_context_t *p_ctx, uint64_t nb_bytes)
{
    // curr_found_pos (current input parser offset) could be equal to nb_bytes (NAL size) when offset jumps back to 0 due to repeat option
    if (p_ctx->curr_file_offset >= nb_bytes)
    {
        p_ctx->curr_file_offset -= nb_bytes;
    } else
    {
        ni_log(NI_LOG_ERROR, "Error %s %d bytes!\n", __func__, nb_bytes);
    }
}

// Note we do not need to consider padding bytes from yuv/rgba file reading
int frame_read_buffer_size(int w, int h, ni_pix_fmt_t pix_fmt,
                           ni_sw_pix_fmt_t sw_pix_fmt)
{
    int data_len = 0;

    if (sw_pix_fmt == NI_SW_PIX_FMT_YUV444P)
    {
        data_len = w * h * 3;
    } else if (sw_pix_fmt == NI_SW_PIX_FMT_YUV444P10LE)
    {
        data_len = w * h * 6;
    } else
    {
        switch (pix_fmt)
        {
            case NI_PIX_FMT_NV12:
            case NI_PIX_FMT_YUV420P:
                data_len = w * h * 3 / 2;
                break;
            case NI_PIX_FMT_P010LE:
            case NI_PIX_FMT_YUV420P10LE:
                data_len = w * h * 3;
                break;
            case NI_PIX_FMT_RGBA:
            case NI_PIX_FMT_BGRA:
            case NI_PIX_FMT_ARGB:
            case NI_PIX_FMT_ABGR:
            case NI_PIX_FMT_BGR0:
            case NI_PIX_FMT_BGRP:
                data_len = w * 4 * h;
                break;
            default:
                break;
        }
    }

    return data_len;
}

// return actual bytes read from file, in requested size
uint32_t read_next_chunk_from_file(ni_demo_context_t *p_ctx, FILE *fp,
                                   uint8_t *p_dst, uint32_t to_read)
{
    uint64_t data_left_size = p_ctx->total_file_size - p_ctx->curr_file_offset;

    ni_log(NI_LOG_DEBUG, "%s: p_dst %p len %u total size %llu current offset %llu\n",
           __func__, p_dst, to_read, p_ctx->total_file_size, p_ctx->curr_file_offset);

    if (data_left_size == 0)
    {
        if (p_ctx->loops_left > 1)
        {
            p_ctx->loops_left--;
            ni_log(NI_LOG_DEBUG, "input processed %d left\n", p_ctx->loops_left);
            fseek(fp, 0, SEEK_SET);   //back to beginning
            data_left_size = p_ctx->total_file_size;
            p_ctx->curr_file_offset = 0;
        } else
        {
            return 0;
        }
    } else if (data_left_size < to_read)
    {
        to_read = data_left_size;
    }

    int read_rc = fread(p_dst, to_read, 1, fp);
    if (read_rc != 1)
    {
        ni_log(NI_LOG_ERROR, "Error: Failed to read input file, %lu bytes left to read\n",
               data_left_size);
        return -1;
    }
    p_ctx->curr_file_offset += to_read;

    return to_read;
}

int read_yuv_from_file(ni_demo_context_t *p_ctx, FILE *pfs, void *yuv_buf, int width, int height,
                       ni_pix_fmt_t pix_fmt, ni_sw_pix_fmt_t sw_pix_fmt,
                       int *eos, ni_session_run_state_t run_state)
{
    int chunk_size, frame_size;

    if (run_state == SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        // The first YUV frame was consumed on sequence change. Reset the file
        // pointer until the end of encoded packet is read.
        ni_log(NI_LOG_DEBUG, "read_yuv_from_file wait sequence change finish\n");
        p_ctx->total_file_size = get_total_file_size(pfs);
        p_ctx->curr_file_offset = 0;
        return 0;
    }

    frame_size = frame_read_buffer_size(width, height, pix_fmt, sw_pix_fmt);

    chunk_size = read_next_chunk_from_file(p_ctx, pfs, yuv_buf, frame_size);
    if (chunk_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: could not read file!");
        return -1;
    } else if (chunk_size == 0)
    {
        *eos = 1;
        ni_log(NI_LOG_DEBUG, "%s: read chunk size 0, eos!\n", __func__);
        return 0;
    } else
    {
        *eos = 0;
        return chunk_size;
    }
}

int convert_yuv_444p_to_420p(ni_session_data_io_t *p_frame,
                             void *yuv_buf, int width, int height,
                             ni_sw_pix_fmt_t sw_pix_fmt, int mode,
                             ni_codec_format_t codec_format)
{
    int i, factor;
    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS] = { NULL };
    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = { 0 };
    int height_aligned[NI_MAX_NUM_DATA_POINTERS] = { 0 };

    if (yuv_buf == NULL)
    {
        // EOS
        return 0;
    }

    switch (sw_pix_fmt)
    {
        case NI_SW_PIX_FMT_YUV444P:
            factor = 1;
            break;
        case NI_SW_PIX_FMT_YUV444P10LE:
            factor = 2;
            break;
        default:
            ni_log(NI_LOG_ERROR, "Error: invalid sw pix fmt %d\n", sw_pix_fmt);
            return -1;
    }

    ni_get_hw_yuv420p_dim(width, height, factor, 0, dst_stride, height_aligned);

    for (i = 0; i < 2; i++)
    {
        ni_frame_t *frame = &p_frame[i].data.frame;
        ni_encoder_frame_buffer_alloc(frame, width, height, dst_stride,
                                      codec_format == NI_CODEC_FORMAT_H264,
                                      NI_APP_ENC_FRAME_META_DATA_SIZE, 0);
        if (frame->p_data[0] == NULL)
        {
            ni_log(NI_LOG_ERROR, "Error: could not allocate YUV frame buffer!\n");
            return -1;
        }
    }

    p_src[0] = yuv_buf;
    p_src[1] = p_src[0] + width * factor * height;
    p_src[2] = p_src[1] + width * factor * height;

    ni_copy_yuv_444p_to_420p(p_frame[0].data.frame.p_data,
                             p_frame[1].data.frame.p_data,
                             p_src, width, height, factor, mode);

    return 0;
}

/*!*****************************************************************************
 *  \brief  Write hwdl data to files.
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int write_rawvideo_data(FILE *p_file, int input_aligned_width, int input_aligned_height,
                        int output_width, int output_height, int format, ni_frame_t *p_out_frame)
{
    int i, j;
    uint8_t *src;
    int plane_width, plane_height, write_width, write_height, bit_depth_factor;

    if (p_file && p_out_frame)
    {
        switch (format)
        {
        case NI_PIX_FMT_YUV420P:
        case NI_PIX_FMT_YUV420P10LE:
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
        {
            for (i = 0; i < 3; i++)
            {
                src = p_out_frame->p_data[i];

                plane_width = input_aligned_width;
                plane_height = input_aligned_height;
                write_width = output_width;
                write_height = output_height;

                // support for 8/10 bit depth
                bit_depth_factor = 1;
                if (format == NI_PIX_FMT_YUV420P10LE || format == NI_PIX_FMT_P010LE) {
                    bit_depth_factor = 2;
                }
                write_width *= bit_depth_factor;

                if (i == 1 || i == 2)
                {
                    plane_height /= 2;
                    // U/V stride size is multiple of 128, following the calculation
                    // in ni_decoder_frame_buffer_alloc
                    plane_width = ((output_width / 2 * bit_depth_factor + 127) / 128) * 128;

                    if (format == NI_PIX_FMT_NV12 || format == NI_PIX_FMT_P010LE)
                    {
                        plane_width = (((output_width * bit_depth_factor + 127) / 128) * 128);
                        // for semi-planar format, output UV at same time (data[1]) and skip data[2]
                        if (i == 1)
                        {
                            write_width *= 2;
                        }
                        if (i == 2)
                        {
                            plane_height = 0;
                        }
                    }

                    write_height /= 2;
                    write_width /= 2;
                }

                // apply the cropping window in writing out the YUV frame
                // for now the window is usually crop-left = crop-top = 0, and we
                // use this to simplify the cropping logic
                for (j = 0; j < plane_height; j++)
                {
                    if (j < write_height &&
                        fwrite(src, write_width, 1, p_file) != 1)
                    {
                        ni_log(NI_LOG_ERROR,
                                "Error: writing data plane %d: height %d error!\n",
                                i, plane_height);
                        ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                        return NI_RETCODE_FAILURE;
                    }
                    src += plane_width;
                }
            }
            break;
        }
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_BGR0:
        {
            src = p_out_frame->p_data[0];
            if (fwrite(src, output_width * output_height * 4, 1, p_file) != 1)
            {
                ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                return NI_RETCODE_FAILURE;
            }
            break;
        }
        case NI_PIX_FMT_BGRP:
        {
            for (i = 0; i < 3; i++)
            {
                src = p_out_frame->p_data[i];
                if (fwrite(src, output_width * output_height, 1, p_file) != 1)
                {
                    ni_log(NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                    return NI_RETCODE_FAILURE;
                }
            }
            break;
        }
        default:
        {
            ni_log(NI_LOG_ERROR, "Unsupported format %d\n", format);
            return NI_RETCODE_FAILURE;
        }
        }

        if (fflush(p_file))
        {
            ni_log(NI_LOG_ERROR,
                   "Error: writing data frame flush failed! errno %d\n",
                   errno);
            return NI_RETCODE_FAILURE;
        }
    }
    return NI_RETCODE_SUCCESS;
}

static ni_hwframe_ref_t g_hwframe_pool[NI_MAX_DR_HWDESC_FRAME_INDEX];

// final scan clean up of ref counted HW frame pool, return number of recycled
// HW frames.
int scan_and_clean_hwdescriptors(void)
{
    int i;
    int recycled = 0;

    for (i = 0; i < NI_MAX_DR_HWDESC_FRAME_INDEX; i++)
    {
        if (g_hwframe_pool[i].ref_cnt &&
            g_hwframe_pool[i].surface.ui16FrameIdx)
        {
            ni_log(NI_LOG_DEBUG, "clean/recycle frame idx %u ref_cnt %d\n,",
                   g_hwframe_pool[i].surface.ui16FrameIdx,
                   g_hwframe_pool[i].ref_cnt);
            ni_hwframe_buffer_recycle2(&g_hwframe_pool[i].surface);
            g_hwframe_pool[i].ref_cnt = 0;
            recycled++;
        }
    }

    return recycled;
}

// reference HW frame
void ni_hw_frame_ref(const niFrameSurface1_t *p_surface)
{
    uint16_t hwframe_index;

    if (!p_surface)
    {
        return;
    }

    hwframe_index = p_surface->ui16FrameIdx;
    g_hwframe_pool[hwframe_index].ref_cnt++;
    if (1 == g_hwframe_pool[hwframe_index].ref_cnt)
    {
        memcpy(&g_hwframe_pool[hwframe_index].surface, p_surface,
               sizeof(niFrameSurface1_t));
    }
    ni_log(NI_LOG_TRACE, "%s frame idx %u ref_cnt %d ..\n", __func__,
           hwframe_index, g_hwframe_pool[hwframe_index].ref_cnt);
}

// unref HW frame
void ni_hw_frame_unref(uint16_t hwframe_index)
{
    if (g_hwframe_pool[hwframe_index].ref_cnt > 0)
    {
        g_hwframe_pool[hwframe_index].ref_cnt--;
        if (0 == g_hwframe_pool[hwframe_index].ref_cnt &&
            g_hwframe_pool[hwframe_index].surface.ui16FrameIdx)
        {
            ni_log(NI_LOG_TRACE, "%s frame idx recycing frame idx %u\n", __func__,
                   g_hwframe_pool[hwframe_index].surface.ui16FrameIdx);

            ni_hwframe_buffer_recycle2(&g_hwframe_pool[hwframe_index].surface);
        }
        ni_log(NI_LOG_TRACE, "%s frame idx %u ref_cnt now: %d\n", __func__,
               hwframe_index, g_hwframe_pool[hwframe_index].ref_cnt);
    } else
    {
        ni_log(NI_LOG_ERROR, "%s error frame idx %u ref_cnt %d <= 0\n",
               __func__, hwframe_index,
               g_hwframe_pool[hwframe_index].ref_cnt);
    }
}

/*!*****************************************************************************
 *  \brief  Download hw frames by HwDesc.
 *
 *  \param
 *
 *  \return number of bytes downloaded if successful, <= 0 if failed
 ******************************************************************************/
int hwdl_frame(ni_session_context_t *p_ctx,
               ni_session_data_io_t *p_session_data, ni_frame_t *p_src_frame,
               int output_format)
{
    niFrameSurface1_t *src_surf = (niFrameSurface1_t *)(p_src_frame->p_data[3]);
    int ret = 0;

    ret = ni_frame_buffer_alloc_dl(&(p_session_data->data.frame),
                                   src_surf->ui16width, src_surf->ui16height,
                                   output_format);

    if (ret != NI_RETCODE_SUCCESS)
    {
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    p_ctx->is_auto_dl = false;
    ret = ni_device_session_hwdl(p_ctx, p_session_data, src_surf);
    if (ret <= 0)
    {
        ni_frame_buffer_free(&p_session_data->data.frame);
        return ret;
    }
    return ret;
}

/*!*****************************************************************************
 *  \brief Read from input file, upload to encoder, retrieve HW descriptor
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
int upload_send_data_get_desc(ni_demo_context_t *p_ctx, ni_session_context_t *p_upl_ctx,
                              ni_session_data_io_t *p_swin_data,   //intermediate for swf
                              ni_session_data_io_t *p_in_data,
                              int input_video_width, int input_video_height,
                              void *yuv_buf)
{
    int retval, is_semiplanar;
    ni_frame_t *p_in_frame = &p_in_data->data.frame;       //hwframe
    ni_frame_t *p_swin_frame = &p_swin_data->data.frame;   //swframe
    niFrameSurface1_t *dst_surf = NULL;

    ni_log(NI_LOG_DEBUG, "===> upload_send_data <===\n");

    p_in_frame->start_of_stream = 0;
    p_in_frame->end_of_stream = yuv_buf == NULL;
    p_in_frame->force_key_frame = 0;
    p_in_frame->video_width = p_swin_frame->video_width = input_video_width;
    p_in_frame->video_height = p_swin_frame->video_height = input_video_height;
    // only metadata header for now
    p_in_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;
    p_swin_frame->extra_data_len = NI_APP_ENC_FRAME_META_DATA_SIZE;

    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int dst_height_aligned[NI_MAX_NUM_DATA_POINTERS] = {0};
    ni_get_min_frame_dim(
            input_video_width, input_video_height,
            p_upl_ctx->pixel_format, dst_stride,
            dst_height_aligned);
    is_semiplanar = (
        get_pixel_planar(p_upl_ctx->pixel_format) == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR);
    ni_encoder_sw_frame_buffer_alloc(
            !is_semiplanar, p_swin_frame, input_video_width,
            dst_height_aligned[0], dst_stride, 0,
            (int)p_swin_frame->extra_data_len, false);
    if (!p_swin_frame->p_data[0])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate YUV frame buffer!");
        return -1;
    }

    //can also be ni_frame_buffer_alloc()
    ni_frame_buffer_alloc_hwenc(p_in_frame, input_video_width,
                                input_video_height,
                                (int)p_in_frame->extra_data_len);
    if (!p_in_frame->p_data[3])
    {
        ni_log(NI_LOG_ERROR, "Error: could not allocate hw frame buffer!");
        return -1;
    }

    dst_surf = (niFrameSurface1_t *)p_in_frame->p_data[3];

    ni_log(NI_LOG_DEBUG, "p_dst alloc linesize = %d/%d/%d  src height=%d  "
                   "dst height aligned = %d/%d/%d\n",
                   dst_stride[0], dst_stride[1], dst_stride[2],
                   input_video_height, dst_height_aligned[0],
                   dst_height_aligned[1], dst_height_aligned[2]);

    if (p_in_frame->end_of_stream)
    {
        goto hwupload;
    }

    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS] = {NULL};
    int src_stride[NI_MAX_NUM_DATA_POINTERS] = {0};
    int src_height[NI_MAX_NUM_DATA_POINTERS] = {0};
    src_height[0] = input_video_height;
    src_height[1] = input_video_height / 2;
    src_height[2] = (is_semiplanar) ? 0 : (input_video_height / 2);
    uint32_t conf_win_right = 0;

    switch (p_upl_ctx->pixel_format)
    {
    case NI_PIX_FMT_RGBA:
    case NI_PIX_FMT_BGRA:
    case NI_PIX_FMT_ABGR:
    case NI_PIX_FMT_ARGB:
    case NI_PIX_FMT_BGR0:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_height[0] = input_video_height;
        src_height[1] = 0;
        src_height[2] = 0;
        p_src[0] = yuv_buf;
        break;
    case NI_PIX_FMT_NV12:
    case NI_PIX_FMT_P010LE:
    case NI_PIX_FMT_YUV420P:
    case NI_PIX_FMT_YUV420P10LE:
        src_stride[0] = input_video_width * p_upl_ctx->bit_depth_factor;
        src_stride[1] = is_semiplanar ? src_stride[0] : src_stride[0] / 2;
        src_stride[2] = is_semiplanar ? 0 : src_stride[0] / 2;

        p_src[0] = yuv_buf;
        p_src[1] = p_src[0] + src_stride[0] * src_height[0];
        p_src[2] = p_src[1] + src_stride[1] * src_height[1];
        if (input_video_width < NI_MIN_WIDTH)
        {
            conf_win_right += (NI_MIN_WIDTH - input_video_width) / 2 * 2;
        } else
        {
            conf_win_right += (NI_VPU_CEIL(input_video_width, 2) - input_video_width) / 2 * 2;
        }
        break;
    default:
        ni_log(NI_LOG_ERROR, "%s: Error Invalid pixel format %s\n", __func__,
                ni_pixel_format_name(p_upl_ctx->pixel_format));
        return -1;
    }

    ni_copy_frame_data(
        (uint8_t **)(p_swin_frame->p_data),
        p_src, input_video_width,
        input_video_height, p_upl_ctx->bit_depth_factor,
        p_upl_ctx->pixel_format, conf_win_right, dst_stride,
        dst_height_aligned, src_stride, src_height);

hwupload:
    retval = ni_device_session_hwup(p_upl_ctx, p_swin_data, dst_surf);
    if (retval < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: ni_device_session_hwup():%d, frameNum %u\n",
                retval, p_ctx->num_frames_received);
        return -1;
    } else
    {
        p_ctx->dec_total_bytes_sent += p_swin_frame->data_len[0] + p_swin_frame->data_len[1] +
            p_swin_frame->data_len[2] + p_swin_frame->data_len[3];
        ni_log(NI_LOG_DEBUG, "upload_send_data: total sent data size=%lu\n",
               p_ctx->dec_total_bytes_sent);

        dst_surf->ui16width = input_video_width;
        dst_surf->ui16height = input_video_height;
        dst_surf->encoding_type = is_semiplanar ?
            NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR :
            NI_PIXEL_PLANAR_FORMAT_PLANAR;

        p_ctx->num_frames_received++;

        ni_log(NI_LOG_DEBUG, "upload_send_data: FID = %d success, number:%u\n",
               dst_surf->ui16FrameIdx, p_ctx->num_frames_received);
    }

    return 0;
}

/*!*****************************************************************************
 *  \brief  Uploader session open
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int uploader_open_session(ni_session_context_t *p_upl_ctx, int iXcoderGUID,
                          int width, int height, ni_pix_fmt_t pix_fmt,
                          int is_p2p, int pool_size)
{
    int ret = 0;
    p_upl_ctx->session_id = NI_INVALID_SESSION_ID;

    // assign the card GUID in the encoder context and let session open
    // take care of the rest
    p_upl_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->hw_id = iXcoderGUID;

    // Set the input frame format of the upload session
    ret = ni_uploader_set_frame_format(p_upl_ctx, width, height, pix_fmt, is_p2p);
    if(ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure. Failed to set uploader format!\n", __func__);
        return ret;
    }

    ret = ni_device_session_open(p_upl_ctx, NI_DEVICE_TYPE_UPLOAD);
    if (ret != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
        return ret;
    } else
    {
        ni_log(NI_LOG_INFO, "Uploader device %d session open successful.\n", iXcoderGUID);
    }

    ret = ni_device_session_init_framepool(p_upl_ctx, pool_size, 0);
    if (ret < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: %s failure!\n", __func__);
    } else
    {
        ni_log(NI_LOG_INFO, "Uploader device %d configured successful.\n", iXcoderGUID);
    }

    return ret;
}

niFrameSurface1_t *hwupload_frame(ni_demo_context_t *p_ctx,
                                  ni_session_context_t *p_upl_ctx,
                                  ni_session_context_t *p_sca_ctx,
                                  ni_session_data_io_t *p_sw_data,
                                  ni_session_data_io_t *p_hw_data,
                                  ni_session_data_io_t *p_scale_data,
                                  ni_pix_fmt_t pix_fmt, int width,
                                  int height, FILE *pfs, void *yuv_buf, int *eos)
{
    int ret, chunk_size;
    niFrameSurface1_t *p_hwframe = NULL;

    chunk_size = read_yuv_from_file(p_ctx, pfs, yuv_buf, width, height, pix_fmt,
                                    NI_SW_PIX_FMT_NONE, eos,
                                    p_upl_ctx->session_run_state);
    if (chunk_size < 0)
    {
        ni_log(NI_LOG_ERROR, "Error: read yuv file error\n");
        return NULL;
    }

    // need to have the hwframe before open encoder
    ret = upload_send_data_get_desc(p_ctx, p_upl_ctx, p_sw_data, p_hw_data, width,
                                    height, *eos ? NULL : yuv_buf);
    if (p_upl_ctx->status == NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL)
    {
        ni_log(NI_LOG_DEBUG, "No space to write to, try to read a packet\n");
        //file was read so reset read pointer and try again
        rewind_data_buf_pos_by(p_ctx, chunk_size);
        fseek(pfs, p_ctx->curr_file_offset, SEEK_SET);
        return NULL;
    } else if (ret)
    {
        ni_log(NI_LOG_ERROR, "Error: upload frame error\n");
        return NULL;
    }

    p_hwframe = (niFrameSurface1_t *)p_hw_data->data.frame.p_data[3];
    if (p_hw_data->data.frame.end_of_stream)
    {
        // reach eos
        return p_hwframe;
    }

    // need to convert into pixel format for NI encoding
    if (!is_ni_enc_pix_fmt(pix_fmt))
    {
        ni_hw_frame_ref(p_hwframe);
        ret = scale_filter(p_sca_ctx, &p_hw_data->data.frame, p_scale_data,
                           p_upl_ctx->hw_id, width, height,
                           ni_to_gc620_pix_fmt(pix_fmt), GC620_I420);
        ni_hw_frame_unref(p_hwframe->ui16FrameIdx);
        if (ret)
        {
            ni_log(NI_LOG_ERROR, "Error: upload frame error\n");
            return NULL;
        }
        p_hwframe = (niFrameSurface1_t *)p_scale_data->data.frame.p_data[3];
    }

    return p_hwframe;
}
