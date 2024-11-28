/*******************************************************************************
 *
 * Copyright (C) 2024 NETINT Technologies
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
 *  \file   ni_p2p_read_test.c
 *
 *  \brief  Application for performing video processing using libxcoder API and
 *          P2P DMA. Its code provides examples on how to programatically use
 *          libxcoder API in conjunction with P2P DMA.
 *
 *          This test program requires TWO Quadra devices. One Quadra device
 *          acts as a proxy for the GPU card. The other Quadra device reads
 *          frames from the proxy GPU Quadra device via peer-to-peer then
 *          encodes the frame.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include <sys/mman.h>

#include "ni_device_api.h"
#include "ni_util.h"

#include <sys/ioctl.h>
#include "ni_p2p_ioctl.h"

#define MAX_YUV_FRAME_SIZE (7680 * 4320 * 3 / 2)
#define MAX_ABGR_FRAME_SIZE (7680 * 4320 * 4)

#if MAX_ABGR_FRAME_SIZE > MAX_YUV_FRAME_SIZE
#define MAX_FRAME_SIZE MAX_ABGR_FRAME_SIZE
#else
#define MAX_FRAME_SIZE MAX_YUV_FRAME_SIZE
#endif

#define FILE_NAME_LEN 256

int send_fin_flag = 0;
int receive_fin_flag = 0;
int enc_eos_sent = 0;

uint32_t number_of_frames = 0;
uint32_t number_of_packets = 0;
uint64_t data_left_size = 0;
int g_repeat = 1;

struct timeval start_time;
struct timeval previous_time;
struct timeval current_time;

time_t start_timestamp = 0;
time_t previous_timestamp = 0;
time_t current_timestamp = 0;

unsigned long total_file_size = 0;

uint8_t *g_curr_cache_pos = NULL;
uint8_t *g_raw_frame = NULL;

uint8_t g_rgb2yuv_csc = 0;

/*!****************************************************************************
 *  \brief  Exit on argument error
 *
 *  \param[in]  arg_name    pointer to argument name
 *        [in]  param       pointer to provided parameter
 *
 *  \return     None        program exit
 ******************************************************************************/
void arg_error_exit(char *arg_name, char *param)
{
    fprintf(stderr, "Error: unrecognized argument for %s, \"%s\"\n", arg_name,
            param);
    exit(-1);
}

/*!****************************************************************************
 *  \brief  Read the next frame
 *
 *  \param[in]  fd          file descriptor of input file
 *  \param[out] p_dst       pointer to place the frame
 *  \param[in]  to_read     number of bytes to copy to the pointer
 *
 *  \return     bytes copied
 ******************************************************************************/
int read_next_chunk_from_file(int fd, uint8_t *p_dst, uint32_t to_read)
{
    uint8_t *tmp_dst = p_dst;
    ni_log(NI_LOG_DEBUG,
        "read_next_chunk_from_file:p_dst %p len %u totalSize %llu left %llu\n",
        tmp_dst, to_read, (unsigned long long)total_file_size, (unsigned long long)data_left_size);
    int to_copy = to_read;
    unsigned long tmpFileSize = to_read;
    if (data_left_size == 0)
    {
        if (g_repeat > 1)
        {
            data_left_size = total_file_size;
            g_repeat--;
            ni_log(NI_LOG_DEBUG, "input processed %d left\n", g_repeat);
            lseek(fd, 0, SEEK_SET);   //back to beginning
        } else
        {
            return 0;
        }
    } else if (data_left_size < to_read)
    {
        tmpFileSize = data_left_size;
        to_copy = data_left_size;
    }

    int one_read_size = read(fd, tmp_dst, to_copy);
    if (one_read_size == -1)
    {
        fprintf(stderr, "Error: reading file, quit! left-to-read %lu\n",
                tmpFileSize);
        fprintf(stderr, "Error: input file read error\n");
        return -1;
    }
    data_left_size -= one_read_size;

    return to_copy;
}

/*!****************************************************************************
 *  \brief  Get file size
 *
 *  \param [in] filename        name of input file
 *         [out] bytes_read     number of bytes in file
 *
 *  \return     0 on success
 *              < 0 on error
 ******************************************************************************/
int get_file_size(const char *filename, unsigned long *bytes_read)
{
    struct stat info;

    /* Get information on the file */
    if (stat(filename, &info) < 0)
    {
        fprintf(stderr, "Can't stat %s\n", filename);
        return -1;
    }

    /* Check the file size */
    if (info.st_size <= 0)
    {
        fprintf(stderr, "File %s is empty\n", filename);
        return -1;
    }

    *bytes_read = info.st_size;

    return 0;
}

/*!****************************************************************************
*  \brief  Recycle hw frame back to Quadra
*
*  \param [in] p2p_frame - hw frame to recycle
*
*  \return  Returns NI_RETCODE_SUCCESS or error
*******************************************************************************/
int recycle_frame(ni_frame_t *p2p_frame)
{
    ni_retcode_t rc;

    rc = ni_hwframe_p2p_buffer_recycle(p2p_frame);

    if (rc != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Recycle failed\n");
    }

    return rc;
}

/*!****************************************************************************
 * \brief   Import a dma buf to a Quadra device
 *
 * \param  [in] p_session - upload session to the Quadra device
 *         [in] frame     - frame of the proxy GPU card containing the dma buf fd
 *         [in] frame_size - frame size in bytes
 *         [out] dma_addrs - DMA addresses of the GPU frame
 *
 * \return Returns 0 on success, -1 otherwise
 ******************************************************************************/
static int import_dma_buf(
    ni_session_context_t *p_session,
    ni_frame_t *frame,
    unsigned long frame_size,
    ni_p2p_sgl_t *dma_addr)
{
    niFrameSurface1_t *frame_surface;
    struct netint_iocmd_import_dmabuf uimp;
    int ret,i;

    frame_surface = (niFrameSurface1_t *) frame->p_data[3];

    uimp.fd = frame_surface->dma_buf_fd;
    uimp.flags = 0; // import
    uimp.domain = p_session->domain;
    uimp.bus = p_session->bus;
    uimp.dev = p_session->dev;
    uimp.fn = p_session->fn;

    // Pass frame size to kernel driver. Only used for dma-buf compiled in A1 mode
    uimp.dma_len[0] = frame_size;

    ret = ioctl(p_session->netint_fd, NETINT_IOCTL_IMPORT_DMABUF, &uimp);

    if (ret == 0)
    {
        for (i = 0; i < uimp.nents; i++)
        {
            dma_addr->ui32DMALen[i] = uimp.dma_len[i];
            dma_addr->ui64DMAAddr[i] = uimp.dma_addr[i];
        }
        dma_addr->ui32NumEntries = uimp.nents;
    }

    return ret;
}

/*!****************************************************************************
 * \brief   Unimport a dma buf to a Quadra device
 *
 * \param  [in] p_session - upload session to the Quadra device
 *         [in] frame     - frame of the GPU card containing the dma buf fd
 *
 * \return Returns 0 on success, -1 otherwise
 ******************************************************************************/
static int unimport_dma_buf(
    ni_session_context_t *p_session,
    ni_frame_t *frame)
{
    niFrameSurface1_t *frame_surface;
    struct netint_iocmd_import_dmabuf uimp;
    int ret;

    frame_surface = (niFrameSurface1_t *) frame->p_data[3];

    uimp.fd = frame_surface->dma_buf_fd;
    uimp.flags = 1; // unimport
    uimp.domain = p_session->domain;
    uimp.bus = p_session->bus;
    uimp.dev = p_session->dev,
    uimp.fn = p_session->fn;

    ret = ioctl(p_session->netint_fd, NETINT_IOCTL_IMPORT_DMABUF, &uimp);

    return ret;
}

/*!****************************************************************************
 *  \brief  Reads video data from input file then calls a special libxcoder API
 *          function to transfer the video data into the hardware frame on
 *          the proxy GPU Quadra device.
 *
 *  \param  [in]    p_ctx               pointer to upload session context
 *          [in]    fd                  file descriptor of input file
 *          [in]    pp_data             address of pointer to frame data
 *          [in]    p_in_frame          pointer to hardware frame
 *          [in]    input_video_width   video width
 *          [in]    input_video_height  video height
 *          [out]   bytes_sent          updated byte count of total data read
 *          [out]   input_exhausted     set to 1 when we reach end-of-file
 *
 *  \return  0 on success
 *          -1 on error
 ******************************************************************************/
int gpu_render_frame(
    ni_session_context_t *p_ctx,
    int fd,
    uint8_t **pp_data,
    ni_frame_t *p_in_frame,
    int input_video_width,
    int input_video_height,
    unsigned long *bytes_sent,
    int *input_exhausted)
{
    static uint8_t tmp_buf[MAX_FRAME_SIZE];
    void *p_buffer;
    uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS];
    uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS];
    int src_stride[NI_MAX_NUM_DATA_POINTERS];
    int src_height[NI_MAX_NUM_DATA_POINTERS];
    int dst_stride[NI_MAX_NUM_DATA_POINTERS] = {0, 0, 0, 0};
    int dst_height[NI_MAX_NUM_DATA_POINTERS] = {0, 0, 0, 0};
    int frame_size;
    int chunk_size;
    int alignedh;
    int Ysize;
    int Usize;
    int Vsize;
    int linewidth;
    int row;
    uint8_t *pSrc,*pDst;
    int total_size;

    ni_log2(p_ctx, NI_LOG_DEBUG,  "===> gpu render frame <===\n");

    if (g_rgb2yuv_csc)
    {
        /* An 8-bit RGBA frame is in a packed format */
        /* and occupies width * height * 4 bytes     */
        frame_size = input_video_width * input_video_height * 4;
    }
    else
    {
        /* An 8-bit YUV420 planar frame occupies */
        /* [(width x height x 3)/2] bytes        */
        frame_size = input_video_height * input_video_width * 3 / 2;
    }

    chunk_size = read_next_chunk_from_file(fd, tmp_buf, frame_size);

    if (chunk_size == 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: read chunk size 0, eos!\n", __func__);
        *input_exhausted = 1;
    }

    p_in_frame->video_width = input_video_width;
    p_in_frame->video_height = input_video_height;
    p_in_frame->extra_data_len = 0;

    ni_get_frame_dim(input_video_width, input_video_height,
                     g_rgb2yuv_csc ? NI_PIX_FMT_ABGR : NI_PIX_FMT_YUV420P,
                     dst_stride, dst_height);

    ni_log2(p_ctx, NI_LOG_DEBUG,  "p_dst alloc linesize = %d/%d/%d  src height=%d  "
                   "dst height aligned = %d/%d/%d  \n",
                   dst_stride[0], dst_stride[1], dst_stride[2],
                   input_video_height, dst_height[0], dst_height[1],
                   dst_height[2]);

    if (g_rgb2yuv_csc)
    {
        linewidth = NI_VPU_ALIGN16(input_video_width) * 4;
        total_size = linewidth * input_video_height;

        // Round to nearest 4K
        total_size = NI_VPU_ALIGN4096(total_size);

        if (*pp_data == NULL)
        {
            if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), total_size))
            {
                fprintf(stderr, "Can't alloc memory\n");
                return -1;
            }

            *pp_data = p_buffer;
        }

        pSrc = tmp_buf;
        pDst = *pp_data;

        for (row = 0; row < input_video_height; row++)
        {
            memcpy(pDst, pSrc, linewidth);
            pSrc += linewidth;
            pDst += linewidth;
        }
    }
    else
    {
        src_stride[0] = input_video_width * p_ctx->bit_depth_factor;
        src_stride[1] = src_stride[2] = src_stride[0] / 2;

        src_height[0] = input_video_height;
        src_height[1] = src_height[0] / 2;
        src_height[2] = src_height[1];

        p_src[0] = tmp_buf;
        p_src[1] = tmp_buf + src_stride[0] * src_height[0];
        p_src[2] = p_src[1] + src_stride[1] * src_height[1];

        alignedh = (input_video_height + 1) & ~1;

        Ysize = dst_stride[0] * alignedh;
        Usize = dst_stride[1] * alignedh / 2;
        Vsize = dst_stride[2] * alignedh / 2;

        total_size = Ysize + Usize + Vsize;
        total_size = NI_VPU_ALIGN4096(total_size);

        if (*pp_data == NULL)
        {
            if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), total_size))
            {
                fprintf(stderr, "Can't alloc memory\n");
                return -1;
            }

            *pp_data = p_buffer;
        }

        p_dst[0] = *pp_data;
        p_dst[1] = *pp_data + Ysize;
        p_dst[2] = *pp_data + Ysize + Usize;
        p_dst[3] = NULL;

        ni_copy_hw_yuv420p(p_dst, p_src, input_video_width, input_video_height, 1,
                           0, 0, dst_stride, dst_height, src_stride, src_height);
    }

    *bytes_sent = total_size;

    /* Write a frame of video data to our proxy GPU Quadra device */
    ni_uploader_p2p_test_load(p_ctx, *pp_data, total_size, p_in_frame);

    return 0;
}


/*!****************************************************************************
 *  \brief  Prepare frame on the proxy GPU Quadra device
 *
 *  \param [in] p_gpu_ctx           pointer to caller allocated gpu
 *                                  session context
 *         [in] input_video_width   video width
 *         [in] input_video_height  video height
 *         [out] gpu_frame          gpu frame
 *
 *  \return  0  on success
 *          -1  on error
 ******************************************************************************/
int gpu_prepare_frame(ni_session_context_t *p_gpu_ctx, int input_video_width,
                       int input_video_height, ni_frame_t *gpu_frame)
{
    int ret = 0;

    // Allocate memory for a hardware frame
    gpu_frame->start_of_stream = 0;
    gpu_frame->end_of_stream = 0;
    gpu_frame->force_key_frame = 0;
    gpu_frame->extra_data_len = 0;

    // Allocate a hardware ni_frame structure for the encoder
    if (ni_frame_buffer_alloc_hwenc(
           gpu_frame, input_video_width, input_video_height,
           (int)gpu_frame->extra_data_len) != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: could not allocate hw frame buffer!");
        ret = -1;
        goto fail_out;
    }

#ifndef _WIN32
    // Acquire a hw frame from the proxy GPU session. This obtains a handle
    // to Quadra memory from the previously created frame pool.
    if (ni_device_session_acquire(p_gpu_ctx, gpu_frame))
    {
        fprintf(stderr, "Error: failed ni_device_session_acquire()\n");
        ret = -1;
        goto fail_out;
    }
#endif

    return ret;

fail_out:
    ni_frame_buffer_free(gpu_frame);
    return ret;
}

/*!****************************************************************************
 *  \brief  Prepare frame on the encoding Quadra device
 *
 *  \param [in] p_upl_ctx           pointer to caller allocated upload
 *                                  session context
 *         [in] input_video_width   video width
 *         [in] input_video_height  video height
 *         [out] p2p_frame          p2p frame
 *
 *  \return  0  on success
 *          -1  on error
 ******************************************************************************/
int enc_prepare_frame(ni_session_context_t *p_upl_ctx, int input_video_width,
                  int input_video_height, ni_frame_t *p2p_frame)
{
    int ret = 0;

    p2p_frame->start_of_stream = 0;
    p2p_frame->end_of_stream = 0;
    p2p_frame->force_key_frame = 0;
    p2p_frame->extra_data_len = 0;

    // Allocate a hardware ni_frame structure for the encoder
    if (ni_frame_buffer_alloc_hwenc(
        p2p_frame, input_video_width, input_video_height,
        (int)p2p_frame->extra_data_len) != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: could not allocate hw frame buffer!\n");
        ret = -1;
        goto fail_out;
    }

#ifndef _WIN32
    if (ni_device_session_acquire_for_read(p_upl_ctx, p2p_frame))
    {
        fprintf(stderr, "Error: failed ni_device_session_acquire()\n");
        ret = -1;
        goto fail_out;
    }
#endif

    return ret;

fail_out:

    ni_frame_buffer_free(p2p_frame);
    return ret;
}

/*!****************************************************************************
 *  \brief  Send the Quadra encoder a hardware frame which triggers
 *          Quadra to encode the frame
 *
 *  \param  [in] p_enc_ctx              pointer to encoder context
 *          [in] p_in_frame             pointer to hw frame
 *          [in] input_exhausted        flag indicating this is the last frame
 *          [in/out] need_to_resend     flag indicating need to re-send
 *
 *  \return  0 on success
 *          -1 on failure
 ******************************************************************************/
int encoder_encode_frame(ni_session_context_t *p_enc_ctx,
                         ni_frame_t *p_in_frame, int input_exhausted,
                         int *need_to_resend)
{
    static int started = 0;
    int oneSent;
    ni_session_data_io_t in_data;

    ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "===> encoder_encode_frame <===\n");

    if (enc_eos_sent == 1)
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encoder_encode_frame: ALL data (incl. eos) sent "
                       "already!\n");
        return 0;
    }

    if (*need_to_resend)
    {
        goto send_frame;
    }

    p_in_frame->start_of_stream = 0;

    // If this is the first frame, mark the frame as start-of-stream
    if (!started)
    {
        started = 1;
        p_in_frame->start_of_stream = 1;
    }

    // If this is the last frame, mark the frame as end-of-stream
    p_in_frame->end_of_stream = input_exhausted ? 1 : 0;
    p_in_frame->force_key_frame = 0;

send_frame:

    in_data.data.frame = *p_in_frame;
    oneSent =
        ni_device_session_write(p_enc_ctx, &in_data, NI_DEVICE_TYPE_ENCODER);

    if (oneSent < 0)
    {
        fprintf(stderr,
                "Error: failed ni_device_session_write() for encoder\n");
        *need_to_resend = 1;
        return -1;
    } else if (oneSent == 0 && !p_enc_ctx->ready_to_close)
    {
        *need_to_resend = 1;
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "NEEDED TO RESEND");
    } else
    {
        *need_to_resend = 0;

        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encoder_encode_frame: total sent data size=%u\n",
                       p_in_frame->data_len[3]);

        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encoder_encode_frame: success\n");

        if (p_enc_ctx->ready_to_close)
        {
            enc_eos_sent = 1;
        }
    }

    return 0;
}

/*!****************************************************************************
 *  \brief  Receive output packet data from the Quadra encoder
 *
 *  \param  [in] p_enc_ctx              pointer to encoder session context
 *          [in] p_out_data             pointer to output data session
 *          [in] p_file                 pointer to file to write the packet
 *          [out] total_bytes_received  running counter of bytes read
 *          [in] print_time             1 = print the time
 *
 *  \return 0 - success got packet
 *          1 - received eos
 *          2 - got nothing, need retry
 *         -1 - failure
 ******************************************************************************/
int encoder_receive_data(ni_session_context_t *p_enc_ctx,
                         ni_session_data_io_t *p_out_data, FILE *p_file,
                         unsigned long long *total_bytes_received,
                         int print_time)
{
    int packet_size = NI_MAX_TX_SZ;
    int rc = 0;
    int end_flag = 0;
    int rx_size = 0;
    int meta_size = p_enc_ctx->meta_size;
    ni_packet_t *p_out_pkt = &(p_out_data->data.packet);
    static int received_stream_header = 0;

    ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "===> encoder_receive_data <===\n");

    if (NI_INVALID_SESSION_ID == p_enc_ctx->session_id ||
        NI_INVALID_DEVICE_HANDLE == p_enc_ctx->blk_io_handle)
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encode session not opened yet, return\n");
        return 0;
    }

    if (p_file == NULL)
    {
        ni_log2(p_enc_ctx, NI_LOG_ERROR,  "Bad file pointer, return\n");
        return -1;
    }

    rc = ni_packet_buffer_alloc(p_out_pkt, packet_size);
    if (rc != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: malloc packet failed, ret = %d!\n", rc);
        return -1;
    }

    /*
     * The first data read from the encoder session context
     * is a stream header read.
     */
    if (!received_stream_header)
    {
        /* Read the encoded stream header */
        rc = ni_encoder_session_read_stream_header(p_enc_ctx, p_out_data);

        if (rc > 0)
        {
            /* Write out the stream header */
            if (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
                       p_out_pkt->data_len - meta_size, 1, p_file) != 1)
            {
                fprintf(stderr, "Error: writing data %u bytes error!\n",
                        p_out_pkt->data_len - meta_size);
                fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
            }

            *total_bytes_received += (rx_size - meta_size);
            number_of_packets++;
            received_stream_header = 1;
        } else if (rc != 0)
        {
            fprintf(stderr, "Error: reading header %d\n", rc);
            return -1;
        }

        if (print_time)
        {
            int timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
            if (timeDiff == 0)
            {
                timeDiff = 1;
            }
            printf("[R] Got:%d   Packets= %u fps=%u  Total bytes %llu\n",
                   rx_size, number_of_packets, number_of_packets / timeDiff,
                   *total_bytes_received);
        }

        /* This shouldn't happen */
        if (p_out_pkt->end_of_stream)
        {
            return 1;
        } else if (rc == 0)
        {
            return 2;
        }
    }

receive_data:
    rc = ni_device_session_read(p_enc_ctx, p_out_data, NI_DEVICE_TYPE_ENCODER);

    end_flag = p_out_pkt->end_of_stream;
    rx_size = rc;

    ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encoder_receive_data: received data size=%d\n", rx_size);

    if (rx_size > meta_size)
    {
        if (fwrite((uint8_t *)p_out_pkt->p_data + meta_size,
                   p_out_pkt->data_len - meta_size, 1, p_file) != 1)
        {
            fprintf(stderr, "Error: writing data %u bytes error!\n",
                    p_out_pkt->data_len - meta_size);
            fprintf(stderr, "Error: ferror rc = %d\n", ferror(p_file));
        }

        *total_bytes_received += rx_size - meta_size;
        number_of_packets++;

        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "Got:   Packets= %u\n", number_of_packets);
    } else if (rx_size != 0)
    {
        fprintf(stderr, "Error: received %d bytes, <= metadata size %d!\n",
                rx_size, meta_size);
        return -1;
    } else if (!end_flag &&
               (((ni_xcoder_params_t *)(p_enc_ctx->p_session_config))
                   ->low_delay_mode))
    {
        ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "low delay mode and NO pkt, keep reading...\n");
        goto receive_data;
    }

    if (print_time)
    {
        int timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
        if (timeDiff == 0)
        {
            timeDiff = 1;
        }
        printf("[R] Got:%d   Packets= %u fps=%u  Total bytes %llu\n", rx_size,
               number_of_packets, number_of_packets / timeDiff,
               *total_bytes_received);
    }

    if (end_flag)
    {
        printf("Encoder Receiving done\n");
        return 1;
    } else if (0 == rx_size)
    {
        return 2;
    }

    ni_log2(p_enc_ctx, NI_LOG_DEBUG,  "encoder_receive_data: success\n");

    return 0;
}

/*!****************************************************************************
 *  \brief  Open an encoder session to Quadra
 *
 *  \param  [out] p_enc_ctx         pointer to an encoder session context
 *          [in]  dst_codec_format  AVC or HEVC
 *          [in]  iXcoderGUID       id to identify the Quadra device
 *          [in]  p_enc_params      sets the encoder parameters
 *          [in]  width             width of frames to encode
 *          [in]  height            height of frames to encode
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int encoder_open_session(ni_session_context_t *p_enc_ctx, int dst_codec_format,
                         int iXcoderGUID, ni_xcoder_params_t *p_enc_params,
                         int width, int height, ni_frame_t *p_frame)
{
    int ret = 0;

    // Enable hardware frame encoding
    p_enc_ctx->hw_action = NI_CODEC_HW_ENABLE;
    p_enc_params->hwframes = 1;

    // Provide the first frame to the Quadra encoder
    p_enc_params->p_first_frame = p_frame;

    // Specify codec, AVC vs HEVC
    p_enc_ctx->codec_format = dst_codec_format;

    p_enc_ctx->p_session_config = p_enc_params;
    p_enc_ctx->session_id = NI_INVALID_SESSION_ID;

    // Assign the card GUID in the encoder context to open a session
    // to that specific Quadra device
    p_enc_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;
    p_enc_ctx->hw_id = iXcoderGUID;

    if (g_rgb2yuv_csc)
        p_enc_ctx->pixel_format = NI_PIX_FMT_ABGR;

    ni_encoder_set_input_frame_format(p_enc_ctx, p_enc_params, width, height, 8,
                                      NI_FRAME_LITTLE_ENDIAN, 1);

    // Encoder will operate in P2P mode
    ret = ni_device_session_open(p_enc_ctx, NI_DEVICE_TYPE_ENCODER);
    if (ret != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: encoder open session failure\n");
    } else
    {
        printf("Encoder device %d session open successful\n", iXcoderGUID);
    }

    return ret;
}

/*!****************************************************************************
 *  \brief  Open an upload session to Quadra
 *
 *  \param  [out] p_upl_ctx   pointer to an upload context of the open session
 *          [in]  iXcoderGUID pointer to  Quadra card hw id
 *          [in]  width       width of the frames
 *          [in]  height      height of the frames
 *          [in]  p2p         p2p session
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
int uploader_open_session(ni_session_context_t *p_upl_ctx, int *iXcoderGUID,
                          int width, int height, int p2p)
{
    int ret = 0;
    ni_pix_fmt_t frame_format;

    p_upl_ctx->session_id = NI_INVALID_SESSION_ID;

    // Assign the card GUID in the encoder context
    p_upl_ctx->device_handle = NI_INVALID_DEVICE_HANDLE;
    p_upl_ctx->blk_io_handle = NI_INVALID_DEVICE_HANDLE;

    // Assign the card id to specify the specific Quadra device
    p_upl_ctx->hw_id = *iXcoderGUID;

    // Assign the pixel format we want to use
    frame_format = g_rgb2yuv_csc ? NI_PIX_FMT_ABGR : NI_PIX_FMT_YUV420P;

    // Set the input frame format of the upload session
    ni_uploader_set_frame_format(p_upl_ctx, width, height, frame_format, 1);

    ret = ni_device_session_open(p_upl_ctx, NI_DEVICE_TYPE_UPLOAD);
    if (ret != NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: uploader_open_session failure!\n");
        return ret;
    }
    else
    {
        printf("Uploader device %d session opened successfully\n",
               *iXcoderGUID);
        *iXcoderGUID = p_upl_ctx->hw_id;
    }

    // Create a P2P frame pool for the uploader sesson of pool size 1
    ret = ni_device_session_init_framepool(p_upl_ctx, 1, p2p);
    if (ret < 0)
    {
        fprintf(stderr, "Error: Can't create frame pool\n");
        ni_device_session_close(p_upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
    } else
    {
        printf("Uploader device %d configured successfully\n", *iXcoderGUID);
    }

    return ret;
}

/*!****************************************************************************
 *  \brief    Print usage information
 *
 *  \param    none
 *
 *  \return   none
 ******************************************************************************/
void print_usage(void)
{
    printf("Video encoder/P2P application directly using Netint "
           "Libxcoder release v%s\n"
           "Usage: xcoderp2p_read [options]\n"
           "\n"
           "options:\n"
           "--------------------------------------------------------------------------------\n"
           "  -h | --help        Show help.\n"
           "  -v | --version     Print version info.\n"
           "  -l | --loglevel    Set loglevel of libxcoder API.\n"
           "                     [none, fatal, error, info, debug, trace]\n"
           "                     Default: info\n"
           "  -c | --card        Set card index to use.\n"
           "                     See `ni_rsrc_mon` for cards on system.\n"
           "                     (Default: 0)\n"
           "  -g | --gpucard     Set gpu card index to use.\n"
           "                     See `ni_rsrc_mon` for cards on system.\n"
           "  -i | --input       Input file path.\n"
           "  -r | --repeat      (Positive integer) to Repeat input X times "
           "for performance \n"
           "                     test. (Default: 1)\n"
           "  -s | --size        Resolution of input file in format "
           "WIDTHxHEIGHT.\n"
           "                     (eg. '1920x1080')\n"
           "  -m | --mode        Input to output codec processing mode in "
           "format:\n"
           "                     INTYPE2OUTTYPE. [p2a, p2h, r2a, r2h]\n"
           "                     Type notation: p=P2P, a=AVC, h=HEVC, r=ABGR\n"
           "  -o | --output      Output file path.\n",
           NI_XCODER_REVISION);
}

/*!****************************************************************************
 *  \brief  Parse user command line arguments
 *
 *  \param [in] argc                argument count
 *         [in] argv                argument vector
 *         [out] input_filename     input filename
 *         [out] output_filename    output filename
 *         [out] iXcoderGUID        Quadra device
 *         [out] iGpuGUID           Quadra device (GPU proxy device)
 *         [out] arg_width          resolution width
 *         [out] arg_height         resolution height
 *         [out] dst_codec_format   codec (AVC vs HEVC)
 *
 *  \return nothing                 program exit on error
 ******************************************************************************/
void parse_arguments(int argc, char *argv[], char *input_filename,
                     char *output_filename, int *iXcoderGUID, int *iGpuGUID,
                     int *arg_width, int *arg_height, int *dst_codec_format)
{
    char xcoderGUID[32];
    char gpuGUID[32];
    char mode_description[128];
    char *n;   // used for parsing width and height from --size
    size_t i;
    int opt;
    int opt_index;
    ni_log_level_t log_level;

    static const char *opt_string = "hvl:c:g:i:s:m:o:r:";
    static const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {"loglevel", no_argument, NULL, 'l'},
        {"card", required_argument, NULL, 'c'},
        {"gpucard", required_argument, NULL, 'g'},
        {"input", required_argument, NULL, 'i'},
        {"size", required_argument, NULL, 's'},
        {"mode", required_argument, NULL, 'm'},
        {"output", required_argument, NULL, 'o'},
        {"repeat", required_argument, NULL, 'r'},
        {NULL, 0, NULL, 0},
    };

    while ((opt = getopt_long(argc, argv, opt_string, long_options,
                              &opt_index)) != -1)
    {
        switch (opt)
        {
            case 'h':
                print_usage();
                exit(0);
            case 'v':
                printf("Release ver: %s\n"
                       "API ver:     %s\n"
                       "Date:        %s\n"
                       "ID:          %s\n",
                       NI_XCODER_REVISION, LIBXCODER_API_VERSION,
                       NI_SW_RELEASE_TIME, NI_SW_RELEASE_ID);
                exit(0);
            case 'l':
                log_level = arg_to_ni_log_level(optarg);
                if (log_level != NI_LOG_INVALID)
                {
                    ni_log_set_level(log_level);
                } else {
                    arg_error_exit("-l | --loglevel", optarg);
                }
                break;
            case 'c':
                strcpy(xcoderGUID, optarg);
                *iXcoderGUID = (int)strtol(optarg, &n, 10);
                // No numeric characters found in left side of optarg
                if (n == xcoderGUID)
                    arg_error_exit("-c | --card", optarg);
                break;
            case 'g':
                strcpy(gpuGUID, optarg);
                *iGpuGUID = (int)strtol(optarg, &n, 10);
                if (n == gpuGUID)
                    arg_error_exit("-g | --gpu_card", optarg);
                break;
            case 'i':
                strcpy(input_filename, optarg);
                break;
            case 's':
                *arg_width = (int)strtol(optarg, &n, 10);
                *arg_height = atoi(n + 1);
                if ((*n != 'x') || (!*arg_width || !*arg_height))
                    arg_error_exit("-s | --size", optarg);
                break;
            case 'm':
                if (!(strlen(optarg) == 3))
                    arg_error_exit("-m | --mode", optarg);

                // convert to lower case for processing
                for (i = 0; i < strlen(optarg); i++)
                    optarg[i] = (char)tolower((unsigned char)optarg[i]);

                if (strcmp(optarg, "p2a") != 0 && strcmp(optarg, "p2h") != 0 &&
                    strcmp(optarg, "r2a") != 0 && strcmp(optarg, "r2h") != 0)
                    arg_error_exit("-, | --mode", optarg);

                // determine codec
                sprintf(mode_description, "P2P + Encoding");

                g_rgb2yuv_csc = (optarg[0] == 'r') ? 1 : 0;

                if (optarg[2] == 'a')
                {
                    *dst_codec_format = NI_CODEC_FORMAT_H264;
                    strcat(mode_description, " to AVC");
                }

                if (optarg[2] == 'h')
                {
                    *dst_codec_format = NI_CODEC_FORMAT_H265;
                    strcat(mode_description, " to HEVC");
                }
                printf("%s...\n", mode_description);

                break;
            case 'o':
                strcpy(output_filename, optarg);
                break;
            case 'r':
                if (!(atoi(optarg) >= 1))
                    arg_error_exit("-r | --repeat", optarg);
                g_repeat = atoi(optarg);
                break;
            default:
                print_usage();
                exit(1);
        }
    }

    // Check required args are present
    if (!input_filename[0])
    {
        printf("Error: missing argument for -i | --input\n");
        exit(-1);
    }

    if (!output_filename[0])
    {
        printf("Error: missing argument for -o | --output\n");
        exit(-1);
    }

    // Check that the GPU card and Xcoder card numbers are different.
    // Loopback to the same card not supported.
    if (*iXcoderGUID == *iGpuGUID)
    {
        printf("Error: card and gpucard arguments cannot be the same\n");
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    static char input_filename[FILE_NAME_LEN];
    static char output_filename[FILE_NAME_LEN];
    unsigned long frame_size;
    unsigned long long total_bytes_received;
    int input_video_width;
    int input_video_height;
    int iXcoderGUID = 0;
    int iGpuGUID = 0;
    int arg_width = 0;
    int arg_height = 0;
    int input_exhausted = 0;
    int dst_codec_format = 0;
    int ret=0;
    int timeDiff;
    int print_time;
    int need_to_resend = 0;
    FILE *p_file = NULL;
    ni_xcoder_params_t api_param;
    ni_session_context_t enc_ctx = {0};
    ni_session_context_t upl_ctx = {0};
    ni_session_context_t gpu_ctx = {0};
    ni_frame_t p2p_frame = {0};
    ni_frame_t gpu_frame = {0};
    ni_session_data_io_t out_packet = {0};
    int input_file_fd = -1;
    ni_p2p_sgl_t dma_addrs = {0};

    parse_arguments(argc, argv, input_filename, output_filename, &iXcoderGUID,
                    &iGpuGUID, &arg_width, &arg_height, &dst_codec_format);

    // Get size of input file
    if (get_file_size(input_filename, &total_file_size) < 0)
    {
        exit(-1);
    }

    data_left_size = total_file_size;

    // Create output file
    if (strcmp(output_filename, "null") != 0)
    {
        p_file = fopen(output_filename, "wb");
        if (p_file == NULL)
        {
            fprintf(stderr, "Error: cannot open %s\n", output_filename);
            goto end;
        }
    }

    printf("SUCCESS: Opened output file: %s\n", output_filename);

    if (ni_device_session_context_init(&enc_ctx) < 0)
    {
        fprintf(stderr, "Error: init encoder context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&upl_ctx) < 0)
    {
        fprintf(stderr, "Error: init uploader context error\n");
        return -1;
    }

    if (ni_device_session_context_init(&gpu_ctx) < 0)
    {
        fprintf(stderr, "Error: init gpu uploader context error\n");
        return -1;
    }

    total_bytes_received = 0;
    frame_size = 0;

    send_fin_flag = 0;
    receive_fin_flag = 0;

    printf("User video resolution: %dx%d\n", arg_width, arg_height);

    if (arg_width == 0 || arg_height == 0)
    {
        input_video_width = 1280;
        input_video_height = 720;
    }
    else
    {
        input_video_width = arg_width;
        input_video_height = arg_height;
    }

    ni_gettimeofday(&start_time, NULL);
    ni_gettimeofday(&previous_time, NULL);
    ni_gettimeofday(&current_time, NULL);
    start_timestamp = previous_timestamp = current_timestamp = time(NULL);

    printf("P2P Encoding resolution: %dx%d\n", input_video_width,
           input_video_height);

    // Open a P2P upload session to the destination Quadra device that will
    // be doing the video encoding
    if (uploader_open_session(&upl_ctx, &iXcoderGUID, arg_width, arg_height, 0))
    {
        goto end;
    }

    // Open a P2P upload session for the source Quadra device. The source
    // Quadra device acts as a proxy for the GPU card.
    if (uploader_open_session(&gpu_ctx, &iGpuGUID, arg_width, arg_height, 1))
    {
        goto end;
    }

    // Prepare a frame on the proxy GPU Quadra device
    if (gpu_prepare_frame(&gpu_ctx, input_video_width, input_video_height,
                          &gpu_frame))
    {
        goto end;
    }

#ifdef _WIN32
    input_file_fd = open(input_filename, O_RDONLY | O_BINARY);
#else
    input_file_fd = open(input_filename, O_RDONLY);
#endif

    if (input_file_fd < 0)
    {
        fprintf(stderr, "Error: cannot open input file %s\n", input_filename);
        goto end;
    }

    // Render a frame on the proxy GPU Quadra device
    if (gpu_render_frame(&gpu_ctx, input_file_fd, &g_raw_frame,
                         &gpu_frame, input_video_width, input_video_height,
                         &frame_size, &input_exhausted))
    {
        fprintf(stderr, "Cannot render frame on source Quadra device\n");
        goto end;
    }

    ret = import_dma_buf(&upl_ctx, &gpu_frame, frame_size, &dma_addrs);

    if (ret < 0)
    {
        fprintf(stderr, "Cannot import dma buffer %d\n",ret);
        goto end;
    }

    ret = enc_prepare_frame(&upl_ctx, input_video_width, input_video_height,
                  &p2p_frame);

    if (ret < 0)
    {
        goto end;
    }

    // Configure the encoder parameter structure. We'll use some basic
    // defaults: 30 fps, 200000 bps CBR encoding, AVC or HEVC encoding
    if (ni_encoder_init_default_params(&api_param, 30, 1, 200000, arg_width,
                                       arg_height, enc_ctx.codec_format) < 0)
    {
        fprintf(stderr, "Error: encoder init default set up error\n");
        goto end;
    }

    // For P2P demo, change some of the encoding parameters from
    // the default. Enable low delay encoding.
    if ((ret = ni_encoder_params_set_value(&api_param, "lowDelay", "1")) !=
        NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: can't set low delay mode %d\n", ret);
        goto end;
    }

    // Use a GOP preset of 9 which represents a GOP pattern of
    // IPPPPPPP....This will be low latency encoding.
    if ((ret = ni_encoder_params_set_value(&api_param, "gopPresetIdx", "9")) !=
        NI_RETCODE_SUCCESS)
    {
        fprintf(stderr, "Error: can't set gop preset %d\n", ret);
        goto end;
    }

    if (g_rgb2yuv_csc)
    {
        // Quadra encoder always generates full range YCbCr
        if (ni_encoder_params_set_value(&api_param, "videoFullRangeFlag", "1") !=
            NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "Error: can't set video full range\n");
            goto end;
        }

        // sRGB has the same color primaries as BT.709/IEC-61966-2-1
        if (ni_encoder_params_set_value(&api_param, "colorPri", "1") !=
            NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "Error: can't set color primaries\n");
            goto end;
        }

        // Quadra encoder converts to YUV420 using BT.709 matrix
        if (ni_encoder_params_set_value(&api_param, "colorSpc", "1") !=
            NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "Error: can't set color space\n");
            goto end;
        }

        // sRGB transfer characteristics is IEC-61966-2-1
        if (ni_encoder_params_set_value(&api_param, "colorTrc", "13") !=
            NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "Error: can't set color transfer characteristics\n");
            goto end;
        }
    }

    // Open the encoder session with given parameters
    ret = encoder_open_session(&enc_ctx, dst_codec_format, iXcoderGUID,
                               &api_param, arg_width, arg_height, &p2p_frame);

    if (ret < 0)
    {
        fprintf(stderr, "Could not open encoder session\n");
        goto end;
    }

    while (send_fin_flag == 0 || receive_fin_flag == 0)
    {
        ni_gettimeofday(&current_time, NULL);

        // Print the time if >= 1 second has passed
        print_time = ((current_time.tv_sec - previous_time.tv_sec) > 1);

        // Execute a P2P read into the frame
        if (ni_p2p_recv(&upl_ctx, &dma_addrs, &p2p_frame) != NI_RETCODE_SUCCESS)
        {
            fprintf(stderr, "Error: can't read frame\n");
            break;
        }

        // Encode the frame
        send_fin_flag = encoder_encode_frame(&enc_ctx, &p2p_frame,
                                             input_exhausted, &need_to_resend);

        // Error, exit
        if (send_fin_flag == 2)
        {
            break;
        }

        // Fill the frame buffer with YUV data while the previous frame is being encoded
        if (!input_exhausted && need_to_resend == 0)
        {
            gpu_render_frame(&gpu_ctx, input_file_fd, &g_raw_frame,
                         &gpu_frame, input_video_width, input_video_height,
                         &frame_size, &input_exhausted);
        }

        // Receive encoded packet data from the encoder
        receive_fin_flag = encoder_receive_data(
            &enc_ctx, &out_packet, p_file, &total_bytes_received, print_time);

        if (print_time)
        {
            previous_time = current_time;
        }

        // Error or eos
        if (receive_fin_flag < 0 || out_packet.data.packet.end_of_stream)
        {
            break;
        }
    }

    timeDiff = (int)(current_time.tv_sec - start_time.tv_sec);
    timeDiff = (timeDiff > 0) ? timeDiff : 1;   // avoid division by zero

    printf("[R] Got:  Packets= %u fps=%u  Total bytes %llu\n",
           number_of_packets, number_of_packets / timeDiff,
           total_bytes_received);

    unimport_dma_buf(&upl_ctx, &gpu_frame);

    // Recycle the hardware frames
    recycle_frame(&p2p_frame);
    recycle_frame(&gpu_frame);

    ni_device_session_close(&enc_ctx, 1, NI_DEVICE_TYPE_ENCODER);
    ni_device_session_close(&upl_ctx, 1, NI_DEVICE_TYPE_UPLOAD);
    ni_device_session_close(&gpu_ctx, 1, NI_DEVICE_TYPE_UPLOAD);

    ni_device_session_context_clear(&enc_ctx);
    ni_device_session_context_clear(&upl_ctx);
    ni_device_session_context_clear(&gpu_ctx);

    ni_frame_buffer_free(&p2p_frame);
    ni_frame_buffer_free(&gpu_frame);

    ni_packet_buffer_free(&(out_packet.data.packet));

end:
    if (upl_ctx.session_id != NI_INVALID_SESSION_ID)
        ni_device_session_close(&upl_ctx, 0, NI_DEVICE_TYPE_UPLOAD);

    if (gpu_ctx.session_id != NI_INVALID_SESSION_ID)
        ni_device_session_close(&gpu_ctx, 0, NI_DEVICE_TYPE_UPLOAD);

    if (enc_ctx.session_id != NI_INVALID_SESSION_ID)
        ni_device_session_close(&enc_ctx, 0, NI_DEVICE_TYPE_ENCODER);

    ni_device_close(upl_ctx.blk_io_handle);
    ni_device_close(gpu_ctx.blk_io_handle);
    ni_device_close(enc_ctx.blk_io_handle);

    if (g_raw_frame != NULL)
    {
        free(g_raw_frame);
    }

    close(input_file_fd);

    if (p_file)
    {
        fclose(p_file);
    }

    printf("All done\n");

    return 0;
}
