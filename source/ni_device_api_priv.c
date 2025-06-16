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
 *  \file   ni_device_api_priv.c
 *
 *  \brief  Private definitions used by ni_device_api.c for video processing
 *          tasks
 ******************************************************************************/

#ifdef _WIN32
#include <windows.h>
#elif __linux__ || __APPLE__
#if __linux__
#include <linux/types.h>
#include <malloc.h>
#include <sys/prctl.h>
#endif
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <limits.h>
#include <signal.h>
#include <dirent.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#if !defined(_ANDROID) && !defined(__OPENHARMONY__)
#include <execinfo.h>
#endif
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifdef __linux__
#include <stdint.h>
#include <poll.h>
#include "ni_p2p_ioctl.h"
#endif
#include "inttypes.h"
#include "ni_nvme.h"
#include "ni_device_api.h"
#include "ni_device_api_priv.h"
#include "ni_util.h"
#include "ni_lat_meas.h"
#include "ni_rsrc_priv.h"

//Map the gopPresetIdx to gopSize,
//gopPresetIdx -1: Adaptive Gop, using the biggest gopsize 8
//gopPresetIdx 0 : Custom Gop as defined by -xcoder-gop
//g_map_preset_to_gopsize[lookAheadEnable][gopPresetIdx + 1];
//for the not support gopPresetIdx value, such as gopPresetIdx == 2, set g_map_preset_to_gopsize to -1
static const int g_map_preset_to_gopsize[2][18] = {{8, 0,  1, -1,  1, 2, 4, -1,  4, 8, 1,  4, -1, -1, -1, -1, 16},
                                            {8, 0, -1, -1, -1, 2, 4, -1, -1, 8, 1, -1, -1, -1, -1, -1, -1}};
#define MAGIC_P2P_VALUE "p2p"
#define AI_MODEL_TYPE_HVSPLUS_FILTER 3

typedef enum _ni_t35_sei_mesg_type
{
    NI_T35_SEI_CLOSED_CAPTION = 0,
    NI_T35_SEI_HDR10_PLUS = 1
} ni_t35_sei_mesg_type_t;

typedef enum
{
    OPT_1 = 1, // is a combination of OPT_3 and OPT_2 in that order
    OPT_2 = 2, // returns NI_RETCODE_FAILURE after NI_XCODER_FAILURES_MAX attempts, and LRETURNs for failures
    OPT_3 = 3  // queries session statistics
} check_err_rc_option_t;

static uint8_t g_itu_t_t35_cc_sei_hdr_hevc[NI_CC_SEI_HDR_HEVC_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x4e,
    0x01,   // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
    // nuh_layer_id=0 nuh_temporal_id_plus1=1)
    0x04,     // payloadType= 4 (user_data_registered_itu_t_t35)
    0 + 11,   // payLoadSize= ui16Len + 11; to be set (index 7)
    0xb5,     //  itu_t_t35_country_code =181 (North America)
    0x00,
    0x31,   //  itu_t_t35_provider_code = 49
    0x47, 0x41, 0x39,
    0x34,       // ATSC_user_identifier = "GA94"
    0x03,       // ATSC1_data_user_data_type_code=3
    0 | 0xc0,   // (ui16Len/3) | 0xc0 (to be set; index 16) (each CC character
    //is 3 bytes)
    0xFF   // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_hevc[NI_HDR10P_SEI_HDR_HEVC_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x4e,
    0x01,   // nal_unit_header() {forbidden bit=0 nal_unit_type=39,
    // nuh_layer_id=0 nuh_temporal_id_plus1=1)
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0x00,   // payLoadSize; to be set (index 7)
    0xb5,   //  u8 itu_t_t35_country_code =181 (North America)
    //0x00,
    //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
    //0x00,
    //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
    // payLoadSize count starts from itu_t_t35_provider_code and goes until
    // and including trailer
};

static uint8_t g_itu_t_t35_cc_sei_hdr_h264[NI_CC_SEI_HDR_H264_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x06,   // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0 + 11,   // payLoadSize= ui16Len + 11; to be set (index 6)
    0xb5,     //  itu_t_t35_country_code =181 (North America)
    0x00,
    0x31,   //  itu_t_t35_provider_code = 49
    0x47, 0x41, 0x39,
    0x34,       // ATSC_user_identifier = "GA94"
    0x03,       // ATSC1_data_user_data_type_code=3
    0 | 0xc0,   // (ui16Len/3) | 0xc0 (to be set; index 15) (each CC character
    //is 3 bytes)
    0xFF   // em_data = 255
};

static uint8_t g_itu_t_t35_hdr10p_sei_hdr_h264[NI_HDR10P_SEI_HDR_H264_LEN] = {
    0x00, 0x00, 0x00, 0x01,   // NAL start code 00 00 00 01
    0x06,   // nal_unit_header() {forbidden bit=0 nal_ref_idc=0, nal_unit_type=6
    0x04,   // payloadType= 4 (user_data_registered_itu_t_t35)
    0x00,   // payLoadSize; to be set (index 6)
    0xb5,   //  itu_t_t35_country_code =181 (North America)
    //0x00,
    //0x3c, //  u16 itu_t_t35_provider_code = 0x003c
    //0x00,
    //0x01, //  u16 itu_t_t35_provider_oriented_code = 0x0001
    // payLoadSize count starts from itu_t_t35_provider_code and goes until
    // and including trailer
};

static uint8_t g_sei_trailer[NI_CC_SEI_TRAILER_LEN] = {
    0xFF,   // marker_bits = 255
    0x80   // RBSP trailing bits - rbsp_stop_one_bit and 7 rbsp_alignment_zero_bit
};

#define NI_XCODER_FAILURES_MAX 25
#define DP_IPC_PASSTHRU 0xFFFFFFFF

#ifdef _WIN32
#ifdef XCODER_SELF_KILL_ERR
#undef XCODER_SELF_KILL_ERR
#endif
#endif

void SwapSWBytes(uint8_t *buf, uint32_t bytes)
{
  uint32_t i;
  for (i = 0; i < bytes; i += 2)
  {
    uint8_t val = buf[i];
    buf[i]            = buf[i + 1];
    buf[i + 1]        = val;
  }
}

void SwapSW16(uint16_t *buf, uint32_t bytes)
{
  uint32_t i, words = (bytes + 1) / 2;

  for (i = 0; i < words; i += 2)
  {
    uint16_t val = buf[i];
    buf[i]       = buf[i + 1];
    buf[i + 1]   = val;
  }
}

void SwapSW32(uint32_t *buf, uint32_t bytes)
{
  uint32_t i, words = (bytes + 3) / 4;
  for (i = 0; i < words; i += 2)
  {
    uint32_t val = buf[i];
    buf[i]       = buf[i + 1];
    buf[i + 1]   = val;
  }
}

void SwapSW64(uint64_t *buf, uint64_t bytes)
{
  uint64_t i, words = (bytes + 7) / 8;
  for (i = 0; i < words; i += 2)
  {
    uint64_t val = buf[i];
    buf[i]       = buf[i + 1];
    buf[i + 1]   = val;
  }
}

static void memcpyToPelByBits (uint8_t *dst_u8, uint8_t *src, int width, int height, int dst_stride, int src_stride, int srcValidBitsDepth)
{
  int i, j;
  uint8_t bitsDepth = srcValidBitsDepth;
  uint32_t mask = (1<<bitsDepth)-1;
  uint16_t *dst = (uint16_t *)dst_u8;

  //fast for byte write
  if (bitsDepth == 8)
  {
    for (j = 0; j < height; j ++)
    {
      for (i = 0; i< width; i ++)
        dst_u8[i] = src[i] & 0xff;

      src += src_stride;
      dst_u8 += dst_stride;
    }
    return;
  }

  //for arbitrary bits
  for (j = 0; j < height; j ++)
  {
    uint8_t *src_line = src;
    uint32_t cache = 0, cache_bits = 0;
    for (i = 0; i< width; i ++)
    {
      while(cache_bits < bitsDepth)
      {
        cache |= ((*src_line++) << cache_bits);
        cache_bits += 8;
      }

      if (cache_bits >= bitsDepth)
      {
        dst[i] = cache & mask;
        cache >>= bitsDepth;
        cache_bits -= bitsDepth;
      }
    }

    src += src_stride;
    dst += dst_stride;
  }
}

/*
static void write_(FILE *fp, uint8_t *data, int32_t width, int32_t height, int32_t stripe)
{
  int32_t i;

  for (i = 0; i < height; i++)
  {
    if (fwrite(data, sizeof(uint8_t ), width, fp) < (size_t)width)
    {
      return;
    }
    data += stripe;
  }
}
*/

static void trace_recon_tile2raster_y(FILE *fRecon, uint8_t *mem, int width, int height,
                                    int leftOffset, int topOffset,
                                    int src_stride, int pixDepth, uint8_t *luma_mem_temp)
{
  int i;
  int write_data_len = 0;
  int bytesPerPix = (pixDepth == 8 ? 1 : 2);
  uint8_t *tmp_mem = (uint8_t *)malloc(bytesPerPix*src_stride);
  int num_tiles = (leftOffset + width + 3) / 4 - leftOffset/4;
  if(tmp_mem == NULL)
    return;
  mem += topOffset/4*4*src_stride + leftOffset/4*16*pixDepth/8;
  for(i = topOffset/4*4; i < topOffset + height; i += 4) {
    memcpyToPelByBits(tmp_mem, mem, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
    if(i >= topOffset && i < topOffset + height){
      // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
      memcpy(luma_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
      write_data_len += width * bytesPerPix;
    }
    memcpyToPelByBits(tmp_mem, mem+4*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
    if(i+1 >= topOffset && i+1 < topOffset + height)
    {
      // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
      memcpy(luma_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
      write_data_len += width * bytesPerPix;
    }
    memcpyToPelByBits(tmp_mem, mem+8*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
    if(i+2 >= topOffset && i+2 < topOffset + height)
    {
      // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
      memcpy(luma_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
      write_data_len += width * bytesPerPix;
    }
    memcpyToPelByBits(tmp_mem, mem+12*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
    if(i+3 >= topOffset && i+3 < topOffset + height)
    {
      // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
      memcpy(luma_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
      write_data_len += width * bytesPerPix;
    }
    mem += 4*src_stride;
  }
  free(tmp_mem);
}

static void trace_recon_tile2raster_uv(FILE *fRecon, uint8_t *mem_uv, int width, int height,
                                    int leftOffset, int topOffset,
                                    int src_stride, int pixDepth, uint8_t *ch_mem_temp)
{
  int i, j, k;
  int bytesPerPix = (pixDepth == 8 ? 1 : 2);
  uint8_t *tmp_mem = (uint8_t *)malloc(bytesPerPix*src_stride*2);
  int num_tiles = 2*((leftOffset + width + 3) / 4 - leftOffset/4);
  if(tmp_mem == NULL)
    return;
  uint8_t *mem;
  int uv;
  int write_data_len = 0;
  memset(tmp_mem, 0x00, bytesPerPix * src_stride * 2);
  for(uv = 0; uv <= 1; uv ++) {
    mem = mem_uv + topOffset/4*8*src_stride + leftOffset/4*32*pixDepth/8;
    for(i = topOffset/4*4; i < topOffset + height; i += 4) {
      if(i >= topOffset && i < topOffset + height) {
        memcpyToPelByBits(tmp_mem, mem, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
        for(j = uv, k = 0; j < 4 * num_tiles; j += 2, k++) {
          if(bytesPerPix == 1)
            tmp_mem[k] = tmp_mem[j];
          else
            ((uint16_t*)tmp_mem)[k] = ((uint16_t*)tmp_mem)[j];
        }
        // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
        memcpy(ch_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
        write_data_len += width * bytesPerPix;
      }
      if(i+1 >= topOffset && i+1 < topOffset + height) {
        memcpyToPelByBits(tmp_mem, mem+4*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
        for(j = uv, k = 0; j < 4 * num_tiles; j += 2, k++) {
          if(bytesPerPix == 1)
            tmp_mem[k] = tmp_mem[j];
          else
            ((uint16_t*)tmp_mem)[k] = ((uint16_t*)tmp_mem)[j];
        }
        // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
        memcpy(ch_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
        write_data_len += width * bytesPerPix;
      }
      if(i+2 >= topOffset && i+2 < topOffset + height) {
        memcpyToPelByBits(tmp_mem, mem+8*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
        for(j = uv, k = 0; j < 4 * num_tiles; j += 2, k++) {
          if(bytesPerPix == 1)
            tmp_mem[k] = tmp_mem[j];
          else
            ((uint16_t*)tmp_mem)[k] = ((uint16_t*)tmp_mem)[j];
        }
        // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
        memcpy(ch_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
        write_data_len += width * bytesPerPix;
      }
      memcpyToPelByBits(tmp_mem, mem+12*pixDepth/8, 4, num_tiles, 4, 16*pixDepth/8, pixDepth);
      if(i+3 >= topOffset && i+3 < topOffset + height) {
        for(j = uv, k = 0; j < 4 * num_tiles; j += 2, k++) {
          if(bytesPerPix == 1)
            tmp_mem[k] = tmp_mem[j];
          else
            ((uint16_t*)tmp_mem)[k] = ((uint16_t*)tmp_mem)[j];
        }
        // write_(fRecon, tmp_mem + (leftOffset & 3)*bytesPerPix, width*bytesPerPix, 1, src_stride);
        memcpy(ch_mem_temp + write_data_len, tmp_mem + (leftOffset & 3) * bytesPerPix, width * bytesPerPix);
        write_data_len += width * bytesPerPix;
      }
      mem += 8*src_stride;
    }
  }
  free(tmp_mem);
}

static double calc_noise(uint8_t *p1, uint8_t *p2, int size)
{
  double sum;
  double diff;
  int i;

  sum = 0;
  for (i = 0; i < size; i++)
  {
    diff = p1[i] - p2[i];
    sum += diff * diff;
  }

  return sum;
}

/*!*****************************************************************************
 *  \brief  Download hw frames by HwDesc.
 *
 *  \param
 *
 *  \return 0 if successful, < 0 otherwise
 ******************************************************************************/
static int hwdl_frame(ni_session_context_t *p_ctx,
               ni_session_data_io_t *p_session_data, ni_frame_t *p_src_frame,
               int pixel_format)
{
    niFrameSurface1_t *src_surf = (niFrameSurface1_t *)(p_src_frame->p_data[3]);
    int ret = 0;

    ret = ni_frame_buffer_alloc_dl(&(p_session_data->data.frame),
                                   src_surf->ui16width, src_surf->ui16height,
                                   pixel_format);

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

void calculate_psnr(ni_session_context_t *p_ctx, ni_packet_t *p_packet)
{
    // char dump_file_tile[256];
    // char dump_file_raster[256];
    //FILE *fout_tile = NULL;
    FILE *fout_raster = NULL;
    // snprintf(dump_file_tile, sizeof(dump_file_tile), "output-%04ld-tile.yuv", (long)p_ctx->pkt_num - 1);
    // snprintf(dump_file_raster, sizeof(dump_file_raster), "output-%04ld-raster.yuv", (long)p_ctx->pkt_num - 1);
    // fout_tile   = fopen(dump_file_tile, "wb");
    // fout_raster = fopen(dump_file_raster, "wb");
    // FILE *f_out = fopen("out.yuv", "a");
    int bit_depth = (p_ctx->pixel_format == NI_PIX_FMT_YUV420P10LE || p_ctx->pixel_format == NI_PIX_FMT_P010LE) ? 10 : 8;

    ni_metadata_enc_bstream_t *p_meta = (ni_metadata_enc_bstream_t *)p_packet->p_data;

    // only h.265 psnr_y is supported by HW
    p_ctx->psnr_y = p_packet->psnr_y = (float)(p_meta->ui16psnr_y / 1000.0);

    if (((ni_xcoder_params_t *)(p_ctx->p_session_config))->cfg_enc_params.get_psnr_mode != 3 &&
        (p_meta->reconLumaSize != 0 || p_meta->reconChromaSize != 0))
    {
      uint32_t lum_sz = 0;
      uint32_t chr_sz = 0;
      uint8_t *luma_mem_temp = NULL;
      uint8_t *ch_mem_temp   = NULL;
      int width  = p_ctx->actual_video_width;
      int height = p_ctx->active_video_height;//p_meta->reconLumaSize / p_meta->reconLumaWidth;

      uint8_t *luma_mem = (uint8_t *)p_packet->p_data + p_packet->data_len - p_meta->reconChromaSize - p_meta->reconLumaSize;
      uint8_t *ch_mem   = (uint8_t *)p_packet->p_data + p_packet->data_len - p_meta->reconChromaSize;

      int bit_scale = (p_ctx->pixel_format == NI_PIX_FMT_YUV420P10LE || p_ctx->pixel_format == NI_PIX_FMT_P010LE) ? 5 : 4;
      lum_sz = (2 * p_meta->frameCropTopOffset + height + 3) / 4 * p_meta->reconLumaWidth * bit_scale;
      chr_sz = (p_meta->frameCropTopOffset + height / 2 + 1) / 2 * p_meta->reconChromaWidth * bit_scale;
      if (p_meta->reconLumaSize)
      {
        uint32_t luma_size = (bit_depth == 8) ? p_meta->reconLumaSize : p_meta->reconLumaSize * 2;
        luma_mem_temp = (uint8_t *)malloc(luma_size);
        memset(luma_mem_temp, 0x00, luma_size);
      }
      if (p_meta->reconChromaSize)
      {
        uint32_t ch_size = (bit_depth == 8) ? p_meta->reconChromaSize : p_meta->reconChromaSize * 2;
        ch_mem_temp   = (uint8_t *)malloc(ch_size);
        memset(ch_mem_temp,   0x00, ch_size);
      }
      ////////////////////////////
      /////////Tile2Raster////////
      ////////////////////////////
      if (p_meta->reconLumaSize != 0)
      {
        SwapSWBytes(luma_mem, lum_sz);
        SwapSW16((uint16_t *)luma_mem, lum_sz);
        SwapSW32((uint32_t *)luma_mem, lum_sz);
        SwapSW64((uint64_t *)luma_mem, lum_sz);
        trace_recon_tile2raster_y (fout_raster, luma_mem,  width,     height,
          2 * p_meta->frameCropLeftOffset, 2 * p_meta->frameCropTopOffset,
          p_meta->reconLumaWidth, bit_depth, luma_mem_temp);
        ni_log2(p_ctx, NI_LOG_DEBUG, "lum_sz %d reconLumaSize %d width %d reconLumaWidth %d\n",
            lum_sz, p_meta->reconLumaSize, width, p_meta->reconLumaWidth);
        // fflush(fout_raster);
      }

      if (p_meta->reconChromaSize != 0)
      {
        SwapSWBytes(ch_mem, chr_sz);
        SwapSW16((uint16_t *)ch_mem, chr_sz);
        SwapSW32((uint32_t *)ch_mem, chr_sz);
        SwapSW64((uint64_t *)ch_mem, chr_sz);
        trace_recon_tile2raster_uv(fout_raster, ch_mem,    width / 2, height / 2,
          p_meta->frameCropLeftOffset, p_meta->frameCropTopOffset,
          p_meta->reconChromaWidth, bit_depth, ch_mem_temp);
        ni_log2(p_ctx, NI_LOG_DEBUG, "chr_sz %d reconChromaSize %d reconChromaWidth %d\n",
          chr_sz, p_meta->reconChromaSize, p_meta->reconChromaWidth);
        // fflush(fout_raster);
      }

      // fwrite((uint8_t *)p_packet->p_data + sizeof(ni_metadata_enc_bstream_t) + (p_packet->data_len - p_meta->reconLumaSize - p_meta->reconChromaSize),
      // p_meta->reconLumaSize + p_meta->reconChromaSize, 1, fout_tile);

      ////////////////////////////
      ///////calculate PSNR///////
      ////////////////////////////
      if (((ni_xcoder_params_t *)(p_ctx->p_session_config))->cfg_enc_params.get_psnr_mode < 3)
      {
        do
        {
            int max;
            double noise_y = 0.0, noise_u = 0.0, noise_v = 0.0, noise_yuv = 0.0;
            double power_y = 0.0, power_u = 0.0, power_v = 0.0, power_yuv = 0.0;
            double psnr_y  = 0.0, psnr_u  = 0.0, psnr_v  = 0.0, psnr_yuv  = 0.0;

            /// For debug, read raster data from file
            // FILE *f_out_raster = (FILE *)fopen(dump_file_raster, "rb");
            // fread(luma_mem_temp, width * height ,    1, f_out_raster);
            // fread(ch_mem_temp,   width * height / 2, 1, f_out_raster);
            // fflush(f_out_raster);
            // fclose(f_out_raster);

            max = (1 << 8) - 1;
            power_y = 1.0 * max * max * width * height;
            power_u = power_y / 4.0;
            power_v = power_y / 4.0;
            power_yuv = power_y * 3.0 / 2.0;

            uint8_t *p_y_src_buf = NULL;
            uint8_t *p_u_src_buf = NULL;
            uint8_t *p_v_src_buf = NULL;
            for (int i = 0; i < 120; i++)
            {
              if (p_ctx->input_frame_fifo[i].p_input_buffer != NULL &&
                  p_ctx->input_frame_fifo[i].pts == p_meta->frame_tstamp)
              {
                p_y_src_buf = p_ctx->input_frame_fifo[i].p_input_buffer;
                p_u_src_buf = p_y_src_buf + p_ctx->input_frame_fifo[i].video_width * p_ctx->input_frame_fifo[i].video_height;
                p_v_src_buf = p_u_src_buf + (p_ctx->input_frame_fifo[i].video_width / 2) * (p_ctx->input_frame_fifo[i].video_height / 2);

                p_ctx->input_frame_fifo[i].usable = 1;
                ni_log2(p_ctx, NI_LOG_DEBUG, "%s %d i %d pkt_num %ld frame_num %ld frame_tstamp %lld\n",
                    __FUNCTION__, __LINE__, i, p_ctx->pkt_num, p_ctx->frame_num, p_meta->frame_tstamp);
                break;
              }
            }
            if (p_y_src_buf != NULL)
            {
              if (p_meta->reconLumaSize != 0)
              {
                noise_y = calc_noise(p_y_src_buf, luma_mem_temp, width * height);
              }
              else if (p_meta->ui16psnr_y != 0)
              {
                noise_y = power_y / pow(10, p_ctx->psnr_y / 10); // This is used for calculation noise_yuv for psnr_yuv
              }

              if (p_meta->reconChromaSize != 0)
              {
                noise_u = calc_noise(p_u_src_buf, ch_mem_temp,   (width / 2) * (height / 2));
                noise_v = calc_noise(p_v_src_buf, ch_mem_temp + (width / 2) * (height / 2),   (width / 2) * (height / 2));
              }
              noise_yuv = noise_y + noise_u + noise_v;

              //This is for the case when encoding some very simple picture
              //this case the quality is very good. sometimes the noise_y may be zero
              noise_y = noise_y < 1 ? 1 : noise_y;
              noise_u = noise_u < 1 ? 1 : noise_u;
              noise_v = noise_v < 1 ? 1 : noise_v;
              noise_yuv = noise_yuv < 1 ? 1 : noise_yuv;

              if (p_meta->reconLumaSize != 0)
              {
                psnr_y   = 10 * log10(power_y / noise_y);
              }
              else if (p_meta->ui16psnr_y != 0)
              {
                psnr_y = p_ctx->psnr_y;
              }

              if (p_meta->reconChromaSize != 0)
              {
                psnr_u   = 10 * log10(power_u / noise_u);
                psnr_v   = 10 * log10(power_v / noise_v);
              }
              psnr_yuv = 10 * log10(power_yuv / noise_yuv);
              p_ctx->psnr_y = p_packet->psnr_y = psnr_y;
              p_ctx->psnr_u = p_packet->psnr_u = psnr_u;
              p_ctx->psnr_v = p_packet->psnr_v = psnr_v;
              p_ctx->average_psnr = p_packet->average_psnr = psnr_yuv;
            }
        } while(0);
      }
      else
      {
        // get recontructed frame here
        // int h = 0;
        // luma
        // for (h = 0; h < height; h++)
        // {
        //   fwrite(luma_mem_temp + (width * h), 1, width, f_out);
        // }
        //chroma
        // for (h = 0; h < height; h++)
        // {
        //   fwrite(ch_mem_temp + ((width / 2) * h), 1, width / 2, f_out);
        // }
        // fclose(f_out);
      }

      if (luma_mem_temp)
      {
          free(luma_mem_temp);
          luma_mem_temp = NULL;
      }
      if (ch_mem_temp)
      {
          free(ch_mem_temp);
          ch_mem_temp = NULL;
      }
      // fflush(fout_tile);
      // fclose(fout_tile);
      // fflush(fout_raster);
      // fclose(fout_raster);
    //}
    }

    if(p_meta->reconChromaSize == 0)
    {
      p_ctx->psnr_u = p_ctx->psnr_v = 0;
      p_ctx->average_psnr = p_ctx->psnr_y;
    }
    if (p_meta->reconLumaSize == 0 && p_meta->ui16psnr_y == 0)
    {
      p_ctx->psnr_y = 0;
      p_ctx->average_psnr = (p_ctx->psnr_u + p_ctx->psnr_v) / 2;
    }


    ni_log2(p_ctx, NI_LOG_DEBUG, "pkt_num %d psnr_y %.4f psnr_u %.4f psnr_v %.4f psnr_yuv %.4f reconLumaWidth %d width %d height %d reconLumaSize 0x%x reconChromaSize 0x%x\n",
            p_ctx->pkt_num, p_ctx->psnr_y, p_ctx->psnr_u, p_ctx->psnr_v, p_ctx->average_psnr, p_meta->reconLumaWidth,p_ctx->actual_video_width, p_ctx->active_video_height, p_meta->reconLumaSize, p_meta->reconChromaSize);

    p_packet->data_len -= p_meta->reconLumaSize + p_meta->reconChromaSize;
}

// Check for critical failures.
// Invalid parameters or resource busy do not account for failures that cause error count to be incremented so they can be retried indefinitely
static ni_retcode_t check_err_rc(
    ni_session_context_t* ctx, ni_retcode_t rc, ni_session_statistic_t *stat_full, int opcode, uint32_t type, int hw_id, uint32_t *inst_id, int opt, const char* func, int line)
{
    ni_retcode_t retval = rc;
    ni_session_stats_t stat = {0};
    uint16_t ui16ErrorCount = 0;
    uint32_t ui32LastErrorStatus = 0;

    if(opt == OPT_1 || opt == OPT_3)
    {
      retval = ni_query_session_stats(ctx, type, &stat, rc, opcode);
      ui16ErrorCount = stat.ui16ErrorCount;
      ui32LastErrorStatus = stat.ui32LastErrorStatus;
    }

    if (retval != NI_RETCODE_SUCCESS)
    {
      ni_log2(ctx, NI_LOG_ERROR, "Query for statistics failed with %d!\n", retval);
      return NI_RETCODE_FAILURE;
    }

    if(opt == OPT_1)
    {
      retval = ni_nvme_check_error_code(stat.ui32LastTransactionCompletionStatus, opcode, type, hw_id, inst_id);

      if(!retval)
        retval = ni_nvme_check_error_code(ui32LastErrorStatus, opcode, type, hw_id, inst_id);
    }
    else if(opt == OPT_2){
      if(!stat_full){
        ni_log2(ctx, NI_LOG_ERROR, "ERROR %s(): passed parameters are null!, return\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
      }
      retval = ni_nvme_check_error_code(stat_full->ui32LastTransactionCompletionStatus, opcode, type, hw_id, inst_id);
      ui16ErrorCount = stat_full->ui16ErrorCount;
      ui32LastErrorStatus = stat_full->ui32LastErrorStatus;
      /* re-query error status for transactionId 0xFFFF */
      if(stat_full->ui32LastErrorTransactionId == 0xFFFF &&
         ui16ErrorCount > 0 && !retval &&
         type == NI_DEVICE_TYPE_DECODER)
      {
        retval = ni_nvme_check_error_code(ui32LastErrorStatus, opcode, type, hw_id, inst_id);
      }
    }
    else if(opt == OPT_3)
    {
      retval = ni_nvme_check_error_code(stat.ui32LastTransactionCompletionStatus, opcode, type, hw_id, inst_id);
    }

    if (retval)
    {
      ni_log2(ctx,
              NI_LOG_ERROR,
              "Persistent failures detected, %s() line-%d: session_no 0x%x "
              "sess_err_count %u inst_err_no %u\n",
              func, line, *(inst_id), ui16ErrorCount,
              ui32LastErrorStatus);
#ifdef XCODER_SELF_KILL_ERR
      if(opt != OPT_3)
      {
          kill(getpid(), SIGTERM);
      }
#endif
    }

    return retval;
}

//Following macros will only check for critical failures.
//After the macro runs, rc should contain the status of the last command sent
//For non-critical failures, it is assumed in the code that eventually after enough retries, a command will succeed.
//Invalid parameters or resource busy do not account for failures that cause error count to be incremented so they can be retried indefinitely
#define CHECK_ERR_RC(ctx, rc, info, opcode, type, hw_id, inst_id, opt)         \
    {                                                                          \
        (rc) = check_err_rc(ctx, rc, info, opcode, type, hw_id, inst_id, opt, __func__, __LINE__); \
        if((rc) && ((opt) != OPT_3)) LRETURN;                                  \
    }

#define CHECK_VPU_RECOVERY(ret)                                                \
    {                                                                          \
        if (NI_RETCODE_NVME_SC_VPU_RECOVERY == (ret))                          \
        {                                                                      \
            ni_log(NI_LOG_ERROR, "Error, vpu reset.\n");                       \
            (ret) = NI_RETCODE_ERROR_VPU_RECOVERY;                             \
            LRETURN;                                                           \
        }                                                                      \
    }

// ctx->decoder_low_delay is used as condition wait timeout for both decoder
// and encoder send/recv multi-thread in low delay mode.
static void low_delay_wait(ni_session_context_t* p_ctx)
{
  const char *name = p_ctx->device_type == NI_DEVICE_TYPE_DECODER ? \
                     "decoder" : "encoder";
  if (p_ctx->async_mode && p_ctx->decoder_low_delay > 0)
  {
    int ret;
    uint64_t abs_time_ns;
    struct timespec ts;

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s waiting for %s recv thread\n", __FUNCTION__, name);

    abs_time_ns = ni_gettime_ns();
    abs_time_ns += p_ctx->decoder_low_delay * 1000000LL;
    ts.tv_sec = abs_time_ns / 1000000000LL;
    ts.tv_nsec = abs_time_ns % 1000000000LL;

    ni_pthread_mutex_lock(&p_ctx->low_delay_sync_mutex);
    while (p_ctx->low_delay_sync_flag)
    {
      // In case of dead lock on waiting for notification from recv thread.
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ret = ni_pthread_cond_timedwait(&p_ctx->low_delay_sync_cond,
                                      &p_ctx->low_delay_sync_mutex, &ts);
      ni_pthread_mutex_lock(&p_ctx->mutex);
      if (ret == ETIMEDOUT)
      {
        p_ctx->low_delay_sync_flag = 0;
      }
    }
    ni_pthread_mutex_unlock(&p_ctx->low_delay_sync_mutex);
  }
}

static void low_delay_signal(ni_session_context_t* p_ctx)
{
  const char *name = p_ctx->device_type == NI_DEVICE_TYPE_DECODER ? \
                     "decoder" : "encoder";
  if (p_ctx->async_mode && p_ctx->decoder_low_delay > 0)
  {
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: wake up %s send thread\n", __FUNCTION__, name);
    ni_pthread_mutex_lock(&p_ctx->low_delay_sync_mutex);
    p_ctx->low_delay_sync_flag = 0;
    ni_pthread_cond_signal(&p_ctx->low_delay_sync_cond);
    ni_pthread_mutex_unlock(&p_ctx->low_delay_sync_mutex);
  }
}

static void query_sleep(ni_session_context_t* p_ctx)
{
  if (p_ctx->async_mode)
  {
    // To avoid IO spam on NP core from queries and high volumens on latency.
    ni_pthread_mutex_unlock(&p_ctx->mutex);
    ni_usleep(NI_RETRY_INTERVAL_100US);
    ni_pthread_mutex_lock(&p_ctx->mutex);
  }
}

// create folder bearing the card name (nvmeX) if not existing
// start working inside this folder: nvmeX
// find the earliest saved and/or non-existing stream folder and use it as
// the pkt saving destination; at most 128 such folders to be checked/created;
// folder name is in the format of: streamY, where Y is [1, 128]
static void decoder_dump_dir_open(ni_session_context_t *p_ctx)
{
#ifdef _WIN32
#elif __linux__ || __APPLE__
    FILE *fp;
    char dir_name[128] = {0};
    char file_name[512] = {0};
    ni_device_context_t *p_device_context;
    DIR *dir;
    struct dirent *stream_folder;
    int curr_stream_idx = 0;
    int earliest_stream_idx = 0;
    int max_exist_idx = 0;
    time_t earliest_time = 0;
    struct stat file_stat;

    p_device_context =
        ni_rsrc_get_device_context(p_ctx->device_type, p_ctx->hw_id);
    if (!p_device_context)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error retrieve device context for decoder guid %d\n",
                       p_ctx->hw_id);
        return;
    }

    flock(p_device_context->lock, LOCK_EX);

    strcpy(dir_name, &p_ctx->dev_xcoder_name[5]);
    if (0 != access(dir_name, F_OK))
    {
        if (0 != mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "Error create folder %s, errno %d\n", dir_name,
                           NI_ERRNO);
        } else
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Created pkt folder for: %s\n", dir_name);
        }
    }

    if (NULL == (dir = opendir(dir_name)))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error %d: failed to open directory %s\n",
                       NI_ERRNO, dir_name);
    } else
    {
        // have a quick first pass of streamY folders, and if existing Y < 128,
        // create a new folder Y+1 directly without checking existing ones'
        // content
        while ((stream_folder = readdir(dir)))
        {
            if (!strncmp(stream_folder->d_name, "stream", strlen("stream")))
            {
                curr_stream_idx =
                    atoi(&(stream_folder->d_name[strlen("stream")]));
                if (curr_stream_idx > 0)
                {
                    if (curr_stream_idx > max_exist_idx)
                    {
                        max_exist_idx = curr_stream_idx;
                    }
                    if (NI_MAX_CONTEXTS_PER_HW_INSTANCE == curr_stream_idx)
                    {
                        break;
                    }
                }
            }
        }

        // if less than 128 streams created then create a new one, otherwise have
        // to pick the stream folder that has the earliest modified file which
        // is most likely done by finished session.
        if (max_exist_idx < NI_MAX_CONTEXTS_PER_HW_INSTANCE)
        {
            curr_stream_idx = max_exist_idx + 1;
        } else
        {
            rewinddir(dir);
            while ((stream_folder = readdir(dir)))
            {
                // go through each of these streamY folders and get modified
                // time of the first pkt-* file to simplify the searching
                if (!strncmp(stream_folder->d_name, "stream", strlen("stream")))
                {
                    snprintf(file_name, sizeof(file_name), "%s/%s/pkt-0001.bin",
                             dir_name, stream_folder->d_name);

                    curr_stream_idx =
                        atoi(&(stream_folder->d_name[strlen("stream")]));

                    if (curr_stream_idx > 0 && 0 == access(file_name, F_OK))
                    {
                        // just take pkt-0001 file timestamp to simplify search
                        if (stat(file_name, &file_stat))
                        {
                            ni_log2(p_ctx, NI_LOG_ERROR,  "Error %d: failed to stat file %s\n",
                                           NI_ERRNO,
                                           file_name);
                        } else
                        {
                            if (0 == earliest_stream_idx ||
                                file_stat.st_mtime < earliest_time)
                            {
                                earliest_stream_idx = curr_stream_idx;
                                earliest_time = file_stat.st_mtime;
                            }
                        }
                    }   // check first file in streamX
                }       // go through each streamX folder
            }           // read all files in nvmeY

            curr_stream_idx = earliest_stream_idx;

            // set the access/modified time of chosen pkt file to NOW so its
            // stream folder won't be taken by other sessions.
            snprintf(file_name, sizeof(file_name), "%s/stream%03d/pkt-0001.bin",
                     dir_name, curr_stream_idx);
            if (utime(file_name, NULL))
            {
                ni_log2(p_ctx, NI_LOG_ERROR,  "Error utime %s\n", file_name);
            }
        }   // 128 streams in nvmeY already
        closedir(dir);
    }

    snprintf(p_ctx->stream_dir_name, sizeof(p_ctx->stream_dir_name),
             "%s/stream%03d", dir_name, curr_stream_idx);

    if (0 != access(p_ctx->stream_dir_name, F_OK))
    {
        if (0 != mkdir(p_ctx->stream_dir_name, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "Error create stream folder %s, errno %d\n",
                           p_ctx->stream_dir_name, NI_ERRNO);
        } else
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Created stream sub folder: %s\n",
                           p_ctx->stream_dir_name);
        }
    } else
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Reusing stream sub folder: %s\n",
                       p_ctx->stream_dir_name);
    }

    flock(p_device_context->lock, LOCK_UN);
    ni_rsrc_free_device_context(p_device_context);

    snprintf(file_name, sizeof(file_name), "%s/process_session_id.txt",
             p_ctx->stream_dir_name);

    fp = fopen(file_name, "wb");
    if (fp)
    {
        char number[64] = {'\0'};
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Decoder pkt dump log created: %s\n", file_name);
        snprintf(number, sizeof(number), "proc id: %ld\nsession id: %u\n",
                 (long)getpid(), p_ctx->session_id);
        fwrite(number, strlen(number), 1, fp);
        fclose(fp);
    } else
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error create decoder pkt dump log: %s\n", file_name);
    }
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__OPEN_HARMONY__) && !defined(_ANDROID)
#define MAX_BDF_LEN 16

// dev_name pattern: nvmeXnY
// pci_addr pattern: dddd:bb:dd.f
static int find_pci_address(const char *dev_name, char *pci_addr) {
    char sys_block_path[256];

    snprintf(sys_block_path, sizeof(sys_block_path), "/sys/class/block/%s/device", dev_name);

    char *real_sysfs_path = realpath(sys_block_path, NULL);
    if (!real_sysfs_path) {
        perror("realpath failed");
        return -1;
    }

    // Walk backward in the resolved path to find a segment that matches PCI BDF format
    char *p = real_sysfs_path + strlen(real_sysfs_path);

    while (p > real_sysfs_path) {
        p = strrchr(real_sysfs_path, '/');
        if (!p) break;

        char segment[MAX_BDF_LEN];
        strncpy(segment, p + 1, MAX_BDF_LEN - 1);
        segment[MAX_BDF_LEN - 1] = '\0';

        unsigned domain, bus, dev, func;
        if (sscanf(segment, "%x:%x:%x.%x", &domain, &bus, &dev, &func) == 4) {
            strncpy(pci_addr, segment, MAX_BDF_LEN);
            pci_addr[MAX_BDF_LEN - 1] = '\0';
            free(real_sysfs_path);
            return 0;
        }

        *p = '\0';  // truncate path to move to the previous segment
    }

    free(real_sysfs_path);
    return -1;
}
#endif

// open netint p2p driver and fill the pcie address to p_ctx
static ni_retcode_t p2p_fill_pcie_address(ni_session_context_t *p_ctx)
{
#if defined(_WIN32) || defined(__APPLE__) || defined(__OPEN_HARMONY__)
    (void)p_ctx;
    return NI_RETCODE_FAILURE;
#else
    int ret = 0;
    char pci_addr[16];
    struct stat bstat;
    char *p_dev;
    char *dom, *bus, *dev, *fnc;
#ifdef _ANDROID
    char syspath[256];
    FILE *fp;
#endif

    if(!p_ctx)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() parameter is NULL\n",__func__);
      return NI_RETCODE_FAILURE;
    }

    p_ctx->netint_fd = open("/dev/netint", O_RDWR);
    if (p_ctx->netint_fd < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Can't open device /dev/netint\n");
      return NI_RETCODE_FAILURE;
    }

    p_dev = &p_ctx->dev_xcoder_name[0];
    if (stat(p_dev, &bstat) < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "failed to get stat of file %s\n", p_dev);
      return NI_RETCODE_FAILURE;
    }

    if ((bstat.st_mode & S_IFMT) != S_IFBLK)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s is not a block device\n", p_dev);
      return NI_RETCODE_FAILURE;
    }

#ifdef _ANDROID
    ret = snprintf(syspath, sizeof(syspath) - 1,
                "/sys/block/%s/device/address",
                p_dev + 5);
    syspath[ret] = '\0';

    fp = fopen(syspath, "r");
    if (fp == NULL)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Failed to read address\n");
      return NI_RETCODE_FAILURE;
    }

    if (fgets(pci_addr, sizeof(pci_addr), fp) == NULL)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Failed to read line from address\n");
      fclose(fp);
      return NI_RETCODE_FAILURE;
    }

    fclose(fp);
#else
    ret = find_pci_address(p_dev + 5, pci_addr);
    if (ret != 0)
    {
      return NI_RETCODE_FAILURE;
    }
#endif

    // pci_addr pattern: 0000:04:00.0
    ni_log2(p_ctx, NI_LOG_INFO, "PCI address string = %12.12s\n", pci_addr);

    errno = 0;
    p_ctx->domain = strtoul(pci_addr, &dom, 16);
    if (errno < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Failed to read PCI domain\n");
      return NI_RETCODE_FAILURE;
    }

    errno = 0;
    p_ctx->bus = strtoul(dom + 1, &bus, 16);
    if (errno < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Failed to read PCI bus\n");
      return NI_RETCODE_FAILURE;
    }

    errno = 0;
    p_ctx->dev = strtoul(bus + 1, &dev, 16);

    if (errno < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Failed to read PCI device\n");
      return NI_RETCODE_FAILURE;
    }

    errno = 0;
    p_ctx->fn = strtoul(dev + 1, &fnc, 16);

    if (errno < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "Falied to read PCI function\n");
      return NI_RETCODE_FAILURE;
    }

    ni_log2(p_ctx, NI_LOG_DEBUG, "PCI address parsed = %04x:%02x:%02x.%x\n",
            p_ctx->domain, p_ctx->bus, p_ctx->dev, p_ctx->fn);

    return NI_RETCODE_SUCCESS;
#endif
}

#if __linux__ || __APPLE__
#if !defined(_ANDROID) && !defined(__OPENHARMONY__)
#ifndef DISABLE_BACKTRACE_PRINT
void ni_print_backtrace() {
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** strs = backtrace_symbols(callstack, frames);

   ni_log(NI_LOG_ERROR, "Call stack:\n");
    for (int i = 0; i < frames; ++i) {
        ni_log(NI_LOG_ERROR, "%s\n", strs[i]);
    }

    free(strs);
}
#endif
#endif
#endif

/*!******************************************************************************
 *  \brief  Open a xcoder decoder instance
 *
 *  \param
 *
 *  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_xcoder_params_t *p_param = NULL;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  char fmt_fw_api_ver1[5], fmt_fw_api_ver2[5];

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_DECODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->max_retry_fail_count[0] = p_ctx->max_retry_fail_count[1] = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;
    p_ctx->session_timestamp = 0;
    p_ctx->is_dec_pkt_512_aligned = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->last_pkt_pos = 0;
    p_ctx->last_frame_offset = 0;
    memset(p_ctx->pkt_custom_sei_set, 0, NI_FIFO_SZ * sizeof(ni_custom_sei_set_t *));

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %d: %s() alloc decoder all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        ni_aligned_free(p_ctx->p_all_zero_buf);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_DECODER, ni_htonl(p_ctx->codec_format), p_ctx->hw_action);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp = ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_high);
    p_ctx->session_timestamp = (p_ctx->session_timestamp << 32) |
        ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_low);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): p_ctx->device_handle=0x%" PRIx64 ", "
               "p_ctx->hw_id=%d, p_ctx->session_id=%d\n", __func__,
               (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "Decoder open session ID:0x%x, timestamp:%" PRIu64 "\n",
                   p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW API version is >= 6.2
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "62") >= 0)
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX], &fmt_fw_api_ver1[0]);
        ni_fmt_fw_api_ver_str((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],  &fmt_fw_api_ver2[0]);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s libxcoder FW API ver %s, FW FW API ver %s\n",
               __func__, fmt_fw_api_ver1, fmt_fw_api_ver2);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

    //VP9 requires a scaler session to be opened internally and attached as
    //well
    if(p_ctx->codec_format == NI_CODEC_FORMAT_VP9)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Adding scaling session to Vp9 decoder\n");
        ui32LBA = OPEN_ADD_CODEC(NI_DEVICE_TYPE_SCALER, ni_htonl(NI_SCALER_OPCODE_SCALE), ni_htons(p_ctx->session_id));
        retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                       p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_open,
                       p_ctx->device_type, p_ctx->hw_id,
                       &(p_ctx->session_id), OPT_1);
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR couldn't add vp9 scaler to decoding session\n");
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s():p_ctx->device_handle=0x%" PRIx64 ", "
                 "p_ctx->hw_id=%d, p_ctx->session_id=%d\n", __func__,
                 (int64_t)p_ctx->device_handle,p_ctx->hw_id, p_ctx->session_id);
          ni_decoder_session_close(p_ctx, 0);
          LRETURN;
        }
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,
           "%s(): p_ctx->device_handle=0x%" PRIx64 ", p_ctx->hw_id=%d, "
           "p_ctx->session_id=%d\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
  }

  //start dec config
  retval = ni_config_instance_set_decoder_params(p_ctx, 0);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: calling ni_config_instance_set_decoder_params(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n", (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    //ni_decoder_session_close(p_ctx, 0); //close happens on above
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }
  //end dec config

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = NI_NOPTS_VALUE;
  p_ctx->last_dts = NI_NOPTS_VALUE;
  p_ctx->last_dts_interval = 0;
  p_ctx->last_pts_interval = 0;
  p_ctx->pts_correction_last_dts = INT64_MIN;
  p_ctx->pts_correction_last_pts = INT64_MIN;

  //p_ctx->p_leftover = malloc(NI_MAX_PACKET_SZ * 2);
  p_ctx->p_leftover = malloc(p_ctx->max_nvme_io_size * 2);
  if (!p_ctx->p_leftover)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Cannot allocate leftover buffer.\n",
             __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      //ni_decoder_session_close(p_ctx, 0);
      LRETURN;
  }

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "dec_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "dec_dts");

  if (p_ctx->p_session_config)
  {
      p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
      ni_params_print(p_param);

      if (p_param->ddr_priority_mode > NI_DDR_PRIORITY_NONE)
      {
          retval = ni_device_set_ddr_configuration(p_ctx, p_param->ddr_priority_mode);
          if (NI_RETCODE_SUCCESS != retval)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): ddr priority setting failure for %s\n",
                     __func__, strerror(NI_ERRNO));
              LRETURN;
          }
      }
  }

  if (p_ctx->force_low_delay)
  {
      if (ni_cmp_fw_api_ver(
              (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
              "6r3") < 0)
      {
          p_ctx->force_low_delay = false; // forceLowDelay not available for fw < 6r3
          ni_log2(p_ctx, NI_LOG_INFO, "Warn %s(): forceLowDelay is not available for fw < 6r3\n",
                 __func__);
      }
  }

  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->actual_video_width = 0;
  p_ctx->pixel_format_changed = 0;

  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

  if (p_param && p_param->dec_input_params.nb_save_pkt)
  {
      decoder_dump_dir_open(p_ctx);
  }

#ifdef XCODER_DUMP_DATA
  char dir_name[256] = {0};

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-dec-fme", (long)getpid(),
           p_ctx->session_id);
  DIR *dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Decoder frame dump dir created: %s\n", dir_name);
  }
  if(dir){
      closedir(dir);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  send a keep alive message to firmware
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_send_session_keep_alive(uint32_t session_id, ni_device_handle_t device_handle, ni_event_handle_t event_handle, void *p_data)
{
    ni_retcode_t retval;
    uint32_t ui32LBA = 0;

    ni_log(NI_LOG_TRACE, "%s(): enter\n", __func__);
    if (NI_INVALID_SESSION_ID == session_id)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Invalid session ID!, return\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    if (NI_INVALID_DEVICE_HANDLE == device_handle)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): xcoder instance id < 0, return\n",
              __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    ui32LBA = CONFIG_SESSION_KeepAlive_W(session_id);
    if (ni_nvme_send_write_cmd(device_handle, event_handle, p_data,
                               NI_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): device_handle=%" PRIx64 " , "
               "session_id=%d\n", __func__, (int64_t)device_handle, session_id);
        retval = NI_RETCODE_FAILURE;
    }
    else
    {
        ni_log(NI_LOG_TRACE,
               "SUCCESS %s(): device_handle=%" PRIx64 " , "
               "session_id=%d\n", __func__, (int64_t)device_handle, session_id);
        retval = NI_RETCODE_SUCCESS;
    }

END:

    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send end of stream signal to the decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_send_eos(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): enter\n", __func__);
  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);

END:

  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): %d, return\n", __func__, retval);
  }

  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Flush decoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_flush(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s(): xcoder instance id < 0, return\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  retval = ni_config_instance_flush(p_ctx, NI_DEVICE_TYPE_DECODER);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);

END:

  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): %d, return\n", __func__, retval);
  }

  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder decoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_decoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int i;
  ni_xcoder_params_t *p_param = NULL;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): Invalid session ID, return.\n", __func__);
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
  }

  if (NI_CODEC_HW_ENABLE == p_ctx->hw_action)
  {
    ni_session_statistic_t sessionStatistic = {0};
    ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_DECODER, &sessionStatistic);
  }

  if (ni_cmp_fw_api_ver(
         (char *) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rg") >= 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO,
          "Decoder_complete_info:session_id 0x%x, total frames input:%u "
          "buffered: %u completed: %u output: %u dropped: %u error: %u corrupted: %u\n",
          p_ctx->session_id, p_ctx->session_statistic.ui32FramesInput,
          p_ctx->session_statistic.ui32FramesBuffered,
          p_ctx->session_statistic.ui32FramesCompleted,
          p_ctx->session_statistic.ui32FramesOutput,
          p_ctx->session_statistic.ui32FramesDropped,
          p_ctx->session_statistic.ui32InstErrors,
          p_ctx->session_statistic.ui32FramesCorrupted);
  } else
  {
    ni_log2(p_ctx, NI_LOG_INFO,
          "Decoder_complete_info:session_id 0x%x, total frames input:%u "
          "buffered: %u completed: %u output: %u dropped: %u error: %u\n",
          p_ctx->session_id, p_ctx->session_statistic.ui32FramesInput,
          p_ctx->session_statistic.ui32FramesBuffered,
          p_ctx->session_statistic.ui32FramesCompleted,
          p_ctx->session_statistic.ui32FramesOutput,
          p_ctx->session_statistic.ui32FramesDropped,
          p_ctx->session_statistic.ui32InstErrors);
  }

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "%s: Invalid session ID, return.\n", __func__);
    retval = NI_RETCODE_SUCCESS;
    LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: malloc decoder close data buffer failed\n", NI_ERRNO);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  if (p_ctx->p_session_config)
  {
      p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
      if (p_param->ddr_priority_mode > NI_DDR_PRIORITY_NONE)
      {
          retval = ni_device_set_ddr_configuration(p_ctx, NI_DDR_PRIORITY_RESET);
          if (NI_RETCODE_SUCCESS != retval)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): ddr priority setting failure for %s\n",
                     __func__, strerror(NI_ERRNO));
              LRETURN;
          }
      }
  }

  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): command failed!\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          p_ctx->session_id = NI_INVALID_SESSION_ID;
          break;
      } else
      {
        //Close should always succeed
        retval = NI_RETCODE_SUCCESS;
        p_ctx->session_id = NI_INVALID_SESSION_ID;
        break;
      }
    /*
    else if (*((ni_retcode_t *)p_buffer) == RETCODE_SUCCESS)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): wait for close\n", __func__);
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
    retry++;
  }

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);
    ni_memfree(p_ctx->p_leftover);

    if (p_ctx->pts_table)
    {
        ni_queue_free(&p_ctx->pts_table->list, p_ctx->buffer_pool);
        ni_memfree(p_ctx->pts_table);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "ni_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
        ni_queue_free(&p_ctx->dts_queue->list, p_ctx->buffer_pool);
        ni_memfree(p_ctx->dts_queue);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "ni_timestamp_done: success\n");
    }

    ni_buffer_pool_free(p_ctx->buffer_pool);
    ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;

    for (i = 0; i < NI_FIFO_SZ; i++)
    {
        ni_memfree(p_ctx->pkt_custom_sei_set[i]);
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s():  CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);

    low_delay_signal(p_ctx);
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a video p_packet to decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_write(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  uint32_t sent_size = 0;
  uint32_t packet_size = 0;
  int current_pkt_size;
  int retval = NI_RETCODE_SUCCESS;
  ni_xcoder_params_t *p_param;
  ni_instance_buf_info_t buf_info = { 0 };
  ni_session_statistic_t sessionStatistic = {0};
  int query_retry = 0;
  uint32_t ui32LBA = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_packet))
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  if ((NI_INVALID_SESSION_ID == p_ctx->session_id))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  low_delay_wait(p_ctx);

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      uint64_t abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_packet->dts);
  }
#endif

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  packet_size = p_packet->data_len;
  current_pkt_size = packet_size;

#ifdef XCODER_311
  int max_retry = 200; // 20ms
  if (p_packet->video_width * p_packet->video_height >= NI_NUM_OF_PIXELS_1440P)
  {
    max_retry = NI_MAX_TX_RETRIES; // 100ms
  }
#endif

  for (;;)
  {
    query_sleep(p_ctx);

    query_retry++;


    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "65") >= 0)
    {
        retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_DECODER,
                                                 &sessionStatistic);
        CHECK_ERR_RC(p_ctx, retval, &sessionStatistic,
                      nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                      p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        CHECK_VPU_RECOVERY(retval);

        buf_info.buf_avail_size = sessionStatistic.ui32WrBufAvailSize;
    } else
    {
        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                            NI_DEVICE_TYPE_DECODER, &buf_info);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);
    }

    if (buf_info.buf_avail_size == DP_IPC_PASSTHRU)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s(): Bad available buffer size %u\n", __FUNCTION__, buf_info.buf_avail_size);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    if (p_ctx->biggest_bitstream_buffer_allocated < buf_info.buf_avail_size)
    {
      p_ctx->biggest_bitstream_buffer_allocated = buf_info.buf_avail_size;
    }
    if (p_ctx->biggest_bitstream_buffer_allocated < packet_size &&
        buf_info.buf_avail_size == p_ctx->biggest_bitstream_buffer_allocated)
    {
      // Reallocate decoder bitstream buffers to accomodate
      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "66") >= 0)
      {
          retval = ni_config_instance_set_write_len(p_ctx,
                                                    NI_DEVICE_TYPE_DECODER,
                                                    // packet buffer aligned to NI_MAX_PACKET_SZ(128k)
                                                    (packet_size / NI_MAX_PACKET_SZ + 1) * NI_MAX_PACKET_SZ);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_config_xcoder_config_set_write_legth,
                  p_ctx->device_type, p_ctx->hw_id,
                  &(p_ctx->session_id), OPT_3);
          CHECK_VPU_RECOVERY(retval);
      }
      else
      {
          retval = ni_config_instance_set_decoder_params(p_ctx, packet_size);
      }
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "%s(): failed to reallocate bitstream\n", __FUNCTION__);
          LRETURN;
      }
      query_retry--;
      continue;
    }

    if (NI_RETCODE_SUCCESS != retval ||
        buf_info.buf_avail_size < packet_size)
    {
        ni_log2(p_ctx, NI_LOG_TRACE,
               "Warning: dec write query fail rc %d or available buf size %u < "
               "pkt size %u , retry: %d max_retry_fail_count %d\n",
               retval, buf_info.buf_avail_size, packet_size, query_retry, p_ctx->max_retry_fail_count[0]);
#ifdef XCODER_311
        if (query_retry > max_retry ||
            sessionStatistic.ui32RdBufAvailSize > 0)
#else
        if (query_retry > NI_MAX_TX_RETRIES ||
            sessionStatistic.ui32RdBufAvailSize > 0)
#endif
        {
            p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
#ifdef XCODER_311
            if (query_retry > max_retry)
#else
            if (query_retry > NI_MAX_TX_RETRIES)
#endif
            {
                p_ctx->max_retry_fail_count[0]++;
            }
            p_ctx->required_buf_size = packet_size;
            retval = (p_ctx->max_retry_fail_count[0] >= NI_XCODER_FAILURES_MAX) ? NI_RETCODE_FAILURE : NI_RETCODE_SUCCESS;
            LRETURN;
        }
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(NI_RETRY_INTERVAL_100US);
      ni_pthread_mutex_lock(&p_ctx->mutex);
    }
    else
    {
      p_ctx->max_retry_fail_count[0] = 0;
      ni_log2(p_ctx, NI_LOG_DEBUG, "Info dec write query success, available buf "
                     "size %u >= pkt size %u !\n",
                     buf_info.buf_avail_size, packet_size);
      break;
    }
  }

  //Configure write size for the buffer
  retval = ni_config_instance_set_write_len(p_ctx, NI_DEVICE_TYPE_DECODER,
                                          packet_size);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_config_xcoder_config_set_write_legth,
                   p_ctx->device_type, p_ctx->hw_id,
                   &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): config pkt size command failed\n",
             __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  //check for start of stream flag
  if (p_packet->start_of_stream)
  {
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Failed to send SOS.\n", __func__);
        LRETURN;
    }

    p_packet->start_of_stream = 0;
  }

  if (p_packet->p_data)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s() had data to send: packet_size=%u, "
             "p_packet->sent_size=%d, p_packet->data_len=%u, "
             "p_packet->start_of_stream=%u, p_packet->end_of_stream=%u, "
             "p_packet->video_width=%u, p_packet->video_height=%u\n",
             __func__, packet_size, p_packet->sent_size, p_packet->data_len,
             p_packet->start_of_stream, p_packet->end_of_stream,
             p_packet->video_width, p_packet->video_height);

      uint8_t *p_data = (uint8_t *)p_packet->p_data;
      // Note: session status is NOT reset but tracked between send
      // and recv to catch and recover from a loop condition
      // p_ctx->status = 0;

    if (packet_size % NI_MEM_PAGE_ALIGNMENT) //packet size, already aligned
    {
        packet_size = ( (packet_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, packet_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // reset session status after successful send
    p_ctx->status = 0;
    p_ctx->required_buf_size = 0;

    sent_size = p_packet->data_len;
    p_packet->data_len = 0;

    if (p_param->dec_input_params.nb_save_pkt)
    {
        char dump_file[512] = {0};
        long curr_pkt_num =
            ((long)p_ctx->pkt_num % p_param->dec_input_params.nb_save_pkt) + 1;
        snprintf(dump_file, sizeof(dump_file), "%s/pkt-%04ld.bin",
                 p_ctx->stream_dir_name, curr_pkt_num);
        FILE *f = fopen(dump_file, "wb");
        if (f)
        {
            fwrite(p_packet->p_data, sent_size, 1, f);
            fflush(f);
            fclose(f);
        }
    }

    p_ctx->pkt_num++;
    p_ctx->low_delay_sync_flag = 1;
  }

  //Handle end of stream flag
  if (p_packet->end_of_stream)
  {
    retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_DECODER);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Failed to send EOS.\n", __func__);
        LRETURN;
    }

    p_packet->end_of_stream = 0;
    p_ctx->ready_to_close = 1;
  }

  {
    p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->pts;
    p_ctx->flags_array[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->flags;
    p_ctx->pkt_pos[p_ctx->pkt_index % NI_FIFO_SZ] = p_packet->pkt_pos;
    if (p_ctx->pkt_index == 0)
    {
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = 0;
      /* minus 1 here. ffmpeg parses the msb 0 of long start code as the last packet's payload for hevc bitstream (hevc_parse).
       * move 1 byte forward on all the pkt_offset so that frame_offset coming from fw can fall into the correct range. */
      //p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = current_pkt_size - 1;
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = current_pkt_size;
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s: (first packet) pkt_index %d i %u "
             "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index %" PRIu64
             " pts_offsets %" PRId64 "\n",
             __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
             p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);
    }
    else
    {
      // cumulate sizes to correspond to FW offsets
      p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ];
      p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index[(p_ctx->pkt_index - 1) % NI_FIFO_SZ] + current_pkt_size;
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s: pkt_index %d i %u pkt_offsets_index_min "
             "%" PRIu64 " pkt_offsets_index %" PRIu64 " pts_offsets %" PRId64
             "\n",
             __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
             p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
             p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);

      //Wrapping 32 bits since FW send u32 wrapped values
      if (p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] > 0xFFFFFFFF)
      {
        p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] - (0x100000000);
        p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ] = p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ] + current_pkt_size;
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "%s: (wrap) pkt_index %d i %u "
               "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index %" PRIu64
               " pts_offsets %" PRId64 "\n",
               __func__, p_ctx->pkt_index, p_ctx->pkt_index % NI_FIFO_SZ,
               p_ctx->pkt_offsets_index_min[p_ctx->pkt_index % NI_FIFO_SZ],
               p_ctx->pkt_offsets_index[p_ctx->pkt_index % NI_FIFO_SZ],
               p_ctx->pts_offsets[p_ctx->pkt_index % NI_FIFO_SZ]);
      }
    }

    /* if this wrap-around pkt_offset_index spot is about to be overwritten, free the previous one. */
    free(p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ]);

    if (p_packet->p_custom_sei_set)
    {
      p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ] = malloc(sizeof(ni_custom_sei_set_t));
      if (p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ])
      {
        ni_custom_sei_set_t *p_custom_sei_set = p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ];
        memcpy(p_custom_sei_set, p_packet->p_custom_sei_set, sizeof(ni_custom_sei_set_t));
      }
      else
      {
        /* warn and lose the sei data. */
        ni_log2(p_ctx, NI_LOG_ERROR,
               "Error %s: failed to allocate custom SEI buffer for pkt.\n",
               __func__);
      }
    }
    else
    {
      p_ctx->pkt_custom_sei_set[p_ctx->pkt_index % NI_FIFO_SZ] = NULL;
    }

    p_ctx->pkt_index++;
  }

  retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue, p_packet->dts, 0);
  if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,
             "ERROR %s(): ni_timestamp_register() for dts returned %d\n",
             __func__, retval);
  }
END:

  ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (NI_RETCODE_SUCCESS == retval)
    {
        ni_log2(p_ctx, NI_LOG_TRACE,
               "%s(): exit: packets: %" PRIu64 " offset %" PRIx64 ""
               " sent_size = %u, status=%d\n",
               __func__, p_ctx->pkt_num, (uint64_t)p_packet->pos, sent_size,
               p_ctx->status);
        return sent_size;
    } else
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): exit: returnErr: %d, p_ctx->status: %d\n", __func__,
               retval, p_ctx->status);
        return retval;
    }
}

static int64_t guess_correct_pts(ni_session_context_t* p_ctx, int64_t reordered_pts, int64_t dts)
{
  int64_t pts = NI_NOPTS_VALUE;
  if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_dts += dts <= p_ctx->pts_correction_last_dts;
    p_ctx->pts_correction_last_dts = dts;
    ni_log2(p_ctx, NI_LOG_DEBUG,
           "%s: pts_correction_last_dts %" PRId64 " "
           "pts_correction_num_faulty_dts %d\n",
           __func__, p_ctx->pts_correction_last_dts,
           p_ctx->pts_correction_num_faulty_dts);
  }
  else if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_dts = reordered_pts;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: pts_correction_last_dts %" PRId64 "\n", __func__,
           p_ctx->pts_correction_last_dts);
  }
  if (reordered_pts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_num_faulty_pts += reordered_pts <= p_ctx->pts_correction_last_pts;
    p_ctx->pts_correction_last_pts = reordered_pts;
    ni_log2(p_ctx, NI_LOG_DEBUG,
           "%s: pts_correction_last_pts %" PRId64 " "
           "pts_correction_num_faulty_pts %d\n",
           __func__, p_ctx->pts_correction_last_pts,
           p_ctx->pts_correction_num_faulty_pts);
  }
  else if (dts != NI_NOPTS_VALUE)
  {
    p_ctx->pts_correction_last_pts = dts;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: pts_correction_last_pts %" PRId64 "\n", __func__,
           p_ctx->pts_correction_last_pts);
  }
  if ((p_ctx->pts_correction_num_faulty_pts<=p_ctx->pts_correction_num_faulty_dts || dts == NI_NOPTS_VALUE)
     && reordered_pts != NI_NOPTS_VALUE)
  {
    pts = reordered_pts;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: (reordered_pts) pts %" PRId64 "\n", __func__,
           pts);
  }
  else
  {
      if ((NI_NOPTS_VALUE == reordered_pts) ||
          (NI_NOPTS_VALUE == p_ctx->last_pts) || (dts >= p_ctx->last_pts))
      {
          pts = dts;
      } else
      {
          pts = reordered_pts;
      }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: (dts) pts %" PRId64 "\n", __func__, pts);
  }
  return pts;
}

static int rotated_array_binary_search(uint64_t *lefts, uint64_t *rights,
                                       int32_t size, uint64_t target)
{
    int lo = 0;
    int hi = size - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (lefts[mid] <= target && target < rights[mid])
        {
            return mid;
        }

        if (rights[mid] == 0)
        {
            // empty in (mid, hi)
            hi = mid - 1;
            continue;
        }

        if (rights[lo] <= rights[mid])
        {
            if (lefts[lo] <= target && target < lefts[mid])
            {
                // Elements are all monotonous in (lo, mid)
                hi = mid - 1;
            } else
            {
                // Rotation in (mid, hi)
                lo = mid + 1;
            }
        } else
        {
            if (rights[mid] <= target && target < rights[hi])
            {
                // Elements are all monotonous in (mid, hi)
                lo = mid + 1;
            } else
            {
                // Rotation in (lo, mid)
                hi = mid - 1;
            }
        }
    }

    return -1;
}

/*!******************************************************************************
 *  \brief  Retrieve a YUV p_frame from decoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_decoder_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  ni_instance_mgr_stream_info_t data = { 0 };
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint8_t *p_data_buffer = NULL;
  int i = 0;
  int is_planar;
  int retval = NI_RETCODE_SUCCESS;
  int metadata_hdr_size = NI_FW_META_DATA_SZ - NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
  int sei_size = 0;
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  ni_instance_buf_info_t buf_info = { 0 };
  ni_session_statistic_t sessionStatistic = {0};
  ni_xcoder_params_t *p_param;

  int query_retry = 0;
  uint32_t ui32LBA = 0;
  unsigned int bytes_read_so_far = 0;
  int query_type = INST_BUF_INFO_RW_READ;
  int low_delay_notify = 0;
  uint32_t frames_dropped = 0;
  uint8_t get_first_metadata = 0;
  uint8_t sequence_change = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_frame))
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: passed parameters are null!, return\n");
    return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

start:
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): xcoder instance id < 0, return\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.
  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
      p_frame->data_len[2] + metadata_hdr_size;
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Total bytes to read %u, low_delay %u\n",
         total_bytes_to_read, p_ctx->decoder_low_delay);

  if (p_ctx->decoder_low_delay > 0 && !p_ctx->ready_to_close)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "frame_num = %" PRIu64 ", pkt_num = %" PRIu64 "\n",
             p_ctx->frame_num, p_ctx->pkt_num);
      frames_dropped = p_ctx->session_statistic.ui32FramesDropped;
      if (p_ctx->force_low_delay && (p_ctx->force_low_delay_cnt < frames_dropped)) {
          p_ctx->force_low_delay_cnt = frames_dropped;
      }
      if (p_ctx->frame_num + p_ctx->force_low_delay_cnt >= p_ctx->pkt_num)
      {
          //nothing to query, leave
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
      }
      query_type = INST_BUF_INFO_RW_READ_BUSY;
  }
  for (;;)
  {
    query_sleep(p_ctx);

    query_retry++;

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "65") >= 0)
    {
        retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_DECODER,
                                                 &sessionStatistic);
        CHECK_ERR_RC(p_ctx, retval, &sessionStatistic,
                      nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                      p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        CHECK_VPU_RECOVERY(retval);

        buf_info.buf_avail_size = sessionStatistic.ui32RdBufAvailSize;
    } else
    {
        retval = ni_query_instance_buf_info(p_ctx, query_type,
                                            NI_DEVICE_TYPE_DECODER, &buf_info);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);
    }

    ni_log2(p_ctx, NI_LOG_TRACE,  "Info query buf_info.size = %u\n",
           buf_info.buf_avail_size);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_TRACE, "Warning: dec read query fail rc %d retry %d max_retry_fail_count %d\n",
               retval, query_retry, p_ctx->max_retry_fail_count[1]);

        if (query_retry >= 1000)
        {
            p_ctx->max_retry_fail_count[1]++;
            low_delay_notify = 1;
            retval = (p_ctx->max_retry_fail_count[1] >= NI_XCODER_FAILURES_MAX) ? NI_RETCODE_FAILURE : NI_RETCODE_SUCCESS;
            LRETURN;
        }
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(NI_RETRY_INTERVAL_100US);
      ni_pthread_mutex_lock(&p_ctx->mutex);
    }
    else if (buf_info.buf_avail_size == DP_IPC_PASSTHRU)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s(): Bad available buffer size %u\n", __FUNCTION__, buf_info.buf_avail_size);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }
    else if (buf_info.buf_avail_size == metadata_hdr_size)
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Info only metadata hdr is available, seq change?\n");
      total_bytes_to_read = metadata_hdr_size;
      sequence_change = 1;
      break;
    }
    else if (0 == buf_info.buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
          ni_log2(p_ctx, NI_LOG_TRACE,  "Info dec query, ready_to_close %u, query eos\n",
                 p_ctx->ready_to_close);
          retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                       p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);

          if (data.is_flushed ||
              query_retry >=
                  NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   // 15000 retries
          {
              if (query_retry >=
                  NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   //15000 retries
              {
                  ni_log2(p_ctx, NI_LOG_ERROR,
                         "WARNING: Dec eos reached but also exceeded max dec read query "
                         "retries. is_flushed=%u try=%d.\n",
                         data.is_flushed, query_retry);
              } else
              {
                  ni_log2(p_ctx, NI_LOG_DEBUG,
                         "Dec eos reached. is_flushed=%u try=%d.\n",
                         data.is_flushed, query_retry);
              }
              p_frame->end_of_stream = 1;
              low_delay_notify = 1;
              retval = NI_RETCODE_SUCCESS;
              LRETURN;
          } else
        {
            ni_log2(p_ctx, NI_LOG_TRACE,  "Dec read available buf size == 0, query try %d,"
                                 " retrying ..\n", query_retry);
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(NI_RETRY_INTERVAL_200US);  // 200 us
            ni_pthread_mutex_lock(&p_ctx->mutex);
            continue;
        }
      }

      ni_log2(p_ctx, NI_LOG_TRACE,  "Dec read available buf size == 0. retry=%d, eos=%d"
             "\n", query_retry, p_frame->end_of_stream);
      if ((NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status ||
           (p_ctx->decoder_low_delay > 0 &&
            ((p_ctx->frame_num + p_ctx->force_low_delay_cnt)
            < p_ctx->pkt_num))) &&
          query_retry < 1000 / 2)
      {
          if (p_ctx->decoder_low_delay && p_ctx->force_low_delay) {
              if (p_ctx->session_statistic.ui32FramesDropped > frames_dropped) {
                  // last pkt sent to decoder marked as dropped, no output,
                  // so just stop query and return
                  p_ctx->force_low_delay_cnt++;
                  low_delay_signal(p_ctx);
                  retval = NI_RETCODE_SUCCESS;
                  LRETURN;
              }
          } else if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status &&
                     sessionStatistic.ui32WrBufAvailSize > p_ctx->required_buf_size)
          {
              ni_log2(p_ctx, NI_LOG_TRACE, "Info dec write buffer is enough, available buf "
                      "size %u >= required size %u !\n",
                      sessionStatistic.ui32WrBufAvailSize, p_ctx->required_buf_size);
              p_ctx->status = 0;
              p_ctx->required_buf_size = 0;
              retval = NI_RETCODE_SUCCESS;
              LRETURN;
          }
          ni_pthread_mutex_unlock(&p_ctx->mutex);
          ni_usleep(25);
          ni_pthread_mutex_lock(&p_ctx->mutex);
          continue;
      } else
      {
        if(p_ctx->decoder_low_delay > 0)
        {
            if (p_ctx->force_low_delay) {
                p_ctx->force_low_delay_cnt++;
                low_delay_signal(p_ctx);
            } else {
                ni_log2(p_ctx, NI_LOG_ERROR,
                      "Warning: ceased using low delay decoding mode after "
                      "excessively long decoder read query.\n");
                // Here it should be the last signal to release the send thread
                // holding the low delay mutex.
                low_delay_signal(p_ctx);
                p_ctx->decoder_low_delay = 0;
            }
        }

        if ((p_param->dec_input_params.min_packets_delay && p_ctx->pkt_delay_cnt))
        {
            if(p_ctx->pkt_num >= (p_ctx->frame_num + p_ctx->pkt_delay_cnt +
                                  p_ctx->session_statistic.ui32FramesDropped))
            {
                if(query_retry <= 2000)
                {
                    ni_pthread_mutex_unlock(&p_ctx->mutex);
                    ni_usleep(25);
                    ni_pthread_mutex_lock(&p_ctx->mutex);
                    continue;
                } else {
                    p_ctx->pkt_delay_cnt++;
                    ni_log2(p_ctx, NI_LOG_ERROR,
                          "Warning: decoder pkt_num %u frame_num %u "
                          "timeout, increasing pkt_delay_cnt to %u\n",
                          p_ctx->pkt_num, p_ctx->frame_num,
                          p_ctx->pkt_delay_cnt);
                }
            }
        }

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning: dec read failed %d retries. rc=%d; eos=%d\n",
               query_retry, p_ctx->status, p_frame->end_of_stream);
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
        // We have to ensure there are adequate number of DTS for picture
        // reorder delay otherwise wait for more packets to be sent to decoder.
        ni_timestamp_table_t *p_dts_queue = p_ctx->dts_queue;
        if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
            !p_ctx->ready_to_close &&
            NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL != p_ctx->status)
        {
            retval = NI_RETCODE_SUCCESS;
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "At least %d packets should be sent before reading the "
                   "first frame!\n",
                   p_ctx->pic_reorder_delay + 1);
            LRETURN;
        }
        p_ctx->max_retry_fail_count[1] = 0;

      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec YUV query, pic size %ux%u xfer frame size "
                       "%ux%u frame-rate %u is_flushed %u\n",
                       data.picture_width, data.picture_height,
                       data.transfer_frame_stride, data.transfer_frame_height,
                       data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->actual_video_width = data.picture_width;
        p_ctx->pixel_format = data.pix_format;
        is_planar = (p_ctx->pixel_format == NI_PIX_FMT_YUV420P) ||
            (p_ctx->pixel_format == NI_PIX_FMT_YUV420P10LE);
        p_ctx->bit_depth_factor = ni_get_bitdepth_factor_from_pixfmt(p_ctx->pixel_format);
        //p_ctx->bit_depth_factor = data.transfer_frame_stride / data.picture_width;
        p_ctx->is_first_frame = 1;
        p_ctx->pixel_format_changed = 0;

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec YUV, adjust frame size from %ux%u to "
                       "%ux%u format = %d\n", p_frame->video_width, p_frame->video_height,
                       p_ctx->active_video_width, p_ctx->active_video_height, p_ctx->pixel_format);

        ni_decoder_frame_buffer_free(p_frame);

        // set up decoder YUV frame buffer pool
        if (ni_dec_fme_buffer_pool_initialize(
              p_ctx, NI_DEC_FRAME_BUF_POOL_SIZE_INIT, p_ctx->actual_video_width,
              p_ctx->active_video_height,
              p_ctx->codec_format == NI_CODEC_FORMAT_H264,
              p_ctx->bit_depth_factor))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Cannot allocate fme buf pool.\n",
                   __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;

            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_decoder_session_close(p_ctx, 0);
#ifdef XCODER_SELF_KILL_ERR
          // if need to terminate at such occasion when continuing is not
          // possible, trigger a codec closure
          ni_log2(p_ctx, NI_LOG_ERROR,  "Terminating due to unable to allocate fme buf "
                 "pool.\n");
          kill(getpid(), SIGTERM);
#endif
          ni_pthread_mutex_lock(&p_ctx->mutex);
          LRETURN;
        }

        retval = ni_decoder_frame_buffer_alloc(
            p_ctx->dec_fme_buf_pool, p_frame, 1,   // get mem buffer
            p_ctx->actual_video_width, p_ctx->active_video_height,
            p_ctx->codec_format == NI_CODEC_FORMAT_H264,
            p_ctx->bit_depth_factor, is_planar);

        if (NI_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
            p_frame->data_len[2] + metadata_hdr_size;
        p_data_buffer = (uint8_t*) p_frame->p_buffer;

        // make sure we don't read more than available
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec buf size: %u YUV frame + meta-hdr size: %u "
                       "available: %u\n", p_frame->buffer_size,
                       total_bytes_to_read, buf_info.buf_avail_size);
      }
      break;
    }
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  if (buf_info.buf_avail_size < total_bytes_to_read)
  {
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_log2(p_ctx, NI_LOG_ERROR,
             "ERROR %s() avaliable size(%u)"
             "less than needed (%u)\n",
             __func__, buf_info.buf_avail_size, total_bytes_to_read);
      abort();
  }

  if (buf_info.buf_avail_size == metadata_hdr_size && (!p_ctx->frame_num || !p_data_buffer))
  {
    if (ni_cmp_fw_api_ver(
        (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
        "6rE") >= 0)
    {
      // allocate p_data_buffer to read the first metadata
      void *p_metadata_buffer = NULL;
      int buffer_size = ((metadata_hdr_size + (NI_MEM_PAGE_ALIGNMENT - 1)) /
                          NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
      if (ni_posix_memalign(&p_metadata_buffer, sysconf(_SC_PAGESIZE), buffer_size))
      {
          ni_log2(p_ctx, NI_LOG_ERROR,
                  "ERROR %d: %s() Cannot allocate metadata buffer.\n",
                  NI_ERRNO, __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
      p_data_buffer = (uint8_t *)p_metadata_buffer;
      get_first_metadata = 1;
      if (!p_ctx->frame_num)
        sequence_change = 0;
    }
  }

  if (!p_data_buffer)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: data buffer is null!, return\n");
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

    read_size_bytes = buf_info.buf_avail_size;
    ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
    if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
    {
        read_size_bytes = ((read_size_bytes / NI_MEM_PAGE_ALIGNMENT) *
                           NI_MEM_PAGE_ALIGNMENT) +
            NI_MEM_PAGE_ALIGNMENT;
    }

    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data_buffer, read_size_bytes, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    } else if (get_first_metadata) {
        // got first metadata alone
        ni_metadata_dec_frame_t *p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_data_buffer);
        ni_log2(p_ctx, NI_LOG_DEBUG, "Got first pkt_delay_cnt %u\n",
                p_meta->metadata_common.pkt_delay_cnt);
        if (p_ctx->pkt_delay_cnt < p_meta->metadata_common.pkt_delay_cnt)
            p_ctx->pkt_delay_cnt = p_meta->metadata_common.pkt_delay_cnt;
        get_first_metadata = 0;
        ni_aligned_free(p_data_buffer);
        goto start;
    } else {
        // command issued successfully, now exit
        ni_metadata_dec_frame_t *p_meta;
        p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                        p_frame->data_len[0] +
                                        p_frame->data_len[1] +
                                        p_frame->data_len[2]);

        if (buf_info.buf_avail_size != metadata_hdr_size)
        {
            low_delay_notify = 1;
            sei_size = p_meta->sei_size;
        } else if (ni_cmp_fw_api_ver(
                (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                "6rE") >= 0)
        {
            p_meta =
                (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer);
            ni_log2(p_ctx, NI_LOG_DEBUG, "Got pkt_delay_cnt %u\n",
                    p_meta->metadata_common.pkt_delay_cnt);
            if (p_ctx->pkt_delay_cnt < p_meta->metadata_common.pkt_delay_cnt)
                p_ctx->pkt_delay_cnt = p_meta->metadata_common.pkt_delay_cnt;
        }
        total_bytes_to_read = total_bytes_to_read + sei_size;
        ni_log2(p_ctx, NI_LOG_DEBUG,  "decoder read success, size %d total_bytes_to_read "
                       "include sei %u sei_size %d\n",
                       retval, total_bytes_to_read, sei_size);
    }

  bytes_read_so_far = total_bytes_to_read ;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  rx_size = ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
  p_ctx->frame_pkt_offset = frame_offset;
  if (p_ctx->decoder_low_delay > 0 && buf_info.buf_avail_size == metadata_hdr_size &&
      p_ctx->enable_low_delay_check)
  {
    ni_log2(p_ctx, NI_LOG_TRACE, "Low delay mode amd check header if has b frame\n");

    ni_metadata_dec_frame_t *p_meta =
        (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                    p_frame->data_len[0] +
                                    p_frame->data_len[1] +
                                    p_frame->data_len[2]);
    if (p_meta->metadata_common.has_b_frame == 1)
    {
      ni_log2(p_ctx, NI_LOG_ERROR,"Warning: session 0x%x decoder lowDelay mode "
                          "is cancelled due to has_b_frames, frame_num %u\n",
                          p_ctx->session_id, p_ctx->frame_num);
      p_ctx->decoder_low_delay = 0;
    }
  }

  if (rx_size > 0)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): s-state %d first_frame %d\n", __func__,
             p_ctx->session_run_state, p_ctx->is_first_frame);

      int64_t tmp_dts, prev_dts = INT64_MIN, ts_diff = 0;
      int nb_diff = 0;
      // at stream start, if there are already frames dropped at decoding, drop
      // equal number of dts from dts queue to try to align with the first
      // returned frame, and calculate the average of dts difference to be used
      // for locating the first frame's dts
      if (p_ctx->is_first_frame)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,
                  "%s(): First frame : session_id 0x%x, pic_reorder_delay: %d "
                  "total frames input:%u buffered: %u completed: %u output: %u "
                  "dropped: %u error: %u\n",
                  __func__, p_ctx->session_id, p_ctx->pic_reorder_delay,
                  p_ctx->session_statistic.ui32FramesInput,
                  p_ctx->session_statistic.ui32FramesBuffered,
                  p_ctx->session_statistic.ui32FramesCompleted,
                  p_ctx->session_statistic.ui32FramesOutput,
                  p_ctx->session_statistic.ui32FramesDropped,
                  p_ctx->session_statistic.ui32InstErrors);

          if (p_ctx->session_statistic.ui32FramesDropped > 0)
          {
              for (i = 0; i < p_ctx->session_statistic.ui32FramesDropped; i++)
              {
                  if (ni_timestamp_get_with_threshold(
                          p_ctx->dts_queue, 0, &tmp_dts,
                          XCODER_FRAME_OFFSET_DIFF_THRES, 0,
                          p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld failed !\n",
                              __func__, i, tmp_dts);
                      break;
                  } else
                  {
                      ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld success !\n",
                              __func__, i, tmp_dts);
                      if (prev_dts != INT64_MIN) {
                          ts_diff += labs(tmp_dts - prev_dts);
                          nb_diff++;
                      }
                      prev_dts = tmp_dts;
                  }
              }
              if (nb_diff > 1)
              {
                  ts_diff = ts_diff / nb_diff;
              }
              p_ctx->decoder_last_drop_frame_num = p_ctx->session_statistic.ui32FramesDropped;
              ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop dts "
                      "average diff: %ld\n", __func__, ts_diff);
          }
      }
      else
      {
          if (p_ctx->decoder_last_drop_frame_num < p_ctx->session_statistic.ui32FramesDropped)
          {
              for(i = p_ctx->decoder_last_drop_frame_num; i < p_ctx->session_statistic.ui32FramesDropped; i++)
              {
                  if (ni_timestamp_get_with_threshold(
                          p_ctx->dts_queue, 0, &tmp_dts,
                          XCODER_FRAME_OFFSET_DIFF_THRES, 0,
                          p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld failed !\n",
                              __func__, i, tmp_dts);
                      break;
                  }
              }
              p_ctx->decoder_last_drop_frame_num = p_ctx->session_statistic.ui32FramesDropped;
              ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop dts %d\n", __func__, p_ctx->decoder_last_drop_frame_num);
          }
      }

      if (ni_timestamp_get_with_threshold(
              p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
              XCODER_FRAME_OFFSET_DIFF_THRES, 0,
              p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
          if (p_ctx->last_dts != NI_NOPTS_VALUE && !p_ctx->ready_to_close)
          {
              p_ctx->pic_reorder_delay++;
              p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
              ni_log2(p_ctx, NI_LOG_DEBUG,  "Padding DTS: %" PRId64 "\n", p_frame->dts);
          } else
          {
              p_frame->dts = NI_NOPTS_VALUE;
          }
      }

      if (p_ctx->is_first_frame)
      {
          for (i = 0; i < p_ctx->pic_reorder_delay; i++)
          {
              if (p_ctx->last_pts == NI_NOPTS_VALUE &&
                  p_ctx->last_dts == NI_NOPTS_VALUE)
              {
                  // If the p_frame->pts is unknown in the very beginning we assume
                  // p_frame->pts == 0 as well as DTS less than PTS by 1000 * 1/timebase
                  if (p_frame->pts >= p_frame->dts &&
                      p_frame->pts - p_frame->dts < 1000)
                  {
                      break;
                  }
              }

              if (ni_timestamp_get_with_threshold(
                      p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                      XCODER_FRAME_OFFSET_DIFF_THRES,
                      p_ctx->frame_num % 500 == 0,
                      p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
              {
                  p_frame->dts = NI_NOPTS_VALUE;
              } else
              {
                  ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): pic_reorder_delay pop "
                          "decoder dts queue %d %ld success !\n",
                          __func__, i, p_frame->dts);
              }
          }
          // Reset for DTS padding counting
          p_ctx->pic_reorder_delay = 0;
      }
    if (p_ctx->codec_format == NI_CODEC_FORMAT_JPEG)//fw won't save frameoffset when decoding jpeg.
    {
        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
        p_frame->pts = p_ctx->pts_offsets[p_ctx->frame_num % NI_FIFO_SZ];
        p_frame->flags = p_ctx->flags_array[p_ctx->frame_num % NI_FIFO_SZ];
        p_frame->pkt_pos = p_ctx->pkt_pos[p_ctx->frame_num % NI_FIFO_SZ];
        ni_log2(p_ctx, NI_LOG_DEBUG,  "p_frame->pts = %u, frame_num = %d, p_frame->dts = %u\n",
               p_frame->pts, p_ctx->frame_num, p_frame->dts);
    }
    else
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: frame_offset %" PRIu64 "\n", __func__,
               frame_offset);

        // search for the pkt_offsets of received frame according to frame_offset.
        // here we get the index(i) which promises (p_ctx->pkt_offsets_index_min[i] <= frame_offset && p_ctx->pkt_offsets_index[i] > frame_offset)
        // i = -1 if not found
        i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                        p_ctx->pkt_offsets_index, NI_FIFO_SZ,
                                        frame_offset);
        if (i >= 0)
        {
            p_frame->pts = p_ctx->pts_offsets[i];
            p_frame->flags = p_ctx->flags_array[i];
            p_frame->pkt_pos = p_ctx->pkt_pos[i];
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "%s: (found pts) dts %" PRId64 " pts "
                   "%" PRId64 " frame_offset %" PRIu64 " i %d "
                   "pkt_offsets_index_min %" PRIu64 " pkt_offsets_index "
                   "%" PRIu64 " pkt_pos %" PRIu64 " \n",
                   __func__, p_frame->dts, p_frame->pts, frame_offset, i,
                   p_ctx->pkt_offsets_index_min[i],
                   p_ctx->pkt_offsets_index[i],
                   p_ctx->pkt_pos[i]);

            if (p_ctx->is_first_frame)
            {
                // if the first frame is I frame and there are dropped frames,
                // find the dts closest to its pts using the average of dts diff
                if (p_frame->dts != NI_NOPTS_VALUE &&
                    p_frame->pts != NI_NOPTS_VALUE &&
                    PIC_TYPE_I == p_frame->ni_pict_type &&
                    p_ctx->session_statistic.ui32FramesDropped > 0 &&
                    ts_diff > 0)
                {
                    while (p_frame->dts < p_frame->pts &&
                           labs(p_frame->pts - p_frame->dts) > ts_diff)
                    {
                        ni_log2(p_ctx, NI_LOG_DEBUG, "%s: First I frame pts %ld "
                                "dts %ld diff. %ld > ts_diff %ld\n",
                                __func__, p_frame->pts, p_frame->dts,
                                labs(p_frame->pts - p_frame->dts), ts_diff);

                        if (ni_timestamp_get_with_threshold(
                                p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                                XCODER_FRAME_OFFSET_DIFF_THRES, 0, p_ctx->buffer_pool))
                        {
                            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): First I frame "
                                    "pop decoder dts queue error.\n", __func__);
                            break;
                        }
                    }
                }
            }

            p_frame->p_custom_sei_set = p_ctx->pkt_custom_sei_set[i];
            p_ctx->pkt_custom_sei_set[i] = NULL;
        } else
        {
            // backup solution pts
            if (p_param->dec_input_params.skip_pts_guess && p_ctx->last_pts != NI_NOPTS_VALUE)
            {
                // if skip guess_correct_pts, use pts interval to get the correct pts
                p_frame->pts = p_ctx->last_pts + (p_ctx->last_pts_interval > 0 ? p_ctx->last_pts_interval : 1);
            }
            else
            {
                p_frame->pts = p_ctx->last_pts + (p_frame->dts - p_ctx->last_dts);
            }
            p_frame->pkt_pos = p_ctx->last_pkt_pos + (frame_offset - p_ctx->last_frame_offset);
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: Frame pts %" PRId64 " not found for offset "
                   "%" PRIu64 "\n", p_frame->pts, frame_offset);
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "%s: (not found use default) dts %" PRId64 " pts %" PRId64
                   "\n",
                   __func__, p_frame->dts, p_frame->pts);
        }

        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
    }
    p_frame->orignal_pts = p_frame->pts;
    p_ctx->last_pkt_pos = p_frame->pkt_pos;
    p_ctx->last_frame_offset = frame_offset;
    if (!p_param->dec_input_params.skip_pts_guess)
        p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts);
    if (p_frame->pts != NI_NOPTS_VALUE && p_ctx->last_pts != NI_NOPTS_VALUE)
        p_ctx->last_pts_interval = p_frame->pts - p_ctx->last_pts;
    p_ctx->last_pts = p_frame->pts;
    if (p_frame->dts != NI_NOPTS_VALUE && p_ctx->last_dts != NI_NOPTS_VALUE)
        p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    p_ctx->last_dts = p_frame->dts;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: (best_effort_timestamp) pts %" PRId64 "\n",
           __func__, p_frame->pts);
    p_ctx->frame_num++;

    p_frame->error_ratio = p_ctx->session_statistic.ui32FramesErrorRatio;
    if (p_frame->error_ratio > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: frame number = %d, error_ratio = %u\n",
                        __func__, p_ctx->frame_num, p_frame->error_ratio);
    }

#ifdef MEASURE_LATENCY
#ifndef XCODER_311
    ni_log2(p_ctx, NI_LOG_INFO, "DEC pkt_num %d, fme_num %d, latecy is %d\n",
                        p_ctx->pkt_num, p_ctx->frame_num, p_ctx->pkt_num - p_ctx->frame_num);
#endif
#endif

#ifdef XCODER_DUMP_DATA
    char dump_file[256];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-dec-fme/fme-%04ld.yuv",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);
    FILE *f = fopen(dump_file, "wb");
    fwrite(p_frame->p_buffer,
           p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
           1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): received data: [0x%08x]\n", __func__, rx_size);
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): p_frame->data_len[0/1/2]=%u/%u/%u\n", __func__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "Decoder pts queue size = %u  dts queue size = %u\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
      // scan and clean up
      ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue,
                                p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
    uint64_t abs_time_ns = ni_gettime_ns();
    ni_lat_meas_q_t *q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
    ni_log2(p_ctx, NI_LOG_INFO, "DTS:%" PRId64 ",DELTA:%" PRId64 ",dLAT:%" PRIu64 ";\n",
           p_frame->dts, abs_time_ns - q->last_benchmark_time,
           ni_lat_meas_q_check_latency(q, abs_time_ns, p_frame->dts));
    q->last_benchmark_time = abs_time_ns;
  }
#endif

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (get_first_metadata && p_data_buffer)
        ni_aligned_free(p_data_buffer);
    if (sequence_change && p_ctx->frame_num)
    {
        if (p_ctx->actual_video_width ==  p_frame->video_width &&
            p_ctx->active_video_height == p_frame->video_height)
        {
            p_ctx->pixel_format_changed = 1;
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): format changed\n", __func__);
       }
    }

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s(): bad exit, retval = %d\n", __func__, retval);
        if (retval == NI_RETCODE_ERROR_VPU_RECOVERY)
        {
            low_delay_signal(p_ctx);
        }
        return retval;
    } else
    {
        ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit, rx_size = %d\n", __func__, rx_size);
        if (low_delay_notify)
        {
            low_delay_signal(p_ctx);
        }
        return rx_size;
    }
}

/*!******************************************************************************
 *  \brief  Query current xcoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_xcoder_session_query(ni_session_context_t *p_ctx,
                            ni_device_type_t device_type)
{
  ni_instance_mgr_general_status_t data;
  int retval = NI_RETCODE_SUCCESS;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): device_type %d:%s; enter\n", __func__,
         device_type, g_device_type_str[device_type]);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  retval = ni_query_general_status(p_ctx, device_type, &data);
  if (NI_RETCODE_SUCCESS == retval)
  {
    p_ctx->overall_load_query.overall_current_load = (uint32_t)data.process_load_percent_overall;
    p_ctx->overall_load_query.overall_fw_model_load = data.fw_model_load_overall;
    p_ctx->overall_load_query.overall_instance_count = (uint32_t) data.active_sub_instances_cnt_overall;
    p_ctx->overall_load_query.admin_queried = (uint32_t)data.admin_nsid;
    p_ctx->load_query.current_load =
          (uint32_t)data.process_load_percent | (uint32_t)(data.process_load_percent_upper << 8);
    p_ctx->load_query.fw_model_load = (uint32_t)data.fw_model_load;
    p_ctx->load_query.fw_load = (uint32_t)data.fw_load;
    p_ctx->load_query.total_pixel_load = data.ui32UploadRate;

    switch (device_type)
    {
      case NI_DEVICE_TYPE_DECODER:
      case NI_DEVICE_TYPE_ENCODER:
      case NI_DEVICE_TYPE_UPLOAD:
        if (p_ctx->load_query.fw_load == 1 &&
            0 == p_ctx->overall_load_query.overall_instance_count)
        {
          // ignore negligible non-video traffic
          p_ctx->load_query.fw_load -= 1;
        }
        break;
      default:
        break;
    }
    p_ctx->load_query.total_contexts = (uint32_t)data.active_sub_instances_cnt;
    p_ctx->load_query.fw_video_mem_usage = (uint32_t)data.fw_video_mem_usage;
    p_ctx->load_query.fw_video_shared_mem_usage = (uint32_t)data.fw_video_shared_mem_usage;
    p_ctx->load_query.fw_share_mem_usage = (uint32_t)data.fw_share_mem_usage;
    p_ctx->load_query.fw_p2p_mem_usage = (uint32_t)data.fw_p2p_mem_usage;
    p_ctx->load_query.active_hwuploaders =
    (uint32_t)data.active_hwupload_sub_inst_cnt;
    ni_log2(p_ctx, NI_LOG_DEBUG, "%s blk_dev %s blk_xcoder %s dev_xcoder %s current_load:%u model_load:%u fw_load:%u "
           "total_contexts:%u fw_video_mem_usage:%u "
           "fw_video_shared_mem_usage:%u fw_share_mem_usage:%u "
           "fw_p2p_mem_usage:%u active_hwuploaders:%u\n", __func__,
           p_ctx->blk_dev_name,
           p_ctx->blk_xcoder_name,
           p_ctx->dev_xcoder_name,
           p_ctx->load_query.current_load,
           p_ctx->load_query.fw_model_load,
           p_ctx->load_query.fw_load,
           p_ctx->load_query.total_contexts,
           p_ctx->load_query.fw_video_mem_usage,
           p_ctx->load_query.fw_video_shared_mem_usage,
           p_ctx->load_query.fw_share_mem_usage,
           p_ctx->load_query.fw_p2p_mem_usage,
           p_ctx->load_query.active_hwuploaders);
    if (p_ctx->overall_load_query.admin_queried)
    {
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Overall load %u, model load %u, instance count %u\n",
                   p_ctx->overall_load_query.overall_current_load,
                   p_ctx->overall_load_query.overall_fw_model_load,
                   p_ctx->overall_load_query.overall_instance_count);
    }
  }

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query current xcoder status
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_xcoder_session_query_detail(ni_session_context_t *p_ctx,
                            ni_device_type_t device_type, void *detail_data, int ver)
{
  int retval = NI_RETCODE_SUCCESS;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): device_type %d:%s; enter\n", __func__,
         device_type, g_device_type_str[device_type]);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  retval = ni_query_detail_status(p_ctx, device_type, detail_data, ver);

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Open a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/

ni_retcode_t ni_encoder_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_xcoder_params_t *p_param;
  void *p_buffer = NULL;
  ni_instance_buf_info_t buf_info = {0};
  uint32_t ui32LBA = 0;
  uint32_t max_cu_size;
  uint32_t block_size;
  int32_t width, height;
  int m_threshold = 0;
  char fmt_fw_api_ver1[5], fmt_fw_api_ver2[5];

  if (!p_ctx || !p_ctx->p_session_config)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  bool isrgba = (p_ctx->pixel_format == NI_PIX_FMT_ABGR || p_ctx->pixel_format == NI_PIX_FMT_ARGB
                      || p_ctx->pixel_format == NI_PIX_FMT_RGBA || p_ctx->pixel_format == NI_PIX_FMT_BGRA);
  if (isrgba)
  {
      if (p_param->source_width > NI_MAX_RESOLUTION_RGBA_WIDTH || p_param->source_height > NI_MAX_RESOLUTION_RGBA_HEIGHT
          ||  p_param->source_width < NI_MIN_WIDTH || p_param->source_height < NI_MIN_HEIGHT)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() RGBA / BGRA / ARGB / AGBR resolution invalid, return\n",
                 __func__);
          retval = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
  }

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter hwframes = %d\n", __func__,
         p_param->hwframes);

  // calculate encoder ROI map size: each QP info takes 8-bit, represent 8 x 8
  // pixel block
  max_cu_size = (NI_CODEC_FORMAT_H264 == p_ctx->codec_format) ? 16 : 64;

  width = p_param->source_width;
  height = p_param->source_height;
  // AV1 non-8x8-aligned resolution is implicitly cropped due to Quadra HW limitation
  if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
  {
    width = (width / 8) * 8;
    height = (height / 8) * 8;
  }

  block_size =
      ((width + max_cu_size - 1) & (~(max_cu_size - 1))) *
      ((height + max_cu_size - 1) & (~(max_cu_size - 1))) /
      (8 * 8);
  p_ctx->roi_len = ((block_size + 63) & (~63));   // align to 64 bytes

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): finish init\n", __func__);

  //Check if there is an instance or we need a new one
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_ENCODER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->keyframe_factor = 1;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->av1_pkt_num = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->force_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->auto_dl_handle = 0;
    //Sequence change tracking related stuff
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->p_all_zero_buf = NULL;
    p_ctx->actual_video_width = 0;
    p_ctx->enc_pts_w_idx = 0;
    p_ctx->enc_pts_r_idx = 0;
    p_ctx->session_timestamp = 0;
    memset(p_ctx->pkt_custom_sei_set, 0, NI_FIFO_SZ * sizeof(ni_custom_sei_set_t *));
    memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));
    if (p_ctx->session_run_state != SESSION_RUN_STATE_SEQ_CHANGE_DRAINING)
    {
        p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
    }

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        ni_aligned_free(p_ctx->p_all_zero_buf);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_ENCODER,
                                 ni_htonl(p_ctx->codec_format), 0);
    ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer,
                          NI_DATA_BUFFER_LEN, ui32LBA);
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id =
        ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp = ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_high);
    p_ctx->session_timestamp = (p_ctx->session_timestamp << 32) |
        ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_low);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64 ", "
               "p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        ni_encoder_session_close(p_ctx, 0);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "Encoder open session ID:0x%x timestamp:%" PRIu64 "\n",
                   p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW API version is >= 6.2
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "62") >= 0)
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX], &fmt_fw_api_ver1[0]);
        ni_fmt_fw_api_ver_str((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],  &fmt_fw_api_ver2[0]);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s libxcoder FW API ver %s, FW FW API ver %s\n",
              __func__, fmt_fw_api_ver1, fmt_fw_api_ver2);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rc") >= 0)
        {
          // For FW API ver 6rc or newer, initialize with the most current size
          p_ctx->meta_size = sizeof(ni_metadata_enc_bstream_t);
        }
        else if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6p") >= 0)
          // For FW API ver 6.p or newer, initialize with the most current size
          p_ctx->meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE_UNDER_MAJOR_6_MINOR_rc;
        else
          // For FW API ver 6.2 or newer, initialize with the most current size
          p_ctx->meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE_UNDER_MAJOR_6_MINOR_o;
    }
    else
    {
        // For FW API ver 6.1 or older, initialize with metadata size 32
        p_ctx->meta_size = NI_FW_ENC_BITSTREAM_META_DATA_SIZE;
    }

    // Open AI session for AI Enhance
    if(p_param->enable_ai_enhance)
    {
        // Check if the bit depth & resoultion
        if ((p_ctx->bit_depth_factor != 1) ||
            !((p_param->source_width == 1280 && p_param->source_height == 720) ||
            (p_param->source_width == 1920 && p_param->source_height == 1080) ||
            (p_param->source_width == 3840 && p_param->source_height == 2160) ||
            (p_param->source_width == 720 && p_param->source_height == 1280) ||
            (p_param->source_width == 768 && p_param->source_height == 1280) ||
            (p_param->source_width == 1080 && p_param->source_height == 1920) ||
            (p_param->source_width == 2496 && p_param->source_height == 1080)))
        {
            p_param->enable_ai_enhance = 0;
            ni_log2(p_ctx, NI_LOG_ERROR,  "Resoultion=%dx%d,depth=%d\n",p_param->source_width,p_param->source_height,p_ctx->bit_depth_factor);
            ni_log2(p_ctx, NI_LOG_ERROR,  "The bit depth or resoultion is not supported on ai enhance mode\n");
        }else{
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Adding ai enhance session to encoder\n");
            ui32LBA = OPEN_ADD_CODEC(NI_DEVICE_TYPE_AI, 0, ni_htons(p_ctx->session_id));
            retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                           p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

            CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_open,
                         p_ctx->device_type, p_ctx->hw_id,
                         &(p_ctx->session_id), OPT_1);
            if (NI_RETCODE_SUCCESS != retval)
            {
                ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR couldn't add ai to encoder session\n");
                ni_log2(p_ctx, NI_LOG_ERROR,
                       "ERROR %s(): p_ctx->device_handle=%" PRIx64 ", "
                       "p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
                       __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
                       p_ctx->session_id);
                ni_encoder_session_close(p_ctx, 0);
                LRETURN;
            }
        }
    }

    if (p_param->ddr_priority_mode > NI_DDR_PRIORITY_NONE)
    {
        retval = ni_device_set_ddr_configuration(p_ctx, p_param->ddr_priority_mode);
        if (NI_RETCODE_SUCCESS != retval)
        {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): ddr priority setting failure for %s\n",
                     __func__, strerror(NI_ERRNO));
            LRETURN;
        }
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "Open session completed\n");
  }

  /* Modify mmap_threshold and trim_threshold of malloc */
  // calculate the default threshold which equals to the resolution + padding size.
  // 64 is AV_INPUT_BUFFER_PADDING_SIZE
  m_threshold = p_param->source_width * p_param->source_height * 3 / 2 *
                p_ctx->bit_depth_factor + 64;
  m_threshold = ((m_threshold + (NI_MEM_PAGE_ALIGNMENT - 1)) /
                NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  // The upper limit is DEFAULT_MMAP_THRESHOLD_MAX: 512*1024 on 32-bit systems,
  // or 4*1024*1024*sizeof(long) on  64-bit  systems.
  // This macro is not defined in header file.
  // So, assume that is running on 64-bit systems
  if (m_threshold > 4 * 1024 * 1024 * sizeof(long))
  {
      ni_log2(p_ctx, NI_LOG_INFO, "Warning: m_threshold (%d) is bigger than "
             "DEFAULT_MMAP_THRESHOLD_MAX, use default value (%d)\n",
             m_threshold, 4 * 1024 * 1024 * sizeof(long));
      m_threshold = 4 * 1024 * 1024 * sizeof(long);
  }

  if (p_param->staticMmapThreshold) // Set Static Mmap Threshold
  {
#if defined(__linux__) && !defined(_ANDROID) && !defined(__OPENHARMONY__)
      // If the malloc buffer is larger than the threshold,
      // glibc will use mmap to malloc the memory, which is a low speed method.
      // So, set the M_MMAP_THRESHOLD >= 1 yuv buffer size here.
      if (mallopt(M_MMAP_THRESHOLD, m_threshold) == 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "Error: could not set M_MMAP_THRESHOLD to %d\n",
                 m_threshold);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }

      // When the amount of contiguous free memory at the top of the heap
      // grows larger than M_TRIM_THRESHOLD,
      // free(3) employs sbrk(2) to release this memory back to the system.
      // So, set the M_TRIM_THRESHOLD to 3*m_threshold,
      // to avoid an inefficient case, frequently releasing and requesting physical memory.
      if (mallopt(M_TRIM_THRESHOLD, 3 * m_threshold) == 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "Error: could not set M_TRIM_THRESHOLD to %ds\n",
                 3 * m_threshold);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
#else
      ni_log2(p_ctx, NI_LOG_INFO,
             "Warning: this platform does not support staticMmapThreshold\n");
#endif
  }
  else // default
  {
      // a way to dynamically trigger a pre-mature increase in mmap&trim threshold
      // make the mmap_threshold larger than frame size to avoid frequent page faults
      m_threshold = (int) (m_threshold * 1.25); // An experience value
      void *temp_buf = malloc((size_t) m_threshold);
      ni_log2(p_ctx, NI_LOG_TRACE,  "trigger a pre-mature increase in mmap&trim threshold: 0x%p = malloc(%d)\n",
             temp_buf, m_threshold);
      free(temp_buf);
  }

  if (p_ctx->hw_action == NI_CODEC_HW_ENABLE)
  {
      ni_device_capability_t sender_cap, receiver_cap;
      bool device_in_ctxt = true;
      retval = ni_device_capability_query2(p_ctx->sender_handle, &sender_cap, device_in_ctxt);
      if (retval != NI_RETCODE_SUCCESS)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: ni_device_capability_query2 returned %d\n",
                         retval);
          LRETURN;
      }
      retval = ni_device_capability_query2(p_ctx->blk_io_handle, &receiver_cap, device_in_ctxt);
      if (retval != NI_RETCODE_SUCCESS)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: ni_device_capability_query2 returned %d\n",
                         retval);
          LRETURN;
      }

      for (uint8_t ui8Index = 0; ui8Index < 20; ui8Index++)
      {
          if (sender_cap.serial_number[ui8Index] !=
              receiver_cap.serial_number[ui8Index])
          {
              // QDFW-315 Autodownload
              p_ctx->auto_dl_handle = p_ctx->sender_handle;
              ni_log2(p_ctx, NI_LOG_DEBUG,  "Autodownload device handle set %p!\n",
                     p_ctx->auto_dl_handle);
              p_ctx->hw_action = NI_CODEC_HW_NONE;
              break;
          }
          else
          {
              if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                    "6m") >= 0)
              {
                  ni_device_vf_ns_id_t sender_vf_ns_id = {0};
                  ni_device_vf_ns_id_t curr_vf_ns_id = {0};
                  retval = ni_query_vf_ns_id(p_ctx->sender_handle, &sender_vf_ns_id, p_ctx->fw_rev);
                  if (retval != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: calling ni_query_vf_ns_id(): "
                            "p_ctx->sender_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%u\n",
                            (int64_t)p_ctx->sender_handle, p_ctx->hw_id, p_ctx->session_id);
                      ni_encoder_session_close(p_ctx, 0);
                      LRETURN;
                  }
                  retval = ni_query_vf_ns_id(p_ctx->blk_io_handle, &curr_vf_ns_id, p_ctx->fw_rev);
                  if (retval != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: calling ni_query_vf_ns_id(): "
                            "p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%u\n",
                            (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
                      ni_encoder_session_close(p_ctx, 0);
                      LRETURN;
                  }

                  if ((sender_vf_ns_id.ns_id != curr_vf_ns_id.ns_id) ||
                      (sender_vf_ns_id.vf_id != curr_vf_ns_id.vf_id))
                  {
                        p_ctx->auto_dl_handle = p_ctx->sender_handle;
                        ni_log2(p_ctx, NI_LOG_DEBUG,  "Autodownload device handle set %p!\n",
                                 p_ctx->auto_dl_handle);
                        p_ctx->hw_action = NI_CODEC_HW_NONE;
                        break;
                  }
              }
          }
      }
  }

  retval = ni_config_instance_set_encoder_params(p_ctx);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: calling ni_config_instance_set_encoder_params(): "
           "p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
           (int64_t)p_ctx->device_handle, p_ctx->hw_id, p_ctx->session_id);
    ni_encoder_session_close(p_ctx, 0);
    LRETURN;
  }
  if(p_param->enable_ai_enhance)
  {
    retval = ni_ai_query_network_ready(p_ctx);
    if(retval != NI_RETCODE_SUCCESS)
    {
          ni_log2(p_ctx, NI_LOG_ERROR,
                 "ERROR: ni_ai_query_network_ready returned %d\n", retval);
          LRETURN;
    }
  }

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "enc_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "enc_dts");

  // init close caption SEI header and trailer
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_hevc, g_itu_t_t35_cc_sei_hdr_hevc,
         NI_CC_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_cc_sei_hdr_h264, g_itu_t_t35_cc_sei_hdr_h264,
         NI_CC_SEI_HDR_H264_LEN);
  memcpy(p_ctx->sei_trailer, g_sei_trailer, NI_CC_SEI_TRAILER_LEN);
  // init hdr10+ SEI header
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_hevc, g_itu_t_t35_hdr10p_sei_hdr_hevc,
         NI_HDR10P_SEI_HDR_HEVC_LEN);
  memcpy(p_ctx->itu_t_t35_hdr10p_sei_hdr_h264, g_itu_t_t35_hdr10p_sei_hdr_h264,
         NI_HDR10P_SEI_HDR_H264_LEN);

  // query to check the final encoder config status
  for (;;)
  {
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                        NI_DEVICE_TYPE_ENCODER, &buf_info);

    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);

    if (NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT == retval
        || NI_RETCODE_FAILURE == retval)
    {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() line-%d return %d\n", __func__, __LINE__, retval);
      LRETURN;
    }
    else if (buf_info.buf_avail_size > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): buf_avail_size %u\n", __func__,
               buf_info.buf_avail_size);
        break;
    }
    else
    {
        ni_usleep(1000);
    }
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

#ifdef XCODER_DUMP_DATA
  char dir_name[256] = {0};
  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-pkt", (long)getpid(),
           p_ctx->session_id);
  DIR *dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Encoder pkt dump dir created: %s\n", dir_name);
  }

  if(dir){
      closedir(dir);
  }

  snprintf(dir_name, sizeof(dir_name), "%ld-%u-enc-fme", (long)getpid(),
           p_ctx->session_id);
  dir = opendir(dir_name);
  if (!dir && ENOENT == NI_ERRNO)
  {
      mkdir(dir_name, S_IRWXU | S_IRWXG | S_IRWXO);
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Encoder frame dump dir created: %s\n", dir_name);
  }
  if(dir){
      closedir(dir);
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Flush encoder output
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_send_eos(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval;
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);
  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);

END:

  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): %d, return\n", __func__, retval);
  }

  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Close a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_close(ni_session_context_t* p_ctx, int eos_recieved)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  int i = 0;
  ni_xcoder_params_t *p_param = NULL;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): Invalid session ID, return.\n", __func__);
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
  }

  if (p_ctx->device_type == NI_DEVICE_TYPE_ENCODER)
  {
      ni_log2(p_ctx, NI_LOG_INFO,
             "Encoder_complete_info:session_id 0x%x, total frames input:%u "
             "buffered: %u completed: %u output: %u dropped: %u error: %u\n",
             p_ctx->session_id, p_ctx->session_statistic.ui32FramesInput,
             p_ctx->session_statistic.ui32FramesBuffered,
             p_ctx->session_statistic.ui32FramesCompleted,
             p_ctx->session_statistic.ui32FramesOutput,
             p_ctx->session_statistic.ui32FramesDropped,
             p_ctx->session_statistic.ui32InstErrors);
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %d: %s() alloc data buffer failed\n",
             NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  if (p_ctx->p_session_config)
  {
      p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
      if (p_param->ddr_priority_mode > NI_DDR_PRIORITY_NONE)
      {
          retval = ni_device_set_ddr_configuration(p_ctx, NI_DDR_PRIORITY_RESET);
          if (NI_RETCODE_SUCCESS != retval)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): ddr priority setting failure for %s\n",
                     __func__, strerror(NI_ERRNO));
              LRETURN;
          }
      }
  }

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): command failed\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          p_ctx->session_id = NI_INVALID_SESSION_ID;
          break;
      } else
      {
        //Close should always succeed
        retval = NI_RETCODE_SUCCESS;
        p_ctx->session_id = NI_INVALID_SESSION_ID;
        break;
      }
    /*
    else if(((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): wait for close\n");
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
    retry++;
  }

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);

    //Sequence change related stuff cleanup here
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;
    //End of sequence change related stuff cleanup

    if (p_ctx->pts_table)
    {
        ni_queue_free(&p_ctx->pts_table->list, p_ctx->buffer_pool);
        ni_memfree(p_ctx->pts_table);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "ni_timestamp_done: success\n");
    }

    if (p_ctx->dts_queue)
    {
        ni_queue_free(&p_ctx->dts_queue->list, p_ctx->buffer_pool);
        ni_memfree(p_ctx->dts_queue);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "ni_timestamp_done: success\n");
    }

    ni_buffer_pool_free(p_ctx->buffer_pool);
    p_ctx->buffer_pool = NULL;

    for (i = 0; i < NI_FIFO_SZ; i++)
    {
        ni_memfree(p_ctx->pkt_custom_sei_set[i]);
    }

    for (i = 0; i < 120 ; i++)
    {
      if (p_ctx->input_frame_fifo[i].p_input_buffer != NULL)
      {
        free(p_ctx->input_frame_fifo[i].p_input_buffer);
        p_ctx->input_frame_fifo[i].p_input_buffer = NULL;
      }
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

  low_delay_signal(p_ctx);
  ni_pthread_mutex_unlock(&p_ctx->mutex);

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

  return retval;
}


void enqueue_ni_frame(ni_session_context_t *p_ctx, ni_frame_t *ni_frame, int32_t source_width, int32_t source_height)
{
  int32_t width_stride[4]  = {0};
  int32_t height_stride[4] = {0};

  if (((ni_xcoder_params_t *)(p_ctx->p_session_config))->zerocopy_mode &&
      ((ni_xcoder_params_t *)(p_ctx->p_session_config))->luma_linesize != 0 &&
      ((ni_xcoder_params_t *)(p_ctx->p_session_config))->chroma_linesize != 0)
  {
    //zero copy happend
    width_stride[0] = ((ni_xcoder_params_t *)(p_ctx->p_session_config))->luma_linesize;
    width_stride[1] = width_stride[2] = ((ni_xcoder_params_t *)(p_ctx->p_session_config))->chroma_linesize;
  }
  else
  {
    int is_semiplanar = !ni_get_planar_from_pixfmt(p_ctx->pixel_format);
    ni_get_hw_yuv420p_dim(ni_frame->video_width, ni_frame->video_height,
                          p_ctx->bit_depth_factor, is_semiplanar,
                          width_stride, height_stride);
  }

  for (int i = 0; i < 120; i++)
  {
    // ni_log2(p_ctx, NI_LOG_ERROR, "%s %d i %d frame_num %d p_input_buffer %p data_len[0:2] %d %d %d p_data[0:2] %p %p %p video_width %d video_height %d\n",
      // __FUNCTION__, __LINE__, i, p_ctx->frame_num, p_ctx->input_frame_fifo[i].p_input_buffer,
      // ni_frame->data_len[0], ni_frame->data_len[1], ni_frame->data_len[2],
      // ni_frame->p_data[0], ni_frame->p_data[1], ni_frame->p_data[2],
      // ni_frame->video_width, ni_frame->video_height);
    if ((source_width != p_ctx->input_frame_fifo[i].video_width ||
         source_height != p_ctx->input_frame_fifo[i].video_height) &&
         p_ctx->input_frame_fifo[i].p_input_buffer != NULL &&
         p_ctx->input_frame_fifo[i].usable == 1)
    {
      free(p_ctx->input_frame_fifo[i].p_input_buffer);
      p_ctx->input_frame_fifo[i].p_input_buffer = NULL;
      p_ctx->input_frame_fifo[i].usable = -1;
    }

    if (p_ctx->input_frame_fifo[i].p_input_buffer == NULL)
    {
      p_ctx->input_frame_fifo[i].p_input_buffer = (uint8_t *)malloc(source_width * p_ctx->bit_depth_factor * source_height * 3 / 2);

      for (int j = 0; j < source_height; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * j,
              ni_frame->p_data[0] + j * width_stride[0],
              source_width * p_ctx->bit_depth_factor);
      }
      for (int j = 0; j < (source_height + 1) / 2; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * source_height + (source_width * p_ctx->bit_depth_factor / 2) * j,
              ni_frame->p_data[1] + j * width_stride[1],
              source_width / 2 * p_ctx->bit_depth_factor);
      }
      for (int j = 0; j < (source_height + 1) / 2; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * source_height * 5 / 4 + (source_width * p_ctx->bit_depth_factor / 2) * j,
              ni_frame->p_data[2] + j * width_stride[2],
              source_width / 2 * p_ctx->bit_depth_factor);
      }

      p_ctx->input_frame_fifo[i].video_width  = source_width;
      p_ctx->input_frame_fifo[i].video_height = source_height;

      p_ctx->input_frame_fifo[i].usable = -1;
      p_ctx->input_frame_fifo[i].pts = ni_frame->pts;
      //  ni_log2(p_ctx, NI_LOG_DEBUG, "%s %d i %d frame_num %d buffer_size %d video_width %d video_height %d pts %lld\n",
            //  __FUNCTION__, __LINE__, i, p_ctx->frame_num, ni_frame->buffer_size, ni_frame->video_width, ni_frame->video_height,
            //  p_ctx->input_frame_fifo[i].pts);
      break;
    }
    else if (p_ctx->input_frame_fifo[i].usable == 1)
    {
      for (int j = 0; j < source_height; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * j,
              ni_frame->p_data[0] + j * width_stride[0],
              source_width * p_ctx->bit_depth_factor);
      }
      for (int j = 0; j < (source_height + 1) / 2; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * source_height + (source_width * p_ctx->bit_depth_factor / 2) * j,
              ni_frame->p_data[1] + j * width_stride[1],
              source_width / 2 * p_ctx->bit_depth_factor);
      }
      for (int j = 0; j < (source_height + 1) / 2; j++)
      {
        memcpy(p_ctx->input_frame_fifo[i].p_input_buffer + source_width * p_ctx->bit_depth_factor * source_height * 5 / 4 + (source_width * p_ctx->bit_depth_factor / 2) * j,
              ni_frame->p_data[2] + j * width_stride[2],
              source_width / 2 * p_ctx->bit_depth_factor);
      }

      p_ctx->input_frame_fifo[i].video_width  = source_width;
      p_ctx->input_frame_fifo[i].video_height = source_height;

      p_ctx->input_frame_fifo[i].usable = -1;
      p_ctx->input_frame_fifo[i].pts = ni_frame->pts;
      break;
    }
  }
}

/*!******************************************************************************
 *  \brief  Send a YUV p_frame to encoder
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_encoder_session_write(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
  bool ishwframe = false;
  uint32_t size = 0;
  uint32_t send_count = 0;
  uint32_t i = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  int retval = 0;
  ni_instance_buf_info_t buf_info = { 0 };
  ni_session_statistic_t sessionStatistic = {0};

  if (!p_ctx || !p_frame)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }
  ishwframe = p_ctx->hw_action & NI_CODEC_HW_ENABLE;
  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): enter hw=%d\n", __func__, ishwframe);

  if (ishwframe && !p_frame->end_of_stream)
  {
    // check if the hw input frame is valid
    if (!p_frame->p_data[3])
    {
      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() hw input frame is null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
    }
    uint16_t input_frame_idx = ((niFrameSurface1_t*)(p_frame->p_data[3]))->ui16FrameIdx;
    if (!((input_frame_idx > 0 &&
          input_frame_idx < NI_GET_MAX_HWDESC_FRAME_INDEX(p_ctx->ddr_config)) ||
          (input_frame_idx > NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config) &&
           input_frame_idx <= NI_GET_MAX_HWDESC_P2P_BUF_ID(p_ctx->ddr_config))))
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): got invalid frameIdx [%u]\n",
             __func__, input_frame_idx);
      return NI_RETCODE_INVALID_PARAM;
    }
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  uint8_t separate_metadata = p_frame->separate_metadata;

  uint8_t separate_start = (p_frame->separate_start && p_frame->total_start_len) ? 1 : 0;

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  low_delay_wait(p_ctx);

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      uint64_t abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_t *frame_time_q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
      ni_lat_meas_q_add_entry(frame_time_q, abs_time_ns, p_frame->dts);
  }
#endif

  /*!********************************************************************/
  /*!************ Sequence Change related stuff *************************/
  //First check squence changed related stuff.
  //We need to record the current hight/width params if we didn't do it before:

  if( p_frame->video_height)
  {
    p_ctx->active_video_width = p_frame->data_len[0] / p_frame->video_height;
    p_ctx->active_video_height = p_frame->video_height;
  }
  else if (p_frame->video_width)
  {
    ni_log2(p_ctx, NI_LOG_DEBUG,  "WARNING: passed video_height is not valid!, return\n");
    p_ctx->active_video_height = p_frame->data_len[0] / p_frame->video_width;
    p_ctx->active_video_width = p_frame->video_width;
  }
  else
  {
    ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: passed video_height and video_width are not valid!, return\n");
    retval = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }
  // record actual width (in pixels / without padding) for sequnce change detection
  p_ctx->actual_video_width = p_frame->video_width;

  /*!************ Sequence Change related stuff end*************************/
  /*!********************************************************************/

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + p_frame->extra_data_len;
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s: data_len[0] %u data_len[1] %u "
         "data_len[2] %u extra_data_len %u frame_size_bytes %u\n",
         __func__, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2], p_frame->extra_data_len, frame_size_bytes);

  // skip query write buffer because we just send EOS
  if (!p_frame->end_of_stream)
  {
      for (;;)
      {
          query_sleep(p_ctx);

          if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                "65") >= 0)
          {
              retval = ni_query_session_statistic_info(
                  p_ctx, NI_DEVICE_TYPE_ENCODER, &sessionStatistic);
              CHECK_ERR_RC(p_ctx, retval, &sessionStatistic,
                            nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                            p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
              CHECK_VPU_RECOVERY(retval);
              buf_info.buf_avail_size = sessionStatistic.ui32WrBufAvailSize;
          } else
          {
              retval =
                  ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                             NI_DEVICE_TYPE_ENCODER, &buf_info);
              CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                           p_ctx->device_type, p_ctx->hw_id,
                           &(p_ctx->session_id), OPT_1);
              CHECK_VPU_RECOVERY(retval);
          }

          if (NI_RETCODE_FAILURE == retval)
          {
            ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR: %s() line-%d retrun %d\n", __func__, __LINE__, retval);
            LRETURN;
          }
          else if (NI_RETCODE_SUCCESS != retval ||
              buf_info.buf_avail_size < frame_size_bytes)
          {
              ni_log2(p_ctx, NI_LOG_TRACE,
                     "Enc write query retry %d. rc=%d. Available buf size %u < "
                     "frame size %u\n", retval, send_count,
                     buf_info.buf_avail_size, frame_size_bytes);
              if (send_count >= NI_MAX_ENCODER_QUERY_RETRIES)
              {
                  int retval_backup = retval;
                  retval = ni_query_instance_buf_info(
                      p_ctx, INST_BUF_INFO_RW_WRITE_BY_EP, NI_DEVICE_TYPE_ENCODER, &buf_info);
                  if(NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION != retval)
                  {
                    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                      p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
                    CHECK_VPU_RECOVERY(retval);
                  }
                  else
                    retval = NI_RETCODE_SUCCESS;

                  ni_log2(p_ctx, NI_LOG_DEBUG,
                         "Enc write query buf info exceeded max retries: "
                         "%d, rc=%d. Available buf size %u < frame size %u\n",
                         NI_MAX_ENCODER_QUERY_RETRIES, retval_backup,
                         buf_info.buf_avail_size, frame_size_bytes);
                  p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;

                  LRETURN;
              }
              send_count++;
              ni_pthread_mutex_unlock(&p_ctx->mutex);
              ni_usleep(NI_RETRY_INTERVAL_100US);
              ni_pthread_mutex_lock(&p_ctx->mutex);
          } else
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,
                     "Info enc write query success, available buf "
                     "size %u >= frame size %u !\n",
                     buf_info.buf_avail_size, frame_size_bytes);
              break;
          }
    }
  }

  // fill in metadata such as timestamp
  ni_metadata_enc_frame_t *p_meta;
  if (separate_metadata)
  {
      p_meta = (ni_metadata_enc_frame_t *)p_frame->p_metadata_buffer;
  } else
  {
      p_meta = (ni_metadata_enc_frame_t *)((uint8_t *)
                                               p_frame->p_data[2 + ishwframe] +
                                           p_frame->data_len[2 + ishwframe]);
  }

  if (p_meta) //When hwframe xcoding reaches eos, frame looks like swframe but no allocation for p_meta
  {
    p_meta->metadata_common.ui64_data.frame_tstamp = (uint64_t)p_frame->pts;

    p_meta->force_headers = 0;   // p_frame->force_headers not implemented/used
    p_meta->use_cur_src_as_long_term_pic =
        p_frame->use_cur_src_as_long_term_pic;
    p_meta->use_long_term_ref = p_frame->use_long_term_ref;

    ni_log2(p_ctx, NI_LOG_DEBUG,
           "%s: p_meta "
           "use_cur_src_as_long_term_pic %d use_long_term_ref %d\n",
           __func__, p_meta->use_cur_src_as_long_term_pic,
           p_meta->use_long_term_ref);

    p_meta->frame_force_type_enable = p_meta->frame_force_type = 0;
    // frame type to be forced to is supposed to be set correctly
    // in p_frame->ni_pict_type
    if (1 == p_ctx->force_frame_type || p_frame->force_key_frame)
    {
      if (p_frame->ni_pict_type)
      {
        p_meta->frame_force_type_enable = 1;
        p_meta->frame_force_type = p_frame->ni_pict_type;
      }
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s(): ctx->force_frame_type"
             " %d frame->force_key_frame %d force frame_num %" PRIu64 ""
             " type to %d\n",
             __func__, p_ctx->force_frame_type, p_frame->force_key_frame,
             p_ctx->frame_num, p_frame->ni_pict_type);
    }

    // force pic qp if specified
    p_meta->force_pic_qp_enable = p_meta->force_pic_qp_i =
      p_meta->force_pic_qp_p = p_meta->force_pic_qp_b = 0;
    if (p_frame->force_pic_qp)
    {
      p_meta->force_pic_qp_enable = 1;
      p_meta->force_pic_qp_i = p_meta->force_pic_qp_p =
        p_meta->force_pic_qp_b = p_frame->force_pic_qp;
    }
    p_meta->frame_sei_data_size = p_frame->sei_total_len;
    p_meta->frame_roi_map_size = p_frame->roi_len;
    p_meta->frame_roi_avg_qp = p_ctx->roi_avg_qp;
    p_meta->enc_reconfig_data_size = p_frame->reconf_len;

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6Q") >= 0)
    {
        if (separate_start)
        {
            for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
                p_meta->start_len[i] = p_frame->start_len[i];
        }
        else
        {
            memset(p_meta->start_len, 0, sizeof(p_meta->start_len));
        }
        p_meta->inconsecutive_transfer = p_frame->inconsecutive_transfer;
    }

    if (p_ctx->frame_num % ((ni_xcoder_params_t *)(p_ctx->p_session_config))->interval_of_psnr == 0)
      p_meta->get_recon_frame_mode = ((ni_xcoder_params_t *)(p_ctx->p_session_config))->cfg_enc_params.get_psnr_mode;
    else
      p_meta->get_recon_frame_mode = 3;

    ni_log2(p_ctx,
        NI_LOG_DEBUG,
        "%s(): %d.%u p_ctx->frame_num=%" PRIu64 ", "
        "p_frame->start_of_stream=%u, p_frame->end_of_stream=%u, "
        "p_frame->video_width=%u, p_frame->video_height=%u, pts=0x%08x 0x%08x, "
        "dts=0x%08x 0x%08x, sei_len=%u, roi size=%u avg_qp=%u reconf_len=%u "
        "force_pic_qp=%u use_cur_src_as_long_term_pic %u use_long_term_ref "
        "%u start_len [%u,%u,%u] inconsecutive_transfer %u "
        "get_recon_frame_mode %u\n",
        __func__, p_ctx->hw_id, p_ctx->session_id, p_ctx->frame_num,
        p_frame->start_of_stream, p_frame->end_of_stream, p_frame->video_width,
        p_frame->video_height, (uint32_t)((p_frame->pts >> 32) & 0xFFFFFFFF),
        (uint32_t)(p_frame->pts & 0xFFFFFFFF),
        (uint32_t)((p_frame->dts >> 32) & 0xFFFFFFFF),
        (uint32_t)(p_frame->dts & 0xFFFFFFFF), p_meta->frame_sei_data_size,
        p_meta->frame_roi_map_size, p_meta->frame_roi_avg_qp,
        p_meta->enc_reconfig_data_size, p_meta->force_pic_qp_i,
        p_meta->use_cur_src_as_long_term_pic, p_meta->use_long_term_ref,
        p_meta->start_len[0], p_meta->start_len[1], p_meta->start_len[2],
        p_meta->inconsecutive_transfer,
        p_meta->get_recon_frame_mode);
  }
  if (p_frame->start_of_stream)
  {
    //Send Start of stream p_config command here
    retval = ni_config_instance_sos(p_ctx, NI_DEVICE_TYPE_ENCODER);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);
    if (NI_RETCODE_SUCCESS != retval)
    {
      LRETURN;
    }

    p_frame->start_of_stream = 0;
  }

  // skip direct to send eos without sending the passed in p_frame as it's been sent already
  if (p_frame->end_of_stream)
  {
      retval = ni_config_instance_eos(p_ctx, NI_DEVICE_TYPE_ENCODER);
      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
      CHECK_VPU_RECOVERY(retval);
      if (NI_RETCODE_SUCCESS != retval)
      {
          LRETURN;
      }

      p_frame->end_of_stream = 0;
      p_ctx->ready_to_close = 1;
  } else   //handle regular frame sending
  {
      retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue,
                                     p_frame->dts, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,
                 "ERROR %s(): "
                 "ni_timestamp_register() for dts returned: %d\n",
                 __func__, retval);
      }

      if (separate_metadata)
      {
          uint32_t ui32LBA_metadata =
              WRITE_METADATA_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_metadata_buffer = %p, metadata_buffer_size "
                 "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_metadata_buffer,
                 p_frame->metadata_buffer_size, p_ctx->frame_num,
                 ui32LBA_metadata);

          sent_size =
              ((p_frame->metadata_buffer_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(
              p_ctx->blk_io_handle, p_ctx->event_handle,
              p_frame->p_metadata_buffer, sent_size,
              ui32LBA_metadata);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n",
                     __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      if (separate_start)
      {
          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_start_buffer = %p, p_frame->start_buffer_size "
                 "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_start_buffer, p_frame->start_buffer_size, p_ctx->frame_num,
                 ui32LBA);

          sent_size =
              ((p_frame->start_buffer_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                          p_frame->p_start_buffer, sent_size, ui32LBA);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n", __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      if (p_frame->inconsecutive_transfer)
      {
          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

          for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,
                     "%s: p_data = %p, p_frame->buffer_size "
                     "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                     __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
                     ui32LBA);

              if (p_frame->data_len[i])
              {
                  sent_size = p_frame->data_len[i];
                  if (separate_start)
                    sent_size -= p_frame->start_len[i];

                  sent_size =
                      ((sent_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

                  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                                  p_frame->p_data[i]+p_frame->start_len[i], sent_size, ui32LBA);
                  CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
                  CHECK_VPU_RECOVERY(retval);
                  if (retval < 0)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n", __func__);
                      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                      LRETURN;
                  }
              }
          }
      }
      else
      {
          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_data = %p, p_frame->buffer_size "
                 "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
                 ui32LBA);

          sent_size = frame_size_bytes;
          if (separate_metadata)
            sent_size -= p_frame->extra_data_len;
          if (separate_start)
            sent_size -= p_frame->total_start_len;

          sent_size =
              ((sent_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                          p_frame->p_buffer+p_frame->total_start_len, sent_size, ui32LBA);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR,  "ERROR %s(): nvme command failed\n", __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      //Save input frame data used for calculate PSNR
      if ((ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rc") >= 0) &&
          (p_ctx->frame_num == 0 || ((p_ctx->frame_num  % ((ni_xcoder_params_t *)(p_ctx->p_session_config))->interval_of_psnr) == 0)) &&
          (((ni_xcoder_params_t *)(p_ctx->p_session_config))->cfg_enc_params.get_psnr_mode < 3) &&
          (!(((ni_xcoder_params_t *)(p_ctx->p_session_config))->cfg_enc_params.get_psnr_mode == 2 && p_ctx->codec_format == NI_CODEC_FORMAT_H265)))
      {
          if (ishwframe && !p_frame->end_of_stream)
          {
            // uint16_t input_frame_idx = ((niFrameSurface1_t*)(p_frame->p_data[3]))->ui16FrameIdx;
            // use hwdownload to download hw frame, but donot recycle hwframe buffer
            ni_session_data_io_t hwdl_session_data = {0};
            // ni_log2(p_ctx, NI_LOG_ERROR, "pixel_format %d\n", p_ctx->pixel_format);
            int ret = hwdl_frame(p_ctx, &hwdl_session_data, p_frame,
                        p_frame->pixel_format);
            if (ret <= 0)
            {
              ni_frame_buffer_free(&(hwdl_session_data.data.frame));
              return ret;
            }

            hwdl_session_data.data.frame.pts = p_frame->pts;
            enqueue_ni_frame(p_ctx, &hwdl_session_data.data.frame, p_frame->video_width, p_frame->video_height);
            ni_frame_buffer_free(&hwdl_session_data.data.frame);
          }
          else
          {
            enqueue_ni_frame(p_ctx, p_frame, p_frame->video_width, p_frame->video_height);
          }
      }

      if (ishwframe)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s(): session=0x%x ui16FrameIdx=%u\n",
                 __func__,
                 p_ctx->session_id,
                 ((niFrameSurface1_t*)(p_frame->p_data[3]))->ui16FrameIdx);
      }

      p_ctx->status = 0;
      p_ctx->frame_num++;
      size = frame_size_bytes;
      p_ctx->low_delay_sync_flag = 1;

#ifdef XCODER_DUMP_DATA
      char dump_file[256];
      snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-fme/fme-%04ld.yuv",
               (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

      FILE *f = fopen(dump_file, "wb");
      fwrite(p_frame->p_buffer,
             p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
             1, f);
      fflush(f);
      fclose(f);
#endif
  }

  retval = size;

END:

  ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}


int ni_encoder_session_read(ni_session_context_t* p_ctx, ni_packet_t* p_packet)
{
  ni_instance_mgr_stream_info_t data = { 0 };
  uint32_t actual_read_size = 0;
  uint32_t to_read_size = 0;
  int size = 0;
  static long long encq_count = 0LL;
  int retval = NI_RETCODE_SUCCESS;
  int query_retry = 0;
  int query_type = INST_BUF_INFO_RW_READ;
  uint32_t ui32LBA = 0;
  ni_metadata_enc_bstream_t *p_meta = NULL;
  ni_metadata_enc_bstream_rev61_t *p_meta_rev61 = NULL;
  ni_instance_buf_info_t buf_info = { 0 };
  ni_session_statistic_t sessionStatistic = {0};
  int low_delay_notify = 0;
  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): enter\n", __func__);

  if (!p_ctx || !p_packet || !p_packet->p_data)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    ni_log2(p_ctx, NI_LOG_TRACE, "%s(): exit\n", __func__);

    return retval;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 ", av1_pkt_num=%" PRIu64 "\n",
                 p_ctx->frame_num, p_ctx->pkt_num, p_ctx->av1_pkt_num);
  if (((ni_xcoder_params_t *)p_ctx->p_session_config)->low_delay_mode)
  {
      query_type = INST_BUF_INFO_RW_READ_BUSY;
      if (!p_packet->end_of_stream && p_ctx->frame_num < p_ctx->pkt_num)
      {
          if (p_ctx->ready_to_close)
          {
              query_type = INST_BUF_INFO_RW_READ;
          } else
          {   //nothing to query, leave
              retval = NI_RETCODE_SUCCESS;
              low_delay_notify = 1;
              LRETURN;
          }
      }
  }
  for (;;)
  {
      query_sleep(p_ctx);

      query_retry++;

      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "65") >= 0)
      {
          retval = ni_query_session_statistic_info(
              p_ctx, NI_DEVICE_TYPE_ENCODER, &sessionStatistic);
          CHECK_ERR_RC(p_ctx, retval, &sessionStatistic,
                        nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                        p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
          CHECK_VPU_RECOVERY(retval);

          buf_info.buf_avail_size = sessionStatistic.ui32RdBufAvailSize;

          if (((ni_xcoder_params_t *)p_ctx->p_session_config)->cfg_enc_params.lookAheadDepth)
          {
            if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                  "6rX") >= 0)
            {
              if (p_ctx->current_frame_delay < (int)sessionStatistic.ui8AdditionalFramesDelay + p_ctx->initial_frame_delay)
              {
                p_ctx->current_frame_delay = (int)sessionStatistic.ui8AdditionalFramesDelay + p_ctx->initial_frame_delay; // extend frames delay by FW estimated additional number of frames
              }
              ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: initial_frame_delay %d max_frame_delay %d ui8AdditionalFramesDelay %u current_frame_delay %d\n",
                             __FUNCTION__, p_ctx->initial_frame_delay, p_ctx->max_frame_delay, sessionStatistic.ui8AdditionalFramesDelay, p_ctx->current_frame_delay);
            }
          }
      } else
      {
          retval = ni_query_instance_buf_info(
              p_ctx, query_type, NI_DEVICE_TYPE_ENCODER, &buf_info);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                       p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
      }

      ni_log2(p_ctx, NI_LOG_TRACE,
             "Info enc read query rc %d, available buf size %u, "
             "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 "\n",
             retval, buf_info.buf_avail_size, p_ctx->frame_num, p_ctx->pkt_num);

      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log2(p_ctx, NI_LOG_ERROR, "Buffer info query failed in encoder read!!!!\n");
          LRETURN;
      } else if (0 == buf_info.buf_avail_size)
    {
      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_ENCODER, &data);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id,
                     &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log2(p_ctx, NI_LOG_ERROR, "Stream info query failed in encoder read!!!!\n");
          LRETURN;
        }

        if (data.is_flushed)
        {
            p_packet->end_of_stream = 1;
        }
      }
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Info enc read available buf size %u, eos %u !\n",
                     buf_info.buf_avail_size, p_packet->end_of_stream);

      if ((((ni_xcoder_params_t *)p_ctx->p_session_config)->low_delay_mode ||
           (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status)) &&
          !p_packet->end_of_stream && p_ctx->frame_num >= p_ctx->pkt_num)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,  "Encoder low latency mode, eos not sent, frame_num "
                         "%" PRIu64 " >= %" PRIu64 " pkt_num, keep querying p_ctx->status %d\n",
                         p_ctx->frame_num, p_ctx->pkt_num, p_ctx->status);
          ni_pthread_mutex_unlock(&p_ctx->mutex);
          ni_usleep(NI_RETRY_INTERVAL_200US);
          ni_pthread_mutex_lock(&p_ctx->mutex);
          if (query_retry >= NI_MAX_ENCODER_QUERY_RETRIES &&
              NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status)
          {
            low_delay_notify = 1;
            retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
            LRETURN;
          }
          else
          {
            continue;
          }
      }
      else if (((ni_xcoder_params_t *)p_ctx->p_session_config)->minFramesDelay)
      {
          if (p_ctx->pkt_num) // do not busy read until header is received
          {
              uint64_t pkt_num = (p_ctx->codec_format == NI_CODEC_FORMAT_AV1) ? p_ctx->av1_pkt_num : p_ctx->pkt_num - 1; // deduct header from pkt_num
              if (p_ctx->frame_num - pkt_num >= p_ctx->current_frame_delay &&
                  !p_packet->end_of_stream)
              {
                  ni_log2(p_ctx, NI_LOG_TRACE,  "%s: low delay mode 2, keep reading send frame %d receive pkt %d gop %d current_frame_delay %d\n",
                          __FUNCTION__, p_ctx->frame_num, pkt_num,
                          p_ctx->last_gop_size, p_ctx->current_frame_delay);
                  continue;
              }
          }
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    } else
    {
        break;
    }
  }
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Encoder read buf_avail_size %u\n", buf_info.buf_avail_size);

  to_read_size = buf_info.buf_avail_size;

  p_packet->data_len = 0;
  p_packet->pts = NI_NOPTS_VALUE;
  p_packet->dts = 0;

  p_packet->psnr_y = 0;
  p_packet->psnr_u = 0;
  p_packet->psnr_v = 0;
  p_packet->average_psnr = 0;
  p_packet->ssim_y = 0;
  p_packet->ssim_u = 0;
  p_packet->ssim_v = 0;

  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
  actual_read_size = to_read_size;
  if (actual_read_size % NI_MEM_PAGE_ALIGNMENT)
  {
      actual_read_size =
          ((actual_read_size / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) +
          NI_MEM_PAGE_ALIGNMENT;
  }

  if (p_packet->buffer_size < actual_read_size)
  {
      if (ni_packet_buffer_alloc(p_packet, actual_read_size))
      {
          ni_log2(p_ctx, NI_LOG_ERROR,
                 "ERROR %s(): packet buffer size %u allocation "
                 "failed\n",
                 __func__, actual_read_size);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
  }

  retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 p_packet->p_data, actual_read_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  // SSIM is supported if fw_rev is >= 6.2
  if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                        "62") >= 0)
  {
      p_meta = (ni_metadata_enc_bstream_t *)p_packet->p_data;
      p_packet->pts = (int64_t)(p_meta->frame_tstamp);
      p_packet->frame_type = p_meta->frame_type;
      p_packet->avg_frame_qp = p_meta->avg_frame_qp;
      p_packet->recycle_index = p_meta->recycle_index;
      p_packet->av1_show_frame = p_meta->av1_show_frame;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s RECYCLE INDEX = %u!!!\n", __FUNCTION__, p_meta->recycle_index);
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s MetaDataSize %d FrameType %d AvgFrameQp %d ssim %d %d %d\n",
        __FUNCTION__, p_meta->metadata_size, p_meta->frame_type, p_meta->avg_frame_qp, p_meta->ssimY, p_meta->ssimU, p_meta->ssimV);

      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "6r2") >= 0)
      {
        if (((ni_xcoder_params_t *)p_ctx->p_session_config)->cfg_enc_params.lookAheadDepth)
        {
          if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                "6rX") < 0)
          {
            if (p_meta->gop_size) // ignore frame 0 gop size 0 (other I-frame gop size 1)
            {
              if ((int)p_meta->gop_size < p_ctx->last_gop_size)
              {
                p_ctx->current_frame_delay = p_ctx->max_frame_delay; // shortening gop (including I-frame) causes lookahead queue increase, currently assume worst case frame delay
              }
            }
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: current gop_size %u last_gop_size %d initial_frame_delay %d max_frame_delay %d current_frame_delay %d\n",
            __FUNCTION__, p_meta->gop_size, p_ctx->last_gop_size, p_ctx->initial_frame_delay, p_ctx->max_frame_delay, p_ctx->current_frame_delay);
          }
          p_ctx->last_gop_size = p_meta->gop_size;
        }
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: current gop_size %u\n",
        __FUNCTION__, p_meta->gop_size);
      }

      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "6p") >= 0)
      {
          if (ni_cmp_fw_api_ver((char*)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rm") >= 0)
          {
            p_packet->still_image_detected  = p_meta->ui8StillImage;
            p_packet->scene_change_detected = p_meta->ui8SceneChange;
          }
          else
          {
            p_packet->still_image_detected  = 0;
            p_packet->scene_change_detected = 0;
          }

          if(p_meta->frame_size > 0)
            ni_log2(p_ctx, NI_LOG_DEBUG,  "pkt_num %d max_mv x[0] %d x[1] %d y[0] %d y[1] %d min_mv x[0] %d x[1] %d y[0] %d y[1] %d frame_size %u inter_total_count %u intra_total_count %u stillImage %d scenechange %d\n",
              p_ctx->pkt_num,
              p_meta->max_mv_x[0], p_meta->max_mv_x[1], p_meta->max_mv_y[0], p_meta->max_mv_y[1],
              p_meta->min_mv_x[0], p_meta->min_mv_x[1], p_meta->min_mv_y[0], p_meta->min_mv_y[1],
              p_meta->frame_size, p_meta->inter_total_count, p_meta->intra_total_count,
              p_meta->ui8StillImage, p_meta->ui8SceneChange);
      }

      p_ctx->meta_size = p_meta->metadata_size;

      if (p_meta->ssimY != 0)
      {
         p_packet->ssim_y = (float)p_meta->ssimY/10000;
         p_packet->ssim_u = (float)p_meta->ssimU/10000;
         p_packet->ssim_v = (float)p_meta->ssimV/10000;
          // The SSIM Y, U, V values returned by FW are 4 decimal places multiplied by 10000.
      //Divide by 10000 to get the original value.
          ni_log2(p_ctx,
#ifdef NI_LOG_SSIM_AT_INFO
          NI_LOG_INFO,
#else
          NI_LOG_DEBUG,
#endif
          "%s: pkt #%" PRId64 " pts %" PRId64 " ssim "
                 "Y %.4f U %.4f V %.4f\n", __FUNCTION__, p_ctx->pkt_num,
                 p_packet->pts, (float)p_meta->ssimY/10000,
                 (float)p_meta->ssimU/10000, (float)p_meta->ssimV/10000);
      }
  }
  else
  {
      // Up to fw_rev major 6 and minor 1, use the old meta data structure
      p_meta_rev61 = (ni_metadata_enc_bstream_rev61_t *)p_packet->p_data;
      p_packet->pts = (int64_t)(p_meta_rev61->frame_tstamp);
      p_packet->frame_type = p_meta_rev61->frame_type;
      p_packet->avg_frame_qp = p_meta_rev61->avg_frame_qp;
      p_packet->recycle_index = p_meta_rev61->recycle_index;
      p_packet->av1_show_frame = p_meta_rev61->av1_show_frame;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s RECYCLE INDEX = %u!!!\n", __FUNCTION__, p_meta_rev61->recycle_index);

  }

  p_packet->data_len = to_read_size;

  size = p_packet->data_len;

  if (size > 0)
  {
    if (p_ctx->pkt_num >= 1 &&
        ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6rc") >= 0)
    {
      calculate_psnr(p_ctx, p_packet);
    }

    if (p_ctx->pkt_num >= 1)
    {
      if (NI_CODEC_FORMAT_AV1 != p_ctx->codec_format
          || p_packet->av1_show_frame)
      {
        if (ni_timestamp_get_with_threshold(p_ctx->dts_queue, 0, (int64_t*)& p_packet->dts, 0, encq_count % 500 == 0, p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
        {
          p_packet->dts = NI_NOPTS_VALUE;
        }
      }

      p_ctx->pkt_num++;
      if (p_ctx->codec_format == NI_CODEC_FORMAT_AV1 && p_packet->recycle_index != (uint32_t) NI_AV1_INVALID_BUFFER_INDEX) // av1 invalid buffer index indicates show_existing_frame header packet
      {
        p_ctx->av1_pkt_num++;
      }
      low_delay_notify = 1;
    }

    encq_count++;

#ifdef MEASURE_LATENCY
#ifndef XCODER_311
      ni_log2(p_ctx, NI_LOG_INFO, "ENC fme_num %d, pkt_num %d, latency is %d\n",
                        p_ctx->frame_num, p_ctx->pkt_num, p_ctx->frame_num - p_ctx->pkt_num);
#endif
#endif

#ifdef XCODER_DUMP_DATA
    char dump_file[256];
    snprintf(dump_file, sizeof(dump_file), "%ld-%u-enc-pkt/pkt-%04ld.bin",
             (long)getpid(), p_ctx->session_id, (long)p_ctx->pkt_num);

    FILE *f = fopen(dump_file, "wb");
    fwrite((uint8_t *)p_packet->p_data + sizeof(ni_metadata_enc_bstream_t),
           p_packet->data_len - sizeof(ni_metadata_enc_bstream_t), 1, f);
    fflush(f);
    fclose(f);
#endif
  }

  ni_log2(p_ctx,
      NI_LOG_DEBUG,
      "%s(): %d.%u p_packet->start_of_stream=%u, "
      "p_packet->end_of_stream=%u, p_packet->video_width=%u, "
      "p_packet->video_height=%u, p_packet->dts=0x%08x 0x%08x, "
      "p_packet->pts=0x%08x 0x%08x, type=%u, avg_frame_qp=%u, show_frame=%d\n",
      __func__, p_ctx->hw_id, p_ctx->session_id, p_packet->start_of_stream,
      p_packet->end_of_stream, p_packet->video_width, p_packet->video_height,
      (uint32_t)((p_packet->dts >> 32) & 0xFFFFFFFF),
      (uint32_t)(p_packet->dts & 0xFFFFFFFF),
      (uint32_t)((p_packet->pts >> 32) & 0xFFFFFFFF),
      (uint32_t)(p_packet->pts & 0xFFFFFFFF), p_packet->frame_type,
      p_packet->avg_frame_qp, p_packet->av1_show_frame);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): p_packet->data_len=%u, size=%d\n", __func__,
         p_packet->data_len, size);

  if (encq_count % 500 == 0)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "Encoder pts queue size = %u  dts queue size = %u\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
  }

  retval = size;

#ifdef MEASURE_LATENCY
  if ((p_packet->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL) && (p_ctx->pkt_num > 0))
  {
    if (NI_CODEC_FORMAT_AV1 != p_ctx->codec_format
        || p_packet->av1_show_frame)
    {
        uint64_t abs_time_ns = ni_gettime_ns();
        ni_lat_meas_q_t *q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
        ni_log2(p_ctx, NI_LOG_INFO, "DTS:%" PRId64 ",DELTA:%" PRId64 ",eLAT:%" PRIu64 ";\n",
               p_packet->dts, abs_time_ns - q->last_benchmark_time,
               ni_lat_meas_q_check_latency(q, abs_time_ns, p_packet->dts));
        q->last_benchmark_time = abs_time_ns;
    }
  }
#endif

END:

  ni_pthread_mutex_unlock(&p_ctx->mutex);

  if (low_delay_notify)
  {
      low_delay_signal(p_ctx);
  }

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send sequnce change to a xcoder encoder instance
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_encoder_session_sequence_change(ni_session_context_t* p_ctx, ni_resolution_t *p_resolution)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    // re-init last_gop_size and av1_pkt_num for minFramesDelay frame delay estimiation
    int lookAheadEnable = !!(((ni_xcoder_params_t *)p_ctx->p_session_config)->cfg_enc_params.lookAheadDepth);
    int gop_preset_index = ((ni_xcoder_params_t *)p_ctx->p_session_config)->cfg_enc_params.gop_preset_index;
    p_ctx->last_gop_size = g_map_preset_to_gopsize[lookAheadEnable][gop_preset_index + 1];
    p_ctx->av1_pkt_num = 0;

    //Configure encoder sequence change
    retval = ni_config_instance_set_sequence_change(p_ctx, NI_DEVICE_TYPE_ENCODER,
                                                                        p_resolution);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_config_xcoder_config_set_sequence_change,
                            p_ctx->device_type, p_ctx->hw_id,
                            &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);
    if (retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): config encoder sequence change command failed\n",
               __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Open a xcoder scaler instance
 *
 *  \param[in]  p_ctx   pointer to session context
 *
 *  \return     NI_RETCODE_INVALID_PARAM
 *              NI_RETCODE_ERROR_NVME_CMD_FAILED
 *              NI_RETCODE_ERROR_INVALID_SESSION
 *              NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
int ni_scaler_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint32_t ui32LBA = 0;
  char fmt_fw_api_ver1[5], fmt_fw_api_ver2[5];

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_ctx->scaler_operation == NI_SCALER_OPCODE_STACK)
  {
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "64") < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot use stack filter on device with FW API version < 6.4\n");
      return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }
  }

  if (p_ctx->scaler_operation == NI_SCALER_OPCODE_ROTATE)
  {
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "67") < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot use rotate filter on device with FW API version < 6.7\n");
      return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }
  }

  if (p_ctx->scaler_operation == NI_SCALER_OPCODE_IPOVLY)
  {
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6L") < 0)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot use in-place overlay filter on device with FW API version < 6.L\n");
      return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }
  }

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_SCALER;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->dec_fme_buf_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_SCALER, ni_htonl(p_ctx->scaler_operation), 0);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp = ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_high);
    p_ctx->session_timestamp = (p_ctx->session_timestamp << 32) |
        ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_low);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "Scaler open session ID:0x%x\n",p_ctx->session_id);
    ni_log2(p_ctx, NI_LOG_DEBUG,
           "%s(): p_ctx->device_handle=%" PRIx64
           ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // Send SW version to FW if FW API version is >= 6.2
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "62") >= 0)
    {
        // Send SW version to session manager
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, NI_XCODER_REVISION, sizeof(uint64_t));
        ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX], &fmt_fw_api_ver1[0]);
        ni_fmt_fw_api_ver_str((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],  &fmt_fw_api_ver2[0]);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s libxcoder FW API ver %s, FW FW API ver %s\n",
               __func__, fmt_fw_api_ver1, fmt_fw_api_ver2);
        ui32LBA = CONFIG_SESSION_SWVersion_W(p_ctx->session_id);
        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): nvme write sw_version command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }
  }

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = 0;
  p_ctx->last_dts = 0;
  p_ctx->active_video_width = 0;
  p_ctx->active_video_height = 0;
  p_ctx->actual_video_width = 0;

#ifndef _WIN32
  if (p_ctx->isP2P)
  {
      retval = p2p_fill_pcie_address(p_ctx);
      if(retval != NI_RETCODE_SUCCESS)
      {
        LRETURN;
      }
  }
#endif

END:

    ni_aligned_free(p_buffer);
    return retval;
}

/*!******************************************************************************
 *  \brief  close a scaler session
 *
 *  \param[in]  p_ctx           pointer to session context
 *  \param[in]  eos_received    (not used)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *******************************************************************************/
ni_retcode_t ni_scaler_session_close(ni_session_context_t* p_ctx, int eos_received)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void * p_buffer = NULL;
  uint32_t ui32LBA = 0;

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      return NI_RETCODE_INVALID_PARAM;
  }

  ni_pthread_mutex_lock(&p_ctx->mutex);

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
      ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): Invalid session ID, return.\n", __func__);
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() malloc data buffer failed\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

  ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_SCALER);

  int retry = 0;
  while (retry < NI_SESSION_CLOSE_RETRY_MAX)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
             "p_ctx->session_id=%d, close_mode=1\n",
             __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
             p_ctx->session_id);

      if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
      {
          ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): command failed!\n", __func__);
          retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
          p_ctx->session_id = NI_INVALID_SESSION_ID;
          break;
      } else
    {
      //Close should always succeed
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    retry++;
  }

#ifndef _WIN32
  if (p_ctx->isP2P)
  {
      if (p_ctx->netint_fd)
      {
          close(p_ctx->netint_fd);
      }
  }
#endif

END:

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure scaling parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_scaler_params_t * params - pointer to the scaler ni_scaler_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_scaler_params(ni_session_context_t *p_ctx,
                                                  ni_scaler_params_t *p_params)
{
    void *p_scaler_config = NULL;
    ni_scaler_config_t *p_cfg = NULL;
    uint32_t buffer_size = sizeof(ni_scaler_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size =
        ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) *
        NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_scaler_config, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() malloc p_scaler_config buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_scaler_config, 0, buffer_size);

    //configure the session here
    ui32LBA = CONFIG_INSTANCE_SetScalerPara_W(p_ctx->session_id,
                                              NI_DEVICE_TYPE_SCALER);

    //Flip the bytes!!
    p_cfg = (ni_scaler_config_t *)p_scaler_config;
    p_cfg->filterblit = p_params->filterblit;
    p_cfg->numInputs = p_params->nb_inputs;
    p_cfg->scaler_param_b = (uint16_t)(0);
    p_cfg->scaler_param_c = (uint16_t)(0.75 * 10000);

    if (p_params->enable_scaler_params)
    {
      // check fw revision
      if (ni_cmp_fw_api_ver(
              (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
              "6s2") < 0)
      {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: not supported config scaler params B and C "
                      "on device with FW API version < 6s1\n", __func__);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log2(p_ctx, NI_LOG_ERROR,
                  "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                  "hw_id, %d, xcoder_inst_id: %d\n",
                  __func__,
                  (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                  p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
        LRETURN;
      }
      if (p_params->scaler_param_b < 0 || p_params->scaler_param_c < 0 ||
          p_params->scaler_param_b > 1 || p_params->scaler_param_c > 1)
      {
        ni_log2(p_ctx, NI_LOG_ERROR,  "%s: scaler_params_b and scaler_params_c must "
                      "be in [0 , 1]. scaler_params_b is %lf, scaler_params_c is %lf\n",
                      __func__, p_params->scaler_param_b, p_params->scaler_param_c);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log2(p_ctx, NI_LOG_ERROR,
                  "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                  "hw_id, %d, xcoder_inst_id: %d\n",
                  __func__,
                  (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                  p_ctx->session_id);
        }

        retval = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      p_cfg->scaler_param_b = (uint16_t)(p_params->scaler_param_b * 10000);
      p_cfg->scaler_param_c = (uint16_t)(p_params->scaler_param_c * 10000);
    }

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                               p_scaler_config, buffer_size, ui32LBA);
    if ((int32_t)retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_write_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %d, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        // Close the session since we can't configure it as per fw
        retval = ni_scaler_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: %s failed: blk_io_handle: %" PRIx64 ","
                   "hw_id, %d, xcoder_inst_id: %d\n",
                   __func__,
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
              p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);

END:

    ni_aligned_free(p_scaler_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  allocate a frame in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  width               width in pixels
 *  \param[in]  height              height in pixels
 *  \param[in]  format              pixel format
 *  \param[in]  options             option flags
 *  \param[in]  rectangle_width     clipping rectangle width in pixels
 *  \param[in]  rectangle_height    clipping rectangle height in pixels
 *  \param[in]  rectangle_x         clipping rectangle x position
 *  \param[in]  rectangle_y         clipping rectangle y position
 *  \param[in]  rgba_color          background colour (only used by pad filter)
 *  \param[in]  frame_index         frame index (only for hardware frames)
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_alloc_frame(ni_session_context_t* p_ctx,
                                   int width,
                                   int height,
                                   int format,
                                   int options,
                                   int rectangle_width,
                                   int rectangle_height,
                                   int rectangle_x,
                                   int rectangle_y,
                                   int rgba_color,
                                   int frame_index)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;
    uint32_t query_retry = 0;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (((options & NI_SCALER_FLAG_IO) && (options & NI_SCALER_FLAG_PC)))
    {
        // this operation is to free/allocate scaler frame pool
        if (rgba_color == 0)
        {
            if (ni_cmp_fw_api_ver(
                    (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                    "6r3") < 0)
            {
              ni_log2(p_ctx, NI_LOG_INFO,
                    "WARNING: Allocate framepool size 0 for session 0x%x\n", p_ctx->session_id);
              return NI_RETCODE_SUCCESS;
            }
            else if (p_ctx->pool_type == NI_POOL_TYPE_NONE)
            {
              ni_log2(p_ctx, NI_LOG_ERROR,
                    "ERROR: %s() try to free session 0x%x framepool while it's not allocated\n",
                    __func__, p_ctx->session_id);
              return NI_RETCODE_INVALID_PARAM;
            }
            else if ((options & NI_SCALER_FLAG_P2) == p_ctx->pool_type)
            {
              ni_log2(p_ctx, NI_LOG_INFO, "Free framepool of scaler 0x%x\n", p_ctx->session_id);
            }
            else
            {
              ni_log2(p_ctx, NI_LOG_ERROR,
                    "ERROR: %s() try to free session 0x%x framepool of type %u while "
                    "passing type %u\n",
                    __func__, p_ctx->session_id, p_ctx->pool_type,
                    (options & NI_SCALER_FLAG_P2));
            }
        }
        else
        {
          if (p_ctx->pool_type != NI_POOL_TYPE_NONE)
          {
            // try to expand the framepool
            if (ni_cmp_fw_api_ver(
                    (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                    "6r3") < 0)
            {
              ni_log2(p_ctx, NI_LOG_ERROR,
                    "ERROR: allocate framepool multiple times for session 0x%x "
                    "not supported in FW API version < 6r3\n",
                    p_ctx->session_id);
              return NI_RETCODE_INVALID_PARAM;
            }
            if ((options & NI_SCALER_FLAG_P2) == p_ctx->pool_type)
            {
              ni_log2(p_ctx, NI_LOG_INFO,
                    "Expand frame pool of scaler 0x%x with %u more frames\n",
                    p_ctx->session_id, rgba_color);
            }
            else
            {
              ni_log2(p_ctx, NI_LOG_ERROR,
                    "ERROR: try to expand session 0x%x framepool with type %u "
                    "while pool type is %u\n",
                    p_ctx->session_id, options & NI_SCALER_FLAG_P2, p_ctx->pool_type);
              return NI_RETCODE_INVALID_PARAM;
            }
          }
        }
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data->picture_width = width;
    p_data->picture_height = height;
    p_data->picture_format = format;
    p_data->options = options;
    p_data->rectangle_width = rectangle_width;
    p_data->rectangle_height = rectangle_height;
    p_data->rectangle_x = rectangle_x;
    p_data->rectangle_y = rectangle_y;
    p_data->rgba_color = rgba_color;
    p_data->frame_index = frame_index;

    ni_log2(p_ctx, NI_LOG_DEBUG, "%s() options %d frame_index %u buffered_frame_index %d\n",
            __func__, options, p_data->frame_index, p_ctx->buffered_frame_index);

    switch (p_ctx->scaler_operation)
    {
        case NI_SCALER_OPCODE_OVERLAY:
        case NI_SCALER_OPCODE_WATERMARK:
            if ((options & NI_SCALER_FLAG_IO) && !(options & NI_SCALER_FLAG_PC))
            {
                p_data->frame_index = frame_index; // background
                if (p_ctx->buffered_frame_index)
                {
                    p_data->rgba_color = p_ctx->buffered_frame_index; // output frame
                }
                else
                {
                    p_data->rgba_color = 0; // no output frame, fw must acquire
                }
            }
            break;

        default:
            // Send acquired buffer index in AdminWr SET_ALLOC_FRAME command to prevent FP
            // ConfigOutput from acquire buffer again
            if ((options & NI_SCALER_FLAG_IO) && p_ctx->buffered_frame_index)
            {
                p_data->frame_index = p_ctx->buffered_frame_index;
            }
            break;
    }

    bool isrgb = ((GC620_RGBA8888 == format) || (GC620_BGRX8888 == format) ||
                  (GC620_ARGB8888 == format) || (GC620_ABGR8888 == format));
    if(width > NI_MAX_RESOLUTION_WIDTH || height > NI_MAX_RESOLUTION_HEIGHT ||
       width < NI_MIN_RESOLUTION_WIDTH_SCALER || height < NI_MIN_RESOLUTION_HEIGHT_SCALER ||
       ((width > NI_MAX_RESOLUTION_RGBA_WIDTH || height > NI_MAX_RESOLUTION_RGBA_HEIGHT) && isrgb))
    {
        if(width < NI_MIN_RESOLUTION_WIDTH_SCALER)
            ni_log2(p_ctx, NI_LOG_ERROR, "Invalid Picture Width: too small for scaler\n");
        if(height < NI_MIN_RESOLUTION_HEIGHT_SCALER)
            ni_log2(p_ctx, NI_LOG_ERROR, "Invalid Picture Height: too small for scaler\n");
        if(width > NI_MAX_RESOLUTION_WIDTH || ((width > NI_MAX_RESOLUTION_RGBA_WIDTH) && isrgb))
            ni_log2(p_ctx, NI_LOG_ERROR, "Invalid Picture Width: too big for scaler\n");
        if(height > NI_MAX_RESOLUTION_HEIGHT || ((height > NI_MAX_RESOLUTION_RGBA_HEIGHT) && isrgb))
            ni_log2(p_ctx, NI_LOG_ERROR, "Invalid Picture Height: too big for scaler\n");

        ni_log2(p_ctx, NI_LOG_ERROR, "Resolution %d x %d not supported for %d format!\n", width, height, format);
        ni_aligned_free(p_data);
        return NI_RETCODE_NVME_SC_INVALID_PARAMETER;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,
           "Session=0x%x: Dev alloc frame: FrameIndex=%d; W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
           p_ctx->session_id, p_data->frame_index,
           p_data->picture_width, p_data->picture_height,
           p_data->picture_format, p_data->rectangle_width,
           p_data->rectangle_height, p_data->rectangle_x, p_data->rectangle_y);

    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    for (;;)
    {
        query_retry++;

        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                        p_data, dataLen, ui32LBA);

        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_3);

        if (retval == NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE)
        {
            if (query_retry >= 1000 || (options & NI_SCALER_FLAG_PC))
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "Error: 2D could not acquire frame\n");
                retval = NI_RETCODE_FAILURE;
                LRETURN;
            }
            ni_usleep(NI_RETRY_INTERVAL_100US);
            continue;
        }
        else
        {
            p_ctx->pool_type = ((options & NI_SCALER_FLAG_IO) && (options & NI_SCALER_FLAG_PC)) ?
                               (options & NI_SCALER_FLAG_P2) : (p_ctx->pool_type);
            break;
        }
    }

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: "
               "blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n", __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_data);
    return retval;
}

/*!******************************************************************************
 *  \brief  config a frame in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_cfg               pointer to frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_config_frame(ni_session_context_t *p_ctx,
                                    ni_frame_config_t *p_cfg)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx || !p_cfg)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data->picture_width = p_cfg->picture_width;
    p_data->picture_height = p_cfg->picture_height;
    p_data->picture_format = p_cfg->picture_format;
    p_data->options = p_cfg->options;

    p_data->rectangle_width = p_cfg->rectangle_width;
    p_data->rectangle_height = p_cfg->rectangle_height;
    p_data->rectangle_x = p_cfg->rectangle_x;
    p_data->rectangle_y = p_cfg->rectangle_y;
    p_data->rgba_color = p_cfg->rgba_color;
    p_data->frame_index = p_cfg->frame_index;
    p_data->session_id = p_cfg->session_id;
    p_data->output_index = p_cfg->output_index;
    switch (p_cfg->orientation)
    {
        case 0:
        case 2:
        case 4:
        case 5:
            p_data->orientation = p_cfg->orientation;
            break;
        case 1:
            p_data->orientation = 3;
            break;
        case 3:
            p_data->orientation = 1;
            break;
        default:
            ni_log2(p_ctx, NI_LOG_ERROR, "Bad orientation: %u\n", p_cfg->orientation);
            return NI_RETCODE_PARAM_INVALID_VALUE;
    }
    bool isrgb = ((GC620_RGBA8888 == p_data->picture_format) || (GC620_BGRX8888 == p_data->picture_format) ||
                  (GC620_ARGB8888 == p_data->picture_format) || (GC620_ABGR8888 == p_data->picture_format));
    if(p_data->picture_width > NI_MAX_RESOLUTION_WIDTH || p_data->picture_height > NI_MAX_RESOLUTION_HEIGHT ||
       p_data->picture_width < NI_MIN_RESOLUTION_WIDTH_SCALER || p_data->picture_height < NI_MIN_RESOLUTION_HEIGHT_SCALER ||
       ((p_data->picture_width > NI_MAX_RESOLUTION_RGBA_WIDTH || p_data->picture_height > NI_MAX_RESOLUTION_RGBA_HEIGHT) && isrgb))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "Resolution %d x %d not supported for %d format!\n", p_data->picture_width, p_data->picture_height, p_data->picture_format);
        ni_aligned_free(p_data);
        return NI_RETCODE_NVME_SC_INVALID_PARAMETER;
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,
           "Session=0x%x: Dev config frame: FrameIndex=%u; W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d; O=%d\n",
           p_ctx->session_id, p_cfg->frame_index,
           p_data->picture_width, p_data->picture_height,
           p_data->picture_format, p_data->rectangle_width,
           p_data->rectangle_height, p_data->rectangle_x,
           p_data->rectangle_y, p_data->orientation);

    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR ni_scaler_config(): nvme command failed!\n");
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_data);
    return retval;
}

/*!******************************************************************************
 *  \brief  config multiple frames in the scaler
 *
 *  \param[in]  p_ctx               pointer to session context
 *  \param[in]  p_cfg_in            pointer to input frame config array
 *  \param[in]  numInCfgs           number of input frame configs in the p_cfg array
 *  \param[in]  p_cfg_out           pointer to output frame config
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *******************************************************************************/
ni_retcode_t ni_scaler_multi_config_frame(ni_session_context_t *p_ctx,
                                          ni_frame_config_t p_cfg_in[],
                                          int numInCfgs,
                                          ni_frame_config_t *p_cfg_out)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_instance_mgr_allocation_info_t *p_data, *p_data_orig;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;
    int i;

    /* Round up to nearest 4096 bytes */
    dataLen =
        sizeof(ni_instance_mgr_allocation_info_t) * (numInCfgs + 1) + NI_MEM_PAGE_ALIGNMENT - 1;
    dataLen = dataLen & 0xFFFFF000;

    if (!p_ctx || (!p_cfg_in && numInCfgs))
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);

    p_data_orig = p_data;

    for (i = 0; i < numInCfgs; i++)
    {
        p_data->picture_width = p_cfg_in[i].picture_width;
        p_data->picture_height = p_cfg_in[i].picture_height;
        p_data->picture_format = p_cfg_in[i].picture_format;
        p_data->options = p_cfg_in[i].options & ~NI_SCALER_FLAG_IO;

        p_data->rectangle_width = p_cfg_in[i].rectangle_width;
        p_data->rectangle_height = p_cfg_in[i].rectangle_height;
        p_data->rectangle_x = p_cfg_in[i].rectangle_x;
        p_data->rectangle_y = p_cfg_in[i].rectangle_y;
        p_data->rgba_color = p_cfg_in[i].rgba_color;
        p_data->frame_index = p_cfg_in[i].frame_index;
        p_data->session_id = p_cfg_in[i].session_id;
        p_data->output_index = p_cfg_in[i].output_index;

        ni_log2(p_ctx, NI_LOG_DEBUG,
               "Session=0x%x: Dev in config frame %d: FrameIndex=%u; Session=0x%x; W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
               p_ctx->session_id, i,
               p_data->frame_index, p_data->session_id,
               p_data->picture_width, p_data->picture_height,
               p_data->picture_format, p_data->rectangle_width,
               p_data->rectangle_height, p_data->rectangle_x, p_data->rectangle_y);

        p_data++;
    }

    if (p_cfg_out)
    {
        p_data->picture_width = p_cfg_out->picture_width;
        p_data->picture_height = p_cfg_out->picture_height;
        p_data->picture_format = p_cfg_out->picture_format;
        p_data->options = p_cfg_out->options | NI_SCALER_FLAG_IO;

        p_data->rectangle_width = p_cfg_out->rectangle_width;
        p_data->rectangle_height = p_cfg_out->rectangle_height;
        p_data->rectangle_x = p_cfg_out->rectangle_x;
        p_data->rectangle_y = p_cfg_out->rectangle_y;
        p_data->rgba_color = p_cfg_out->rgba_color;
        p_data->frame_index = p_cfg_out->frame_index;

        ni_log2(p_ctx, NI_LOG_DEBUG,
               "Session=0x%x: Dev out config frame: FrameIndex=%u; W=%d; H=%d; C=%d; RW=%d; RH=%d; RX=%d; RY=%d\n",
               p_ctx->session_id, p_data->frame_index,
               p_data->picture_width, p_data->picture_height,
               p_data->picture_format, p_data->rectangle_width,
               p_data->rectangle_height, p_data->rectangle_x,
               p_data->rectangle_y);
    }
    if(p_data->picture_width > NI_MAX_RESOLUTION_WIDTH || p_data->picture_height > NI_MAX_RESOLUTION_HEIGHT || ((p_data->picture_width > NI_MAX_RESOLUTION_RGBA_WIDTH || p_data->picture_height> NI_MAX_RESOLUTION_RGBA_HEIGHT) && ((GC620_RGBA8888 == p_data->picture_format) || (GC620_BGRX8888 == p_data->picture_format) || (GC620_ARGB8888 == p_data->picture_format) || (GC620_ABGR8888 == p_data->picture_format))))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "Resolution %d x %d not supported for %d format!\n", p_data->picture_width, p_data->picture_height, p_data->picture_format);
        ni_aligned_free(p_data);
        return NI_RETCODE_NVME_SC_INVALID_PARAMETER;
    }

    ui32LBA = CONFIG_INSTANCE_SetScalerAlloc_W(p_ctx->session_id,
                                               NI_DEVICE_TYPE_SCALER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_data_orig, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);

        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR ni_scaler_config(): nvme command failed!\n");
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }

END:

    ni_aligned_free(p_data_orig);
    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get GeneralStatus data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_general_status_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_general_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_general_status_t* p_gen_status)
{
  void* p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_general_status_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_gen_status))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!IS_XCODER_DEVICE_TYPE(device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ui32LBA = QUERY_GENERAL_GET_STATUS_R(device_type);

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  //No need to flip the bytes since the datastruct has only uint8_t datatypes
  memcpy((void*)p_gen_status, p_buffer, sizeof(ni_instance_mgr_general_status_t));

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): model_load:%u qc:%d percent:%d\n", __func__,
         p_gen_status->fw_model_load, p_gen_status->cmd_queue_count,
         p_gen_status->process_load_percent);
END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get DetailStatus data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_detail_status_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_detail_status(ni_session_context_t* p_ctx, ni_device_type_t device_type, void* p_detail_status, int ver)
{
  void* p_buffer = NULL;
  int retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = 0;
  uint32_t copyLen = 0;
  if(ver == 0)
  {
    ui32LBA = QUERY_DETAIL_GET_STATUS_R(device_type);
    copyLen = sizeof(ni_instance_mgr_detail_status_t) * NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    dataLen = ((copyLen + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  }
  else if(ver == 1)
  {
    ui32LBA = QUERY_DETAIL_GET_STATUS_V1_R(device_type);
    copyLen = sizeof(ni_instance_mgr_detail_status_v1_t);
    dataLen = (copyLen + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT * NI_MEM_PAGE_ALIGNMENT;
  }
  else
  {
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_detail_status))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!IS_XCODER_DEVICE_TYPE(device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_posix_memalign((void **)&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  //No need to flip the bytes since the datastruct has only uint8_t datatypes
  memcpy(p_detail_status, p_buffer, copyLen);

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get Stream Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_mgr_stream_info_t *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_query_stream_info(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_info_t* p_stream_info)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_stream_info))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_STREAM_INFO_R(p_ctx->session_id, device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_stream_info, p_buffer, sizeof(ni_instance_mgr_stream_info_t));

  //flip the bytes to host order
  p_stream_info->picture_width = ni_htons(p_stream_info->picture_width);
  p_stream_info->picture_height = ni_htons(p_stream_info->picture_height);
  p_stream_info->frame_rate = ni_htons(p_stream_info->frame_rate);
  p_stream_info->is_flushed = ni_htons(p_stream_info->is_flushed);
  p_stream_info->transfer_frame_stride = ni_htons(p_stream_info->transfer_frame_stride);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): pix_format = %d\n", __func__,
         p_stream_info->pix_format);   //temp

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Query a particular session to get the stats info
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_session_stats_t *out - Struct preallocated from the
 *           caller where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED
 *            on failure
 ******************************************************************************/
ni_retcode_t ni_query_session_stats(ni_session_context_t *p_ctx,
                                    ni_device_type_t device_type,
                                    ni_session_stats_t *p_session_stats, int rc,
                                    int opcode)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void* p_buffer = NULL;
  uint64_t session_timestamp;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_session_stats_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_device_type_t xc_device_type =
      (device_type != NI_DEVICE_TYPE_UPLOAD ? device_type :
                                              NI_DEVICE_TYPE_ENCODER);

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_session_stats))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!IS_XCODER_DEVICE_TYPE(xc_device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_SESSION_STATS_R(p_ctx->session_id, xc_device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, dataLen);

  // Set session ID to be invalid. In case, the last command fails because the invalid session ID was submitted
  // with the command, the session id would remain invalid.
  // If the last command is processed successfully in session manager, the session id would become valid.
  ((ni_session_stats_t *)p_buffer)->ui16SessionId =
      (uint16_t)NI_INVALID_SESSION_ID;

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA)
      < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): read command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_session_stats, p_buffer, sizeof(ni_session_stats_t));

  // flip the bytes to host order
  // all query commands are guaranteed success once a session is opened
  p_session_stats->ui16SessionId = ni_htons(p_session_stats->ui16SessionId);
  p_session_stats->ui16ErrorCount = ni_htons(p_session_stats->ui16ErrorCount);
  p_session_stats->ui32LastTransactionId =
      ni_htonl(p_session_stats->ui32LastTransactionId);
  p_session_stats->ui32LastTransactionCompletionStatus =
      ni_htonl(p_session_stats->ui32LastTransactionCompletionStatus);
  p_session_stats->ui32LastErrorTransactionId = ni_htonl(p_session_stats->ui32LastErrorTransactionId);
  p_session_stats->ui32LastErrorStatus = ni_htonl(p_session_stats->ui32LastErrorStatus);
  p_session_stats->ui32Session_timestamp_high = ni_htonl(p_session_stats->ui32Session_timestamp_high);
  p_session_stats->ui32Session_timestamp_low = ni_htonl(p_session_stats->ui32Session_timestamp_low);

  session_timestamp = p_session_stats->ui32Session_timestamp_high;
  session_timestamp <<= 32;
  session_timestamp |= p_session_stats->ui32Session_timestamp_low;

  // get the session timestamp when open session
  // check the timestamp during transcoding
  if ((p_ctx->session_timestamp != session_timestamp) &&
      (ni_xcoder_resource_recovery != p_session_stats->ui32LastErrorStatus))
  // if VPU recovery, the session timestamp will be reset.
  {
      p_session_stats->ui32LastErrorStatus =
          NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "instance id invalid:%u, timestamp:%" PRIu64 ", "
             "query timestamp:%" PRIu64 "\n", p_ctx->session_id,
             p_ctx->session_timestamp, session_timestamp);
  }

  // check rc here, if rc != NI_RETCODE_SUCCESS, it means that last read/write command failed
  // failures may be link layer errors, such as physical link errors or ERROR_WRITE_PROTECT in windows.
  if (NI_RETCODE_SUCCESS != rc)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s():last command Failed: rc %d\n", __func__, rc);
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): session id %u ts %lu hw_id %d device_type %u codec_format %u frame_num %lu pkt_num %lu "
               "ready_to_close %u session_run_state %d active_video_width %u active_video_height %u\n",
               __func__,
               p_ctx->session_id,
               p_ctx->session_timestamp,
               p_ctx->hw_id,
               p_ctx->device_type,
               p_ctx->codec_format,
               p_ctx->frame_num,
               p_ctx->pkt_num,
               p_ctx->ready_to_close,
               p_ctx->session_run_state,
               p_ctx->active_video_width,
               p_ctx->active_video_height);

#if __linux__ || __APPLE__
#if !defined(_ANDROID) && !defined(__OPENHARMONY__)
#ifndef DISABLE_BACKTRACE_PRINT
      ni_print_backtrace(); // log backtrace
#endif
#endif
#endif

      p_session_stats->ui32LastTransactionCompletionStatus =
          NI_RETCODE_ERROR_NVME_CMD_FAILED;
      p_session_stats->ui32LastErrorStatus = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  } else if (p_ctx->session_id != p_session_stats->ui16SessionId)
  {
      uint64_t ct = ni_gettime_ns();
      uint64_t dt = ct - p_ctx->last_access_time;
      ni_log2(p_ctx, NI_LOG_ERROR,
             "%s(): device 0x%" PRIx64 " last command Failed due to wrong "
             "session ID. Expected 0x%x, got 0x%x keep alive last access "
             "time %" PRIu64 ", current %" PRIu64 "\n", __func__,
             (int64_t)p_ctx->blk_io_handle, p_ctx->session_id,
             p_session_stats->ui16SessionId, p_ctx->last_access_time, ct);
      if (dt > 1000000000)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,
                 "%s():long delay between last command dt = %" PRId64 " ns, "
                 "process was possibly blocked.\n", __func__, dt);
      }
      p_session_stats->ui32LastErrorStatus = NI_RETCODE_ERROR_INVALID_SESSION;

      // Mark session id to INVALID so that all commands afterward are blocked
      p_ctx->session_id = NI_INVALID_SESSION_ID;
  }

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): error count %u last rc 0x%x inst_err_no 0x%x\n",
         __func__, p_session_stats->ui16ErrorCount,
         p_session_stats->ui32LastTransactionCompletionStatus,
         p_session_stats->ui32LastErrorStatus);

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Query a particular xcoder instance to get End of Output data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   InstMgrStreamComp *out - Struct preallocated from the caller where the
 *  resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_MEM_ALOC
 *  or NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
int ni_query_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_stream_complete_t* p_stream_complete)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen = ((sizeof(ni_instance_mgr_stream_complete_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx || !p_stream_complete)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = QUERY_INSTANCE_EOS_R(p_ctx->session_id, device_type);

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate buffer.\n");
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_stream_complete, p_buffer, sizeof(ni_instance_mgr_stream_complete_t));

  //flip the bytes to host order
  p_stream_complete->is_flushed = ni_htons(p_stream_complete->is_flushed);

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

static const char* ni_get_device_type_str(int type)
{
    if (type < NI_DEVICE_TYPE_DECODER || type > NI_DEVICE_TYPE_AI)
    {
        return "Invalid device type";
    }
    return g_device_type_str[type];
}

static void
ni_parse_session_statistic_info(ni_session_context_t *p_ctx,
                                ni_session_statistic_t *p_session_statistic,
                                void *p_buffer)
{
    memcpy((void *)p_session_statistic, p_buffer,
           sizeof(ni_session_statistic_t));

    //flip the bytes to host order
    p_session_statistic->ui32RdBufAvailSize =
        ni_htonl(p_session_statistic->ui32RdBufAvailSize);
    p_session_statistic->ui32WrBufAvailSize =
        ni_htonl(p_session_statistic->ui32WrBufAvailSize);

    p_session_statistic->ui32FramesInput =
        ni_htonl(p_session_statistic->ui32FramesInput);
    p_session_statistic->ui32FramesBuffered =
        ni_htonl(p_session_statistic->ui32FramesBuffered);
    p_session_statistic->ui32FramesCompleted =
        ni_htonl(p_session_statistic->ui32FramesCompleted);
    p_session_statistic->ui32FramesOutput =
        ni_htonl(p_session_statistic->ui32FramesOutput);
    p_session_statistic->ui32FramesDropped =
        ni_htonl(p_session_statistic->ui32FramesDropped);
    p_session_statistic->ui32InstErrors =
        ni_htonl(p_session_statistic->ui32InstErrors);

    p_session_statistic->ui16SessionId =
        ni_htons(p_session_statistic->ui16SessionId);
    p_session_statistic->ui16ErrorCount =
        ni_htons(p_session_statistic->ui16ErrorCount);
    p_session_statistic->ui32LastTransactionId =
        ni_htonl(p_session_statistic->ui32LastTransactionId);
    p_session_statistic->ui32LastTransactionCompletionStatus =
        ni_htonl(p_session_statistic->ui32LastTransactionCompletionStatus);
    p_session_statistic->ui32LastErrorTransactionId =
        ni_htonl(p_session_statistic->ui32LastErrorTransactionId);
    p_session_statistic->ui32LastErrorStatus =
        ni_htonl(p_session_statistic->ui32LastErrorStatus);
    p_session_statistic->ui32Session_timestamp_high =
        ni_htonl(p_session_statistic->ui32Session_timestamp_high);
    p_session_statistic->ui32Session_timestamp_low =
        ni_htonl(p_session_statistic->ui32Session_timestamp_low);

    // p_session_statistic->ui8AdditionalFramesDelay does not require endian conversion

    if (p_ctx->session_id != p_session_statistic->ui16SessionId)
    {
        uint64_t ct = ni_gettime_ns();
        uint64_t dt = ct - p_ctx->last_access_time;
        ni_log2(p_ctx, NI_LOG_ERROR,
               "%s(): %s device 0x%" PRIx64 " last command Failed due to wrong "
               "session ID. Expected 0x%x, got 0x%x w_r <%u %u> keep alive "
               "last access time %" PRIu64 ", current %" PRIu64 "\n", __func__,
               ni_get_device_type_str(p_ctx->device_type),
               (int64_t)p_ctx->device_handle, p_ctx->session_id,
               p_session_statistic->ui16SessionId,
               p_session_statistic->ui32WrBufAvailSize,
               p_session_statistic->ui32RdBufAvailSize, p_ctx->last_access_time,
               ct);
        if (dt > 1000000000)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "%s():long delay between last command dt = %" PRId64 " ns, "
                   "process was possibly blocked.\n", __func__, dt);
        }
        p_session_statistic->ui32LastErrorStatus =
            NI_RETCODE_ERROR_INVALID_SESSION;
        //Mark session id to INVALID so that all commands afterward are blocked
        p_ctx->session_id = NI_INVALID_SESSION_ID;
    } else
    {
        //Acknowledge that total error count here
    }

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): error count %u last rc 0x%x inst_err_no 0x%x\n",
           __func__, p_session_statistic->ui16ErrorCount,
           p_session_statistic->ui32LastTransactionCompletionStatus,
           p_session_statistic->ui32LastErrorStatus);
}

/*!*****************************************************************************
 *  \brief  Query a particular xcoder session to get session statistics
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_session_statistic_t*out - Struct preallocated from the caller
 *           where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_retcode_t
ni_query_session_statistic_info(ni_session_context_t *p_ctx,
                                ni_device_type_t device_type,
                                ni_session_statistic_t *p_session_statistic)
{
    void *p_buffer = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t dataLen =
        ((sizeof(ni_session_statistic_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) /
         NI_MEM_PAGE_ALIGNMENT) *
        NI_MEM_PAGE_ALIGNMENT;

    if (!p_ctx || !p_session_statistic)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: %s() passed parameters are null!, return\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "65") < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() not supported on device with FW api version < 6.5\n", __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    if (!(NI_DEVICE_TYPE_DECODER == device_type ||
          NI_DEVICE_TYPE_ENCODER == device_type ||
          NI_DEVICE_TYPE_AI == device_type))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
               __func__, device_type);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    ui32LBA = QUERY_INSTANCE_CUR_STATUS_INFO_R(p_ctx->session_id, device_type);

    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, dataLen);

    // Set session ID to be invalid. In case, the last command fails because the invalid session ID was submitted
    // with the command, the session id would remain invalid.
    // If the Last command is processed successfully in session manager, the session id would become valid.
    ((ni_session_statistic_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                              p_buffer, dataLen, ui32LBA) < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): NVME command Failed\n", __func__);
        p_session_statistic->ui32LastTransactionCompletionStatus =
            NI_RETCODE_ERROR_NVME_CMD_FAILED;
        p_session_statistic->ui32LastErrorStatus =
            NI_RETCODE_ERROR_NVME_CMD_FAILED;
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    ni_parse_session_statistic_info(p_ctx, p_session_statistic, p_buffer);
    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, %s return.\n",
               __func__, ni_get_device_type_str(p_ctx->device_type));
        LRETURN;
    }
    p_ctx->session_statistic = *p_session_statistic;

END:
    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s():exit\n", __func__);

    return retval;
}
/*!*****************************************************************************
 *  \brief  Query a particular xcoder instance to get buffer/data Info data
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_instance_buf_info_rw_type_t rw_type
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_instance_buf_info_t *out - Struct preallocated from the caller
 *           where the resulting data will be placed
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_retcode_t ni_query_instance_buf_info(ni_session_context_t *p_ctx,
                                        ni_instance_buf_info_rw_type_t rw_type,
                                        ni_device_type_t device_type,
                                        ni_instance_buf_info_t *p_inst_buf_info)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  uint32_t dataLen =
      ((sizeof(ni_instance_buf_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) /
       NI_MEM_PAGE_ALIGNMENT) *
      NI_MEM_PAGE_ALIGNMENT;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx || !p_inst_buf_info)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_DECODER == device_type ||
        NI_DEVICE_TYPE_ENCODER == device_type ||
        NI_DEVICE_TYPE_SCALER == device_type ||
        NI_DEVICE_TYPE_AI == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (INST_BUF_INFO_RW_READ == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_RBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else if (INST_BUF_INFO_RW_WRITE == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_WBUFF_SIZE_R(p_ctx->session_id, device_type);
  }
  else if(INST_BUF_INFO_RW_WRITE_BY_EP == rw_type)
  {
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "65") >= 0)
    {
      ui32LBA = QUERY_INSTANCE_WBUFF_SIZE_R_BY_EP(p_ctx->session_id, device_type);
    }
    else
    {
      retval = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
      LRETURN;
    }
  }
  else if (INST_BUF_INFO_RW_UPLOAD == rw_type)
  {
    ui32LBA = QUERY_INSTANCE_UPLOAD_ID_R(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_R_ACQUIRE == rw_type)
  {
      ui32LBA = QUERY_INSTANCE_ACQUIRE_BUF(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_RW_READ_BUSY == rw_type)
  {
      ui32LBA =
          QUERY_INSTANCE_RBUFF_SIZE_BUSY_R(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_RW_WRITE_BUSY == rw_type)
  {
      ui32LBA =
          QUERY_INSTANCE_WBUFF_SIZE_BUSY_R(p_ctx->session_id, device_type);
  } else if (INST_BUF_INFO_RW_READ_BY_AI == rw_type)
  {
      ui32LBA = QUERY_INSTANCE_AI_INFO_R(p_ctx->session_id, device_type);
  } else
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown query type %d, return\n",
           __func__, rw_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }

  memset(p_buffer, 0, dataLen);

  if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, dataLen, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

  memcpy((void*)p_inst_buf_info, p_buffer, sizeof(ni_instance_buf_info_t));

  p_inst_buf_info->buf_avail_size = ni_htonl(p_inst_buf_info->buf_avail_size);

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Configure the read/write pipe for a session to control its behavior
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_session_config_rw_t rw_type
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   uint8_t enable
 *  \param   uint8_t hw_action
 *  \param   uint16_t frame_id
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
 *            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
 *            failure
 ******************************************************************************/
ni_retcode_t ni_config_session_rw(ni_session_context_t *p_ctx,
                                  ni_session_config_rw_type_t rw_type,
                                  uint8_t enable, uint8_t hw_action,
                                  uint16_t frame_id)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;
  ni_session_config_rw_t * rw_config = NULL;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!((SESSION_READ_CONFIG == rw_type) || (SESSION_WRITE_CONFIG == rw_type)))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
           __func__, rw_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  buffer_size = ((sizeof(ni_session_config_rw_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  rw_config = (ni_session_config_rw_t *)p_buffer;
  rw_config->ui8Enable = enable;
  rw_config->ui8HWAccess = hw_action;
  switch(rw_type)
  {
    case SESSION_READ_CONFIG:
      rw_config->uHWAccessField.ui16ReadFrameId = frame_id;
      break;
    case SESSION_WRITE_CONFIG:
      rw_config->uHWAccessField.ui16WriteFrameId = frame_id;
      break;
    default:
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
             __func__, rw_type);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  switch(rw_type)
  {
    case SESSION_READ_CONFIG:
      ui32LBA = CONFIG_SESSION_Read_W(p_ctx->session_id);
      break;
    case SESSION_WRITE_CONFIG:
      ui32LBA = CONFIG_SESSION_Write_W(p_ctx->session_id);
      break;
    default:
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown config type %d, return\n",
             __func__, rw_type);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for Start Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION. NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_sos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetSOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command for End Of Stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_eos(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (! (NI_DEVICE_TYPE_DECODER == device_type ||
         NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_SetEOS_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to flush the stream
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_flush(ni_session_context_t* p_ctx, ni_device_type_t device_type)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_DEVICE_TYPE_DECODER != device_type)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  ui32LBA = CONFIG_INSTANCE_Flush_W(p_ctx->session_id, device_type);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_ctx->p_all_zero_buf, NI_DATA_BUFFER_LEN,
                                  ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);

END:

  if (NI_RETCODE_SUCCESS != retval)
  {
    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed with %d\n", __func__, retval);
  }

  ni_log2(p_ctx, NI_LOG_TRACE, "%s(): exit\n", __func__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to set the length for the incoming write packet
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_write_len(ni_session_context_t* p_ctx, ni_device_type_t device_type, uint32_t len)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_DECODER == device_type ||
        NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Unknown device type %d, return\n",
           __func__, device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((sizeof(len) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  memcpy(p_buffer, &len, sizeof(len));

  ui32LBA = CONFIG_INSTANCE_SetPktSize_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to inform encoder sequence change
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
 *  \param   ni_resolution_t p_resolution - sequence change resolution
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_sequence_change(ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_resolution_t *p_resolution)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  void * p_buffer = NULL;
  uint32_t buffer_size = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (!(NI_DEVICE_TYPE_ENCODER == device_type))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Seq Change not supported for device type %d, return\n", device_type);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  buffer_size = ((sizeof(ni_resolution_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_buffer, 0, buffer_size);
  memcpy(p_buffer, p_resolution, sizeof(ni_resolution_t));

  ui32LBA = CONFIG_INSTANCE_SetSeqChange_W(p_ctx->session_id, device_type);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_buffer, buffer_size, ui32LBA) < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_params(ni_session_context_t* p_ctx)
{
  void* p_encoder_config = NULL;
  void* p_encoder_roi_qp_map = NULL;
  ni_encoder_config_t* p_cfg = NULL;
  uint32_t buffer_size = 0;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rh") >= 0)
  {
    buffer_size = NI_CUSTOMIZE_ROI_QPOFFSET_LEVEL * NI_CUSTOMIZE_ROI_QP_NUM;
    ni_xcoder_params_t *session_config = (ni_xcoder_params_t *)p_ctx->p_session_config;
    if (session_config->cfg_enc_params.customize_roi_qp_level > 64) {
      ui32LBA = CONFIG_INSTANCE_SetEncRoiQpMap_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
      buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
      if (ni_posix_memalign(&p_encoder_roi_qp_map, sysconf(_SC_PAGESIZE), buffer_size))
      {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
              NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
      }
      memset(p_encoder_roi_qp_map, 0, buffer_size);
      memcpy(p_encoder_roi_qp_map, session_config->customize_roi_qp_map,
            NI_CUSTOMIZE_ROI_QPOFFSET_LEVEL * NI_CUSTOMIZE_ROI_QP_NUM);
      retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_encoder_roi_qp_map, buffer_size, ui32LBA);
      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                  p_ctx->device_type, p_ctx->hw_id,
                  &(p_ctx->session_id), OPT_1);
      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_encoder_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
          ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        }
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
      }
      session_config->cfg_enc_params.customize_roi_qp_level -= 64;
    }
  }

  buffer_size = sizeof(ni_encoder_config_t);
  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_encoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_encoder_config, 0, buffer_size);

  ni_set_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config);
  retval = ni_validate_custom_template(p_ctx, p_encoder_config, p_ctx->p_session_config, p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg));
  if (NI_RETCODE_PARAM_WARN == retval)
  {
      ni_log2(p_ctx, NI_LOG_INFO, "WARNING: ni_validate_custom_template() . %s\n", p_ctx->param_err_msg);
      fflush(stdout);
  }
  else if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_validate_custom_template() failed. %s\n", p_ctx->param_err_msg);
      fflush(stdout);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  //configure the session here
  ui32LBA = CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  //Flip the bytes!! any param 16bits in size need ni_htons, 32bit need ni_htonl
  p_cfg = (ni_encoder_config_t*)p_encoder_config;
  p_cfg->i32picWidth = ni_htonl(p_cfg->i32picWidth);
  p_cfg->i32picHeight = ni_htonl(p_cfg->i32picHeight);
  p_cfg->i32meBlkMode = ni_htonl(p_cfg->i32meBlkMode);
  p_cfg->i32frameRateInfo = ni_htonl(p_cfg->i32frameRateInfo);
  p_cfg->i32vbvBufferSize = ni_htonl(p_cfg->i32vbvBufferSize);
  p_cfg->i32userQpMax = ni_htonl(p_cfg->i32userQpMax);
  p_cfg->i32maxIntraSize = ni_htonl(p_cfg->i32maxIntraSize);
  p_cfg->i32userMaxDeltaQp = ni_htonl(p_cfg->i32userMaxDeltaQp);
  p_cfg->i32userMinDeltaQp = ni_htonl(p_cfg->i32userMinDeltaQp);
  p_cfg->i32userQpMin = ni_htonl(p_cfg->i32userQpMin);
  p_cfg->i32bitRate = ni_htonl(p_cfg->i32bitRate);
  p_cfg->i32bitRateBL = ni_htonl(p_cfg->i32bitRateBL);
  p_cfg->i32srcBitDepth = ni_htonl(p_cfg->i32srcBitDepth);
  p_cfg->hdrEnableVUI = ni_htonl(p_cfg->hdrEnableVUI);
  p_cfg->ui32VuiDataSizeBits = ni_htonl(p_cfg->ui32VuiDataSizeBits);
  p_cfg->ui32VuiDataSizeBytes = ni_htonl(p_cfg->ui32VuiDataSizeBytes);
  p_cfg->i32hwframes = ni_htonl(p_cfg->i32hwframes);
  p_cfg->ui16HDR10MaxLight = ni_htons(p_cfg->ui16HDR10MaxLight);
  p_cfg->ui16HDR10AveLight = ni_htons(p_cfg->ui16HDR10AveLight);
  p_cfg->ui16gdrDuration = ni_htons(p_cfg->ui16gdrDuration);
  p_cfg->ui32ltrRefInterval = ni_htonl(p_cfg->ui32ltrRefInterval);
  p_cfg->i32ltrRefQpOffset = ni_htonl(p_cfg->i32ltrRefQpOffset);
  p_cfg->ui32ltrFirstGap = ni_htonl(p_cfg->ui32ltrFirstGap);
  p_cfg->i32tolCtbRcInter = ni_htonl(p_cfg->i32tolCtbRcInter);
  p_cfg->i32tolCtbRcIntra = ni_htonl(p_cfg->i32tolCtbRcIntra);
  p_cfg->i16bitrateWindow = ni_htons(p_cfg->i16bitrateWindow);
  p_cfg->ui16hdr10_dx0 = ni_htons(p_cfg->ui16hdr10_dx0);
  p_cfg->ui16hdr10_dy0 = ni_htons(p_cfg->ui16hdr10_dy0);
  p_cfg->ui16hdr10_dx1 = ni_htons(p_cfg->ui16hdr10_dx1);
  p_cfg->ui16hdr10_dy1 = ni_htons(p_cfg->ui16hdr10_dy1);
  p_cfg->ui16hdr10_dx2 = ni_htons(p_cfg->ui16hdr10_dx2);
  p_cfg->ui16hdr10_dy2 = ni_htons(p_cfg->ui16hdr10_dy2);
  p_cfg->ui16hdr10_wx = ni_htons(p_cfg->ui16hdr10_wx);
  p_cfg->ui16hdr10_wy = ni_htons(p_cfg->ui16hdr10_wy);
  p_cfg->ui32hdr10_maxluma = ni_htonl(p_cfg->ui32hdr10_maxluma);
  p_cfg->ui32hdr10_minluma = ni_htonl(p_cfg->ui32hdr10_minluma);
  p_cfg->ui32lumaLinesize = ni_htons(p_cfg->ui32lumaLinesize);
  p_cfg->ui32chromaLinesize = ni_htons(p_cfg->ui32chromaLinesize);

  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_encoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_encoder_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  Send a p_config command to configure encoding p_frame parameters.
 *
 *  \param   ni_session_context_t p_ctx - xcoder Context
 *  \param   ni_encoder_frame_params_t * params - pointer to the encoder ni_encoder_frame_params_t struct
 *
 *  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
 *******************************************************************************/
ni_retcode_t ni_config_instance_set_encoder_frame_params(ni_session_context_t* p_ctx, ni_encoder_frame_params_t* p_params)
{
    ni_encoder_frame_params_t *p_cfg;
    uint32_t buffer_size = sizeof(ni_encoder_frame_params_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_params)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign((void **)&p_cfg, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
    return NI_RETCODE_ERROR_MEM_ALOC;
  }

  //configure the session here
  ui32LBA = CONFIG_INSTANCE_SetEncFramePara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

  //Flip the bytes!!
  p_cfg->force_picture_type = ni_htons(p_params->force_picture_type);
  p_cfg->data_format = ni_htons(p_params->data_format);
  p_cfg->picture_type = ni_htons(p_params->picture_type);
  p_cfg->video_width = ni_htons(p_params->video_width);
  p_cfg->video_height = ni_htons(p_params->video_height);
  p_cfg->timestamp = ni_htonl(p_params->timestamp);

  if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_cfg,
                             buffer_size, ui32LBA) < 0)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_encoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %d, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

  ni_aligned_free(p_cfg);
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

  return retval;
}

/*!******************************************************************************
 *  \brief  Get info from received p_frame
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_create_frame(ni_frame_t* p_frame, uint32_t read_length, uint64_t* p_frame_offset, bool is_hw_frame)
{
    uint32_t rx_size =
        read_length;   //get the length since its the only thing in DW10 now

    if (!p_frame || !p_frame_offset)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    *p_frame_offset = 0;

    unsigned int metadata_size = NI_FW_META_DATA_SZ -
        NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
    unsigned int video_data_size = p_frame->data_len[0] + p_frame->data_len[1] +
        p_frame->data_len[2] + ((is_hw_frame) ? p_frame->data_len[3] : 0);
    ni_log(NI_LOG_DEBUG, "rx_size = %d metadataSize = %d\n", rx_size,
           metadata_size);

    p_frame->p_custom_sei_set = NULL;

    if (rx_size == metadata_size)
    {
        video_data_size = 0;
    }

    if (rx_size > video_data_size)
    {
        ni_metadata_dec_frame_t *p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                        video_data_size);

        *p_frame_offset = p_meta->metadata_common.ui64_data.frame_offset;
        rx_size -= metadata_size;
        p_frame->crop_top = p_meta->metadata_common.crop_top;
        p_frame->crop_bottom = p_meta->metadata_common.crop_bottom;
        p_frame->crop_left = p_meta->metadata_common.crop_left;
        p_frame->crop_right = p_meta->metadata_common.crop_right;
        p_frame->ni_pict_type = p_meta->metadata_common.frame_type;

        p_frame->video_width = p_meta->metadata_common.frame_width;
        p_frame->video_height = p_meta->metadata_common.frame_height;

        ni_log(
            NI_LOG_DEBUG,
            "%s: [metadata] cropRight=%u, cropLeft=%u, "
            "cropBottom=%u, cropTop=%u, frame_offset=%" PRIu64 ", pic=%ux%u, "
            "pict_type=%d, crop=%ux%u, sei header: 0x%0x  number %u size %u\n",
            __func__, p_frame->crop_right, p_frame->crop_left,
            p_frame->crop_bottom, p_frame->crop_top,
            p_meta->metadata_common.ui64_data.frame_offset,
            p_meta->metadata_common.frame_width,
            p_meta->metadata_common.frame_height, p_frame->ni_pict_type,
            p_frame->crop_right - p_frame->crop_left,
            p_frame->crop_bottom - p_frame->crop_top, p_meta->sei_header,
            p_meta->sei_number, p_meta->sei_size);

        p_frame->sei_total_len = 0;
        p_frame->sei_cc_offset = 0;
        p_frame->sei_cc_len = 0;
        p_frame->sei_hdr_mastering_display_color_vol_offset = 0;
        p_frame->sei_hdr_mastering_display_color_vol_len = 0;
        p_frame->sei_hdr_content_light_level_info_offset = 0;
        p_frame->sei_hdr_content_light_level_info_len = 0;
        p_frame->sei_hdr_plus_offset = 0;
        p_frame->sei_hdr_plus_len = 0;
        p_frame->sei_user_data_unreg_offset = 0;
        p_frame->sei_user_data_unreg_len = 0;

        if (p_meta->sei_number)
        {
#if 1 // QUADRA_SEI_FMT
    ni_log(NI_LOG_DEBUG, "ui32SeiHeader 0x%x ui16SeiNumber %d ui16SeiSize %d SEI 0x%02x%02x\n",
      p_meta->sei_header, p_meta->sei_number, p_meta->sei_size,
      *((uint8_t*)p_meta + metadata_size),
      *((uint8_t*)p_meta + metadata_size+1));

     { // retrieve sei from new format
      uint16_t ui16SeiProcessed = 0;
      ni_sei_header_t * pEntryHeader = &p_meta->first_sei_header;
      uint32_t ui32Offset = 0;
      uint32_t ui32Size;

      rx_size -= p_meta->sei_size;

      do
      {
        ui16SeiProcessed++;

        if (pEntryHeader->status)
        {
          ui32Size = pEntryHeader->size;
        }
        else
        {
          ui32Size = 0;
        }

        ni_log(NI_LOG_DEBUG, "SEI #%x st %d size %d ty %d sz/of %u/%u 0x%02x%02x\n",
                       ui16SeiProcessed, pEntryHeader->status,
                       pEntryHeader->size, pEntryHeader->type, ui32Size,
                       ui32Offset,
                       *((uint8_t *)p_meta + metadata_size + ui32Offset),
                       *((uint8_t *)p_meta + metadata_size + ui32Offset + 1));

        // - if multiple entries with same SEI type/subtype, only the last entry is saved;
        //   consider to use sei_offset[] array instead of explicit sei_*_offset
        // - user_data_unreg (UDU) and custom sei passthru via HW and SW respectively
        //   thus custom SEI is not processed here as it is by SW.

        switch(pEntryHeader->type)
        {
          case 1: //SEI_TYPE_PIC_TIMING
            ni_log(NI_LOG_DEBUG,
                 "%s: SEI message dropped (unsupported - check "
                 "decoder SEI bitmap settings);"
                 " type %u size %u status %u offset %u payload bytes %u\n",
                 __func__, pEntryHeader->type, pEntryHeader->size,
                 pEntryHeader->status, ui32Offset, ui32Size);
            break;
          case 4: //HEVC_SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35
            if (ui32Size)
            {
              uint8_t *ptr = (uint8_t*)p_meta + metadata_size + ui32Offset;
              if(ptr[0] == NI_HDR10P_SEI_BYTE0 && ptr[1] == NI_HDR10P_SEI_BYTE1 &&
                 ptr[2] == NI_HDR10P_SEI_BYTE2 && ptr[3] == NI_HDR10P_SEI_BYTE3 &&
                 ptr[4] == NI_HDR10P_SEI_BYTE4 && ptr[5] == NI_HDR10P_SEI_BYTE5)
              {
                p_frame->sei_hdr_plus_len = ui32Size;
                p_frame->sei_hdr_plus_offset =
                video_data_size + metadata_size + ui32Offset;

                p_frame->sei_total_len += ui32Size;

                ni_log(NI_LOG_DEBUG, "%s: hdr10+ size=%u hdr10+ offset=%u\n",
                       __func__, p_frame->sei_hdr_plus_len,
                       p_frame->sei_hdr_plus_offset);
              }
              else if(ptr[0] == NI_CC_SEI_BYTE0 && ptr[1] == NI_CC_SEI_BYTE1 &&
                      ptr[2] == NI_CC_SEI_BYTE2 && ptr[3] == NI_CC_SEI_BYTE3 &&
                      ptr[4] == NI_CC_SEI_BYTE4 && ptr[5] == NI_CC_SEI_BYTE5 &&
                      ptr[6] == NI_CC_SEI_BYTE6 && ptr[7] == NI_CC_SEI_BYTE7)
              {
                  // Found CC data
                  // number of 3 byte close captions is bottom 5 bits of
                  // 9th byte of T35 payload
                  // uint32_t ui32CCSize = (ptr[8] & 0x1F) * 3; // avoid overwriting ui32CCSize

                  // return close caption data offset and length, and
                  // skip past 10 header bytes to close caption data
                  p_frame->sei_cc_len = (ptr[8] & 0x1F) * 3; // ui32CCSize;
                  p_frame->sei_cc_offset = video_data_size + metadata_size
                  + ui32Offset + 10;

                  p_frame->sei_total_len += p_frame->sei_cc_len;

                  ni_log(NI_LOG_DEBUG,
                         "%s: close caption size %u ,"
                         "offset %u = video size %u meta size %d off "
                         " %u + 10\n",
                         __func__, p_frame->sei_cc_len, p_frame->sei_cc_offset,
                         video_data_size, metadata_size, ui32Offset);
              }
              else
              {
                  ni_log(NI_LOG_DEBUG,
                         "%s: unsupported T35; type %u size %u status %u "
                         "offset %u\n",
                         __func__, pEntryHeader->type, pEntryHeader->size,
                         pEntryHeader->status, ui32Offset);
              }
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: T35 SEI dropped due to %s; type %u size %u status %u offset %u\n",
                       __func__, pEntryHeader->status ? "missing payload" : "payload size exceeded SEI buffer size",
                       pEntryHeader->type, pEntryHeader->size, pEntryHeader->status, ui32Offset);
            }
            break;

          case 5: // HEVC_SEI_TYPE_USER_DATA_UNREGISTERED
            // set offset now so len=0 will signify an error if this SEI is dropped
            p_frame->sei_user_data_unreg_offset = video_data_size + metadata_size + ui32Offset;
            if (ui32Size)
            {
              p_frame->sei_user_data_unreg_len = ui32Size;
              p_frame->sei_total_len += ui32Size;
              ni_log(NI_LOG_DEBUG, "User Data Unreg size = %u\n", ui32Size);
            }
            else
            {
              p_frame->sei_user_data_unreg_len = 0; // in case there are multiple UDU SEI entries
              ni_log(NI_LOG_ERROR,
                     "Error %s: User Data Unreg dropped due to %s; type %u size %u status %u offset %u\n",
                     __func__, pEntryHeader->status ? "missing payload" : "payload size exceeded SEI buffer size",
                     pEntryHeader->type, pEntryHeader->size, pEntryHeader->status, ui32Offset);
            }
            break;

          case 137: //HEVC_SEI_TYPE_MASTERING_DISPLAY_INFO
            if (ui32Size)
            {
              p_frame->sei_hdr_mastering_display_color_vol_len = ui32Size;
              p_frame->sei_hdr_mastering_display_color_vol_offset =
              video_data_size + metadata_size + ui32Offset;

              p_frame->sei_total_len += ui32Size;
              ni_dec_mastering_display_colour_volume_bytes_t *pColourVolume =
                  (ni_dec_mastering_display_colour_volume_bytes_t
                       *)((uint8_t *)p_meta + metadata_size + ui32Offset);

              ni_log(NI_LOG_DEBUG, "Display Primaries x[0]=%u y[0]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[0][0]),
                             ni_ntohs(pColourVolume->display_primaries[0][1]));
              ni_log(NI_LOG_DEBUG, "Display Primaries x[1]=%u y[1]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[1][0]),
                             ni_ntohs(pColourVolume->display_primaries[1][1]));
              ni_log(NI_LOG_DEBUG, "Display Primaries x[2]=%u y[2]=%u\n",
                             ni_ntohs(pColourVolume->display_primaries[2][0]),
                             ni_ntohs(pColourVolume->display_primaries[2][1]));

              ni_log(NI_LOG_DEBUG, "White Point x=%u y=%u\n",
                             ni_ntohs(pColourVolume->white_point_x),
                             ni_ntohs(pColourVolume->white_point_y));
              ni_log(NI_LOG_DEBUG, "Display Mastering Lum, Max=%u Min=%u\n",
                             ni_ntohl(pColourVolume->max_display_mastering_luminance),
                             ni_ntohl(pColourVolume->min_display_mastering_luminance));
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: mastering display info dropped due to %s; type %u size %u status %u offset %u\n",
                       __func__, pEntryHeader->status ? "missing payload" : "payload size exceeded SEI buffer size",
                       pEntryHeader->type, pEntryHeader->size, pEntryHeader->status, ui32Offset);
            }
            break;

          case 144: //HEVC_SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO
            if (ui32Size)
            {
              p_frame->sei_hdr_content_light_level_info_len = ui32Size;
              p_frame->sei_hdr_content_light_level_info_offset =
              video_data_size + metadata_size + ui32Offset;

              p_frame->sei_total_len += ui32Offset;

              ni_content_light_level_info_bytes_t* pLightLevel =
                (ni_content_light_level_info_bytes_t*)(
                  (uint8_t*)p_meta + metadata_size + ui32Offset);

              ni_log(NI_LOG_DEBUG, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
                             ni_ntohs(pLightLevel->max_content_light_level),
                             ni_ntohs(pLightLevel->max_pic_average_light_level));
            }
            else
            {
                ni_log(NI_LOG_ERROR,
                       "Error %s: content light level info dropped due to %s; type %u size %u status %u offset %u\n",
                       __func__, pEntryHeader->status ? "missing payload" : "payload size exceeded SEI buffer size",
                       pEntryHeader->type, pEntryHeader->size, pEntryHeader->status, ui32Offset);
            }
            break;

          case 200:   // Custom SEI not included in HEVC_SEI_Type
              if (ui32Size)
              {
                  p_frame->vui_len = ui32Size;
                  p_frame->vui_offset =
                      video_data_size + metadata_size + ui32Offset;
                  p_frame->sei_total_len += ui32Size;
              }
              break;
          case 206:   // Customer reset ppu resolution, need dropped
              ni_log(NI_LOG_DEBUG, "Custom SEI PPU_RECONFIG dropped", __func__);
              break;
          default:
              ni_log(NI_LOG_ERROR,
                     "Warning %s: SEI message dropped (unsupported - check "
                     "decoder SEI bitmap settings);"
                     " type %u size %u status %u offset %u payload bytes %u\n",
                     __func__, pEntryHeader->type, pEntryHeader->size,
                     pEntryHeader->status, ui32Offset, ui32Size);
              break;
        }
        ui32Offset += ui32Size;
        pEntryHeader = (ni_sei_header_t *)((uint8_t*)p_meta + metadata_size + ui32Offset);
        ui32Offset += sizeof(ni_sei_header_t);
      } while (ui32Offset <= p_meta->sei_size && ui16SeiProcessed < p_meta->sei_number);

      if (p_meta->sei_number != ui16SeiProcessed)
      {
          ni_log(
              NI_LOG_ERROR,
              "Error %s: number of SEI messages reported %u != processed %u\n",
              __func__, p_meta->sei_number, ui16SeiProcessed);
      }
     }

#else // QUADRA_SEI_FMT

      // ni_assert(p_meta->sei_header);
      // else // backward compatibility
     {
      ni_sei_user_data_entry_t *pEntry;
      uint32_t ui32CCOffset = 0, ui32CCSize = 0;

      rx_size -= p_meta->sei_size;

      pEntry = (ni_sei_user_data_entry_t *)((uint8_t*)p_meta + metadata_size);

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_HDR10_PLUS, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_plus_len = ui32CCSize;
        p_frame->sei_hdr_plus_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_DEBUG, "%s: hdr10+ size=%u hdr10+ offset=%u\n", __func__,
               p_frame->sei_hdr_plus_len, p_frame->sei_hdr_plus_offset);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: hdr+ NOT found in meta data!\n", __func__);
      }

      if (find_t35_sei(p_meta->sei_header, NI_T35_SEI_CLOSED_CAPTION, pEntry,
                       &ui32CCOffset, &ui32CCSize))
      {
        uint8_t *ptr;
        // Found CC data at pEntry + ui32CCOffset
        ptr = (uint8_t*)pEntry + ui32CCOffset;
        // number of 3 byte close captions is bottom 5 bits of
        // 9th byte of T35 payload
        ui32CCSize = (ptr[8] & 0x1F) * 3;

        // return close caption data offset and length, and
        // skip past 10 header bytes to close caption data
        p_frame->sei_cc_len = ui32CCSize;
        p_frame->sei_cc_offset = video_data_size + metadata_size
        + ui32CCOffset + 10;

        p_frame->sei_total_len += p_frame->sei_cc_len;

        ni_log(NI_LOG_DEBUG,
               "%s: close caption size %u ,"
               "offset %u = video size %u meta size %u off "
               " %u + 10\n",
               __func__, p_frame->sei_cc_len, p_frame->sei_cc_offset,
               video_data_size, metadata_size, ui32CCOffset);
      }
      else
      {
          ni_log(NI_LOG_DEBUG, "%s: close caption NOT found in meta data!\n",
                 __func__);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_MASTERING_COLOR_VOL,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_mastering_display_color_vol_len = ui32CCSize;
        p_frame->sei_hdr_mastering_display_color_vol_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_dec_mastering_display_colour_volume_t* pColourVolume =
        (ni_dec_mastering_display_colour_volume_t*)((uint8_t*)pEntry + ui32CCOffset);

        ni_log(NI_LOG_DEBUG, "Display Primaries x[0]=%u y[0]=%u\n",
                       pColourVolume->display_primaries_x[0],
                       pColourVolume->display_primaries_y[0]);
        ni_log(NI_LOG_DEBUG, "Display Primaries x[1]=%u y[1]=%u\n",
                       pColourVolume->display_primaries_x[1],
                       pColourVolume->display_primaries_y[1]);
        ni_log(NI_LOG_DEBUG, "Display Primaries x[2]=%u y[2]=%u\n",
                       pColourVolume->display_primaries_x[2],
                       pColourVolume->display_primaries_y[2]);

        ni_log(NI_LOG_DEBUG, "White Point x=%u y=%u\n",
                       pColourVolume->white_point_x,
                       pColourVolume->white_point_y);
        ni_log(NI_LOG_DEBUG, "Display Mastering Lum, Max=%u Min=%u\n",
                       pColourVolume->max_display_mastering_luminance, pColourVolume->min_display_mastering_luminance);
      }
      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USER_DATA_FLAG_CONTENT_LIGHT_LEVEL_INFO,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_hdr_content_light_level_info_len = ui32CCSize;
        p_frame->sei_hdr_content_light_level_info_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_content_light_level_info_t* pLightLevel =
        (ni_content_light_level_info_t*)((uint8_t*)pEntry + ui32CCOffset);
        ni_log(NI_LOG_DEBUG, "Max Content Light level=%u Max Pic Avg Light Level=%u\n",
                       pLightLevel->max_content_light_level, pLightLevel->max_pic_average_light_level);
      }

      if (find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_PRE,
                   &ui32CCOffset, &ui32CCSize) ||
          find_sei(p_meta->sei_header, pEntry,
                   NI_H265_USERDATA_FLAG_UNREGISTERED_SUF,
                   &ui32CCOffset, &ui32CCSize))
      {
        p_frame->sei_user_data_unreg_len = ui32CCSize;
        p_frame->sei_user_data_unreg_offset =
        video_data_size + metadata_size + ui32CCOffset;

        p_frame->sei_total_len += ui32CCSize;

        ni_log(NI_LOG_DEBUG, "User Data Unreg size = %u\n", ui32CCSize);
      }
     }
#endif // QUADRA_SEI_FMT
      if (0 == p_frame->sei_total_len)
      {
        ni_log(NI_LOG_DEBUG, "Warning retrieved 0 supported SEI !\n");
      }
        }
    }

    p_frame->dts = NI_NOPTS_VALUE;
    p_frame->pts = NI_NOPTS_VALUE;
    //p_frame->end_of_stream = isEndOfStream;
    p_frame->start_of_stream = 0;

    if (rx_size == 0)
    {
        p_frame->data_len[0] = 0;
        p_frame->data_len[1] = 0;
        p_frame->data_len[2] = 0;
        p_frame->data_len[3] = 0;
    }

  ni_log(NI_LOG_DEBUG, "received [0x%08x] data size: %d, end of stream=%u\n",
                 read_length, rx_size, p_frame->end_of_stream);

  return rx_size;
}

/*!******************************************************************************
 *  \brief  Get info from received xcoder capability
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_populate_device_capability_struct(ni_device_capability_t* p_cap, void* p_data,
                          ni_device_handle_t device_handle, bool device_in_ctxt)
{
    int i, total_types = 0, total_modules = 0;
    ni_nvme_identity_t *p_id_data = (ni_nvme_identity_t *)p_data;

    COMPILE_ASSERT(sizeof(p_cap->xcoder_cnt) <= sizeof(p_id_data->xcoder_cnt) &&
                   NI_DEVICE_TYPE_XCODER_MAX *
                           sizeof(p_id_data->xcoder_cnt[0]) ==
                       sizeof(p_cap->xcoder_cnt));

    if (!p_cap || !p_data)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s(): Null pointer parameters passed\n",
               __func__);
        LRETURN;
    }

  if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
    (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
  {
      if (device_in_ctxt)
      {
          ni_log(NI_LOG_ERROR,
                "ERROR: Previously in context device got an invalid vendor ID 0x%X SSVID 0x%X. Netint "
                "ID 0x%X. Retrying\n",
                p_id_data->ui16Vid, p_id_data->ui16Ssvid, NETINT_PCI_VENDOR_ID);
          //print some other fields as a test to see if they are invalid too
          ni_log(NI_LOG_ERROR, "Model Number: %.*s\n",
                 (int)sizeof(p_id_data->ai8Sn), p_id_data->ai8Sn);
          ni_log(NI_LOG_ERROR, "Serial Number: %.*s\n",
                 (int)sizeof(p_id_data->ai8Mn), p_id_data->ai8Mn);
          ni_log(NI_LOG_ERROR, "Firmware Revision: %.*s\n",
                 (int)sizeof(p_id_data->ai8Fr), p_id_data->ai8Fr);
          ni_log(NI_LOG_ERROR, "xcoder_num_elements: %d\n",
                 p_id_data->xcoder_num_elements);

          ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
          uint32_t ui32LBA = IDENTIFY_DEVICE_R;
          if (ni_nvme_send_read_cmd(device_handle, event_handle, p_data,
                                    NI_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
          {
            LRETURN;
          }
          if ((p_id_data->ui16Vid != NETINT_PCI_VENDOR_ID) ||
              (p_id_data->ui16Ssvid != NETINT_PCI_VENDOR_ID))
          {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): Retry got an invalid vendor ID too 0x%X SSVID "
                   "0x%X!\n",
                   __func__, p_id_data->ui16Vid, p_id_data->ui16Ssvid);
            //print some other fields as a test to see if they are invalid too
            ni_log(NI_LOG_ERROR, "Model Number: %.*s\n",
                   (int)sizeof(p_id_data->ai8Sn), p_id_data->ai8Sn);
            ni_log(NI_LOG_ERROR, "Serial Number: %.*s\n",
                   (int)sizeof(p_id_data->ai8Mn), p_id_data->ai8Mn);
            ni_log(NI_LOG_ERROR, "Firmware Revision: %.*s\n",
                   (int)sizeof(p_id_data->ai8Fr), p_id_data->ai8Fr);
            ni_log(NI_LOG_ERROR, "xcoder_num_elements: %d\n",
                   p_id_data->xcoder_num_elements);
            LRETURN;
          }
          else
          {
            ni_log(NI_LOG_ERROR,
                   "Retry got valid a vendor ID 0x%X SSVID 0x%X. Netint ID 0x%X\n",
                   p_id_data->ui16Vid, p_id_data->ui16Ssvid, NETINT_PCI_VENDOR_ID);
          }
      }
      else
      {
          LRETURN;
      }
  }

  memcpy(p_cap->serial_number, p_id_data->ai8Sn, sizeof(p_cap->serial_number));
  memcpy(p_cap->model_number, p_id_data->ai8Mn, sizeof(p_cap->model_number));

  memset(p_cap->fw_rev, 0, sizeof(p_cap->fw_rev));
  memcpy(p_cap->fw_rev, p_id_data->ai8Fr, sizeof(p_cap->fw_rev));
  ni_log(NI_LOG_DEBUG, "F/W rev: %.*s\n", (int)sizeof(p_cap->fw_rev),
                 p_cap->fw_rev);

  if (p_id_data->xcoder_num_elements)
  {
      ni_log(NI_LOG_DEBUG, "xcoder_num_elements: %d xcoder_num_devices: %d\n",
                     p_id_data->xcoder_num_elements,
                     p_id_data->xcoder_num_devices);

      for (i = 0; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
      {
          if (p_id_data->xcoder_cnt[i])
          {
              total_types++;
              total_modules += p_id_data->xcoder_cnt[i];
              ni_log(NI_LOG_DEBUG, "type #%d: xcoder_cnt[%d] = %d\n", total_types, i,
                             p_id_data->xcoder_cnt[i]);
          }
      }

      if (p_id_data->xcoder_num_elements != total_types ||
          p_id_data->xcoder_num_devices != total_modules)
      {
          ni_log(NI_LOG_ERROR,
                 "Error: mismatch; xcoder_num_elements: %d (calculated: %d) "
                 "xcoder_num_devices: %d (calculated: %d)\n",
                 p_id_data->xcoder_num_elements, total_types,
                 p_id_data->xcoder_num_devices, total_modules);
          LRETURN;
      }

      p_cap->device_is_xcoder = NI_XCODER_QUADRA;
      p_cap->hw_elements_cnt = p_id_data->xcoder_num_elements;
      p_cap->xcoder_devices_cnt = p_id_data->xcoder_num_devices;
      memcpy(p_cap->xcoder_cnt, p_id_data->xcoder_cnt,
             NI_DEVICE_TYPE_XCODER_MAX * sizeof(p_id_data->xcoder_cnt[0]));

      for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
      {
          if (!p_id_data->xcoder_cnt[i])
              continue;

          p_cap->xcoder_devices[i].hw_id = p_id_data->xcoder_devices[i].hw_id;
          p_cap->xcoder_devices[i].max_number_of_contexts =
              p_id_data->xcoder_devices[i].hw_max_number_of_contexts;
          p_cap->xcoder_devices[i].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
          p_cap->xcoder_devices[i].codec_format =
              p_id_data->xcoder_devices[i].hw_codec_format;
          p_cap->xcoder_devices[i].codec_type =
              p_id_data->xcoder_devices[i].hw_codec_type;
          p_cap->xcoder_devices[i].max_video_width = NI_PARAM_MAX_WIDTH;
          p_cap->xcoder_devices[i].max_video_height = NI_PARAM_MAX_HEIGHT;
          if (i == NI_DEVICE_TYPE_ENCODER)
          {
            p_cap->xcoder_devices[i].min_video_width = NI_ENC_MIN_RESOLUTION_WIDTH;
            p_cap->xcoder_devices[i].min_video_height = NI_ENC_MIN_RESOLUTION_HEIGHT;
          }
          else
          {
            p_cap->xcoder_devices[i].min_video_width = NI_MIN_RESOLUTION_WIDTH;
            p_cap->xcoder_devices[i].min_video_height = NI_MIN_RESOLUTION_HEIGHT;
          }

          p_cap->xcoder_devices[i].video_profile =
              p_id_data->xcoder_devices[i].hw_video_profile;
          p_cap->xcoder_devices[i].video_level =
              p_id_data->xcoder_devices[i].hw_video_level;
      }

      goto CAP_POPULATED;
  }

  p_cap->device_is_xcoder = p_id_data->device_is_xcoder;
  ni_log(NI_LOG_DEBUG, "device_is_xcoder: value in id cmd: %u\n",
         p_cap->device_is_xcoder);
  if (0 == p_cap->device_is_xcoder)
  {
      ni_log(NI_LOG_ERROR, "Not an xcoder device !\n");

      if (device_in_ctxt)
      {
          ni_log(NI_LOG_ERROR,
                 "ERROR: Previously in context device is not a xcoder device "
                 "now!\n");
      }
      LRETURN;
  }

  p_cap->hw_elements_cnt = p_id_data->xcoder_num_hw;

  total_modules = p_cap->xcoder_cnt[NI_DEVICE_TYPE_DECODER] =
      p_id_data->xcoder_num_h264_decoder_hw +
      p_id_data->xcoder_num_h265_decoder_hw;

  p_cap->xcoder_cnt[NI_DEVICE_TYPE_ENCODER] =
      p_id_data->xcoder_num_h264_encoder_hw +
      p_id_data->xcoder_num_h265_encoder_hw;

  total_modules += p_cap->xcoder_cnt[NI_DEVICE_TYPE_ENCODER];

  p_cap->xcoder_devices_cnt = total_modules;

  if (total_modules >= 1)
  {
    p_cap->xcoder_devices[0].hw_id = p_id_data->hw0_id;
    p_cap->xcoder_devices[0].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[0].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[0].codec_format = p_id_data->hw0_codec_format;
    p_cap->xcoder_devices[0].codec_type = p_id_data->hw0_codec_type;
    p_cap->xcoder_devices[0].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[0].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[0].min_video_width = NI_MIN_RESOLUTION_WIDTH;
    p_cap->xcoder_devices[0].min_video_height = NI_MIN_RESOLUTION_HEIGHT;
    p_cap->xcoder_devices[0].video_profile = p_id_data->hw0_video_profile;
    p_cap->xcoder_devices[0].video_level = p_id_data->hw0_video_level;
  }
  if (total_modules >= 2)
  {
    p_cap->xcoder_devices[1].hw_id = p_id_data->hw1_id;
    p_cap->xcoder_devices[1].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[1].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[1].codec_format = p_id_data->hw1_codec_format;
    p_cap->xcoder_devices[1].codec_type = p_id_data->hw1_codec_type;
    p_cap->xcoder_devices[1].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[1].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[1].min_video_width = NI_ENC_MIN_RESOLUTION_WIDTH;
    p_cap->xcoder_devices[1].min_video_height = NI_ENC_MIN_RESOLUTION_HEIGHT;
    p_cap->xcoder_devices[1].video_profile = p_id_data->hw1_video_profile;
    p_cap->xcoder_devices[1].video_level = p_id_data->hw1_video_level;
  }
  if (total_modules >= 3)
  {
    p_cap->xcoder_devices[2].hw_id = p_id_data->hw2_id;
    p_cap->xcoder_devices[2].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[2].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[2].codec_format = p_id_data->hw2_codec_format;
    p_cap->xcoder_devices[2].codec_type = p_id_data->hw2_codec_type;
    p_cap->xcoder_devices[2].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[2].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[2].min_video_width = NI_MIN_RESOLUTION_WIDTH;
    p_cap->xcoder_devices[2].min_video_height = NI_MIN_RESOLUTION_HEIGHT;
    p_cap->xcoder_devices[2].video_profile = p_id_data->hw2_video_profile;
    p_cap->xcoder_devices[2].video_level = p_id_data->hw2_video_level;
  }
  if (total_modules >= 4)
  {
    p_cap->xcoder_devices[3].hw_id = p_id_data->hw3_id;
    p_cap->xcoder_devices[3].max_number_of_contexts =
        NI_MAX_CONTEXTS_PER_HW_INSTANCE;
    p_cap->xcoder_devices[3].max_4k_fps = NI_MAX_4K_FPS_QUADRA;
    p_cap->xcoder_devices[3].codec_format = p_id_data->hw3_codec_format;
    p_cap->xcoder_devices[3].codec_type = p_id_data->hw3_codec_type;
    p_cap->xcoder_devices[3].max_video_width = NI_PARAM_MAX_WIDTH;
    p_cap->xcoder_devices[3].max_video_height = NI_PARAM_MAX_HEIGHT;
    p_cap->xcoder_devices[3].min_video_width = NI_MIN_RESOLUTION_WIDTH;
    p_cap->xcoder_devices[3].min_video_height = NI_MIN_RESOLUTION_HEIGHT;
    p_cap->xcoder_devices[3].video_profile = p_id_data->hw3_video_profile;
    p_cap->xcoder_devices[3].video_level = p_id_data->hw3_video_level;
  }

CAP_POPULATED:

    for (i = 0; i < NI_MAX_DEVICES_PER_HW_INSTANCE; i++)
    {
        ni_log(NI_LOG_DEBUG, "HW%d hw_id: %d\n", i, p_cap->xcoder_devices[i].hw_id);
        ni_log(NI_LOG_DEBUG, "HW%d max_number_of_contexts: %d\n", i,
                       p_cap->xcoder_devices[i].max_number_of_contexts);
        ni_log(NI_LOG_DEBUG, "HW%d max_4k_fps: %d\n", i,
               p_cap->xcoder_devices[i].max_4k_fps);
        ni_log(NI_LOG_DEBUG, "HW%d codec_format: %d\n", i,
                       p_cap->xcoder_devices[i].codec_format);
        ni_log(NI_LOG_DEBUG, "HW%d codec_type: %d\n", i,
                       p_cap->xcoder_devices[i].codec_type);
        ni_log(NI_LOG_DEBUG, "HW%d max_video_width: %d\n", i,
                       p_cap->xcoder_devices[i].max_video_width);
        ni_log(NI_LOG_DEBUG, "HW%d max_video_height: %d\n", i,
                       p_cap->xcoder_devices[i].max_video_height);
        ni_log(NI_LOG_DEBUG, "HW%d min_video_width: %d\n", i,
                       p_cap->xcoder_devices[i].min_video_width);
        ni_log(NI_LOG_DEBUG, "HW%d min_video_height: %d\n", i,
                       p_cap->xcoder_devices[i].min_video_height);
        ni_log(NI_LOG_DEBUG, "HW%d video_profile: %d\n", i,
                       p_cap->xcoder_devices[i].video_profile);
        ni_log(NI_LOG_DEBUG, "HW%d video_level: %d\n", i,
                       p_cap->xcoder_devices[i].video_level);
    }

    memset(p_cap->fw_branch_name, 0, sizeof(p_cap->fw_branch_name));
    memcpy(p_cap->fw_branch_name, p_id_data->fw_branch_name,
           sizeof(p_cap->fw_branch_name) - 1);
    ni_log(NI_LOG_DEBUG, "F/W branch name: %s\n", p_cap->fw_branch_name);
    memset(p_cap->fw_commit_time, 0, sizeof(p_cap->fw_commit_time));
    memcpy(p_cap->fw_commit_time, p_id_data->fw_commit_time,
           sizeof(p_cap->fw_commit_time) - 1);
    ni_log(NI_LOG_DEBUG, "F/W commit time: %s\n", p_cap->fw_commit_time);
    memset(p_cap->fw_commit_hash, 0, sizeof(p_cap->fw_commit_hash));
    memcpy(p_cap->fw_commit_hash, p_id_data->fw_commit_hash,
           sizeof(p_cap->fw_commit_hash) - 1);
    ni_log(NI_LOG_DEBUG, "F/W commit hash: %s\n", p_cap->fw_commit_hash);
    memset(p_cap->fw_build_time, 0, sizeof(p_cap->fw_build_time));
    memcpy(p_cap->fw_build_time, p_id_data->fw_build_time,
           sizeof(p_cap->fw_build_time) - 1);
    ni_log(NI_LOG_DEBUG, "F/W build time: %s\n", p_cap->fw_build_time);
    memset(p_cap->fw_build_id, 0, sizeof(p_cap->fw_build_id));
    memcpy(p_cap->fw_build_id, p_id_data->fw_build_id,
           sizeof(p_cap->fw_build_id) - 1);
    ni_log(NI_LOG_DEBUG, "F/W build id: %s\n", p_cap->fw_build_id);

END:
    return;
}

static uint32_t presetGopSize[] = {
  1, /*! Custom GOP, Not used */
  1, /*! All Intra */
  1, /*! IPP Cyclic GOP size 1 */
  1, /*! IBB Cyclic GOP size 1 */
  2, /*! IBP Cyclic GOP size 2 */
  4, /*! IBBBP */
  4,
  4,
  8 };

/*!******************************************************************************
 *  \brief  insert the 32 bits of integer value at bit position pos
 *
 *  \param int pos, int value
 *
 *  \return void
 ******************************************************************************/
void ni_fix_VUI(uint8_t *vui, int pos, int value)
{
  int pos_byte    = (pos/8);
  int pos_in_byte = pos%8;
  int remaining_bytes_in_current_byte = 8 - pos_in_byte;

  if (pos_in_byte == 0) // at beginning of the byte
  {
    vui[pos_byte] = (uint8_t)(value >> 24);
    vui[pos_byte+1] = (uint8_t)(value >> 16);
    vui[pos_byte+2] = (uint8_t)(value >> 8);
    vui[pos_byte+3] = (uint8_t)(value);
  }
  else
  {
    vui[pos_byte]   = vui[pos_byte] + (uint8_t)(value >> (32-remaining_bytes_in_current_byte));
    vui[pos_byte+1] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-8));
    vui[pos_byte+2] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-16));
    vui[pos_byte+3] = (uint8_t)(value >> (32-remaining_bytes_in_current_byte-24));
    vui[pos_byte+4] = vui[pos_byte+4] + ((uint8_t)(value << remaining_bytes_in_current_byte));
  }

}

/*!******************************************************************************
*  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
*
*  \param
*
*  \return
******************************************************************************/
void ni_set_custom_dec_template(ni_session_context_t *p_ctx,
                                ni_decoder_config_t *p_cfg,
                                ni_xcoder_params_t *p_src,
                                uint32_t max_pkt_size)
{
  int i,j;
  ni_decoder_input_params_t* p_dec = NULL;
  if ((!p_ctx) || (!p_cfg) || (!p_src))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
           __func__);
    return;
  }
  p_dec = &p_src->dec_input_params;
  int w = p_src->source_width;
  int h = p_src->source_height;
  bool shift_params = false;

  p_cfg->ui8HWFrame = p_dec->hwframes;
  p_cfg->ui8MCMode = p_dec->mcmode;
  p_cfg->ui8UduSeiEnabled = p_ctx->enable_user_data_sei_passthru || p_dec->enable_user_data_sei_passthru;
  p_cfg->ui16MaxSeiDataSize = NI_MAX_SEI_DATA;
  p_cfg->ui8DisablePictureReordering = p_dec->decoder_low_delay > 0;
  p_ctx->force_low_delay = p_dec->force_low_delay;
  p_cfg->ui8EnablelowDelayCheck = p_dec->enable_low_delay_check == 1;
  p_cfg->ui32SourceWidth = w;
  p_cfg->ui32SourceHeight = h;

  if (max_pkt_size)
  {
    p_cfg->ui32MaxPktSize = max_pkt_size;
  } else {
    // p_cfg->ui32MaxPktSize = width x height x 3/2 x min compression ratio(QP=0);
    // set min compression ratio = 1/2, so MaxPktSize = w * h * 3/4
    p_cfg->ui32MaxPktSize = w * h * 3 / 4;
  }
  // packet buffer aligned to NI_MAX_PACKET_SZ(128k)
  p_cfg->ui32MaxPktSize =
      (((p_cfg->ui32MaxPktSize) / NI_MAX_PACKET_SZ) + 1) * NI_MAX_PACKET_SZ;

  p_cfg->fps_number =
      ((ni_xcoder_params_t *)p_ctx->p_session_config)->fps_number;
  p_cfg->fps_denominator =
      ((ni_xcoder_params_t *)p_ctx->p_session_config)->fps_denominator;
  ni_log2(p_ctx, NI_LOG_INFO, "%s height %d width %d fps_number %d fps_denominator %d\n",
          __func__, h, w, p_cfg->fps_number, p_cfg->fps_denominator);

  p_cfg->asOutputConfig[0].ui8Enabled = 1;   // always enabled
  p_cfg->asOutputConfig[1].ui8Enabled = p_dec->enable_out1;
  p_cfg->asOutputConfig[2].ui8Enabled = p_dec->enable_out2;
  if (p_cfg->asOutputConfig[2].ui8Enabled && p_cfg->asOutputConfig[1].ui8Enabled == 0)
  {
    p_cfg->asOutputConfig[1].ui8Enabled = 1;
    p_cfg->asOutputConfig[2].ui8Enabled = 0;
    shift_params = true;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "Output 2 used before output 1, Shifting output2 settings to output1 and disabling output 2\n");
  }

  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    if (!shift_params || i == 0)
    {
      j = i;
    }
    else
    {
      j = (i == 1) ? 2 : 1; //swap settings
    }
    p_cfg->asOutputConfig[i].ui8Force8Bit = p_dec->force_8_bit[j];
    p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled = p_dec->semi_planar[j];
    p_cfg->asOutputConfig[i].ui8CropMode = p_dec->crop_mode[j];
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16W =
        (uint16_t)((p_dec->crop_whxy[j][0]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16H =
        (uint16_t)((p_dec->crop_whxy[j][1]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16X =
        (uint16_t)((p_dec->crop_whxy[j][2]) & 0xFFFE);
    p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y =
        (uint16_t)((p_dec->crop_whxy[j][3]) & 0xFFFE);

    //Offset resized if out of bounds
    if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16X + p_cfg->asOutputConfig[i].sCroppingRectable.ui16W > w)
    {
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16X = w - p_cfg->asOutputConfig[i].sCroppingRectable.ui16W;
    }
    if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y + p_cfg->asOutputConfig[i].sCroppingRectable.ui16H > h)
    {
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y = h - p_cfg->asOutputConfig[i].sCroppingRectable.ui16H;
    }

    if (p_dec->enable_ppu_scale_adapt)
    {
      p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt = p_dec->enable_ppu_scale_adapt;
    }
    else if (p_dec->scale_long_short_edge[j] == 1)
    {
      p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt = 3;
    }
    else if (p_dec->scale_long_short_edge[j] == 2)
    {
      p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt = 4;
    }
    else
    {
      p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt = 0;
    }
    p_cfg->asOutputConfig[i].ui8EnablePpuScaleLimit = p_dec->enable_ppu_scale_limit;

    if (p_dec->scale_round[j] == -1)
    {
      if (p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt == 1 ||
        p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt == 2)
      {
        p_cfg->asOutputConfig[i].ui8ScaleResCeil = p_dec->scale_resolution_ceil[j] - 1;
      }
      else
      {
        p_cfg->asOutputConfig[i].ui8ScaleResCeil = p_dec->scale_resolution_ceil[j];
      }
    }
    else if (p_dec->scale_round[j] == 0)
    {
      p_cfg->asOutputConfig[i].ui8ScaleResCeil = p_dec->scale_resolution_ceil[j];
    }
    else
    {
      p_cfg->asOutputConfig[i].ui8ScaleResCeil = p_dec->scale_resolution_ceil[j] - 1;
    }

    p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width =
        (uint16_t)((p_dec->scale_wh[j][0]+1) & 0xFFFE);
    p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height =
        (uint16_t)((p_dec->scale_wh[j][1]+1) & 0xFFFE);

    if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width ||
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height)
    {
      p_cfg->asOutputConfig[i].ui8ScaleEnabled = 1;
    }
    else
    {
      p_cfg->asOutputConfig[i].ui8ScaleEnabled = 0;
    }
  }

  p_cfg->ui8MaxExtraHwFrameCnt = p_dec->max_extra_hwframe_cnt;
  if (p_cfg->ui8MaxExtraHwFrameCnt != 255 &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rB") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): maxExtraHwFrameCnt is not support for FW < 6rB\n", __func__);
  }
  p_cfg->ui8EcPolicy = p_dec->ec_policy;
  p_cfg->ui8EnableAdvancedEc = p_dec->enable_advanced_ec;
  p_cfg->ui32ErrRatioThreshold = p_dec->error_ratio_threshold;
  if (p_cfg->ui8EnableAdvancedEc == 2 &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rO") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): (enableAdvancedEc == 2) is not support for FW < 6rO\n", __func__);
    p_cfg->ui8EnableAdvancedEc = 1;
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): reset enableAdvancedEc to %d\n", __func__, p_cfg->ui8EnableAdvancedEc);
  }
  if (p_cfg->ui8EcPolicy == NI_EC_POLICY_LIMITED_ERROR &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6ri") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): (EcPolicy == limited_error) not supported for FW < 6ri\n", __func__);
    p_cfg->ui8EcPolicy = NI_EC_POLICY_DEFAULT;
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): reset EcPolicy to %d\n", __func__, p_cfg->ui8EcPolicy);
  }
  if (p_cfg->ui32ErrRatioThreshold != NI_EC_ERR_THRESHOLD_DEFAULT &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6ri") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): setting ecErrThreshold not supported for FW < 6ri\n", __func__);
    p_cfg->ui32ErrRatioThreshold = NI_EC_ERR_THRESHOLD_DEFAULT;
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): reset ecErrThreshold to %d\n", __func__, NI_EC_ERR_THRESHOLD_DEFAULT);
  }
  if (p_cfg->ui8EcPolicy == NI_EC_POLICY_BEST_EFFORT_OUT_DC &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6s1") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): (EcPolicy == best_effort_out_dc) not supported for FW < 6s1\n", __func__);
    p_cfg->ui8EcPolicy = NI_EC_POLICY_DEFAULT;
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): reset EcPolicy to %d\n", __func__, p_cfg->ui8EcPolicy);
  }
  p_cfg->ui8DisableAdaptiveBuffers = p_dec->disable_adaptive_buffers;
  p_cfg->ui8SurviveStreamErr = p_dec->survive_stream_err;
  if (p_cfg->ui8SurviveStreamErr != 0 &&
      ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rl") < 0) {
    ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): surviveStreamErr is not supported for FW < 6rl\n", __func__);
  }
  if (p_dec->reduce_dpb_delay)
  {
    if (p_ctx->codec_format == NI_CODEC_FORMAT_H264 &&
        ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rs") >= 0)
      p_cfg->ui8ReduceDpbDelay = p_dec->reduce_dpb_delay;
    else
    {
      ni_log2(p_ctx, NI_LOG_INFO, "Warning %s():Not support to reduce dpb delay and reset.\n", __func__);
      p_dec->reduce_dpb_delay = 0;
    }
  }

  //print it all out
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8HWFrame = %d\n", p_cfg->ui8HWFrame);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8MCMode = %d\n", p_cfg->ui8MCMode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8UduSeiEnabled = %d\n", p_cfg->ui8UduSeiEnabled);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui16MaxSeiDataSize = %d\n", p_cfg->ui16MaxSeiDataSize);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8DisablePictureReordering = %d\n", p_cfg->ui8DisablePictureReordering);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8Enabled0 = %d\n", p_cfg->asOutputConfig[0].ui8Enabled);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8Enabled1 = %d\n", p_cfg->asOutputConfig[1].ui8Enabled);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8Enabled2 = %d\n", p_cfg->asOutputConfig[2].ui8Enabled);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui32MaxPktSize = %u\n", p_cfg->ui32MaxPktSize);
  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    ni_log2(p_ctx, NI_LOG_DEBUG,  "[%d] ui8Force8Bit %d\n", i, p_cfg->asOutputConfig[i].ui8Force8Bit);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "[%d] ui8SemiPlanarEnabled %d\n", i, p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "[%d] ui8CropMode %d\n", i, p_cfg->asOutputConfig[i].ui8CropMode);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "[%d] sCroppingRectable.ui16XYWH %d,%d - %d x %d\n", i,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16X,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16W,
      p_cfg->asOutputConfig[i].sCroppingRectable.ui16H);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "[%d] sOutputPictureSize.ui16Width x height %d x %d\n", i,
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width,
      p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height);
    ni_log2(p_ctx, NI_LOG_DEBUG, "[%d] ui8ScaleEnabled %d\n", i, p_cfg->asOutputConfig[i].ui8ScaleEnabled);
    ni_log2(p_ctx, NI_LOG_DEBUG, "[%d] ui8EnablePpuScaleAdapt %u\n", i, p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt);
    ni_log2(p_ctx, NI_LOG_DEBUG, "[%d] ui8EnablePpuScaleLimit %u\n", i, p_cfg->asOutputConfig[i].ui8EnablePpuScaleLimit);
    ni_log2(p_ctx, NI_LOG_DEBUG, "[%d] ui8ScaleResCeil %u\n", i, p_cfg->asOutputConfig[i].ui8ScaleResCeil);
  }
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8MaxExtraHwFrameCnt %u\n", p_cfg->ui8MaxExtraHwFrameCnt);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EcPolicy = %u\n", p_cfg->ui8EcPolicy);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EnableAdvancedEc = %u\n", p_cfg->ui8EnableAdvancedEc);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EnablelowDelayCheck = %u\n", p_cfg->ui8EnablelowDelayCheck);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8DisableAdaptiveBuffers = %u\n", p_cfg->ui8DisableAdaptiveBuffers);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32ErrRatioThreshold = %u\n", p_cfg->ui32ErrRatioThreshold);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8ReduceDpbDelay = %u\n", p_cfg->ui8ReduceDpbDelay);

}


/*!******************************************************************************
 *  \brief  Setup all xcoder configurations with custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_custom_template(ni_session_context_t *p_ctx,
                            ni_encoder_config_t *p_cfg,
                            ni_xcoder_params_t *p_src)
{
  ni_t408_config_t* p_t408 = NULL;
  ni_encoder_cfg_params_t *p_enc = NULL;
  int i = 0;

  if ((!p_ctx) || (!p_cfg) || (!p_src))
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
           __func__);
    return;
  }
  p_t408 = &(p_cfg->niParamT408);
  p_enc = &p_src->cfg_enc_params;

  ni_set_default_template(p_ctx, p_cfg);

  p_cfg->i32picWidth = p_src->source_width;
  p_cfg->i32picHeight = p_src->source_height;
  p_t408->tier = p_enc->high_tier;
  p_t408->gop_preset_index = p_enc->gop_preset_index;
  p_t408->use_recommend_enc_params = p_enc->use_recommend_enc_params;
  p_t408->cu_size_mode = p_enc->cu_size_mode;
  p_t408->max_num_merge = p_enc->max_num_merge;
  p_cfg->ui8AiEnhanceMode = p_src->enable_ai_enhance;
  p_cfg->ui8enable2PassGopPatern = p_src->enable2PassGop;
  // enhance_level is in range [1,3] set level when paramters is valid, otherwise make it to 1.
  if(p_cfg->ui8AiEnhanceMode && p_src->ai_enhance_level > 0 && p_src->ai_enhance_level < 4){
    p_cfg->ui8AiEnhanceLevel = p_src->ai_enhance_level;
  }else{
    p_cfg->ui8AiEnhanceLevel = 1;
  }
  p_cfg->i8statisticOutputLevel = p_enc->statistic_output_level;
  p_cfg->i8skipFrameEnable = p_enc->skip_frame_enable;
  p_cfg->i8maxConsecutiveSkipFrameNum = p_enc->max_consecutive_skip_num;
  p_cfg->u8skipFrameInterval = p_enc->skip_frame_interval;
  p_cfg->ui16iFrameSizeRatio = p_enc->iframe_size_ratio;
  p_cfg->ui8EnableAcqLimit = p_enc->enable_acq_limit;
  p_cfg->ui8stillImageDetectLevel = p_enc->still_image_detect_level;
  p_cfg->ui8sceneChangeDetectLevel = p_enc->scene_change_detect_level;
  if (p_enc->crfFloat == (float)-1.0 && p_enc->enable_smooth_crf == 1)
  {
    //smooth crf only support when setting float crf
    p_enc->enable_smooth_crf = 0;
  }
  p_cfg->ui8enableSmoothCrf = p_enc->enable_smooth_crf;
  p_cfg->ui8enableCompensateQp = p_enc->enable_compensate_qp;

  // enable_dynamic_8x8_merge, enable_dynamic_16x16_merge, enable_dynamic_32x32_merge,
  // trans_rate, enable_hvs_qp_scale:
  // are not present in Rev B p_config

  p_cfg->ui8rcEnable = p_enc->rc.enable_rate_control;

  if(p_ctx->last_bitrate != 0)
  {
      // Slow sequence change happened. Retain the last bitrate.
      ni_log2(p_ctx, NI_LOG_DEBUG,  "### %s: Slow sequence happened retain last_bitrate %d. assigned bitrate %d\n",
              __FUNCTION__, p_ctx->last_bitrate, p_src->bitrate);
      p_src->bitrate = p_ctx->last_bitrate;
  }

  if (p_src->bitrate != 0)
  {
    p_cfg->i32bitRate = p_src->bitrate;
  }

  // Update the bitrate to be used after Slow sequence change
  p_ctx->last_bitrate = p_cfg->i32bitRate;

#if 0
  if ((p_enc->rc.enable_rate_control == 0) &&
      ((p_enc->rc.enable_mb_level_rc == 1) || (p_enc->rc.enable_cu_level_rate_control == 1)))
  {
      p_enc->rc.enable_mb_level_rc = p_enc->rc.enable_cu_level_rate_control = 0;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "force enable_mb_level_rc & enable_cu_level_rate_control to 0 because rate control is disabled\n");
  }
#endif

    p_t408->enable_cu_level_rate_control = p_enc->rc.enable_cu_level_rate_control;
    p_t408->enable_hvs_qp = p_enc->rc.enable_hvs_qp;
    p_t408->hvs_qp_scale = p_enc->rc.hvs_qp_scale;
    p_t408->minQpI = p_enc->rc.min_qp;
    p_t408->minQpP = p_enc->rc.min_qp;
    p_t408->minQpB = p_enc->rc.min_qp;
    p_t408->maxQpI = p_enc->rc.max_qp;
    p_t408->maxQpP = p_enc->rc.max_qp;
    p_t408->maxQpB = p_enc->rc.max_qp;

    p_t408->max_delta_qp = p_enc->rc.max_delta_qp;
    if (p_enc->rc.vbv_buffer_size != -1)
    {
        p_cfg->i32vbvBufferSize = p_enc->rc.vbv_buffer_size;
    }
    else
    {
        if (p_enc->rc.enable_rate_control)
            p_cfg->i32vbvBufferSize = 3000; // enable CBR (default vbv buffer size 3000) even if user does not set vbvBufferSize
        else
            p_cfg->i32vbvBufferSize = 0; // if user sets CRF but not vbvBufferSize, do not eanble capped CRF
    }
    p_cfg->ui32vbvMaxRate  = p_enc->rc.vbv_max_rate;
    p_cfg->i8intraQpDelta = p_enc->rc.intra_qp_delta;
    p_cfg->ui8fillerEnable = p_enc->rc.enable_filler;
    p_cfg->ui8picSkipEnable = p_enc->rc.enable_pic_skip;
    p_cfg->ui16maxFrameSize = p_enc->maxFrameSize / 2000;
    p_t408->intra_period = p_enc->intra_period;
    p_t408->roiEnable = p_enc->roi_enable;
    p_t408->useLongTerm = p_enc->long_term_ref_enable;
    if (QUADRA)
    {
        p_cfg->ui32setLongTermInterval = p_enc->long_term_ref_interval;
        p_cfg->ui8setLongTermCount = p_enc->long_term_ref_count;
    }
    p_t408->conf_win_top = p_enc->conf_win_top;
    p_t408->conf_win_bottom = p_enc->conf_win_bottom;
    p_t408->conf_win_left = p_enc->conf_win_left;
    p_t408->conf_win_right = p_enc->conf_win_right;
    p_t408->avcIdrPeriod = p_enc->intra_period;

  if (QUADRA)
  {
    if(p_ctx->last_framerate.framerate_num != 0)
    {
      // Slow sequence change happened. Retain the last framerate.
      ni_log2(p_ctx, NI_LOG_DEBUG,  "### %s: Slow sequence happened retain last_framerate num %d den %d. assigned num %d den %d\n",
              __FUNCTION__, p_ctx->last_framerate.framerate_num, p_ctx->last_framerate.framerate_denom,
              p_cfg->i32frameRateInfo, p_cfg->i32frameRateDenominator);
      p_src->fps_number = p_ctx->last_framerate.framerate_num;
      p_src->fps_denominator = p_ctx->last_framerate.framerate_denom;

      if (!p_src->enable_vfr) {
          p_enc->frame_rate = (int)(p_src->fps_number / p_src->fps_denominator);
      }
    }

    if (p_cfg->i32frameRateInfo != p_enc->frame_rate)
    {
      p_cfg->i32frameRateInfo = p_enc->frame_rate;
      p_cfg->i32frameRateDenominator = 1;
      if (p_src->fps_denominator != 0 &&
          (p_src->fps_number % p_src->fps_denominator) != 0)
      {
          uint32_t numUnitsInTick = 1000;
          p_cfg->i32frameRateDenominator = numUnitsInTick + 1;
          p_cfg->i32frameRateInfo += 1;
          p_cfg->i32frameRateInfo *= numUnitsInTick;
      }
    }

    // Update the framerate to be used after Slow sequence change
    p_ctx->last_framerate.framerate_num = p_cfg->i32frameRateInfo;
    p_ctx->last_framerate.framerate_denom = p_cfg->i32frameRateDenominator;
  }
  else
  {
    if (p_cfg->i32frameRateInfo != p_enc->frame_rate)
    {
      p_cfg->i32frameRateInfo = p_enc->frame_rate;
      p_t408->numUnitsInTick = 1000;
      if (p_src->fps_denominator != 0 &&
        (p_src->fps_number % p_src->fps_denominator) != 0)
      {
        p_t408->numUnitsInTick += 1;
        p_cfg->i32frameRateInfo += 1;
      }
      p_t408->timeScale = p_cfg->i32frameRateInfo * 1000;
      if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
      {
        p_t408->timeScale *= 2;
      }
    }
  }

    p_t408->intra_qp = p_enc->rc.intra_qp;

  // "repeatHeaders" value 1 (all I frames) maps to forcedHeaderEnable
  // value 2; all other values are ignored
  if (p_t408->forcedHeaderEnable != p_enc->forced_header_enable &&
    p_enc->forced_header_enable == NI_ENC_REPEAT_HEADERS_ALL_I_FRAMES)
  {
    p_t408->forcedHeaderEnable = 2;
    p_cfg->ui8repeatHeaders = p_enc->forced_header_enable;
  }

    p_t408->decoding_refresh_type = p_enc->decoding_refresh_type;

  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    switch (p_t408->decoding_refresh_type)
    {
    case 0: // Non-IRAP I-p_frame
    {
      // intra_period set to user-configured (above), avcIdrPeriod set to 0
      p_t408->avcIdrPeriod = 0;
      break;
    }
    case 1: // CRA
    case 2: // IDR
    {
      // intra_period set to 0, avcIdrPeriod set to user-configured (above)
      p_t408->intra_period = 0;
      break;
    }
    default:
    {
        ni_log(
            NI_LOG_ERROR,
            "ERROR: %s() unknown value for p_t408->decoding_refresh_type: %d\n",
            __func__, p_t408->decoding_refresh_type);
        break;
    }
    }
  } else if (STD_HEVC == p_cfg->ui8bitstreamFormat ||
             STD_AV1 == p_cfg->ui8bitstreamFormat)
  {
    p_t408->avcIdrPeriod = 0;
  }

  // Rev. B: H.264 only parameters.
  p_t408->enable_transform_8x8 = p_enc->enable_transform_8x8;
  p_t408->entropy_coding_mode = p_enc->entropy_coding_mode;

  // Rev. B: shared between HEVC and H.264
  p_t408->slice_mode = p_enc->slice_mode;
  p_t408->slice_arg = p_enc->slice_arg;
  if (p_t408->intra_mb_refresh_mode != p_enc->intra_mb_refresh_mode)
  {
    p_t408->intra_mb_refresh_mode = p_enc->intra_mb_refresh_mode;
    if (1 != p_t408->intra_mb_refresh_mode)
    {
      p_t408->intra_mb_refresh_mode = 1;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "force intraRefreshMode to 1 because quadra only supports intra refresh by rows\n");
    }
  }

  if (p_t408->intra_mb_refresh_arg != p_enc->intra_mb_refresh_arg)
  {
    p_t408->intra_mb_refresh_arg = p_enc->intra_mb_refresh_arg;
    if (1 == p_t408->intra_mb_refresh_mode)
    {
      int mbHeight = (p_cfg->ui8bitstreamFormat == STD_AVC) ? 16 : 64;
      int mbRows = (p_cfg->i32picHeight + mbHeight - 1) / mbHeight;
      p_cfg->ui16gdrDuration = (mbRows + p_t408->intra_mb_refresh_arg - 1) / p_t408->intra_mb_refresh_arg;
    }
  }

   p_cfg->ui8intraResetRefresh = p_enc->intra_reset_refresh;

  // Rev. B: could be shared for HEVC and H.264
    p_t408->enable_mb_level_rc = p_enc->rc.enable_mb_level_rc;

  // profile setting: if user specified profile
  if (0 != p_enc->profile)
  {
    p_t408->profile = p_enc->profile;
  }

  p_t408->level = p_enc->level_idc;

  // main, extended or baseline profile of 8 bit (if input is 10 bit, Quadra auto converts to 8 bit) H.264 requires the following:
  // main:     profile = 2  transform8x8Enable = 0
  // extended: profile = 3  entropyCodingMode = 0, transform8x8Enable = 0
  // baseline: profile = 1  entropyCodingMode = 0, transform8x8Enable = 0 and
  //                        gop with no B frames (gopPresetIdx=1, 2, 6, or 0
  //                        (custom with no B frames)
  if (STD_AVC == p_cfg->ui8bitstreamFormat)
  {
    if (2 == p_t408->profile)
    {
      p_t408->enable_transform_8x8 = 0;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "enable_transform_8x8 set to 0 for profile 2 (main)\n");
    }
    else if (3 == p_t408->profile || 1 == p_t408->profile)
    {
      p_t408->entropy_coding_mode = p_t408->enable_transform_8x8 = 0;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "entropy_coding_mode and enable_transform_8x8 set to 0 "
        "for profile 3 (extended) or 1 (baseline)\n");
    }
  }

  if (QUADRA)
  {
    if (0 == p_t408->entropy_coding_mode && 1 == p_enc->EnableRdoQuant)
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "RDOQ does not support entropy_coding_mode 0 (CAVLC) "
        "force EnableRdoQuant 0 to accommodate HW limiation\n");
      p_enc->EnableRdoQuant = 0;
    }
  }

#ifndef QUADRA
  if (!QUADRA)
  {
    if (GOP_PRESET_IDX_CUSTOM == p_t408->gop_preset_index)
    {
      p_t408->custom_gop_params.custom_gop_size = p_enc->custom_gop_params.custom_gop_size;
      for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
      {
        p_t408->custom_gop_params.pic_param[i].pic_type = p_enc->custom_gop_params.pic_param[i].pic_type;
        p_t408->custom_gop_params.pic_param[i].poc_offset = p_enc->custom_gop_params.pic_param[i].poc_offset;
        p_t408->custom_gop_params.pic_param[i].pic_qp = p_enc->custom_gop_params.pic_param[i].pic_qp + p_t408->intra_qp;
        p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0 = p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0;
        p_t408->custom_gop_params.pic_param[i].ref_poc_L0 = p_enc->custom_gop_params.pic_param[i].ref_poc_L0;
        p_t408->custom_gop_params.pic_param[i].ref_poc_L1 = p_enc->custom_gop_params.pic_param[i].ref_poc_L1;
        p_t408->custom_gop_params.pic_param[i].temporal_id = p_enc->custom_gop_params.pic_param[i].temporal_id;
      }
    }
  }
  else // QUADRA
#endif
  {
      if (p_enc->custom_gop_params.custom_gop_size &&
          (GOP_PRESET_IDX_CUSTOM == p_t408->gop_preset_index ||
           GOP_PRESET_IDX_DEFAULT == p_t408->gop_preset_index))
      {
          p_t408->custom_gop_params.custom_gop_size =
              p_enc->custom_gop_params.custom_gop_size;
          for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
          {
              p_t408->custom_gop_params.pic_param[i].poc_offset =
                  p_enc->custom_gop_params.pic_param[i].poc_offset;
              p_t408->custom_gop_params.pic_param[i].qp_offset =
                  p_enc->custom_gop_params.pic_param[i].qp_offset;
              p_t408->custom_gop_params.pic_param[i].qp_factor =
                  p_enc->custom_gop_params.pic_param[i].qp_factor;
              p_t408->custom_gop_params.pic_param[i].temporal_id =
                  p_enc->custom_gop_params.pic_param[i].temporal_id;
              p_t408->custom_gop_params.pic_param[i].pic_type =
                  p_enc->custom_gop_params.pic_param[i].pic_type;
              p_t408->custom_gop_params.pic_param[i].num_ref_pics =
                  p_enc->custom_gop_params.pic_param[i].num_ref_pics;
              for (int j = 0;
                   j < p_enc->custom_gop_params.pic_param[i].num_ref_pics; j++)
              {
                  p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic =
                      p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic;
                  p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic_used =
                      p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic_used;
              }
          }
    }
  }

  p_ctx->key_frame_type = p_t408->decoding_refresh_type; //Store to use when force key p_frame

  // forceFrameType=1 requires intraPeriod=0 and avcIdrPeriod=0 and gopPresetIdx=8
  if (1 == p_src->force_frame_type)
  {
    p_t408->intra_period = 0;
    p_t408->avcIdrPeriod = 0;
    p_t408->gop_preset_index = 8;
    p_ctx->force_frame_type = 1;
  }

    p_cfg->hdrEnableVUI = p_src->hdrEnableVUI;

  if (p_cfg->i32hwframes != p_src->hwframes)
  {
    if (p_src->hwframes && p_ctx->auto_dl_handle == 0)
    {
      p_cfg->i32hwframes = p_src->hwframes;
    }
    else
    {
      p_cfg->i32hwframes = 0;
    }
  }
    p_cfg->ui16rootBufId = p_src->rootBufId;

  //set VUI info deprecated
  //p_cfg->ui32VuiDataSizeBits = p_src->ui32VuiDataSizeBits;
  //p_cfg->ui32VuiDataSizeBytes = p_src->ui32VuiDataSizeBytes;
  //memcpy(p_cfg->ui8VuiRbsp, p_src->ui8VuiRbsp, NI_MAX_VUI_SIZE);
  //if ((p_src->pos_num_units_in_tick > p_src->ui32VuiDataSizeBits) || (p_src->pos_time_scale > p_src->ui32VuiDataSizeBits))
  //{
  //    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() VUI filling error\n", __func__);
  //    return;
  //}
  //else
  //{
  //  ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_num_units_in_tick, p_t408->numUnitsInTick);
  //  ni_fix_VUI(p_cfg->ui8VuiRbsp, p_src->pos_time_scale, p_t408->timeScale);
  //}

  if (p_src->enable_vfr)
  {
      p_cfg->ui8fixedframerate = 0;
  } else
  {
      p_cfg->ui8fixedframerate = 1;
  }

  //new QUADRA param
  if (p_enc->EnableAUD != 0)
  {
    p_cfg->ui8EnableAUD = p_enc->EnableAUD;
  }
  if (p_enc->lookAheadDepth != 0)
  {
    p_cfg->ui8LookAheadDepth = p_enc->lookAheadDepth;
  }
  if (p_enc->rdoLevel != 1)
  {
    p_cfg->ui8rdoLevel = p_enc->rdoLevel;
  }
  if (p_enc->crf != -1)
  {
    p_cfg->i8crf = p_enc->crf;
  }
  if (p_enc->HDR10MaxLight != 0)
  {
    p_cfg->ui16HDR10MaxLight = p_enc->HDR10MaxLight;
  }
  if (p_enc->HDR10AveLight != 0)
  {
    p_cfg->ui16HDR10AveLight = p_enc->HDR10AveLight;
  }
  if (p_enc->HDR10CLLEnable != 0)
  {
      p_cfg->ui8HDR10CLLEnable = p_enc->HDR10CLLEnable;
  }
  if (p_enc->HDR10Enable != 0)
  {
      p_cfg->ui8hdr10_enable = p_enc->HDR10Enable;
      p_cfg->ui16hdr10_dx0 = p_enc->HDR10dx0;
      p_cfg->ui16hdr10_dy0 = p_enc->HDR10dy0;
      p_cfg->ui16hdr10_dx1 = p_enc->HDR10dx1;
      p_cfg->ui16hdr10_dy1 = p_enc->HDR10dy1;
      p_cfg->ui16hdr10_dx2 = p_enc->HDR10dx2;
      p_cfg->ui16hdr10_dy2 = p_enc->HDR10dy2;
      p_cfg->ui16hdr10_wx = p_enc->HDR10wx;
      p_cfg->ui16hdr10_wy = p_enc->HDR10wy;
      p_cfg->ui32hdr10_maxluma = p_enc->HDR10maxluma;
      p_cfg->ui32hdr10_minluma = p_enc->HDR10minluma;
  }

  if (p_enc->EnableRdoQuant != 0)
  {
    p_cfg->ui8EnableRdoQuant = p_enc->EnableRdoQuant;
  }
  if (p_enc->ctbRcMode != 0)
  {
    p_cfg->ui8ctbRcMode = p_enc->ctbRcMode;
  }
  if (p_enc->gopSize != 0)
  {
    p_cfg->ui8gopSize = p_enc->gopSize;
  }
  if (p_src->use_low_delay_poc_type != 0)
  {
    p_cfg->ui8useLowDelayPocType = p_src->use_low_delay_poc_type;
  }
  if (p_enc->gopLowdelay != 0)
  {
    p_cfg->ui8gopLowdelay = p_enc->gopLowdelay;
  }
  if (p_enc->gdrDuration != 0)
  {
    p_cfg->ui16gdrDuration = p_enc->gdrDuration;
  }
  if (p_enc->colorDescPresent)
  {
    p_cfg->ui8colorDescPresent = 1;
    p_cfg->ui8colorPrimaries = p_enc->colorPrimaries;
    p_cfg->ui8colorTrc = p_enc->colorTrc;
    p_cfg->ui8colorSpace = p_enc->colorSpace;
    p_cfg->ui8videoFullRange = p_enc->videoFullRange;
  }
  if (p_enc->videoFullRange)
  {
      p_cfg->ui8videoFullRange = p_enc->videoFullRange;
  }
  if (p_enc->hrdEnable != 0)
  {
    p_cfg->ui8hrdEnable = p_enc->hrdEnable;
  }

  p_cfg->ui8planarFormat = p_src->cfg_enc_params.planar;
  p_cfg->ui16aspectRatioWidth = p_enc->aspectRatioWidth;
  p_cfg->ui16aspectRatioHeight = p_enc->aspectRatioHeight;

  if (p_enc->ltrRefInterval != 0)
  {
    p_cfg->ui32ltrRefInterval = p_enc->ltrRefInterval;
    p_cfg->i32ltrRefQpOffset = p_enc->ltrRefQpOffset;
    p_cfg->ui32ltrFirstGap = p_enc->ltrFirstGap;
    p_cfg->ui32ltrNextInterval = p_enc->ltrNextInterval;
  }
  if (p_enc->multicoreJointMode != 0)
  {
      p_cfg->ui8multicoreJointMode = p_enc->multicoreJointMode;
  }
  p_cfg->ui32QLevel = p_enc->qlevel;

  if (p_enc->chromaQpOffset != 0)
  {
      p_cfg->i8chromaQpOffset = p_enc->chromaQpOffset;
  }

  if (p_enc->tolCtbRcInter != (float)0.1)
  {
      p_cfg->i32tolCtbRcInter = (int32_t)(p_enc->tolCtbRcInter * 1000);
  }

  if (p_enc->tolCtbRcIntra != (float)0.1)
  {
      p_cfg->i32tolCtbRcIntra = (int32_t)(p_enc->tolCtbRcIntra * 1000);
  }

  if (p_enc->bitrateWindow != -255)
  {
      p_cfg->i16bitrateWindow = p_enc->bitrateWindow;
  }

  if (p_enc->inLoopDSRatio != 1)
  {
      p_cfg->ui8inLoopDSRatio = p_enc->inLoopDSRatio;
  }

  if (p_enc->blockRCSize != 0)
  {
      p_cfg->ui8blockRCSize = p_enc->blockRCSize;
  }

  if (p_enc->rcQpDeltaRange != 10)
  {
      p_cfg->ui8rcQpDeltaRange = p_enc->rcQpDeltaRange;
  }

  if (p_enc->ctbRowQpStep != 0)
  {
      p_cfg->i16ctbRowQpStep = p_enc->ctbRowQpStep;
  }

  if (p_enc->newRcEnable != -1)
  {
      p_cfg->ui8NewRCEnable = p_enc->newRcEnable;
  }

  // convert enable_mb_level_rc, enable_cu_level_rate_control, and enable_hvs_qp to ctbRcMode
  if (QUADRA)
  {
    // ctbRcMode has priority over enable_mb_level_rc, enable_cu_level_rate_control, and enable_hvs_qp
    if (!p_cfg->ui8ctbRcMode)
    {
      if (p_t408->enable_mb_level_rc || p_t408->enable_cu_level_rate_control)
      {
          if (p_t408->enable_hvs_qp)
          {
              p_cfg->ui8ctbRcMode = 3;
              // If hvsQP is enabled, disable strongIntraSmooth to apply one filtering at a time for better VQ.
              p_t408->strongIntraSmoothEnable = 0;
              ni_log(NI_LOG_INFO,
                     "Turning off strongIntraSmoothing because hvsQPEnable=1 "
                     "for better subjective VQ\n");
          } else
        {
          p_cfg->ui8ctbRcMode = 2;
        }
      }
      else if (p_t408->enable_hvs_qp)
      {
        p_cfg->ui8ctbRcMode = 1;
        // If hvsQP is enabled, disable strongIntraSmooth to apply one filtering at a time for better VQ.
        p_t408->strongIntraSmoothEnable = 0;
        ni_log(NI_LOG_INFO,
               "Turning off strongIntraSmoothing because hvsQPEnable=1 for "
               "better subjective VQ\n");
      }
    }
  }

  if (p_src->low_delay_mode != 0)
  {
      p_cfg->ui8LowDelay = !!(p_src->low_delay_mode);
  }

  if (p_enc->enable_ssim != 0)
  {
      p_cfg->ui8enableSSIM = p_enc->enable_ssim;
  }

  if (p_enc->avcc_hvcc != 0)
  {
      p_cfg->ui8avccHvcc = p_enc->avcc_hvcc;
  }

  if (p_enc->av1_error_resilient_mode != 0)
  {
      p_cfg->ui8av1ErrResilientMode = p_enc->av1_error_resilient_mode;
  }

  if (p_enc->temporal_layers_enable != 0)
  {
      p_cfg->ui8temporalLayersEnable = p_enc->temporal_layers_enable;
  }

  if (p_enc->spatial_layers > 1)
  {
      p_cfg->ui8spatialLayersMinusOne = p_enc->spatial_layers - 1;
      if (ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rw") < 0) {
        ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): spatialLayers is not supported for FW < 6rw\n", __func__);
      }
  }

  if (p_enc->spatial_layers_ref_base_layer != 0)
  {
      p_cfg->ui8spatialLayersRefBaseLayer = p_enc->spatial_layers_ref_base_layer;
      if (ni_cmp_fw_api_ver((char *)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6s0") < 0) {
        ni_log2(p_ctx, NI_LOG_INFO, "Warning %s(): spatialLayers is not supported for FW < 6s0\n", __func__);
      }
  }

  if (p_ctx->pixel_format != NI_PIX_FMT_YUV420P)
  {
      p_cfg->ui8PixelFormat = p_ctx->pixel_format;
  }

  if (p_enc->get_psnr_mode != 3)
  {
    if (p_enc->get_psnr_mode == 2 && p_ctx->codec_format == NI_CODEC_FORMAT_H265)
    {
       // h.265 psnr_y is supported by HW for 8-bit & 10-bit, no need to reject setting or disable luma RFC
      if (p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P && p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P10LE
          && p_cfg->ui8PixelFormat != NI_PIX_FMT_NV12 && p_cfg->ui8PixelFormat != NI_PIX_FMT_P010LE)
      {
        p_enc->get_psnr_mode = 3;
        ni_log(NI_LOG_INFO, "Warning h.265 psnr_y is only supported for YUV420P, YUV420P10LE, NV12, and P010LE\n");
      }
    }
    else if (p_enc->get_psnr_mode == 4)
    {
      p_cfg->ui8compressor = 0;
      if (p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P && p_cfg->ui8PixelFormat != NI_PIX_FMT_NV12)
      {
        p_enc->get_psnr_mode = 3;
        p_cfg->ui8compressor = 3;
        ni_log(NI_LOG_INFO, "Warning reconstructed frames only supported for YUV420P, NV12\n");
      }
    }
    else
    {
      p_cfg->ui8compressor = p_enc->get_psnr_mode;
      if (p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P)
      {
        p_cfg->ui8compressor = 3;
        p_enc->get_psnr_mode = 3;
        ni_log(NI_LOG_INFO, "Warning get psnr feature only support YUV420P (except for h.265 psnr_y)\n");
      }
      if (p_enc->crop_width || p_enc->crop_height)
      {
        p_cfg->ui8compressor = 3;
        p_enc->get_psnr_mode = 3;
        ni_log(NI_LOG_INFO, "Warning get psnr feature is not supported when cropWidth x cropHeight are set\n");
      }
    }
  }

  if (p_src->zerocopy_mode == -1) // zero copy auto mode - disable zero copy for low resolution
  {
      bool is_rgba = (p_ctx->pixel_format == NI_PIX_FMT_RGBA ||
                            p_ctx->pixel_format == NI_PIX_FMT_BGRA ||
                            p_ctx->pixel_format == NI_PIX_FMT_ARGB ||
                            p_ctx->pixel_format == NI_PIX_FMT_ABGR)  ? true : false;
      if (is_rgba ||
          ((ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6Q") >= 0) &&
           p_src->source_width*p_src->source_height >= NI_NUM_OF_PIXELS_1080P))
        p_src->zerocopy_mode = 1;
      else
        p_src->zerocopy_mode = 0;
  }

  if (p_src->zerocopy_mode)
  {
      p_cfg->ui32lumaLinesize = p_src->luma_linesize;
      p_cfg->ui32chromaLinesize = p_src->chroma_linesize;
  }
  else
  {
      p_cfg->ui32lumaLinesize = p_src->luma_linesize = 0;
      p_cfg->ui32chromaLinesize = p_src->chroma_linesize = 0;
  }
  // for fast sequence change linesize check
  p_ctx->ori_luma_linesize = p_src->luma_linesize;
  p_ctx->ori_chroma_linesize = p_src->chroma_linesize;

  // calculate number for frames delay for minFramesDelay
  int lookAheadEnable = !!p_cfg->ui8LookAheadDepth;
  int gopSize = g_map_preset_to_gopsize[lookAheadEnable][p_t408->gop_preset_index + 1];
  int mulitcoreDelay = p_cfg->ui8multicoreJointMode ? 3 : 0;

  if (p_t408->gop_preset_index == 0) // Custom GOP
    gopSize = p_t408->custom_gop_params.custom_gop_size;

  if (lookAheadEnable)
  {
    int firstGopEnd = gopSize + 1 + mulitcoreDelay; // first I-frame gopSize is 1
    int lookaheadGopEnd = mulitcoreDelay ?
        p_cfg->ui8LookAheadDepth + mulitcoreDelay + (gopSize - ((p_cfg->ui8LookAheadDepth-1+mulitcoreDelay) % gopSize)) :
        p_cfg->ui8LookAheadDepth + (gopSize - ((p_cfg->ui8LookAheadDepth-1) % gopSize)); // lookAheadDepth-1 because lookahead queue includes first I-frame
    int initialDelayNum = (firstGopEnd > lookaheadGopEnd) ? firstGopEnd : lookaheadGopEnd;
    int maxDelayNum = p_cfg->ui8LookAheadDepth + 1 + gopSize / 2 + mulitcoreDelay;
    int maxLookaheadQueue = initialDelayNum + (gopSize - 1) + mulitcoreDelay;  // assume worst case scenario - gop size changes from initial gop to gop size 1

    p_ctx->initial_frame_delay = initialDelayNum + (mulitcoreDelay ? 4 : 0); // for multicore pass-2, need to add 4 more frames before pass-2 could output frame
    p_ctx->max_frame_delay = ((maxDelayNum > maxLookaheadQueue) ?  maxDelayNum : maxLookaheadQueue) + (mulitcoreDelay ? 4 : 0); // for multicore pass-2, need to add 4 more frames before pass-2 could output frame
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6r2") >= 0)
    {
        p_ctx->last_gop_size = gopSize; // for adaptive gop, gop size change can happen in pass-1, causing the first non-IDR output to carrry gop size 4 insetad of 8 and increase lookahead queue
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6rX") >= 0)
        {
            if (p_t408->gop_preset_index == GOP_PRESET_IDX_DEFAULT || mulitcoreDelay) // for adaptive gop or multicore, just set max frame delay to workaround encoding stuck
                p_ctx->current_frame_delay = p_ctx->max_frame_delay;
            else
                p_ctx->current_frame_delay = p_ctx->initial_frame_delay;
        }
        else
        {
            p_ctx->current_frame_delay = p_ctx->max_frame_delay;
        }
    }
    else
    {
        p_ctx->last_gop_size = gopSize;
        p_ctx->current_frame_delay = p_ctx->max_frame_delay;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: firstGopEnd %d lookaheadGopEnd %d initialDelayNum %d maxDelayNum %d maxLookaheadQueue %d\n",
            __FUNCTION__, firstGopEnd, lookaheadGopEnd, initialDelayNum, maxDelayNum, maxLookaheadQueue);
  }
  else
  {
    p_ctx->last_gop_size = gopSize;
    p_ctx->initial_frame_delay = p_ctx->max_frame_delay = p_ctx->current_frame_delay = gopSize + mulitcoreDelay;
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: preset %d lookAheadDepth %d gopSize %d mulitcoreDelay %d "
          "last_gop_size %d linitial_frame_delay %d current_frame_delay %d max_frame_delay %d\n",
          __FUNCTION__, p_t408->gop_preset_index, p_cfg->ui8LookAheadDepth, gopSize, mulitcoreDelay,
          p_ctx->last_gop_size, p_ctx->initial_frame_delay, p_ctx->current_frame_delay, p_ctx->max_frame_delay);

  if (p_enc->crop_width != 0 && p_enc->crop_height != 0)
  {
      p_cfg->ui32cropWidth = p_enc->crop_width;
      p_cfg->ui32cropHeight = p_enc->crop_height;
      p_cfg->ui32horOffset = p_enc->hor_offset;
      p_cfg->ui32verOffset = p_enc->ver_offset;
  }

  if (p_enc->crfMax != -1)
  {
    p_cfg->i8crfMax = (int8_t)(p_enc->crfMax) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  }

  if (p_enc->qcomp != (float)0.6)
  {
      p_cfg->i32qcomp = (int32_t)(p_enc->qcomp * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  }

  if (p_enc->noMbtree != 0)
  {
      p_cfg->ui8noMbtree = p_enc->noMbtree;
  }

  if (p_enc->noHWMultiPassSupport != 0)
  {
      p_cfg->ui8noHWMultiPassSupport = p_enc->noHWMultiPassSupport;
  }

  if (p_enc->cuTreeFactor != 5)
  {
    p_cfg->i8cuTreeFactor = p_enc->cuTreeFactor;
  }

  if (p_enc->ipRatio != (float)1.4)
  {
      p_cfg->i32ipRatio = (int32_t)(p_enc->ipRatio * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  }

  if (p_enc->pbRatio != (float)1.3)
  {
      p_cfg->i32pbRatio = (int32_t)(p_enc->pbRatio * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  }

  if (p_enc->cplxDecay != (float)0.5)
  {
      p_cfg->i32cplxDecay = (int32_t)(p_enc->cplxDecay * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  }

  if (p_enc->pps_init_qp != -1)
  {
      p_cfg->i8ppsInitQp = (int8_t)(p_enc->pps_init_qp) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  }

  if (p_enc->bitrateMode != -1)
  {
      p_cfg->ui8bitrateMode = p_enc->bitrateMode;
  }

  if (p_enc->pass1_qp != -1)
  {
      p_cfg->i8pass1Qp = (int8_t)(p_enc->pass1_qp) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  }

  if (p_enc->crfFloat != (float)-1.0)
  {
    // for old libxcoder backward compatibility
    p_cfg->i8crf = (int8_t)(p_enc->crfFloat);
    p_cfg->i8crfDecimal = (int8_t)((p_enc->crfFloat - (float)p_cfg->i8crf) * 100);
  }

  if (p_enc->hvsBaseMbComplexity != 15)
  {
      p_cfg->i8hvsBaseMbComplexity = (int8_t)p_enc->hvsBaseMbComplexity - 15; // for old libxcoder backward compatibility, use -15 to represent 0
  }

  if (p_enc->enableipRatio != 0 && p_cfg->i32vbvBufferSize != 0) //vbvBufferSize !=0 for CBR not for ABR
  {
    p_cfg->i8enableipRatio = p_enc->enableipRatio;
  }

  if (p_enc->crf_max_iframe_enable != 0)
  {
      p_cfg->ui8crfMaxIframeEnable = p_enc->crf_max_iframe_enable;
  }

  if (p_enc->vbv_min_rate != 0)
  {
      p_cfg->ui32vbvMinRate  = p_enc->vbv_min_rate;
  }

  if (p_enc->disableBframeRdoq != 0)
  {
      p_cfg->ui8disableBframeRDOQ = p_enc->disableBframeRdoq;
  }

  if (p_enc->forceBframeQpfactor != (float)-1.0)
  {
      p_cfg->i32forceBframeQpFactor = (int32_t)(p_enc->forceBframeQpfactor * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  }

  if (p_enc->tune_bframe_visual != 0)
  {
      p_cfg->ui8tuneBframeVisual = p_enc->tune_bframe_visual;
  }

  if (p_enc->customize_roi_qp_level != 0) {
      p_cfg->u8customizeRoiQpLevel = p_enc->customize_roi_qp_level;
  }

  if (p_enc->motionConstrainedMode != 0)
  {
      p_cfg->ui8motionConstrainedMode = p_enc->motionConstrainedMode;
  }

  if(p_enc->encMallocStrategy != 0)
  {
      p_cfg->ui8mallocStrategy = p_enc->encMallocStrategy;
  }

  if (p_enc->enable_timecode != 0)
  {
      p_cfg->ui8enableTimecode = p_enc->enable_timecode;
  }

  if (p_enc->vbvBufferReencode != 0)
  {
      p_cfg->ui8vbvBufferReencode = p_enc->vbvBufferReencode;
  }

  if (p_enc->totalCuTreeDepth != 0)
  {
      p_cfg->ui8totalCuTreeDepth = p_enc->totalCuTreeDepth;
  }

  if (p_enc->adaptiveCuTree != 0)
  {
      p_cfg->ui8adaptiveCuTree = p_enc->adaptiveCuTree;
  }

  if (p_enc->preIntraHandling != 1)
  {
      p_cfg->ui8preIntraHandling = (uint8_t)p_enc->preIntraHandling - 1; // for old libxcoder backward compatibility, use -1 to represent 0
  }

  if (p_enc->baseLayerOnly != 0)
  {
      p_cfg->ui8baseLayerOnly = p_enc->baseLayerOnly;
  }

  if (p_enc->pastFrameMaxIntraRatio != 20)
  {
      p_cfg->ui8pastFrameMaxIntraRatio = (uint8_t)p_enc->pastFrameMaxIntraRatio - 20; // for old libxcoder backward compatibility, use -20 to represent 0
  }

  if (p_enc->linkFrameMaxIntraRatio != 40)
  {
      p_cfg->ui8linkFrameMaxIntraRatio = (uint8_t)p_enc->linkFrameMaxIntraRatio - 40; // for old libxcoder backward compatibility, use -40 to represent 0
  }

  for (i = 0; i < NI_MAX_SPATIAL_LAYERS; i++)
  {
    if (p_enc->spatialLayerBitrate[i] != 0)
    {
      p_cfg->i32spatialLayerBitrate[i] = p_enc->spatialLayerBitrate[i];
    }
  }

  ni_log2(p_ctx, NI_LOG_DEBUG, "lowDelay=%d\n", p_src->low_delay_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8bitstreamFormat=%d\n", p_cfg->ui8bitstreamFormat);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32picWidth=%d\n", p_cfg->i32picWidth);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32picHeight=%d\n", p_cfg->i32picHeight);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32meBlkMode=%d\n", p_cfg->i32meBlkMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8sliceMode=%d\n", p_cfg->ui8sliceMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32frameRateInfo=%d\n", p_cfg->i32frameRateInfo);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32vbvBufferSize=%d\n", p_cfg->i32vbvBufferSize);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32userQpMax=%d\n", p_cfg->i32userQpMax);
  ni_log2(p_ctx, NI_LOG_DEBUG, "enableSSIM=%d\n", p_cfg->ui8enableSSIM);
  // AVC only
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32maxIntraSize=%d\n", p_cfg->i32maxIntraSize);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32userMaxDeltaQp=%d\n", p_cfg->i32userMaxDeltaQp);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32userMinDeltaQp=%d\n", p_cfg->i32userMinDeltaQp);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32userQpMin=%d\n", p_cfg->i32userQpMin);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32bitRate=%d\n", p_cfg->i32bitRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32bitRateBL=%d\n", p_cfg->i32bitRateBL);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8rcEnable=%d\n", p_cfg->ui8rcEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32srcBitDepth=%d\n", p_cfg->i32srcBitDepth);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8enablePTS=%d\n", p_cfg->ui8enablePTS);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui8lowLatencyMode=%d\n", p_cfg->ui8lowLatencyMode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "ui32sourceEndian=%u\n", p_cfg->ui32sourceEndian);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "hdrEnableVUI=%u\n", p_cfg->hdrEnableVUI);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "i32hwframes=%i\n", p_cfg->i32hwframes);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "** ni_t408_config_t: \n");
  ni_log2(p_ctx, NI_LOG_DEBUG,  "profile=%d\n", p_t408->profile);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "level=%d\n", p_t408->level);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "tier=%d\n", p_t408->tier);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "internalBitDepth=%d\n", p_t408->internalBitDepth);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "losslessEnable=%d\n", p_t408->losslessEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "constIntraPredFlag=%d\n", p_t408->constIntraPredFlag);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "decoding_refresh_type=%d\n", p_t408->decoding_refresh_type);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "intra_qp=%d\n", p_t408->intra_qp);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "intra_period=%d\n", p_t408->intra_period);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "roi_enable=%d\n", p_t408->roiEnable);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "useLongTerm=%u\n", p_t408->useLongTerm);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "setLongTermInterval=%u\n", p_cfg->ui32setLongTermInterval);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "setLongTermCount=%u\n", p_cfg->ui8setLongTermCount);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "conf_win_top=%d\n", p_t408->conf_win_top);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "conf_win_bottom=%d\n", p_t408->conf_win_bottom);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "conf_win_left=%d\n", p_t408->conf_win_left);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "conf_win_right=%d\n", p_t408->conf_win_right);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "independSliceMode=%d\n", p_t408->independSliceMode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "independSliceModeArg=%d\n", p_t408->independSliceModeArg);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "dependSliceMode=%d\n", p_t408->dependSliceMode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "dependSliceModeArg=%d\n", p_t408->dependSliceModeArg);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "intraRefreshMode=%d\n", p_t408->intraRefreshMode);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "intraRefreshArg=%d\n", p_t408->intraRefreshArg);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "use_recommend_enc_params=%d\n", p_t408->use_recommend_enc_params);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "scalingListEnable=%d\n", p_t408->scalingListEnable);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu_size_mode=%d\n", p_t408->cu_size_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "tmvpEnable=%d\n", p_t408->tmvpEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "wppEnable=%d\n", p_t408->wppEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "max_num_merge=%d\n", p_t408->max_num_merge);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "disableDeblk=%d\n", p_t408->disableDeblk);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "lfCrossSliceBoundaryEnable=%d\n", p_t408->lfCrossSliceBoundaryEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "betaOffsetDiv2=%d\n", p_t408->betaOffsetDiv2);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "tcOffsetDiv2=%d\n", p_t408->tcOffsetDiv2);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "skipIntraTrans=%d\n", p_t408->skipIntraTrans);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "saoEnable=%d\n", p_t408->saoEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "intraNxNEnable=%d\n", p_t408->intraNxNEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bitAllocMode=%d\n", p_t408->bitAllocMode);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "enable_cu_level_rate_control=%d\n", p_t408->enable_cu_level_rate_control);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "enable_hvs_qp=%d\n", p_t408->enable_hvs_qp);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "hvs_qp_scale=%d\n", p_t408->hvs_qp_scale);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "max_delta_qp=%d\n", p_t408->max_delta_qp);

  // CUSTOM_GOP
  ni_log2(p_ctx, NI_LOG_DEBUG,  "gop_preset_index=%d\n", p_t408->gop_preset_index);
#ifndef QUADRA
  if (!QUADRA)
  {
    if (p_t408->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
      for (i = 0; i < 8; i++)
        //for (i = 0; i < p_t408->custom_gop_params.custom_gop_size; i++)
      {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_type);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].poc_offset);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_qp);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].num_ref_pic_L0);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L0);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_t408->custom_gop_params.pic_param[i].ref_poc_L1);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_t408->custom_gop_params.pic_param[i].temporal_id);
      }
    }
  }
  else // QUADRA
#endif
  {
    if (p_t408->custom_gop_params.custom_gop_size)
    {
      int j;
      ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.custom_gop_size=%d\n", p_t408->custom_gop_params.custom_gop_size);
      for (i = 0; i < NI_MAX_GOP_NUM; i++)
      {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].poc_offset);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].qp_offset=%d\n", i, p_t408->custom_gop_params.pic_param[i].qp_offset);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].qp_factor=%f\n", i, p_t408->custom_gop_params.pic_param[i].qp_factor);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_t408->custom_gop_params.pic_param[i].temporal_id);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_t408->custom_gop_params.pic_param[i].pic_type);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].num_ref_pics=%d\n", i, p_t408->custom_gop_params.pic_param[i].num_ref_pics);
        for (j = 0; j < NI_MAX_REF_PIC; j++)
        {
          ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].rps[%d].ref_pic=%d\n", i, j, p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic);
          ni_log2(p_ctx, NI_LOG_DEBUG,  "custom_gop_params.pic_param[%d].rps[%d].ref_pic_used=%d\n", i, j, p_t408->custom_gop_params.pic_param[i].rps[j].ref_pic_used);
        }
      }
    }
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "roiEnable=%d\n", p_t408->roiEnable);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "numUnitsInTick=%u\n", p_t408->numUnitsInTick);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "timeScale=%u\n", p_t408->timeScale);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "numTicksPocDiffOne=%u\n", p_t408->numTicksPocDiffOne);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "chromaCbQpOffset=%d\n", p_t408->chromaCbQpOffset);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "chromaCrQpOffset=%d\n", p_t408->chromaCrQpOffset);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "initialRcQp=%d\n", p_t408->initialRcQp);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrYEnable=%u\n", p_t408->nrYEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrCbEnable=%u\n", p_t408->nrCbEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrCrEnable=%u\n", p_t408->nrCrEnable);

  // ENC_NR_WEIGHT
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrIntraWeightY=%u\n", p_t408->nrIntraWeightY);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrIntraWeightCb=%u\n", p_t408->nrIntraWeightCb);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrIntraWeightCr=%u\n", p_t408->nrIntraWeightCr);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrInterWeightY=%u\n", p_t408->nrInterWeightY);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrInterWeightCb=%u\n", p_t408->nrInterWeightCb);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrInterWeightCr=%u\n", p_t408->nrInterWeightCr);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrNoiseEstEnable=%u\n", p_t408->nrNoiseEstEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrNoiseSigmaY=%u\n", p_t408->nrNoiseSigmaY);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrNoiseSigmaCb=%u\n", p_t408->nrNoiseSigmaCb);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "nrNoiseSigmaCr=%u\n", p_t408->nrNoiseSigmaCr);

  // newly added for T408
  ni_log2(p_ctx, NI_LOG_DEBUG,  "monochromeEnable=%u\n", p_t408->monochromeEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "strongIntraSmoothEnable=%u\n",
                 p_t408->strongIntraSmoothEnable);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "weightPredEnable=%u\n", p_t408->weightPredEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bgDetectEnable=%u\n", p_t408->bgDetectEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bgThrDiff=%u\n", p_t408->bgThrDiff);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bgThrMeanDiff=%u\n", p_t408->bgThrMeanDiff);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bgLambdaQp=%u\n", p_t408->bgLambdaQp);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "bgDeltaQp=%d\n", p_t408->bgDeltaQp);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "customLambdaEnable=%u\n", p_t408->customLambdaEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "customMDEnable=%u\n", p_t408->customMDEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu04DeltaRate=%d\n", p_t408->pu04DeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu08DeltaRate=%d\n", p_t408->pu08DeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu16DeltaRate=%d\n", p_t408->pu16DeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu32DeltaRate=%d\n", p_t408->pu32DeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu04IntraPlanarDeltaRate=%d\n", p_t408->pu04IntraPlanarDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu04IntraDcDeltaRate=%d\n", p_t408->pu04IntraDcDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu04IntraAngleDeltaRate=%d\n", p_t408->pu04IntraAngleDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu08IntraPlanarDeltaRate=%d\n", p_t408->pu08IntraPlanarDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu08IntraDcDeltaRate=%d\n", p_t408->pu08IntraDcDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu08IntraAngleDeltaRate=%d\n", p_t408->pu08IntraAngleDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu16IntraPlanarDeltaRate=%d\n", p_t408->pu16IntraPlanarDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu16IntraDcDeltaRate=%d\n", p_t408->pu16IntraDcDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu16IntraAngleDeltaRate=%d\n", p_t408->pu16IntraAngleDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu32IntraPlanarDeltaRate=%d\n", p_t408->pu32IntraPlanarDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu32IntraDcDeltaRate=%d\n", p_t408->pu32IntraDcDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "pu32IntraAngleDeltaRate=%d\n", p_t408->pu32IntraAngleDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu08IntraDeltaRate=%d\n", p_t408->cu08IntraDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu08InterDeltaRate=%d\n", p_t408->cu08InterDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu08MergeDeltaRate=%d\n", p_t408->cu08MergeDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu16IntraDeltaRate=%d\n", p_t408->cu16IntraDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu16InterDeltaRate=%d\n", p_t408->cu16InterDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu16MergeDeltaRate=%d\n", p_t408->cu16MergeDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu32IntraDeltaRate=%d\n", p_t408->cu32IntraDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu32InterDeltaRate=%d\n", p_t408->cu32InterDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "cu32MergeDeltaRate=%d\n", p_t408->cu32MergeDeltaRate);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "coefClearDisable=%d\n", p_t408->coefClearDisable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "minQpI=%d\n", p_t408->minQpI);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "maxQpI=%d\n", p_t408->maxQpI);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "minQpP=%d\n", p_t408->minQpP);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "maxQpP=%d\n", p_t408->maxQpP);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "minQpB=%d\n", p_t408->minQpB);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "maxQpB=%d\n", p_t408->maxQpB);

  // for H.264 on T408
  ni_log2(p_ctx, NI_LOG_DEBUG,  "avcIdrPeriod=%d\n", p_t408->avcIdrPeriod);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "rdoSkip=%d\n", p_t408->rdoSkip);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "lambdaScalingEnable=%d\n", p_t408->lambdaScalingEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "enable_transform_8x8=%d\n", p_t408->enable_transform_8x8);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "slice_mode=%d\n", p_t408->slice_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "slice_arg=%d\n", p_t408->slice_arg);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "intra_mb_refresh_mode=%d\n", p_t408->intra_mb_refresh_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "intra_mb_refresh_arg=%d\n", p_t408->intra_mb_refresh_arg);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "enable_mb_level_rc=%d\n", p_t408->enable_mb_level_rc);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "entropy_coding_mode=%d\n", p_t408->entropy_coding_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "forcedHeaderEnable=%u\n", p_t408->forcedHeaderEnable);

  //QUADRA
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EnableAUD=%d\n", p_cfg->ui8EnableAUD);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8LookAheadDepth=%d\n", p_cfg->ui8LookAheadDepth);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8rdoLevel=%d\n", p_cfg->ui8rdoLevel);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8crf=%d\n", p_cfg->i8crf);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8crfDecimal=%d\n", p_cfg->i8crfDecimal);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16HDR10MaxLight=%d\n", p_cfg->ui16HDR10MaxLight);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16HDR10AveLight=%d\n", p_cfg->ui16HDR10AveLight);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8HDR10CLLEnable=%d\n", p_cfg->ui8HDR10CLLEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EnableRdoQuant=%d\n", p_cfg->ui8EnableRdoQuant);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8ctbRcMode=%d\n", p_cfg->ui8ctbRcMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8gopSize=%d\n", p_cfg->ui8gopSize);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8useLowDelayPocType=%d\n", p_cfg->ui8useLowDelayPocType);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8gopLowdelay=%d\n", p_cfg->ui8gopLowdelay);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16gdrDuration=%d\n", p_cfg->ui16gdrDuration);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8hrdEnable=%d\n", p_cfg->ui8hrdEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8colorDescPresent=%d\n", p_cfg->ui8colorDescPresent);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8colorPrimaries=%d\n", p_cfg->ui8colorPrimaries);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8colorTrc=%d\n", p_cfg->ui8colorTrc);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8colorSpace=%d\n", p_cfg->ui8colorSpace);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16aspectRatioWidth=%d\n", p_cfg->ui16aspectRatioWidth);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16aspectRatioHeight=%d\n", p_cfg->ui16aspectRatioHeight);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16rootBufId=%d\n", p_cfg->ui16rootBufId);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8planarFormat=%d\n", p_cfg->ui8planarFormat);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8PixelFormat=%d\n", p_cfg->ui8PixelFormat);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32ltrRefInterval=%u\n", p_cfg->ui32ltrRefInterval);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32ltrRefQpOffset=%d\n", p_cfg->i32ltrRefQpOffset);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32ltrFirstGap=%u\n", p_cfg->ui32ltrFirstGap);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32ltrNextInterval=%u\n", p_cfg->ui32ltrNextInterval);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8multicoreJointMode=%d\n", p_cfg->ui8multicoreJointMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8videoFullRange=%u\n", p_cfg->ui8videoFullRange);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32QLevel=%u\n", p_cfg->ui32QLevel);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8chromaQpOffset=%d\n", p_cfg->i8chromaQpOffset);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32tolCtbRcInter=0x%x\n", p_cfg->i32tolCtbRcInter);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32tolCtbRcIntra=0x%x\n", p_cfg->i32tolCtbRcIntra);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i16bitrateWindow=%d\n", p_cfg->i16bitrateWindow);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8inLoopDSRatio=%u\n", p_cfg->ui8inLoopDSRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8blockRCSize=%u\n", p_cfg->ui8blockRCSize);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8rcQpDeltaRange=%u\n", p_cfg->ui8rcQpDeltaRange);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i16ctbRowQpStep=%u\n", p_cfg->i16ctbRowQpStep);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8NewRCEnable=%u\n", p_cfg->ui8NewRCEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8LowDelay=%d\n", p_cfg->ui8LowDelay);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8hdr10_enable=%u\n", p_cfg->ui8hdr10_enable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dx0=%u\n", p_cfg->ui16hdr10_dx0);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dy0=%u\n", p_cfg->ui16hdr10_dy0);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dx1=%u\n", p_cfg->ui16hdr10_dx1);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dy1=%u\n", p_cfg->ui16hdr10_dy1);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dx2=%u\n", p_cfg->ui16hdr10_dx2);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_dy2=%u\n", p_cfg->ui16hdr10_dy2);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_wx=%u\n", p_cfg->ui16hdr10_wx);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16hdr10_wy=%u\n", p_cfg->ui16hdr10_wy);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32hdr10_maxluma=%u\n", p_cfg->ui32hdr10_maxluma);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32hdr10_minluma=%u\n", p_cfg->ui32hdr10_minluma);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8avccHvcc=%u\n", p_cfg->ui8avccHvcc);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8av1ErrResilientMode=%u\n", p_cfg->ui8av1ErrResilientMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8intraResetRefresh=%d\n", p_cfg->ui8intraResetRefresh);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8temporalLayersEnable=%u\n", p_cfg->ui8temporalLayersEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8enable2PassGopPattern=%u\n", p_cfg->ui8enable2PassGopPatern);
  ni_log2(p_ctx, NI_LOG_DEBUG, "zerocopy_mode=%d\n", p_src->zerocopy_mode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32lumaLinesize=%u\n", p_cfg->ui32lumaLinesize);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32chromaLinesize=%u\n", p_cfg->ui32chromaLinesize);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32cropWidth=%u\n", p_cfg->ui32cropWidth);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32cropHeight=%u\n", p_cfg->ui32cropHeight);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32horOffset=%u\n", p_cfg->ui32horOffset);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32verOffset=%u\n", p_cfg->ui32verOffset);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8crfMax=%d\n", p_cfg->i8crfMax);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32qcomp=%d\n", p_cfg->i32qcomp);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8noMbtree=%u\n", p_cfg->ui8noMbtree);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8noHWMultiPassSupport=%u\n", p_cfg->ui8noHWMultiPassSupport);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8cuTreeFactor=%d\n", p_cfg->i8cuTreeFactor);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32ipRatio=%d\n", p_cfg->i32ipRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32pbRatio=%d\n", p_cfg->i32pbRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32cplxDecay=%d\n", p_cfg->i32cplxDecay);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8ppsInitQp=%d\n", p_cfg->i8ppsInitQp);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8bitrateMode=%u\n", p_cfg->ui8bitrateMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8pass1Qp=%d\n", p_cfg->i8pass1Qp);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8crfDecimal=%d\n", p_cfg->i8crfDecimal);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8hvsBaseMbComplexity=%u\n", p_cfg->i8hvsBaseMbComplexity);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i8enableipRatio=%d\n",p_cfg->i8enableipRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui16iFrameSizeRatio=%u\n",p_cfg->ui16iFrameSizeRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8crfMaxIframeEnable=%u\n", p_cfg->ui8crfMaxIframeEnable);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui32vbvMinRate=%u\n", p_cfg->ui32vbvMinRate);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8disableBframeRDOQ=%u\n", p_cfg->ui8disableBframeRDOQ);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32forceBframeQpFactor=%d\n", p_cfg->i32forceBframeQpFactor);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8tuneBframeVisual=%u\n", p_cfg->ui8tuneBframeVisual);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8EnableAcqLimit=%u\n", p_cfg->ui8EnableAcqLimit);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8compressor=%u\n", p_cfg->ui8compressor);
  ni_log2(p_ctx, NI_LOG_DEBUG, "u8customizeRoiQpLevel=%u\n", p_cfg->u8customizeRoiQpLevel);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8motionConstrainedMode=%d\n", p_cfg->ui8motionConstrainedMode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8mallocStrategy=%d\n", p_cfg->ui8mallocStrategy);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8spatialLayersMinusOne=%d\n", p_cfg->ui8spatialLayersMinusOne);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8enableTimecode=%d\n", p_cfg->ui8enableTimecode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8spatialLayersRefBaseLayer=%d\n", p_cfg->ui8spatialLayersRefBaseLayer);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8vbvBufferReencode=%d\n", p_cfg->ui8vbvBufferReencode);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8totalCuTreeDepth=%u\n", p_cfg->ui8totalCuTreeDepth);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8adaptiveCuTree=%u\n", p_cfg->ui8adaptiveCuTree);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8preIntraHandling=%u\n", p_cfg->ui8preIntraHandling);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8baseLayerOnly=%u\n", p_cfg->ui8baseLayerOnly);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8pastFrameMaxIntraRatio=%u\n", p_cfg->ui8pastFrameMaxIntraRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "ui8linkFrameMaxIntraRatio=%u\n", p_cfg->ui8linkFrameMaxIntraRatio);
  ni_log2(p_ctx, NI_LOG_DEBUG, "i32spatialLayerBitrate=%d,%d,%d,%d\n", p_cfg->i32spatialLayerBitrate[0],
          p_cfg->i32spatialLayerBitrate[1], p_cfg->i32spatialLayerBitrate[2], p_cfg->i32spatialLayerBitrate[3]);
}

/*!******************************************************************************
 *  \brief  Setup and initialize all xcoder configuration to default (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
void ni_set_default_template(ni_session_context_t* p_ctx, ni_encoder_config_t* p_config)
{
  int i = 0;

  if( (!p_ctx) || (!p_config) )
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      return;
  }

  memset(p_config, 0, sizeof(ni_encoder_config_t));

  // fill in common attributes values
  p_config->i32picWidth = 720;
  p_config->i32picHeight = 480;
  p_config->i32meBlkMode = 0; // (AVC ONLY) 0 means use all possible block partitions
  p_config->ui8sliceMode = 0; // 0 means 1 slice per picture
  p_config->i32frameRateInfo = 30;
  p_config->i32frameRateDenominator = 1;
  p_config->i32vbvBufferSize = 0; //0; parameter is ignored if rate control is off, if rate control is on, 0 means do not check vbv constraints
  p_config->i32userQpMax = 51;       // this should also be h264-only parameter

  // AVC only
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->i32maxIntraSize = 8000000; // how big an intra p_frame can get?
    p_config->i32userMaxDeltaQp = 51;
    p_config->i32userMinDeltaQp = 51;
    p_config->i32userQpMin = 8;
  }

  p_config->i32bitRate = 0;
  p_config->i32bitRateBL = 0;
  p_config->ui8rcEnable = 0;
  p_config->i32srcBitDepth = p_ctx->src_bit_depth;
  p_config->ui8enablePTS = 0;
  p_config->ui8lowLatencyMode = 0;

  // profiles for H.264: 1 = baseline, 2 = main, 3 = extended, 4 = high
  //                     5 = high10  (default 8 bit: 4, 10 bit: 5)
  // profiles for HEVC:  1 = main, 2 = main10  (default 8 bit: 1, 10 bit: 2)

  // bitstream type: H.264 or HEVC
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->ui8bitstreamFormat = STD_AVC;

    p_config->niParamT408.profile = 4;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 5;
    }
  } else if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format)
  {
      p_config->ui8bitstreamFormat = STD_JPEG;
  } else if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
  {
      p_config->ui8bitstreamFormat = STD_AV1;

      p_config->niParamT408.profile = 1;
  } else
  {
    ni_assert(NI_CODEC_FORMAT_H265 == p_ctx->codec_format);

    p_config->ui8bitstreamFormat = STD_HEVC;

    p_config->niParamT408.profile = 1;
    if (10 == p_ctx->src_bit_depth)
    {
      p_config->niParamT408.profile = 2;
    }
  }

  p_config->hdrEnableVUI = 0;
  p_config->ui32sourceEndian = p_ctx->src_endian;

  p_config->niParamT408.level = 0;
  p_config->niParamT408.tier = 0;    // 0 means main tier

  p_config->niParamT408.internalBitDepth = p_ctx->src_bit_depth;
  p_config->niParamT408.losslessEnable = 0;
  p_config->niParamT408.constIntraPredFlag = 0;

  if (QUADRA)
      p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_DEFAULT;
  else
      p_config->niParamT408.gop_preset_index = GOP_PRESET_IDX_IBBBP;

  p_config->niParamT408.decoding_refresh_type = 1;
  p_config->niParamT408.intra_qp = 22;
  // avcIdrPeriod (H.264 on T408), NOT shared with intra_period
  p_config->niParamT408.intra_period = 120;
  p_config->niParamT408.avcIdrPeriod = 120;

  p_config->niParamT408.conf_win_top = 0;
  p_config->niParamT408.conf_win_bottom = 0;
  p_config->niParamT408.conf_win_left = 0;
  p_config->niParamT408.conf_win_right = 0;

  p_config->niParamT408.independSliceMode = 0;
  p_config->niParamT408.independSliceModeArg = 0;
  p_config->niParamT408.dependSliceMode = 0;
  p_config->niParamT408.dependSliceModeArg = 0;
  p_config->niParamT408.intraRefreshMode = 0;
  p_config->niParamT408.intraRefreshArg = 0;

  p_config->niParamT408.use_recommend_enc_params = 0; //1;
  p_config->niParamT408.scalingListEnable = 0;

  p_config->niParamT408.cu_size_mode = 7;
  p_config->niParamT408.tmvpEnable = 1;
  p_config->niParamT408.wppEnable = 0;
  p_config->niParamT408.max_num_merge = 2;
  p_config->niParamT408.disableDeblk = 0;
  p_config->niParamT408.lfCrossSliceBoundaryEnable = 1;
  p_config->niParamT408.betaOffsetDiv2 = 0;
  p_config->niParamT408.tcOffsetDiv2 = 0;
  p_config->niParamT408.skipIntraTrans = 1;
  p_config->niParamT408.saoEnable = 1;
  p_config->niParamT408.intraNxNEnable = 1;

  p_config->niParamT408.bitAllocMode = 0;

  for (i = 0; i < NI_MAX_GOP_NUM; i++)
  {
    p_config->niParamT408.fixedBitRatio[i] = 1;
  }

  if (QUADRA)
    p_config->niParamT408.enable_cu_level_rate_control = 0;
  else
    p_config->niParamT408.enable_cu_level_rate_control = 1;

  p_config->niParamT408.enable_hvs_qp = 0;
  p_config->niParamT408.hvs_qp_scale = 2;

  p_config->niParamT408.max_delta_qp = 10;

  // CUSTOM_GOP
  p_config->niParamT408.custom_gop_params.custom_gop_size = 0;
#ifndef QUADRA
  if (!QUADRA)
  {
    for (i = 0; i < p_config->niParamT408.custom_gop_params.custom_gop_size; i++)
    {
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_qp = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pic_L0 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L0 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].ref_poc_L1 = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
    }
  }
  else // QUADRA
#endif
  {
    int j;
    for (i = 0; i < NI_MAX_GOP_NUM; i++)
    {
      p_config->niParamT408.custom_gop_params.pic_param[i].poc_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].qp_offset = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].qp_factor =
          (float)0.3;   // QP Factor range is between 0.3 and 1, higher values mean lower quality and less bits
      p_config->niParamT408.custom_gop_params.pic_param[i].temporal_id = 0;
      p_config->niParamT408.custom_gop_params.pic_param[i].pic_type = PIC_TYPE_I;
      p_config->niParamT408.custom_gop_params.pic_param[i].num_ref_pics= 0;
      for (j = 0; j < NI_MAX_REF_PIC; j++)
      {
        p_config->niParamT408.custom_gop_params.pic_param[i].rps[j].ref_pic = 0;
        p_config->niParamT408.custom_gop_params.pic_param[i].rps[j].ref_pic_used = -1;
      }
    }
  }

  p_config->niParamT408.roiEnable = 0;

  p_config->niParamT408.numUnitsInTick = 1000;
  p_config->niParamT408.timeScale = p_config->i32frameRateInfo * 1000;
  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    p_config->niParamT408.timeScale *= 2;
  }

  p_config->niParamT408.numTicksPocDiffOne = 0;

  p_config->niParamT408.chromaCbQpOffset = 0;
  p_config->niParamT408.chromaCrQpOffset = 0;

  p_config->niParamT408.initialRcQp = 63; //-1;

  p_config->niParamT408.nrYEnable = 0;
  p_config->niParamT408.nrCbEnable = 0;
  p_config->niParamT408.nrCrEnable = 0;

  // ENC_NR_WEIGHT
  p_config->niParamT408.nrIntraWeightY = 7;
  p_config->niParamT408.nrIntraWeightCb = 7;
  p_config->niParamT408.nrIntraWeightCr = 7;
  p_config->niParamT408.nrInterWeightY = 4;
  p_config->niParamT408.nrInterWeightCb = 4;
  p_config->niParamT408.nrInterWeightCr = 4;

  p_config->niParamT408.nrNoiseEstEnable = 0;
  p_config->niParamT408.nrNoiseSigmaY = 0;
  p_config->niParamT408.nrNoiseSigmaCb = 0;
  p_config->niParamT408.nrNoiseSigmaCr = 0;

  p_config->niParamT408.useLongTerm = 0;

  // newly added for T408
  p_config->niParamT408.monochromeEnable = 0;
  p_config->niParamT408.strongIntraSmoothEnable = 1;

  p_config->niParamT408.weightPredEnable = 0;
  p_config->niParamT408.bgDetectEnable = 0;
  p_config->niParamT408.bgThrDiff = 8;     // matching the C-model
  p_config->niParamT408.bgThrMeanDiff = 1; // matching the C-model
  p_config->niParamT408.bgLambdaQp = 32;   // matching the C-model
  p_config->niParamT408.bgDeltaQp = 3;     // matching the C-model

  p_config->niParamT408.customLambdaEnable = 0;
  p_config->niParamT408.customMDEnable = 0;
  p_config->niParamT408.pu04DeltaRate = 0;
  p_config->niParamT408.pu08DeltaRate = 0;
  p_config->niParamT408.pu16DeltaRate = 0;
  p_config->niParamT408.pu32DeltaRate = 0;
  p_config->niParamT408.pu04IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu04IntraDcDeltaRate = 0;
  p_config->niParamT408.pu04IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu08IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu08IntraDcDeltaRate = 0;
  p_config->niParamT408.pu08IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu16IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu16IntraDcDeltaRate = 0;
  p_config->niParamT408.pu16IntraAngleDeltaRate = 0;
  p_config->niParamT408.pu32IntraPlanarDeltaRate = 0;
  p_config->niParamT408.pu32IntraDcDeltaRate = 0;
  p_config->niParamT408.pu32IntraAngleDeltaRate = 0;
  p_config->niParamT408.cu08IntraDeltaRate = 0;
  p_config->niParamT408.cu08InterDeltaRate = 0;
  p_config->niParamT408.cu08MergeDeltaRate = 0;
  p_config->niParamT408.cu16IntraDeltaRate = 0;
  p_config->niParamT408.cu16InterDeltaRate = 0;
  p_config->niParamT408.cu16MergeDeltaRate = 0;
  p_config->niParamT408.cu32IntraDeltaRate = 0;
  p_config->niParamT408.cu32InterDeltaRate = 0;
  p_config->niParamT408.cu32MergeDeltaRate = 0;
  p_config->niParamT408.coefClearDisable = 0;
  p_config->niParamT408.minQpI = 8;
  p_config->niParamT408.maxQpI = 51;
  p_config->niParamT408.minQpP = 8;
  p_config->niParamT408.maxQpP = 51;
  p_config->niParamT408.minQpB = 8;
  p_config->niParamT408.maxQpB = 51;

  // for H.264 on T408
  p_config->niParamT408.rdoSkip = 0;
  p_config->niParamT408.lambdaScalingEnable = 0;
  p_config->niParamT408.enable_transform_8x8 = 1;
  p_config->niParamT408.slice_mode = 0;
  p_config->niParamT408.slice_arg = 0;
  p_config->niParamT408.intra_mb_refresh_mode = 0;
  p_config->niParamT408.intra_mb_refresh_arg = 0;
  if (QUADRA)
    p_config->niParamT408.enable_mb_level_rc = 0;
  else
    p_config->niParamT408.enable_mb_level_rc = 1;
  p_config->niParamT408.entropy_coding_mode = 1; // 1 means CABAC, make sure profile is main or above, can't have CABAC in baseline
  p_config->niParamT408.forcedHeaderEnable = 0; // first IDR frame

  //QUADRA
  p_config->ui8EnableAUD = 0;
  p_config->ui8LookAheadDepth = 0;
  p_config->ui8rdoLevel = (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format) ? 0 : 1;
  p_config->i8crf = -1;
  p_config->ui16HDR10MaxLight = 0;
  p_config->ui16HDR10AveLight = 0;
  p_config->ui8HDR10CLLEnable = 0;
  p_config->ui8EnableRdoQuant = 0;
  p_config->ui8ctbRcMode = 0;
  p_config->ui8gopSize = 0;
  p_config->ui8useLowDelayPocType = 0;
  p_config->ui8gopLowdelay = 0;
  p_config->ui16gdrDuration = 0;
  p_config->ui8hrdEnable = 0;
  p_config->ui8colorDescPresent = 0;
  ////AVCOL_PRI_UNSPECIFIED =2 ffmpeg defaults
  p_config->ui8colorPrimaries = 2;
  //AVCOL_TRC_UNSPECIFIED
  p_config->ui8colorTrc = 2;
  //AVCOL_SPC_UNSPECIFIED
  p_config->ui8colorSpace = 2;
  p_config->ui16aspectRatioWidth = 0;
  p_config->ui16aspectRatioHeight = 0;
  p_config->ui8planarFormat = NI_PIXEL_PLANAR_FORMAT_PLANAR;
  p_config->ui8PixelFormat = NI_PIX_FMT_YUV420P;
  p_config->ui32ltrRefInterval = 0;
  p_config->i32ltrRefQpOffset = 0;
  p_config->ui32ltrFirstGap= 0;
  p_config->ui32ltrNextInterval = 1;
  p_config->ui8multicoreJointMode = 0;
  p_config->ui8videoFullRange = 0;
  p_config->ui32setLongTermInterval = 0;
  p_config->ui8setLongTermCount = 2;
  p_config->ui32QLevel = -1;
  p_config->i8chromaQpOffset = 0;
  p_config->i32tolCtbRcInter = (int32_t)(0.1 * 1000);
  p_config->i32tolCtbRcIntra = (int32_t)(0.1 * 1000);
  p_config->i16bitrateWindow = -255;
  p_config->ui8inLoopDSRatio = 1;
  p_config->ui8blockRCSize = 0;
  p_config->ui8rcQpDeltaRange = 10;
  p_config->i16ctbRowQpStep = 0;
  p_config->ui8NewRCEnable = 255;
  p_config->ui8LowDelay = 0;
  p_config->ui8fixedframerate = 1;
  p_config->ui8enableSSIM = 0;
  p_config->ui8hdr10_enable = 0;
  p_config->ui16hdr10_dx0 = 0;
  p_config->ui16hdr10_dy0 = 0;
  p_config->ui16hdr10_dx1 = 0;
  p_config->ui16hdr10_dy1 = 0;
  p_config->ui16hdr10_dx2 = 0;
  p_config->ui16hdr10_dy2 = 0;
  p_config->ui16hdr10_wx = 0;
  p_config->ui16hdr10_wy = 0;
  p_config->ui32hdr10_maxluma = 0;
  p_config->ui32hdr10_minluma = 0;
  p_config->ui8avccHvcc = 0;
  p_config->ui8av1ErrResilientMode = 0;
  p_config->ui8intraResetRefresh = 0;
  p_config->ui8temporalLayersEnable = 0;
  p_config->ui8enable2PassGopPatern = 0;
  p_config->i8crfMax = (int8_t)(-1) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  p_config->i32qcomp = (int32_t)(0.6 * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  p_config->ui8noMbtree = 0;
  p_config->ui8noHWMultiPassSupport = 0;
  p_config->i8cuTreeFactor = 5;
  p_config->i32ipRatio = (int32_t)(1.4 * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  p_config->i32pbRatio = (int32_t)(1.3 * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  p_config->i32cplxDecay = (int32_t)(0.5 * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  p_config->ui32vbvMaxRate = 0;
  p_config->i8ppsInitQp = (int8_t)(-1) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  p_config->ui8bitrateMode = 0;  // bitrateMode 0 = max bitrate (actual bitrate can be lower than bitrate target), bitrateMode 1 = average bitrate (actual bitrate approximately equal to bitrate target)
  p_config->i8pass1Qp = (int8_t)(-1) + 1; // for old libxcoder backward compatibility, use 1 to represent 0
  p_config->i8crfDecimal = 0;
  p_config->i8hvsBaseMbComplexity = (int8_t)(15)-15; // for old libxcoder backward compatibility, use -15 to represent 0
  p_config->i8statisticOutputLevel = 0;
  p_config->i8enableipRatio = 0;
  p_config->ui16iFrameSizeRatio = 100;
  p_config->ui8compressor = 3;
  p_config->u8skipFrameInterval = 0;
  p_config->ui8crfMaxIframeEnable = 0;
  p_config->ui32vbvMinRate = 0;
  p_config->ui8disableBframeRDOQ = 0;
  p_config->i32forceBframeQpFactor = (int32_t)(-1.0 * 1000) + 1000; // for old libxcoder backward compatibility, use 1000 to represent 0
  p_config->ui8tuneBframeVisual = 0;
  p_config->ui8EnableAcqLimit = 0;
  p_config->u8customizeRoiQpLevel = 0;
  p_config->ui32cropWidth = 0;
  p_config->ui32cropHeight = 0;
  p_config->ui32horOffset = 0;
  p_config->ui32verOffset = 0;
  p_config->ui8motionConstrainedMode = 0;
  p_config->ui8stillImageDetectLevel = 0;
  p_config->ui8sceneChangeDetectLevel = 0;
  p_config->ui8mallocStrategy = 0;
  p_config->ui8spatialLayersMinusOne = 0;
  p_config->ui8enableTimecode = 0;
  p_config->ui8spatialLayersRefBaseLayer = 0;
  p_config->ui8vbvBufferReencode = 0;
  p_config->ui8totalCuTreeDepth = 0;
  p_config->ui8adaptiveCuTree = 0;
  p_config->ui8preIntraHandling = (uint8_t)(1)-1; // for old libxcoder backward compatibility, use -1 to represent 0
  p_config->ui8baseLayerOnly = 0;
  p_config->ui8pastFrameMaxIntraRatio = (uint8_t)(20)-20; // for old libxcoder backward compatibility, use -20 to represent 0
  p_config->ui8linkFrameMaxIntraRatio = (uint8_t)(40)-40; // for old libxcoder backward compatibility, use -40 to represent 0
  for (i = 0; i < NI_MAX_SPATIAL_LAYERS; i++)
  {
    p_config->i32spatialLayerBitrate[i] = 0;
  }
}

/*!******************************************************************************
*  \brief  Perform validation on custom dec parameters (Rev. B)
*
*  \param
*
*  \return
******************************************************************************/
ni_retcode_t ni_validate_custom_dec_template(ni_xcoder_params_t *p_src,
                                             ni_session_context_t *p_ctx,
                                             ni_decoder_config_t *p_cfg,
                                             char *p_param_err,
                                             uint32_t max_err_len)
{
  ni_retcode_t param_ret = NI_RETCODE_SUCCESS;
  ni_retcode_t warning = NI_RETCODE_SUCCESS;
  int w = p_src->source_width;
  int h = p_src->source_height;
  char *p_param_warn = NULL;
  int i;

  if (!p_ctx || !p_cfg || !p_param_err)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  // Zero out the error buffer
  p_param_warn = malloc(sizeof(p_ctx->param_err_msg));
  if (!p_param_warn)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() p_param_warn malloc failure\n",
             NI_ERRNO, __func__);
      param_ret = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }

  memset(p_param_err, 0, max_err_len);
  memset(p_param_warn, 0, max_err_len);


  //if (p_cfg->i32bitRate > NI_MAX_BITRATE)
  //{
  //  strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
  //  param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
  //  LRETURN;
  //}

  if (p_cfg->ui8HWFrame == 0 &&
    (p_cfg->asOutputConfig[1].ui8Enabled || p_cfg->asOutputConfig[2].ui8Enabled))
  {
    strncpy(p_param_err, "Incompatible output format: hw frame must be used if out1 or out2 used", max_err_len);
    param_ret = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (p_src->ddr_priority_mode >= 0)
  {
      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "6e") < 0)
      {
          strncpy(p_param_err, "ddr_priority_mode not supported on device with FW api version < 6.e",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
          LRETURN;
      }
  }

  if ((p_ctx->codec_format == NI_CODEC_FORMAT_JPEG || p_ctx->codec_format == NI_CODEC_FORMAT_VP9)  && (p_cfg->ui8DisablePictureReordering))
  {
      strncpy(p_param_err, "LowDelay is not supported on jpeg/vp9 decoder",
              max_err_len);
      param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
      LRETURN;
  }

  for (i = 0; i < NI_MAX_NUM_OF_DECODER_OUTPUTS; i++)
  {
    //checking cropping param
    if (p_cfg->asOutputConfig[i].ui8CropMode == NI_DEC_CROP_MODE_MANUAL)
    {
      if (NI_MINIMUM_CROPPED_LENGTH > w - p_cfg->asOutputConfig[i].sCroppingRectable.ui16X ||
        NI_MINIMUM_CROPPED_LENGTH > h - p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y)
      {
        strncpy(p_param_err, "Invalid crop offset: extends past 48x48 minimum window", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (NI_MINIMUM_CROPPED_LENGTH > p_cfg->asOutputConfig[i].sCroppingRectable.ui16W ||
        NI_MINIMUM_CROPPED_LENGTH > p_cfg->asOutputConfig[i].sCroppingRectable.ui16H)
      {
        strncpy(p_param_err, "Invalid crop w or h: must be at least 48x48 minimum window", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_TOO_SMALL;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16W > w ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H > h)
      {
        strncpy(p_param_err, "Invalid crop w or h: must be smaller than input", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16W + p_cfg->asOutputConfig[i].sCroppingRectable.ui16X > w ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H + p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y > h)
      {
        strncpy(p_param_err, "Invalid crop rect: must fit in input", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_OOR;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16X & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16Y & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16W & 1 ||
        p_cfg->asOutputConfig[i].sCroppingRectable.ui16H & 1)
      {
        strncpy(p_param_err, "Invalid crop value: even values only", max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
      {
        strncpy(p_param_warn, "crop param used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }

    }

    //checking scaling param
    if (p_cfg->asOutputConfig[i].ui8ScaleEnabled)
    {
      if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height == 0 ||
        p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width == 0)
      {
        if (p_cfg->asOutputConfig[i].ui8EnablePpuScaleAdapt)
        {
          //may need to revisit if based on cropped input or base input for w/h limit
          strncpy(p_param_err, "Invalid scale dimensions: zero", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_TOO_SMALL;
          LRETURN;
        }
      }
      if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height & 1 ||
        p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width & 1 )
      {
        strncpy(p_param_err, "Invalid scale value: even values only", max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }

      if (p_cfg->asOutputConfig[i].ui8EnablePpuScaleLimit == 1)
      {
        if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width > w ||
          p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height > h)
        {
            strncpy(p_param_err, "Invalid scale value: downscale only", max_err_len);
            param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
      }

      if (p_cfg->asOutputConfig[i].ui8CropMode == NI_DEC_CROP_MODE_MANUAL)
      {
        //reject if scale dimensions exceed crop dimensions
        if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height > p_cfg->asOutputConfig[i].sCroppingRectable.ui16H ||
          p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width > p_cfg->asOutputConfig[i].sCroppingRectable.ui16W)
        {
          strncpy(p_param_err, "Invalid scale dimensions: downscale only after cropping", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_OOR;
          LRETURN;
        }
      }
      if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
      {
        strncpy(p_param_warn, "scale param used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
    }
    if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
    {
      if (p_cfg->asOutputConfig[i].ui8Force8Bit || p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled)
      {
        strncpy(p_param_warn, "force8bit or semiPlanar used but output not enabled!", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
    }

    //check tiled format compatibility
    if (p_cfg->asOutputConfig[i].ui8SemiPlanarEnabled == NI_PIXEL_PLANAR_FORMAT_TILED4X4)
    {
      if (p_cfg->asOutputConfig[i].ui8Enabled == 0)
      {
          continue;
      }
      if (p_cfg->ui8HWFrame == 0)
      {
        strncpy(p_param_err,
                "Invalid pairing: out=HW must be set with tiled format",
                max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      if (p_ctx->codec_format == NI_CODEC_FORMAT_VP9)
      {
        strncpy(p_param_err, "Invalid pairing: VP9 not compatible with tiled format",
                max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
      if (p_cfg->asOutputConfig[i].ui8ScaleEnabled)
      {
        //only no need to check crop compat if scale is set
        if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height % 4 ||
          p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Height < 128)
        {
          strncpy(p_param_err,
                  "Invalid scale height: mult of 4 only, >= 128",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
          LRETURN;
        }
        if (p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width % 4 ||
          p_cfg->asOutputConfig[i].sOutputPictureSize.ui16Width < NI_MIN_WIDTH)
        {
          //minimum supported dec is 128 but min enc is 144 so round up
          strncpy(p_param_err,
                  "Invalid scale width: mult of 128 only, >= 144",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
          LRETURN;
        }
      }
      else if (p_cfg->asOutputConfig[i].ui8CropMode == NI_DEC_CROP_MODE_MANUAL)
      {
        if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16H % 4 ||
          p_cfg->asOutputConfig[i].sCroppingRectable.ui16H < 128)
        {
          strncpy(p_param_err,
                  "Invalid crop height: mult of 4 only, >= 128",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
          LRETURN;
        }
        if (p_cfg->asOutputConfig[i].sCroppingRectable.ui16W % 4 ||
            p_cfg->asOutputConfig[i].sCroppingRectable.ui16W < NI_MIN_WIDTH)
        {
          //minimum supported dec is 128 but min enc is 144 so round up
          strncpy(p_param_err,
                  "Invalid crop width: mult of 128 only, >= 144",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
          LRETURN;
        }
      }
      if (p_cfg->asOutputConfig[i].ui8Force8Bit)
      {
        strncpy(p_param_err, "Force 8 bit: not supported with tiled format\n",
                max_err_len);
        param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
        LRETURN;
      }
    }

  }//end forloop
  if (warning == NI_RETCODE_PARAM_WARN && param_ret == NI_RETCODE_SUCCESS)
  {
    param_ret = NI_RETCODE_PARAM_WARN;
    strncpy(p_param_err, p_param_warn, max_err_len);
  }

END:

    free(p_param_warn);
    return param_ret;
}

// Check encoder level parameters
static ni_retcode_t ni_check_level(int level, int codec_id)
{
    const int l_levels_264[] = {10, 11, 12, 13, 20, 21, 22, 30, 31, 32,
                                40, 41, 42, 50, 51, 52, 60, 61, 62, 0};
    const int l_levels_265[] = {10, 20, 21, 30, 31, 40, 41,
                                50, 51, 52, 60, 61, 62, 0};
    const int l_levels_av1[] = {20, 21, 30, 31, 40, 41, 50, 51, 0};
    const int *l_levels = l_levels_264;

    if (level == 0)
    {
        return NI_RETCODE_SUCCESS;
    }

    if (codec_id == NI_CODEC_FORMAT_H265)
    {
        l_levels = l_levels_265;
    } else if (codec_id == NI_CODEC_FORMAT_AV1)
    {
        l_levels = l_levels_av1;
    }

    while (*l_levels != 0)
    {
        if (*l_levels == level)
        {
            return NI_RETCODE_SUCCESS;
        }
        l_levels++;
    }

    return NI_RETCODE_FAILURE;
}

/*!******************************************************************************
 *  \brief  Perform validation on custom parameters (Rev. B)
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_validate_custom_template(ni_session_context_t *p_ctx,
                                         ni_encoder_config_t *p_cfg,
                                         ni_xcoder_params_t *p_src,
                                         char *p_param_err,
                                         uint32_t max_err_len)
{
  ni_retcode_t param_ret = NI_RETCODE_SUCCESS;
  ni_retcode_t warning = NI_RETCODE_SUCCESS;
  char* p_param_warn = malloc(sizeof(p_ctx->param_err_msg));
  ni_encoder_cfg_params_t *p_enc;
  int i;

  if( (!p_ctx) || (!p_cfg) || (!p_src) || (!p_param_err) )
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Zero out the error buffer
  p_enc = &p_src->cfg_enc_params;
  memset(p_param_err, 0, max_err_len);
  memset(p_param_warn, 0, max_err_len);

  if (0 == p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid frame_rate of 0 value", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_FRATE;
    LRETURN;
  }

  if (((p_cfg->i32frameRateInfo + p_cfg->i32frameRateDenominator - 1) /
       p_cfg->i32frameRateDenominator) > NI_MAX_FRAMERATE)
  {
      strncpy(p_param_err, "Invalid i32frameRateInfo: too big", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_FRATE;
      LRETURN;
  }

  if (p_cfg->i32bitRate <= p_cfg->i32frameRateInfo)
  {
    strncpy(p_param_err, "Invalid i32bitRate: smaller than or equal to frame rate", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate > NI_MAX_BITRATE)
  {
    strncpy(p_param_err, "Invalid i32bitRate: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_cfg->i32bitRate < NI_MIN_BITRATE )
  {
    strncpy(p_param_err, "Invalid i32bitRate: too low", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_BRATE;
    LRETURN;
  }

  if (p_src->source_width < XCODER_MIN_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_width > XCODER_MAX_ENC_PIC_WIDTH)
  {
    strncpy(p_param_err, "Invalid Picture Width: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
    LRETURN;
  }

  if (p_src->source_height < XCODER_MIN_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too small", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (p_src->source_height > XCODER_MAX_ENC_PIC_HEIGHT)
  {
    strncpy(p_param_err, "Invalid Picture Height: too big", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
    LRETURN;
  }

  if (p_cfg->ui32cropWidth || p_cfg->ui32cropHeight)
  {
      // check cropping width & height are both non-zero
      if (!p_cfg->ui32cropWidth || !p_cfg->ui32cropHeight)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width x Height (%u x %u): both need to be specified",
                         p_cfg->ui32cropWidth, p_cfg->ui32cropHeight);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      // check cropping width & height are even
      if ((p_cfg->ui32cropWidth % 2) || (p_cfg->ui32cropHeight % 2))
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width x Height (%u x %uu): must be even",
                         p_cfg->ui32cropWidth, p_cfg->ui32cropHeight);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      // check cropping width & height meet resoultion constraint (including AV1 max resoultion)
      if (p_cfg->ui32cropWidth < NI_MIN_WIDTH)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width: less than %d",
                         NI_MIN_WIDTH);
        param_ret = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL;
        LRETURN;
      }
      if (p_cfg->ui32cropHeight < NI_MIN_HEIGHT)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Height: less than %d",
                         NI_MIN_HEIGHT);
        param_ret = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL;
        LRETURN;
      }
      if (p_cfg->ui32cropWidth > NI_PARAM_MAX_WIDTH)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width: exceeds %d",
                         NI_PARAM_MAX_WIDTH);
        param_ret = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
        LRETURN;
      }
      if (p_cfg->ui32cropHeight > NI_PARAM_MAX_HEIGHT)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Height: exceeds %d",
                         NI_PARAM_MAX_HEIGHT);
        param_ret = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
        LRETURN;
      }
      if (p_cfg->ui32cropWidth * p_cfg->ui32cropHeight > NI_MAX_RESOLUTION_AREA)
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width x Height: exceeds %d",
                         NI_MAX_RESOLUTION_AREA);
        param_ret = NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
        LRETURN;
      }

      if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
      {
        if (p_cfg->ui32cropWidth > NI_PARAM_AV1_MAX_WIDTH)
        {
          snprintf(p_param_err, max_err_len, "Invalid Crop Width: exceeds %d",
                           NI_PARAM_AV1_MAX_WIDTH);
          param_ret = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
          LRETURN;
        }
        if (p_cfg->ui32cropHeight > NI_PARAM_AV1_MAX_HEIGHT)
        {
          snprintf(p_param_err, max_err_len, "Invalid Crop Height: exceeds %d",
                           NI_PARAM_AV1_MAX_HEIGHT);
          param_ret = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
          LRETURN;
        }
        if (p_cfg->ui32cropWidth * p_cfg->ui32cropHeight > NI_PARAM_AV1_MAX_AREA)
        {
          snprintf(p_param_err, max_err_len, "Invalid Crop Width x Height: exceeds %d",
                           NI_PARAM_AV1_MAX_AREA);
          param_ret = NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
          LRETURN;
        }
      }

      // check cropping horOffset & verOffset are even
      if ((p_cfg->ui32horOffset % 2) || (p_cfg->ui32verOffset % 2))
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop horOffset %u and/or verOffset %u: must be even",
                         p_cfg->ui32horOffset, p_cfg->ui32verOffset);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }

      if (p_src->source_width < (p_cfg->ui32horOffset + p_cfg->ui32cropWidth))
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Width: offset %u + crop width %u too big > %d",
                         p_cfg->ui32horOffset, p_cfg->ui32cropWidth, p_src->source_width);
        param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
        LRETURN;
      }
      if (p_src->source_height < (p_cfg->ui32verOffset + p_cfg->ui32cropHeight))
      {
        snprintf(p_param_err, max_err_len, "Invalid Crop Height: offset %u + crop height %u too big > %d",
                         p_cfg->ui32verOffset, p_cfg->ui32cropHeight, p_src->source_height);
        param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
        LRETURN;
      }
  }
  else // check source width / height meet AV1 max resoultion constraint if cropping not specified
  {
      if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
      {
        if (p_src->source_width > NI_PARAM_AV1_MAX_WIDTH)
        {
          snprintf(p_param_err, max_err_len, "Invalid Picture Width: exceeds %d",
                           NI_PARAM_AV1_MAX_WIDTH);
          param_ret = NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG;
          LRETURN;
        }
        if (p_src->source_height > NI_PARAM_AV1_MAX_HEIGHT)
        {
          snprintf(p_param_err, max_err_len, "Invalid Picture Height: exceeds %d",
                           NI_PARAM_AV1_MAX_HEIGHT);
          param_ret = NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG;
          LRETURN;
        }
        if (p_src->source_width * p_src->source_height > NI_PARAM_AV1_MAX_AREA)
        {
          snprintf(p_param_err, max_err_len, "Invalid Picture Width x Height: exceeds %d",
                           NI_PARAM_AV1_MAX_AREA);
          param_ret = NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG;
          LRETURN;
        }
      }
  }

  if (p_cfg->ui8planarFormat >= NI_PIXEL_PLANAR_MAX)
  {
      strncpy(p_param_err, "Invalid input planar format: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_OOR;
      LRETURN;
  }
  else if (p_cfg->ui8planarFormat == NI_PIXEL_PLANAR_FORMAT_TILED4X4)
  {
      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "68") < 0)
      {
          strncpy(p_param_err, "Invalid input planar format for device with FW api version < 6.8",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
          LRETURN;
      }
      if (p_ctx->auto_dl_handle)
      {
          strncpy(p_param_err, "Invalid Encoder Selected: Tiled format must be on same device",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_INVALID_HANDLE;
          LRETURN;
      }
      if (p_src->source_height % 4 != 0)
      {
          strncpy(p_param_err,
                  "Invalid Picture Height: tiled format only supports "
                  "multiples of 4",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_PIC_HEIGHT;
          LRETURN;
      }
      if (p_src->source_width % 4 != 0)
      {
          strncpy(p_param_err,
                  "Invalid Picture Width: tiled format only supports "
                  "multiples of 4",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
          LRETURN;
      }
  }

  if (p_cfg->ui8PixelFormat == NI_PIX_FMT_RGBA ||
      p_cfg->ui8PixelFormat == NI_PIX_FMT_BGRA ||
      p_cfg->ui8PixelFormat == NI_PIX_FMT_ABGR ||
      p_cfg->ui8PixelFormat == NI_PIX_FMT_ARGB)
  {
      if (p_src->zerocopy_mode == 0)
      {
          strncpy(p_param_err, "zeroCopyMode must not be disabled for RGBA / BGRA / ABGR / ARGB pixel formats", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
      if (p_cfg->ui8PixelFormat == NI_PIX_FMT_RGBA ||
          p_cfg->ui8PixelFormat == NI_PIX_FMT_BGRA)
      {
          if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                "6Y") < 0)
          {
              strncpy(p_param_err, "RGBA / BGRA pixel formats not supported on device with FW api version < 6.Y",
                      max_err_len);
              param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
              LRETURN;
          }
      }
  }
  else if (p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P &&
           p_cfg->ui8PixelFormat != NI_PIX_FMT_YUV420P10LE &&
           p_cfg->ui8PixelFormat != NI_PIX_FMT_NV12 &&
           p_cfg->ui8PixelFormat != NI_PIX_FMT_P010LE &&
           p_cfg->ui8PixelFormat != NI_PIX_FMT_8_TILED4X4 &&
           p_cfg->ui8PixelFormat != NI_PIX_FMT_10_TILED4X4)
  {
      snprintf(p_param_err, max_err_len, "Unsupported pixel format %d in encoder", p_cfg->ui8PixelFormat);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (p_src->ddr_priority_mode >= 0)
  {
      if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                            "6e") < 0)
      {
          strncpy(p_param_err, "ddr_priority_mode not supported on device with FW api version < 6.e",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
          LRETURN;
      }
  }

  if (NI_RETCODE_SUCCESS !=
      ni_check_level(p_src->cfg_enc_params.level_idc, p_ctx->codec_format))
  {
      strncpy(p_param_err, "Invalid Encoder Level: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_OOR;
      LRETURN;
  }

  if ((p_src->cfg_enc_params.intra_mb_refresh_mode < 0 ||
       p_src->cfg_enc_params.intra_mb_refresh_mode > 4) ||
      (NI_CODEC_FORMAT_H264 == p_ctx->codec_format &&
       p_src->cfg_enc_params.intra_mb_refresh_mode == 4))
  {
      strncpy(p_param_err, "Invalid intra_mb_refresh_mode: out of range",
              max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_OOR;
      LRETURN;
  }

  if (!QUADRA)
  {
    if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
        NI_MAX_GOP_NUM)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }
  }
  else // QUADRA
  {
      if (GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
          if (!p_cfg->niParamT408.custom_gop_params.custom_gop_size)
          {
              strncpy(p_param_err,
                      "Invalid custom GOP paramaters: custom gop size must > 0",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
              LRETURN;
          }
      }
    if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
    {
        if (GOP_PRESET_IDX_CUSTOM != p_cfg->niParamT408.gop_preset_index &&
            GOP_PRESET_IDX_DEFAULT != p_cfg->niParamT408.gop_preset_index)
        {
            strncpy(p_param_err,
                    "Invalid custom GOP paramaters: selected gopPresetIdx is "
                    "not compatible with custom gop",
                    max_err_len);
            param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
            LRETURN;
        }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size < 1)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too small", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size >
        NI_MAX_GOP_NUM)
      {
        strncpy(p_param_err, "Invalid custom GOP paramaters: custom_gop_size too big", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }

      for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
      {
        if (p_cfg->niParamT408.custom_gop_params.pic_param[i].poc_offset >
              p_cfg->niParamT408.custom_gop_params.custom_gop_size)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: poc_offset larger"
                  " than GOP size", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }
      }
    }

    if(p_cfg->ui8useLowDelayPocType  != 0)
    {
        if (NI_CODEC_FORMAT_H264 != p_ctx->codec_format)
        {
            p_cfg->ui8useLowDelayPocType = 0;
            strncpy(p_param_warn, "useLowDelayPocType is only supported for H.264. Change useLowDelayPocType to 0", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
    }
    if(p_cfg->niParamT408.entropy_coding_mode != 1)
    {
        if (NI_CODEC_FORMAT_H264 != p_ctx->codec_format)
        {
            strncpy(p_param_err,
                    "entropyCodingMode is only supported for H.264.",
                    max_err_len);
            param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
            LRETURN;
        }
    }
    if (p_cfg->ui8av1ErrResilientMode)
    {
        if (NI_CODEC_FORMAT_AV1 != p_ctx->codec_format)
        {
            strncpy(p_param_err,
                    "av1ErrorResilientMode is only supported for AV1.",
                    max_err_len);
            param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
            LRETURN;
        }
    }
    if (0 != p_src->enable_ai_enhance)
    {
        if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format)
        {
          snprintf(p_param_err, max_err_len, "%s is not supported for JPEG.",
                  ((p_src->enable_ai_enhance == 2) ? "enableHVSPlus" : "enableAIEnhance"));
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
        }
    }
    if (-1 != p_cfg->ui32QLevel)
    {
        if (NI_CODEC_FORMAT_JPEG != p_ctx->codec_format)
        {
          strncpy(p_param_err, "qLevel is only supported for JPEG.",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
        }
    }

    if(p_cfg->ui8enableSSIM != 0)
    {
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "62") < 0 || NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
        {
            p_cfg->ui8enableSSIM = 0;
            strncpy(p_param_warn, "enableSSIM only supported on device with FW api version < 6.2 "
                    "and the encoder is not av1_ni_quadra_enc. Reported ssim will be 0.", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
    }

    if (p_cfg->niParamT408.slice_mode || p_cfg->niParamT408.slice_arg)
    {
        if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format || NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
        {
          strncpy(p_param_err, "sliceMode/sliceArg is only supported for H.264 or H.265.",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
        }

        if (p_cfg->niParamT408.slice_mode)
        {
            int ctu_mb_size = (NI_CODEC_FORMAT_H264 == p_ctx->codec_format) ? 16 : 64;
            int max_num_ctu_mb_row = (p_src->source_height + ctu_mb_size - 1) / ctu_mb_size;
            if (p_cfg->niParamT408.slice_arg < 1 || p_cfg->niParamT408.slice_arg > max_num_ctu_mb_row ||
                p_cfg->niParamT408.slice_arg > NI_MAX_SLICE_SIZE)
            {
                snprintf(p_param_err, max_err_len, "Invalid number of rows per slice: should be between 1 and %d",
                         max_num_ctu_mb_row < NI_MAX_SLICE_SIZE ? max_num_ctu_mb_row : NI_MAX_SLICE_SIZE);
                param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
                LRETURN;
            }
        }
    }
    if (p_src->use_low_delay_poc_type)
    {
        if (NI_CODEC_FORMAT_H264 != p_ctx->codec_format)
        {
            strncpy(p_param_err, "useLowDelayPocType is only supported for H.264.",
                    max_err_len);
            param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
            LRETURN;
        }
    }

    if(p_cfg->ui8enableCompensateQp != 0)
    {
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6ru") < 0 ||
            NI_CODEC_FORMAT_AV1 == p_ctx->codec_format || p_cfg->ui16maxFrameSize == 0)
        {
            p_cfg->ui8enableCompensateQp = 0;
            strncpy(p_param_warn, "ui8enableCompensateQp only supported when device with FW api version < 6rt "
                    "and the encoder is not av1_ni_quadra_enc and ui16maxFrameSize > 0. Reported enableCompensateQp will be 0.", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
    }

    if (p_cfg->ui8enableTimecode != 0)
    {
        if (NI_CODEC_FORMAT_H264 != p_ctx->codec_format)
        {
            p_cfg->ui8enableTimecode = 0;
            if (NI_CODEC_FORMAT_H265 == p_ctx->codec_format)
            {
                strncpy(p_param_warn, "enableTimecode does not need to be set for H.265. Use ni_enc_insert_timecode API"
                        "from libxcoder directly to insert time code SEI", max_err_len);
            }
            else
            {
                strncpy(p_param_warn, "enableTimecode not supported for the codec used. Forcing value to 0.", max_err_len);
            }
            warning = NI_RETCODE_PARAM_WARN;
        }
    }
  }

  if (QUADRA)
  {
      if (p_cfg->ui8LookAheadDepth != 0 || p_cfg->i8crf >= 0)
      {
          if ((1 == p_cfg->niParamT408.gop_preset_index && p_cfg->ui8LookAheadDepth != 0)  ||
              3 == p_cfg->niParamT408.gop_preset_index ||
              7 == p_cfg->niParamT408.gop_preset_index ||
              10 == p_cfg->niParamT408.gop_preset_index ||
              15 == p_cfg->niParamT408.gop_preset_index)
          {
              strncpy(p_param_err,
                      "this gopPreset is not supported for lookahead and/or CRF",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
              LRETURN;
          }

          if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
          {
            bool bIsgopLowdelay = true;
            // lookahead does not support gopLowDelay (all B-frames, encode in order) gop patterns
            for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size;
                 i++)
            {
              // check if all frames are B-frames
              if (2 != p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
              {
                bIsgopLowdelay = false;
                break;
              }
              // check if all frames are encoded in display order
              if (p_cfg->niParamT408.custom_gop_params.pic_param[i].poc_offset != (i+1))
              {
                bIsgopLowdelay = false;
                break;
              }
            }
            if (bIsgopLowdelay)
            {
              strncpy(p_param_err, "B-frames low delay custom gop is not supported for "
                       "lookahead and/or CRF", max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
            }
          }

          if (p_enc->bitrateMode != -1)
          {
              strncpy(p_param_err,
                      "bitrateMode is invalid when lookahead is enabled (or in CRF mode)",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_PIC_WIDTH;
              LRETURN;
          }
      }

      if (p_cfg->ui8totalCuTreeDepth != 0)
      {
          if (p_cfg->ui8LookAheadDepth == 0 || p_cfg->ui8LookAheadDepth == 1)
          {
              strncpy(p_param_err,
                      "totalCuTreeDepth is invalid without lookahead",
                      max_err_len);
              param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
              LRETURN;
          }

          if (p_cfg->ui8totalCuTreeDepth <= p_cfg->ui8LookAheadDepth)
          {
              strncpy(p_param_warn, "totalCuTreeDepth does not take effects when its value <= lookahead depth", max_err_len);
              warning = NI_RETCODE_PARAM_WARN;
              p_cfg->ui8totalCuTreeDepth = 0;
          }
          else
          {
              if (p_cfg->ui8multicoreJointMode != 0)
              {
                  strncpy(p_param_err,
                          "totalCuTreeDepth is not supported in multicoreJointMode",
                          max_err_len);
                  param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
                  LRETURN;
              }
          }
      }

      if (GOP_PRESET_IDX_DEFAULT == p_cfg->niParamT408.gop_preset_index)
      {
          // when gop_preset_index = -1 (default), gop pattern is decided by gopSize and gopLowdelay
          //p_cfg->ui8gopSize = 0;
          //p_cfg->ui8gopLowdelay = 0;
      }
      if (1 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->niParamT408.intra_period = 1;
          p_cfg->niParamT408.avcIdrPeriod = 1;
          p_cfg->ui8gopSize = 1;
          p_cfg->ui16gdrDuration = 0;
      }
      if (2 == p_cfg->niParamT408.gop_preset_index)
      {
          strncpy(p_param_err,
                  "gopPresetIdx 2 is obsolete, suggest to use gopPresetIdx 9 "
                  "instead",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
      }
      if (3 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 1;
          p_cfg->ui8gopLowdelay = 1;
      }
      if (4 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 2;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (5 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 4;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (6 == p_cfg->niParamT408.gop_preset_index)
      {
          strncpy(p_param_err,
                  "gopPresetIdx 6 is obsolete, suggest to use gopPresetIdx 7 "
                  "instead",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
      }
      if (7 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 4;
          p_cfg->ui8gopLowdelay = 1;
      }
      if (8 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 8;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (9 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 1;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (10 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 4;
          p_cfg->ui8gopLowdelay = 0;
      }
      if (15 == p_cfg->niParamT408.gop_preset_index)
      {
          p_cfg->ui8gopSize = 16;
          p_cfg->ui8gopLowdelay = 0;
      }

      if (p_cfg->ui8LookAheadDepth != 0
          || p_cfg->ui8bitrateMode == 1
          || p_cfg->ui32vbvMaxRate
          || p_cfg->ui32vbvMinRate
          || p_cfg->i8enableipRatio)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG, "Force newRcEnable to 0 in 2-pass encode or in 1-pass average bitrate mode, with ipRatio, VbvMaxRate, or VbvMinRate\n");
          p_cfg->ui8NewRCEnable = 0;
      }
      else if (p_cfg->ui8NewRCEnable == 255)
      {
          if ((p_cfg->ui8gopSize > 1 || p_cfg->ui8gopSize == 0) && p_cfg->ui8gopLowdelay == 0)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,  "Set newRcEnable to 0 in non low delay gop preset\n");
              p_cfg->ui8NewRCEnable = 0;
          }
          else if (p_cfg->niParamT408.intra_period == 1 || p_cfg->niParamT408.avcIdrPeriod == 1)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,  "Set newRcEnable to 0 in intra only mode\n");
              p_cfg->ui8NewRCEnable = 0;
          }
      }

      if ((p_cfg->ui8stillImageDetectLevel != 0 || p_cfg->ui8sceneChangeDetectLevel != 0) &&
          (p_cfg->niParamT408.gop_preset_index != 9 || ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rm") < 0))
      {
        strncpy(p_param_warn, "StillImageDetect or SceneChangeDetect only support gopPresetIdx=9 with FW > 6rm\n", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8stillImageDetectLevel  = 0;
        p_cfg->ui8sceneChangeDetectLevel = 0;
      }

      if (p_cfg->ui8spatialLayersMinusOne > 0)
      {
        if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT)
        {
          strncpy(p_param_err,
                  "currently do not support gop preset in multi spatial layers encode",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
        }
        p_cfg->ui8gopSize = p_cfg->ui8spatialLayersMinusOne + 1;
        p_cfg->ui8gopLowdelay = 0;
      }

      if (p_cfg->ui8spatialLayersRefBaseLayer != 0)
      {
        if (p_cfg->ui8spatialLayersMinusOne == 0)
        {
          strncpy(p_param_err,
                  "higher spatial layers referencing base layer is only supported in multi spatial layers encode",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
        }
      }

      for (i = 0; i < NI_MAX_SPATIAL_LAYERS; i++)
      {
        if (p_cfg->i32spatialLayerBitrate[i] == 0)
        {
          break;
        }
      }
      if (i > 0)
      {
        if (p_cfg->ui8spatialLayersMinusOne == 0)
        {
          strncpy(p_param_err, "spatialLayerBitrate should only be used in multi spatial layers encode", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
        else if (p_cfg->ui8spatialLayersMinusOne + 1 != i)
        {
          strncpy(p_param_err, "number of values specified in spatialLayerBitrate must match the total number of spatial layers", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
        if (p_cfg->ui8multicoreJointMode != 0)
        {
          strncpy(p_param_err, "spatialLayerBitrate is not supported in multicoreJointMode", max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
        }
      }

      if (p_cfg->ui8crfMaxIframeEnable > 0)
      {
        if (p_cfg->i8crf < 0 || p_cfg->i32vbvBufferSize == 0 || p_cfg->ui8LookAheadDepth == 0)
        {
          strncpy(p_param_err,
                  "crfMaxIframeEnable is only supported in Capped CRF mode with lookahead enabled",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
  }

  if (NI_CODEC_FORMAT_H264 == p_ctx->codec_format)
  {
    if (!QUADRA)
    {
      if (10 == p_ctx->src_bit_depth)
      {
        if (p_cfg->niParamT408.profile != 5)
        {
          strncpy(p_param_err, "Invalid profile: must be 5 (high10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      else
      {
        if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5)
        {
          strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                  " 3 (extended), 4 (high), or 5 (high10)", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      if (1 == p_cfg->niParamT408.profile &&
          ! (0 == p_cfg->niParamT408.gop_preset_index ||
             1 == p_cfg->niParamT408.gop_preset_index ||
             2 == p_cfg->niParamT408.gop_preset_index ||
             6 == p_cfg->niParamT408.gop_preset_index))
      {
        strncpy(p_param_err, "Invalid gopPresetIdx for H.264 baseline profile:"
                " must be 1, 2, 6 or 0 (custom with no B frames)", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (1 == p_cfg->niParamT408.profile &&
          GOP_PRESET_IDX_CUSTOM == p_cfg->niParamT408.gop_preset_index)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size;
             i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                    "have B frames", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      }
    }
    else // QUADRA
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 5 || p_cfg->niParamT408.profile == 3)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (baseline), 2 (main),"
                " 4 (high), or 5 (high10)", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (10 == p_ctx->src_bit_depth && p_cfg->niParamT408.profile != 5)
      {
        strncpy(p_param_warn, "AVC Baseline/Main/High Profile do not support 10-bit, auto convert to 8-bit", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
      if (1 == p_cfg->niParamT408.profile)
      {
        if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
        {
          for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
          {
            if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
            {
              strncpy(p_param_err, "H.264 baseline profile: custom GOP can not "
                      "have B frames", max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
            }
          }
        } else if (((p_cfg->ui8gopSize != 1 || p_cfg->ui8gopLowdelay)) &&
                   (p_cfg->niParamT408.intra_period != 1) &&
                   (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_HIERARCHICAL_IPPPP))
        {
            if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT ||
                p_cfg->ui8gopSize != 0 ||
                p_cfg
                    ->ui8gopLowdelay)   // if gopSize is 0 (default / adapative gop), autoset to 1
            {
                strncpy(p_param_err,
                        "Must use gopPresetIdx 1,9,10 (no "
                        "B frames) for profile 1",
                        max_err_len);
                param_ret = NI_RETCODE_INVALID_PARAM;
                LRETURN;
            }
          p_cfg->ui8gopSize = 1; //autoset to 1
        }
      }
      if (p_cfg->niParamT408.tier)
      {
          strncpy(p_param_err, "Tier is not supported for H.264", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
      if (p_cfg->ui8spatialLayersMinusOne > 0)
      {
          strncpy(p_param_err,
                  "spatialLayers is not supported for h.264 encode",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
    }
  }
  else if (NI_CODEC_FORMAT_H265 == p_ctx->codec_format)
  {
    if (!QUADRA)
    {
      if (10 == p_ctx->src_bit_depth)
      {
        if (p_cfg->niParamT408.profile != 2)
        {
          strncpy(p_param_err, "Invalid profile: must be 2 (main10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
      else
      {
        if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
        {
          strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      }
    }
    else // QUADRA
    {
      if (p_cfg->niParamT408.profile < 1 || p_cfg->niParamT408.profile > 2)
      {
        strncpy(p_param_err, "Invalid profile: must be 1 (main) or 2 (main10)",
                max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (10 == p_ctx->src_bit_depth && p_cfg->niParamT408.profile != 2)
      {
        strncpy(p_param_warn, "HEVC Main Profile does not support 10-bit, auto convert to 8-bit", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->ui8spatialLayersMinusOne > 0)
      {
          strncpy(p_param_err,
                  "spatialLayers is not supported for h.265 encode",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
    }
  } else if (NI_CODEC_FORMAT_AV1 == p_ctx->codec_format)
  {
      if (p_cfg->ui8tuneBframeVisual == TUNE_BFRAME_VISUAL_MEDIUM)
      {
          strncpy(p_param_err, "TuneBframeVisual MEDIUM is not supported for AV1", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
      if (p_cfg->niParamT408.profile != 1)
      {
          strncpy(p_param_err, "Invalid profile: must be 1 (main)",
                  max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
      if (p_cfg->niParamT408.level)   // 0 means auto level
      {
          if (p_cfg->niParamT408.level < 20)
          {
              p_cfg->niParamT408.level = 20;
              strncpy(p_param_warn,
                      "AV1 level < 2.0 is not supported, change to level 2.0",
                      max_err_len);
              warning = NI_RETCODE_PARAM_WARN;
          } else if (p_cfg->niParamT408.level > 51)
          {
              p_cfg->niParamT408.level = 51;
              strncpy(p_param_warn,
                      "AV1 level > 5.1 is not supported, change to level 5.1",
                      max_err_len);
              warning = NI_RETCODE_PARAM_WARN;
          }
      }

      if (p_cfg->niParamT408.conf_win_top != 0)
      {
          p_cfg->niParamT408.conf_win_top = p_src->cfg_enc_params.conf_win_top =
              0;
          strncpy(p_param_warn, "confWinTop is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_bottom != 0)
      {
          p_cfg->niParamT408.conf_win_bottom =
              p_src->cfg_enc_params.conf_win_bottom = 0;
          strncpy(p_param_warn, "confWinBottom is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_left != 0)
      {
          p_cfg->niParamT408.conf_win_left =
              p_src->cfg_enc_params.conf_win_left = 0;
          strncpy(p_param_warn, "confWinLeft is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_right != 0)
      {
          p_cfg->niParamT408.conf_win_right =
              p_src->cfg_enc_params.conf_win_right = 0;
          strncpy(p_param_warn, "confWinRight is not supported in AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->ui8hdr10_enable)
      {
          strncpy(p_param_err,
                  "masterDisplay not supported for AV1",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->ui8hrdEnable)
      {
          strncpy(p_param_err, "hrdEnable is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8EnableAUD)
      {
          strncpy(p_param_err, "enableAUD is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8HDR10CLLEnable)
      {
          strncpy(p_param_err, "maxCLL is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8repeatHeaders)
      {
          strncpy(p_param_err, "repeatHeaders is not supported on av1 encoder",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->ui8enableSSIM)
      {
          strncpy(p_param_err, "enableSSIM is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8EnableRdoQuant)
      {
          strncpy(p_param_err, "EnableRdoQuant is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8fillerEnable)
      {
          strncpy(p_param_err, "fillerEnable is not supported on av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i8ppsInitQp)
      {
          strncpy(p_param_err, "ppsInitQp is not supported for av1 encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8vbvBufferReencode && p_cfg->ui8multicoreJointMode)
      {
          strncpy(p_param_err, "vbvBufferReencode is not supported for av1 multicoreJointMode",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
  } else if (NI_CODEC_FORMAT_JPEG == p_ctx->codec_format)
  {
      if (p_cfg->ui32cropWidth || p_cfg->ui32cropHeight || p_cfg->ui32horOffset || p_cfg->ui32verOffset)
      {
          strncpy(p_param_err, "crop Parameters not supported for JPEG", max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.tier)
      {
          strncpy(p_param_err, "Tier is not supported for JPEG", max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8tuneBframeVisual)
      {
          strncpy(p_param_err, "TuneBframeVisual is not supported for JPEG", max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8hdr10_enable)
      {
          strncpy(p_param_err,
                  "masterDisplay not supported for jpeg",
                  max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
      }
      if (p_cfg->niParamT408.conf_win_top != 0)
      {
          strncpy(p_param_err, "confWinTop is not supported in jpeg",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.conf_win_bottom != 0)
      {
          strncpy(p_param_err, "confWinBottom is not supported in jpeg",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.conf_win_left != 0)
      {
          strncpy(p_param_err, "confWinLeft is not supported in jpeg",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.conf_win_right != 0)
      {
          strncpy(p_param_err, "confWinRight is not supported in jpeg",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8hrdEnable)
      {
          strncpy(p_param_err, "hrdEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8EnableAUD)
      {
          strncpy(p_param_err, "enableAUD is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8repeatHeaders)
      {
          strncpy(p_param_err, "repeatHeaders is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->cfg_enc_params.preferred_transfer_characteristics != -1)
      {
          strncpy(p_param_err, "prefTRC is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8HDR10CLLEnable)
      {
          strncpy(p_param_err, "maxCLL is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8colorPrimaries != 2)
      {
          strncpy(p_param_err, "colorPri is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8colorTrc != 2)
      {
          strncpy(p_param_err, "colorTrc is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8colorSpace != 2)
      {
          strncpy(p_param_err, "colorSpc is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->sar_num)
      {
          strncpy(p_param_err, "sarNum is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->sar_denom != 1)
      {
          strncpy(p_param_err, "sarDenom is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->video_full_range_flag != -1)
      {
          strncpy(p_param_err,
                  "videoFullRangeFlag is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8temporalLayersEnable)
      {
          strncpy(p_param_err,
                  "temporalLayersEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8spatialLayersMinusOne > 0)
      {
          strncpy(p_param_err,
                  "spatialLayers is not supported for jpeg encode",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8LowDelay)
      {
          strncpy(p_param_err, "LowDelay is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8rdoLevel)
      {
          strncpy(p_param_err, "rdoLevel is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8EnableRdoQuant)
      {
          strncpy(p_param_err, "EnableRdoQuant is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8enable2PassGopPatern)
      {
          strncpy(p_param_err,
                  "enable2PassGop is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8LookAheadDepth)
      {
          strncpy(p_param_err,
                  "lookAheadDepth is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (GOP_PRESET_IDX_DEFAULT != p_cfg->niParamT408.gop_preset_index)
      {
          strncpy(p_param_err,
                  "gopPresetIdx is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.roiEnable)
      {
          strncpy(p_param_err, "roiEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if(p_src->roi_demo_mode)
      {
          strncpy(p_param_err,
                  "RoiDemoMode is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->cacheRoi)
      {
          strncpy(p_param_err, "cacheRoi is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_src->reconf_demo_mode != XCODER_TEST_RECONF_OFF)
      {
          strncpy(p_param_err,
                  "ReconfDemoMode is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.intraRefreshMode)
      {
          strncpy(p_param_err,
                  "intraRefreshMode is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.intraRefreshArg)
      {
          strncpy(p_param_err,
                  "intraRefreshArg is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.intra_period != 120)
      {
          strncpy(p_param_err,
                  "intraPeriod is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8intraResetRefresh)
      {
          strncpy(
              p_param_err,
              "IntraRefreshResetOnForceIDR is not supported on jpeg encoder",
              max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.useLongTerm)
      {
          strncpy(p_param_err,
                  "longTermReferenceEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui32setLongTermInterval)
      {
          strncpy(p_param_err,
                  "longTermReferenceInterval is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8setLongTermCount != 2)
      {
          strncpy(p_param_err,
                  "longTermReferenceCount is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8multicoreJointMode)
      {
          strncpy(p_param_err,
                  "multicoreJointMode is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8enableSSIM)
      {
          strncpy(p_param_err, "enableSSIM is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_enc->rc.vbv_buffer_size != -1)
      {
          strncpy(p_param_err,
                  "vbvBufferSize is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8fillerEnable)
      {
          strncpy(p_param_err,
                  "fillerEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8picSkipEnable)
      {
          strncpy(p_param_err, "picSkip is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui16maxFrameSize)
      {
          strncpy(p_param_err, "maxFrameSize is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.enable_cu_level_rate_control)
      {
          strncpy(p_param_err,
                  "cuLevelRCEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.enable_hvs_qp)
      {
          strncpy(p_param_err, "hvsQPEnable is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.profile)
      {
          strncpy(p_param_err, "profile is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->niParamT408.intra_mb_refresh_mode || p_cfg->niParamT408.intra_mb_refresh_arg)
      {
          strncpy(p_param_err, "intraRefreshMode or intraRefreshArg is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i8crf != -1)
      {
          strncpy(p_param_err, "crf is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i32tolCtbRcInter != (int32_t)(0.1 * 1000))
      {
          strncpy(p_param_err, "tolCtbRcInter is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i32tolCtbRcIntra != (int32_t)(0.1 * 1000))
      {
          strncpy(p_param_err, "tolCtbRcIntra is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8rcQpDeltaRange != 10)
      {
          strncpy(p_param_err,
                  "rcQpDeltaRange is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i16bitrateWindow != -255)
      {
          strncpy(p_param_err, "bitrateWindow is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i16ctbRowQpStep)
      {
          strncpy(p_param_err, "ctbRowQpStep is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8AiEnhanceMode)
      {
          strncpy(p_param_err,
                  "enableAIEnhance is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i8ppsInitQp)
      {
          strncpy(p_param_err, "ppsInitQp is not supported for jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->i8pass1Qp)
      {
          strncpy(p_param_err, "pass1Qp is not supported for jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_enc->bitrateMode != -1)
      {
          strncpy(p_param_err, "bitrateMode is not supported for jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
      if (p_cfg->ui8vbvBufferReencode)
      {
          strncpy(p_param_err,
                  "vbvBufferReencode is not supported on jpeg encoder",
                  max_err_len);
          param_ret = NI_RETCODE_ERROR_UNSUPPORTED_FEATURE;
          LRETURN;
      }
  }

  if (p_src->force_frame_type != 0 && p_src->force_frame_type != 1)
  {
      strncpy(p_param_err, "Invalid forceFrameType: out of range",
              max_err_len);
      param_ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (p_cfg->niParamT408.forcedHeaderEnable > 2)
  {
      strncpy(p_param_err, "Invalid forcedHeaderEnable: out of range",
              max_err_len);
      param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
      LRETURN;
  }

  if (p_cfg->niParamT408.decoding_refresh_type < 0 ||
    p_cfg->niParamT408.decoding_refresh_type > 2)
  {
    strncpy(p_param_err, "Invalid decoding_refresh_type: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE;
    LRETURN;
  }

  if (!QUADRA)
  {
    if (p_cfg->niParamT408.gop_preset_index < 0 ||
      p_cfg->niParamT408.gop_preset_index > 8)
    {
      strcpy(p_param_err, "Invalid gop_preset_index: out of range");
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }

    if (p_src->low_delay_mode && 1 != p_cfg->niParamT408.gop_preset_index &&
        2 != p_cfg->niParamT408.gop_preset_index &&
        3 != p_cfg->niParamT408.gop_preset_index &&
        6 != p_cfg->niParamT408.gop_preset_index &&
        7 != p_cfg->niParamT408.gop_preset_index &&
        !(0 == p_cfg->niParamT408.gop_preset_index &&
          1 == p_cfg->niParamT408.custom_gop_params.custom_gop_size))
    {
      strcpy(p_param_err, "GOP size must be 1 when lowDelay is enabled");
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }
  }
  else // QUADRA
  {
    if (p_cfg->ui8gopSize > 16)
    {
      strncpy(p_param_err, "Invalid gopSize out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }

    if (p_cfg->ui8gopLowdelay &&
        p_cfg->ui8gopSize > 4)
    {
      strncpy(p_param_err, "GOP size must be <= 4 for low delay GOP", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
      LRETURN;
    }

    if (p_cfg->ui8LookAheadDepth)
    {
      if (p_cfg->ui8LookAheadDepth < 4 || p_cfg->ui8LookAheadDepth > 40)
      {
        strncpy(p_param_err, "Invalid LookAheadDepth: out of range. <[4-40]>", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
        LRETURN;
      }
      if (p_cfg->ui8gopLowdelay)
      {
        strncpy(p_param_err, "2-pass encode does not support low delay GOP", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
        LRETURN;
      }
      if (p_cfg->ui8planarFormat == NI_PIXEL_PLANAR_FORMAT_TILED4X4)
      {
          strncpy(p_param_err, "2-pass encode does not support tile4x4 format",
                  max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
          LRETURN;
      }
      if (p_cfg->ui8spatialLayersMinusOne > 0)
      {
        strncpy(p_param_err, "currently do not support lookahead encode with multi spatial layers", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
        LRETURN;
      }
    }

    if (p_src->low_delay_mode || p_cfg->ui8picSkipEnable)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
        {
          if (p_cfg->niParamT408.custom_gop_params.pic_param[i].poc_offset != (i+1))
          {
            if (p_src->low_delay_mode)
              strncpy(p_param_err, "Custom GOP must not include backward prediction when lowDelay is enabled", max_err_len);
            else
              strncpy(p_param_err, "Custom GOP must not include backward prediction when picSkip is enabled", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      } else if (1 != p_cfg->ui8gopSize && !p_cfg->ui8gopLowdelay &&
                 p_cfg->niParamT408.intra_period != 1 &&
                 p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_HIERARCHICAL_IPPPP)
      {
        if (p_src->low_delay_mode)
        {
          if (p_cfg->ui8spatialLayersMinusOne == 0)
          {
            strncpy(p_param_err, "Must use low delay GOP (gopPresetIdx 1,3,7,9,10) when lowDelay is enabled", max_err_len);
            param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
            LRETURN;
          }
        }
        else
        {
          strncpy(p_param_err, "Must use low delay GOP (gopPresetIdx 1,3,7,9,10) when picSkip is enabled", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
          LRETURN;
        }
      } else if ((p_cfg->ui8LookAheadDepth != 0) && (!p_cfg->ui8useLowDelayPocType))
      {
        if (p_src->low_delay_mode)
          strncpy(p_param_err, "lookAheadDepth must be 0 when lowDelay is enabled", max_err_len);
        else
          strncpy(p_param_err, "lookAheadDepth must be 0 when picSkip is enabled", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }

      if (p_cfg->ui8multicoreJointMode)
      {
          if (p_src->low_delay_mode)
            strncpy(p_param_err,
                    "Cannot use multicoreJointMode when lowDelay is enabled",
                    max_err_len);
          else
            strncpy(p_param_err,
                    "Cannot use multicoreJointMode when picSkip is enabled",
                    max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }

      if (p_src->minFramesDelay)
      {
          if (p_src->low_delay_mode)
            strncpy(p_param_err,
                    "Cannot enable minFramesDelay when lowDelay is enabled",
                    max_err_len);
          else
            strncpy(p_param_err,
                    "Cannot enable minFramesDelay when picSkip is enabled",
                    max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
    }

    if (p_cfg->ui8spatialLayersMinusOne > 0)
    {
      int intra_period_in_frames = p_cfg->niParamT408.intra_period * (p_cfg->ui8spatialLayersMinusOne+1);
      p_cfg->niParamT408.intra_period = p_cfg->niParamT408.avcIdrPeriod = intra_period_in_frames;
    }

    if (p_cfg->ui8useLowDelayPocType)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size > 1)
      {
        strncpy(p_param_err, "Custom GOP size must be 1 when useLowDelayPocType is enabled", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      } else if (1 != p_cfg->ui8gopSize && !p_cfg->ui8gopLowdelay &&
                 p_cfg->niParamT408.intra_period != 1)
      {
        strncpy(p_param_err, "Must use GOP with all frames as reference frames (gopPresetIdx 1,3,7,9) when useLowDelayPocType is enabled", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_GOP_PRESET;
        LRETURN;
      }
    }

    if (p_src->low_delay_mode)
    {
      // minimum acceptable value of maxFrameSize is bitrate / framerate in bytes
      uint32_t min_maxFrameSize = p_cfg->i32bitRate / p_cfg->i32frameRateInfo * p_cfg->i32frameRateDenominator / 8;

      if (p_cfg->ui16maxFrameSize == 0)
      {
          if (p_enc->maxFrameSizeRatio > 0)
          {
              if (min_maxFrameSize * p_enc->maxFrameSizeRatio > NI_MAX_FRAME_SIZE)
              {
                  p_cfg->ui16maxFrameSize = NI_MAX_FRAME_SIZE / 2000;
              } else
              {
                  p_cfg->ui16maxFrameSize = min_maxFrameSize * p_enc->maxFrameSizeRatio / 2000;
              }
              ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: Set maxFrameSize to %d times the "
                     "minimum frame size %d bytes in low delay mode\n", __func__,
                     p_enc->maxFrameSizeRatio, p_cfg->ui16maxFrameSize * 2000);
          } else
          {
              p_cfg->ui16maxFrameSize = ((p_src->source_width * p_src->source_height * 3 / 4) * p_ctx->bit_depth_factor) / 2000;
              ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: maxFrameSize is not set in low delay "
                     "mode. Set it to half of the maximum frame size %d bytes\n",
                     __func__, p_cfg->ui16maxFrameSize*2000);
          }
      }

      if (p_cfg->ui16maxFrameSize < min_maxFrameSize / 2000)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG, "%s: maxFrameSize %u is too small. Changed to minimum value (bitrate/framerate in byte): %u\n",
                 __func__, p_cfg->ui16maxFrameSize*2000, min_maxFrameSize);
          p_cfg->ui16maxFrameSize = min_maxFrameSize / 2000;
      }
    }
    else
    {
      if (p_cfg->ui16maxFrameSize != 0 || p_enc->maxFrameSizeRatio > 0)
      {
          strncpy(p_param_err, "maxFrameSize can only be used when lowDelay is enabled", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
      }
    }
  }

  if (QUADRA)
  {
    if (p_cfg->ui16gdrDuration)
    {
      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        for (i = 0; i < p_cfg->niParamT408.custom_gop_params.custom_gop_size; i++)
        {
          if (2 == p_cfg->niParamT408.custom_gop_params.pic_param[i].pic_type)
          {
            strncpy(p_param_err, "Custom GOP can not have B frames for intra refresh", max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
        }
      }
      else if (p_cfg->ui8gopSize != 1 || p_cfg->ui8gopLowdelay)
      {
          if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT)
          {
              strncpy(p_param_err,
                      "Must use gopPresetIdx 9 (consecutive P frame) for intra refresh",
                      max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
          }
        strncpy(p_param_warn, "GOP size forced to 1 and low delay GOP force disabled (no B frames) for intra refresh", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8gopSize = 1;
        p_cfg->ui8gopLowdelay = 0;
      }
      if (p_cfg->ui16gdrDuration == 1)
      {
        strncpy(p_param_err,
                "intra refresh cycle (height / intraRefreshArg MB or CTU) must > 1",
                max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (p_cfg->ui8LookAheadDepth != 0)
      {
        strncpy(p_param_err, "lookaheadDepth must be 0 for intra refresh", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (STD_HEVC == p_cfg->ui8bitstreamFormat ||
          STD_AV1 == p_cfg->ui8bitstreamFormat)
      {
        if (p_cfg->niParamT408.intra_period < p_cfg->ui16gdrDuration)
        {
          strncpy(p_param_warn, "intraPeriod forced to match intra refersh cycle (intraPeriod must >= intra refersh cycle)", max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
          p_cfg->niParamT408.intra_period = p_cfg->ui16gdrDuration;
        }
      }
      else if (STD_AVC == p_cfg->ui8bitstreamFormat)
      {
        if (p_cfg->niParamT408.avcIdrPeriod < p_cfg->ui16gdrDuration)
        {
          strncpy(p_param_warn, "intraPeriod forced to match intra refersh cycle (intraPeriod must >= intra refersh cycle)", max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
          p_cfg->niParamT408.avcIdrPeriod = p_cfg->ui16gdrDuration;
        }
      }
    }

    if (p_cfg->ui8multicoreJointMode)
    {
        if (p_cfg->ui8hrdEnable)
        {
            strncpy(p_param_warn, "HRD conformance is not guaranteed in multicoreJointMode", max_err_len);
            warning = NI_RETCODE_PARAM_WARN;
        }
    }

    if (p_cfg->ui8temporalLayersEnable)
    {
        if (p_cfg->ui8hrdEnable)
        {
            snprintf(p_param_err, max_err_len, "HRD conformance with multiple temporal layers is currently not supported");
            param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
    }

    if ((p_cfg->ui8hrdEnable) || (p_cfg->ui8fillerEnable))
    {
     // enable rate control
     if (p_cfg->ui8rcEnable == 0)
     {
         p_cfg->ui8rcEnable = p_src->cfg_enc_params.rc.enable_rate_control = 1;
     }

     // enable hrd if it is off
     if (p_cfg->i32vbvBufferSize == 0)
     {
       p_cfg->i32vbvBufferSize = 3000;
     }
    }

    // maxrate must >= bitrate
    if (p_cfg->ui32vbvMaxRate != 0)
    {
        if (p_cfg->ui32vbvMaxRate < p_cfg->i32bitRate)
        {
            snprintf(p_param_err, max_err_len, "vbvMaxRate %u cannot be smaller than bitrate %d",
                     p_cfg->ui32vbvMaxRate, p_cfg->i32bitRate);
            param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
    }

    // minrate must <= bitrate
    if (p_cfg->ui32vbvMinRate != 0)
    {
        if (p_cfg->ui32vbvMinRate > p_cfg->i32bitRate)
        {
            snprintf(p_param_err, max_err_len, "vbvMinRate %u cannot be larger than bitrate %d",
                     p_cfg->ui32vbvMinRate, p_cfg->i32bitRate);
            param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }
    }

    // The supported range of vbvBufferSize are 0, or 1/framerate to 3000. If the vbvBufferSize is not 0.
    // The minimum vbvBufferSize in msec is 1/framerate. The actual minimum in bits is bitrate/framerate.
    if (p_cfg->i32vbvBufferSize != 0)
    {
        // check buffer size compatible to bitrate
        uint32_t minVbvBufferSize = p_cfg->i32frameRateDenominator * 1000 / p_cfg->i32frameRateInfo;
        if (p_cfg->i32vbvBufferSize < minVbvBufferSize)
        {
            snprintf(p_param_err, max_err_len, "vbvBufferSize must be greater than the average frame size. Minimum is %u msec for framerate %d fps",
                     minVbvBufferSize, (p_cfg->i32frameRateInfo / p_cfg->i32frameRateDenominator));
            param_ret = NI_RETCODE_PARAM_INVALID_VALUE;
            LRETURN;
        }

        // check buffer size compatible to maxrate
        if (p_cfg->ui32vbvMaxRate != 0)
        {
            uint32_t maxRateMinVbvBufferSize = ((int64_t)p_cfg->i32frameRateDenominator * 1000 / p_cfg->i32frameRateInfo) * p_cfg->ui32vbvMaxRate / p_cfg->i32bitRate;
            if (p_cfg->i32vbvBufferSize < maxRateMinVbvBufferSize)
            {
                snprintf(p_param_warn, max_err_len, "vbvBufferSize cannot be smaller than one frame size based on vbvMaxRate, force vbvBufferSize to %u msec for bitrate %d vbvMaxRate %u and framerate %d fps",
                         maxRateMinVbvBufferSize, p_cfg->i32bitRate, p_cfg->ui32vbvMaxRate, (p_cfg->i32frameRateInfo / p_cfg->i32frameRateDenominator));
                warning = NI_RETCODE_PARAM_WARN;
                p_cfg->i32vbvBufferSize = maxRateMinVbvBufferSize;
            }
        }
        // check buffer size compatible to minrate (capped CRF may set minrate without maxrate)
        else if (p_cfg->ui32vbvMinRate != 0)
        {
            uint32_t minRateMinVbvBufferSize = ((int64_t)p_cfg->i32frameRateDenominator * 1000 / p_cfg->i32frameRateInfo) * p_cfg->ui32vbvMinRate / p_cfg->i32bitRate;
            if (p_cfg->i32vbvBufferSize < minRateMinVbvBufferSize)
            {
                snprintf(p_param_warn, max_err_len, "vbvBufferSize cannot be smaller than one frame size based on vbvMinRate, force vbvBufferSize to %u msec for bitrate %d vbvMinRate %u and framerate %d fps",
                         minRateMinVbvBufferSize, p_cfg->i32bitRate, p_cfg->ui32vbvMinRate, (p_cfg->i32frameRateInfo / p_cfg->i32frameRateDenominator));
                warning = NI_RETCODE_PARAM_WARN;
                p_cfg->i32vbvBufferSize = minRateMinVbvBufferSize;
            }
        }
    }
    else
    {
        // check buffer size compatible to maxrate and/or minrate
        if ( p_cfg->ui32vbvMaxRate != 0 || p_cfg->ui32vbvMinRate != 0 )
        {
            snprintf(p_param_warn, max_err_len, "vbvMaxRate %u vbvMinRate %u does not take effect when vbvBufferSize is 0, force vbvMaxRate vbvMinRate to 0",
                     p_cfg->ui32vbvMaxRate, p_cfg->ui32vbvMinRate);
            warning = NI_RETCODE_PARAM_WARN;
            p_cfg->ui32vbvMaxRate = 0;
            p_cfg->ui32vbvMinRate = 0;
        }
    }

    if (p_cfg->ui32ltrRefInterval || p_cfg->niParamT408.useLongTerm)
    {
        if (p_cfg->ui32ltrRefInterval && p_cfg->niParamT408.useLongTerm)
        {
            strncpy(p_param_err,
                    "Can't enable ltrRefInterval and longTermReferenceEnable "
                    "at same time",
                    max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
        }

      if (p_cfg->niParamT408.custom_gop_params.custom_gop_size)
      {
        if (p_cfg->niParamT408.custom_gop_params.custom_gop_size > 1)
        {
          strncpy(p_param_err, "Custom GOP size can not be > 1 for long term reference", max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }
      } else if ((p_cfg->ui8gopSize != 1) && (p_cfg->ui8gopLowdelay == 0) &&
                 (p_cfg->niParamT408.intra_period != 1))
      {
          if (p_cfg->niParamT408.gop_preset_index != GOP_PRESET_IDX_DEFAULT)
          {
              strncpy(p_param_err,
                      "Must use low delay GOP (gopPresetIdx 1,3,7,9) for long term reference",
                      max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
          }
        strncpy(p_param_warn, "GOP size forced to 1 for long term reference", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8gopSize = 1;
      }

      if (p_cfg->ui8LookAheadDepth != 0)
      {
        strncpy(p_param_err, "lookaheadDepth must be 0 for long term reference", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
      if (p_cfg->i8crf >= 0)
      {
        strncpy(p_param_err, "crf must < 0 for long term reference", max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
      }
    }

    if (p_cfg->ui32setLongTermInterval && (p_cfg->niParamT408.useLongTerm == 0))
    {
        strncpy(
            p_param_err,
            "Must set longTermReferenceEnable for longTermReferenceInterval",
            max_err_len);
        param_ret = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (p_cfg->ui8av1ErrResilientMode)
    {
      if (STD_AV1 != p_cfg->ui8bitstreamFormat)
      {
          strncpy(p_param_warn, "AV1 err resilient mode forced to 0 when using other codecs", max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
          p_cfg->ui8av1ErrResilientMode = 0;
      }
    }

    if (p_cfg->ui8motionConstrainedMode)
    {
      if (STD_HEVC != p_cfg->ui8bitstreamFormat)
      {
          strncpy(p_param_warn, "Motion Constrained mode force disabled for codecs other than HEVC", max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
          p_cfg->ui8motionConstrainedMode = 0;
      }
      else
      {
        if (p_cfg->ui8rdoLevel != 1)
        {
          strncpy(
              p_param_err,
              "rdoLevel must be 1 for Motion Constrained mode 1 or 2",
              max_err_len);
          param_ret = NI_RETCODE_INVALID_PARAM;
          LRETURN;
        }

        if (p_cfg->ui8motionConstrainedMode == MOTION_CONSTRAINED_QUALITY_MODE)
        {
          if (p_cfg->ui8multicoreJointMode)
          {
            strncpy(
                p_param_err,
                "multicoreJointMode must be 0 for Motion Constrained mode 2",
                max_err_len);
            param_ret = NI_RETCODE_INVALID_PARAM;
            LRETURN;
          }
          if (p_src->source_width % 64 || p_src->source_height % 64)
          {
            if (!p_cfg->ui32cropWidth || !p_cfg->ui32cropHeight ||
                p_cfg->ui32cropWidth % 64 || p_cfg->ui32cropHeight % 64)
            {
              strncpy(
                  p_param_err,
                  "input resolution (or cropping window) must be 64x64 aligned for Motion Constrained mode 2",
                  max_err_len);
              param_ret = NI_RETCODE_INVALID_PARAM;
              LRETURN;
            }
          }
        }
      }
    }

    if (p_cfg->ui8avccHvcc)
    {
      if ((STD_AVC != p_cfg->ui8bitstreamFormat) && (STD_HEVC != p_cfg->ui8bitstreamFormat))
      {
        strncpy(p_param_warn, "AVCC HVCC forced to 0 for codecs other than AVC HEVC", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        p_cfg->ui8avccHvcc = 0;
      }
    }
  }

  if (p_cfg->niParamT408.cu_size_mode < 0 ||
    p_cfg->niParamT408.cu_size_mode > 7)
  {
    strncpy(p_param_err, "Invalid cu_size_mode: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_CU_SIZE_MODE;
    LRETURN;
  }



  if (p_cfg->niParamT408.use_recommend_enc_params < 0 ||
    p_cfg->niParamT408.use_recommend_enc_params > 3)
  {
    strncpy(p_param_err, "Invalid use_recommend_enc_params: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM;
    LRETURN;
  }

  switch (p_cfg->niParamT408.use_recommend_enc_params)
  {
    case 0:
    case 2:
    case 3:
    {
      if (p_cfg->niParamT408.use_recommend_enc_params != 3)
      {
        // in FAST mode (recommendEncParam==3), max_num_merge value will be
        // decided in FW
        if (p_cfg->niParamT408.max_num_merge < 0 ||
          p_cfg->niParamT408.max_num_merge > 3)
        {
          strncpy(p_param_err, "Invalid max_num_merge: out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_MAXNUMMERGE;
          LRETURN;
        }
      }
      break;
    }

    default: break;
  }

  if ( p_cfg->niParamT408.intra_qp < -1 ||
       p_cfg->niParamT408.intra_qp > 51 )
  {
    strncpy(p_param_err, "Invalid intra_qp: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_INTRA_QP;
    LRETURN;
  }

  if (QUADRA)
  {
    if (p_cfg->i8crf >= 0 && p_cfg->i8crf <= 51)
    {
      if (p_cfg->ui8LookAheadDepth < 4 || p_cfg->ui8LookAheadDepth > 40)
      {
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6X") < 0)
        {
          strncpy(p_param_err, "CRF requres LookAheadDepth <[4-40]>", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH;
          LRETURN;
        }
        else
        {
          p_cfg->ui8LookAheadDepth = 1;
          p_cfg->ui8noMbtree = 1;
          strncpy(p_param_warn, "enable lookahead of current frame", max_err_len);
          warning = NI_RETCODE_PARAM_WARN;
        }
      }

      if (p_cfg->ui8rcEnable == 1)
      {
        strncpy(p_param_err, "CRF requires RcEnable 0", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
        LRETURN;
      }
      #if 0
      if (p_cfg->ui8ctbRcMode > 0)
      {
        strncpy(p_param_warn, "Lookahead with cuLevelRCEnable or hvsQPEnable may degrade quality", max_err_len);
        warning = NI_RETCODE_PARAM_WARN;
        //LRETURN;
      }
      #endif
    }

    if (p_cfg->ui8tuneBframeVisual == TUNE_BFRAME_VISUAL_MEDIUM)
    {
        if (p_cfg->ui8LookAheadDepth == 0)
        {
            strncpy(p_param_err, "tuneBframeVisual level 1 (medium) requires lookahead or crf encode", max_err_len);
            param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
            LRETURN;
        }
    }
  }

  if ( p_cfg->niParamT408.enable_mb_level_rc != 1 &&
       p_cfg->niParamT408.enable_mb_level_rc != 0 )
  {
    strncpy(p_param_err, "Invalid enable_mb_level_rc: out of range", max_err_len);
    param_ret = NI_RETCODE_PARAM_ERROR_RCENABLE;
    LRETURN;
  }

  {
    if ( p_cfg->niParamT408.minQpI < 0 ||
         p_cfg->niParamT408.minQpI > 51 )
    {
      strncpy(p_param_err, "Invalid min_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MN_QP;
      LRETURN;
    }

    if ( p_cfg->niParamT408.maxQpI < 0 ||
         p_cfg->niParamT408.maxQpI > 51 )
    {
      strncpy(p_param_err, "Invalid max_qp: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }

    if ( p_cfg->niParamT408.enable_cu_level_rate_control != 1 &&
         p_cfg->niParamT408.enable_cu_level_rate_control != 0 )
    {
      strncpy(p_param_err, "Invalid enable_cu_level_rate_control: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN;
      LRETURN;
    }

    //if (p_cfg->niParamT408.enable_cu_level_rate_control == 1)
    {
      if ( p_cfg->niParamT408.enable_hvs_qp != 1 &&
           p_cfg->niParamT408.enable_hvs_qp != 0 )
      {
        strncpy(p_param_err, "Invalid enable_hvs_qp: out of range", max_err_len);
        param_ret = NI_RETCODE_PARAM_ERROR_HVS_QP_EN;
        LRETURN;
      }

      if (p_cfg->niParamT408.enable_hvs_qp)
      {
        if ( p_cfg->niParamT408.max_delta_qp < 0 ||
             p_cfg->niParamT408.max_delta_qp > 51 )
        {
          strncpy(p_param_err, "Invalid max_delta_qp: out of range", max_err_len);
          param_ret = NI_RETCODE_PARAM_ERROR_MX_DELTA_QP;
          LRETURN;
        }
      }
    }
    // hrd is off when i32vbvBufferSize is 0
    if ((p_cfg->i32vbvBufferSize < 10 && p_cfg->i32vbvBufferSize != 0) || p_cfg->i32vbvBufferSize > 3000)
    {
      strncpy(p_param_err, "Invalid i32vbvBufferSize: out of range", max_err_len);
      param_ret = NI_RETCODE_PARAM_ERROR_VBV_BUFFER_SIZE;
      LRETURN;
    }
  }

  // check valid for common param
  param_ret = ni_check_common_params(&p_cfg->niParamT408, p_src, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  // check valid for RC param
  param_ret = ni_check_ratecontrol_params(p_cfg, p_param_err, max_err_len);
  if (param_ret != NI_RETCODE_SUCCESS)
  {
    LRETURN;
  }

  if (warning == NI_RETCODE_PARAM_WARN && param_ret == NI_RETCODE_SUCCESS)
  {
    param_ret = NI_RETCODE_PARAM_WARN;
    strncpy(p_param_err, p_param_warn, max_err_len);
  }

END:
    free(p_param_warn);
    return param_ret;
}

ni_retcode_t ni_check_common_params(ni_t408_config_t *p_param,
                                    ni_xcoder_params_t *p_src,
                                    char *p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  int32_t low_delay = 0;
  int32_t intra_period_gop_step_size;
  int32_t i, j;

  if (!p_param || !p_src || !p_param_err)
  {
      ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
             __func__);
      ret = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  // check low-delay gop structure
  if (!QUADRA)
  {
    if (0 == p_param->gop_preset_index) // common gop
    {
      if (p_param->custom_gop_params.custom_gop_size > 1)
      {
          int minVal = p_param->custom_gop_params.pic_param[0].poc_offset;
          low_delay = 1;
          for (i = 1; i < p_param->custom_gop_params.custom_gop_size; i++)
          {
              if (minVal > p_param->custom_gop_params.pic_param[i].poc_offset)
              {
                  low_delay = 0;
                  break;
              } else
              {
                  minVal = p_param->custom_gop_params.pic_param[i].poc_offset;
              }
        }
      }
    }
    else if (p_param->gop_preset_index == 2 ||
      p_param->gop_preset_index == 3 ||
      p_param->gop_preset_index == 6 ||
      p_param->gop_preset_index == 7) // low-delay case (IPPP, IBBB)
    {
      low_delay = 1;
    }

    if (low_delay)
    {
      intra_period_gop_step_size = 1;
    }
    else
    {
      if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
      {
        intra_period_gop_step_size = p_param->custom_gop_params.custom_gop_size;
      }
      else
      {
        intra_period_gop_step_size = presetGopSize[p_param->gop_preset_index];
      }
    }

    if (((p_param->intra_period != 0) && ((p_param->intra_period < intra_period_gop_step_size+1) == 1)) ||
        ((p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod < intra_period_gop_step_size+1) == 1)))
    {
      strncpy(p_param_err, "Invalid intra_period and gop_preset_index: gop structure is larger than intra period", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
      LRETURN;
    }

    if (((!low_delay) && (p_param->intra_period != 0) && ((p_param->intra_period % intra_period_gop_step_size) != 0)) ||
        ((!low_delay) && (p_param->avcIdrPeriod != 0) && ((p_param->avcIdrPeriod % intra_period_gop_step_size) != 0)))
    {
      strncpy(p_param_err, "Invalid intra_period and gop_preset_index: intra period is not a multiple of gop structure size", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_INTRA_PERIOD;
      LRETURN;
    }

    if (p_param->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      int temp_poc[NI_MAX_GOP_NUM];
      int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (p_param->custom_gop_params.pic_param[i].temporal_id >= XCODER_MAX_NUM_TEMPORAL_LAYER)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is zero or negative", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }
        temp_poc[i] = p_param->custom_gop_params.pic_param[i].poc_offset;
        if (min_poc > temp_poc[i])
        {
          min_poc = temp_poc[i];
        }
      }
      int count_pos = 0;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        for (j = 0; j < p_param->custom_gop_params.custom_gop_size; j++)
        {
          if (temp_poc[j] == min_poc)
          {
            count_pos++;
            min_poc++;
          }
        }
      }
      if (count_pos != p_param->custom_gop_params.custom_gop_size)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: poc_offset is invalid", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }
  }
  else // QUADRA
  {
    if (p_param->custom_gop_params.custom_gop_size)
    {
      int temp_poc[NI_MAX_GOP_NUM];
      int min_poc = p_param->custom_gop_params.pic_param[0].poc_offset;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        if (p_param->custom_gop_params.pic_param[i].temporal_id >= XCODER_MAX_NUM_TEMPORAL_LAYER)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id larger than 7", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        if (p_param->custom_gop_params.pic_param[i].temporal_id < 0)
        {
          strncpy(p_param_err, "Invalid custom gop parameters: temporal_id is negative", max_err_len);
          ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
          LRETURN;
        }

        for (j = 0; j < p_param->custom_gop_params.pic_param[i].num_ref_pics; j++)
        {
          if (p_param->custom_gop_params.pic_param[i].rps[j].ref_pic == 0)
          {
            strncpy(p_param_err, "Invalid custom gop parameters: ref pic delta cannot be 0", max_err_len);
            ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
            LRETURN;
          }
        }

        for (j = 0; j < NI_MAX_REF_PIC; j++)
        {
          if (p_param->custom_gop_params.pic_param[i].rps[j].ref_pic != 0 &&
              p_param->custom_gop_params.pic_param[i].rps[j].ref_pic_used == -1)
          {
            ni_log(NI_LOG_ERROR,"g%drefPic%d specified without g%drefPic%dUsed specified!\n", i, j, i, j);
            ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
            LRETURN;
          }
        }

        temp_poc[i] = p_param->custom_gop_params.pic_param[i].poc_offset;
        if (min_poc > temp_poc[i])
        {
          min_poc = temp_poc[i];
        }
      }
      int count_pos = 0;
      for (i = 0; i < p_param->custom_gop_params.custom_gop_size; i++)
      {
        for (j = 0; j < p_param->custom_gop_params.custom_gop_size; j++)
        {
          if (temp_poc[j] == min_poc)
          {
            count_pos++;
            min_poc++;
          }
        }
      }
      if (count_pos != p_param->custom_gop_params.custom_gop_size)
      {
        strncpy(p_param_err, "Invalid custom gop parameters: poc_offset is invalid", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSTOM_GOP;
        LRETURN;
      }
    }
  }

  if (0 == p_param->use_recommend_enc_params)
  {
    // RDO
    {
      int align_32_width_flag = p_src->source_width % 32;
      int align_16_width_flag = p_src->source_width % 16;
      int align_8_width_flag = p_src->source_width % 8;
      int align_32_height_flag = p_src->source_height % 32;
      int align_16_height_flag = p_src->source_height % 16;
      int align_8_height_flag = p_src->source_height % 8;

      if (((p_param->cu_size_mode & 0x1) == 0) && ((align_8_width_flag != 0) || (align_8_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 8 pixels when enable CU8x8 of cu_size_mode. Recommend to set cu_size_mode |= 0x1 (CU8x8)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((align_16_width_flag != 0) || (align_16_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 16 pixels when enable CU16x16 of cu_size_mode. Recommend to set cu_size_mode |= 0x2 (CU16x16)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN;
        LRETURN;
      }
      else if (((p_param->cu_size_mode & 0x1) == 0) && ((p_param->cu_size_mode & 0x2) == 0) && ((p_param->cu_size_mode & 0x4) == 0) && ((align_32_width_flag != 0) || (align_32_height_flag != 0)))
      {
        strncpy(p_param_err, "Invalid use_recommend_enc_params and cu_size_mode: picture width and height must be aligned with 32 pixels when enable CU32x32 of cu_size_mode. Recommend to set cu_size_mode |= 0x4 (CU32x32)", max_err_len);
        ret = NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN;
        LRETURN;
      }
    }
  }

  if ((p_param->conf_win_top < 0) || (p_param->conf_win_top > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_top: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }
  if (p_param->conf_win_top % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_top: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP;
    LRETURN;
  }

  if ((p_param->conf_win_bottom < 0) || (p_param->conf_win_bottom > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }
  if (p_param->conf_win_bottom % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_bottom: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT;
    LRETURN;
  }

  if ((p_param->conf_win_left < 0) || (p_param->conf_win_left > 8192))
  {
    strncpy(p_param_err, "Invalid conf_win_left: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }
  if (p_param->conf_win_left % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_left: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_L;
    LRETURN;
  }

  if (p_param->conf_win_right < 0 || p_param->conf_win_right > 8192)
  {
    strncpy(p_param_err, "Invalid conf_win_right: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
    LRETURN;
  }
  if (p_param->conf_win_right % 2)
  {
    strncpy(p_param_err, "Invalid conf_win_right: not multiple of 2", max_err_len);
    ret = NI_RETCODE_PARAM_ERROR_CONF_WIN_R;
  }

END:

    return ret;
}

ni_retcode_t ni_check_ratecontrol_params(ni_encoder_config_t* p_cfg, char* p_param_err, uint32_t max_err_len)
{
  ni_retcode_t ret = NI_RETCODE_SUCCESS;
  ni_t408_config_t* p_param = NULL;

  if( (!p_cfg) || (!p_param_err) )
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() Null pointer parameters passed\n",
           __func__);
    ret = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }
  p_param = &p_cfg->niParamT408;

  //Zero out the error buffer
  memset(p_param_err, 0, max_err_len);

  if (p_param->roiEnable != 0 && p_param->roiEnable != 1)
  {
    strncpy(p_param_err, "Invalid roiEnable: out of range", max_err_len);
    ret = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  if (p_param->roiEnable && p_param->enable_hvs_qp)
  {
    strncpy(p_param_err, "hvsQPEnable and roiEnable: not mutually exclusive", max_err_len);
    ret = NI_RETCODE_PARAM_INVALID_VALUE;
    LRETURN;
  }

  if (p_cfg->ui8rcEnable == 1)
  {
    if (p_param->minQpP > p_param->maxQpP || p_param->minQpB > p_param->maxQpB)
    {
      strncpy(p_param_err, "Invalid min_qp(P/B) and max_qp(P/B): min_qp cannot be larger than max_qp", max_err_len);
      ret = NI_RETCODE_PARAM_ERROR_MX_QP;
      LRETURN;
    }
  }

END:

    return ret;
}


/*!******************************************************************************
 *  \brief  Print xcoder user configurations
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_params_print(ni_xcoder_params_t *const p_encoder_params)
{
  if (!p_encoder_params)
  {
    return;
  }

  ni_encoder_cfg_params_t *p_enc = &p_encoder_params->cfg_enc_params;

  ni_log(NI_LOG_DEBUG, "XCoder Params:\n");

  ni_log(NI_LOG_DEBUG, "preset=%d\n", p_encoder_params->preset);
  ni_log(NI_LOG_DEBUG, "fps_number / fps_denominator=%u / %u\n",
                 p_encoder_params->fps_number,
                 p_encoder_params->fps_denominator);

  ni_log(NI_LOG_DEBUG, "source_width x source_height=%dx%d\n", p_encoder_params->source_width, p_encoder_params->source_height);
  ni_log(NI_LOG_DEBUG, "bitrate=%d\n", p_encoder_params->bitrate);

  ni_log(NI_LOG_DEBUG, "profile=%d\n", p_enc->profile);
  ni_log(NI_LOG_DEBUG, "level_idc=%d\n", p_enc->level_idc);
  ni_log(NI_LOG_DEBUG, "high_tier=%d\n", p_enc->high_tier);

  ni_log(NI_LOG_DEBUG, "frame_rate=%d\n", p_enc->frame_rate);

  ni_log(NI_LOG_DEBUG, "use_recommend_enc_params=%d\n", p_enc->use_recommend_enc_params);
  ni_log(NI_LOG_DEBUG, "cu_size_mode=%d\n", p_enc->cu_size_mode);
  ni_log(NI_LOG_DEBUG, "max_num_merge=%d\n", p_enc->max_num_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_8x8_merge=%d\n", p_enc->enable_dynamic_8x8_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_16x16_merge=%d\n", p_enc->enable_dynamic_16x16_merge);
  ni_log(NI_LOG_DEBUG, "enable_dynamic_32x32_merge=%d\n", p_enc->enable_dynamic_32x32_merge);
  // trans_rate not available in Rev B
  ni_log(NI_LOG_DEBUG, "enable_rate_control=%d\n", p_enc->rc.enable_rate_control);
  ni_log(NI_LOG_DEBUG, "enable_cu_level_rate_control=%d\n", p_enc->rc.enable_cu_level_rate_control);
  ni_log(NI_LOG_DEBUG, "enable_hvs_qp=%d\n", p_enc->rc.enable_hvs_qp);
  ni_log(NI_LOG_DEBUG, "enable_hvs_qp_scale=%d\n", p_enc->rc.enable_hvs_qp_scale);
  ni_log(NI_LOG_DEBUG, "hvs_qp_scale=%d\n", p_enc->rc.hvs_qp_scale);
  ni_log(NI_LOG_DEBUG, "min_qp=%d\n", p_enc->rc.min_qp);
  ni_log(NI_LOG_DEBUG, "max_qp=%d\n", p_enc->rc.max_qp);
  ni_log(NI_LOG_DEBUG, "max_delta_qp=%d\n", p_enc->rc.max_delta_qp);
  ni_log(NI_LOG_DEBUG, "vbv_buffer_size=%d\n", p_enc->rc.vbv_buffer_size);
  ni_log(NI_LOG_DEBUG, "enable_filler=%d\n", p_enc->rc.enable_filler);
  ni_log(NI_LOG_DEBUG, "enable_pic_skip=%d\n", p_enc->rc.enable_pic_skip);

  ni_log(NI_LOG_DEBUG, "forcedHeaderEnable=%d\n", p_enc->forced_header_enable);
  ni_log(NI_LOG_DEBUG, "roi_enable=%d\n", p_enc->roi_enable);
  ni_log(NI_LOG_DEBUG, "long_term_ref_enable=%d\n", p_enc->long_term_ref_enable);
  ni_log(NI_LOG_DEBUG, "long_term_ref_interval=%d\n", p_enc->long_term_ref_interval);
  ni_log(NI_LOG_DEBUG, "long_term_ref_count=%d\n", p_enc->long_term_ref_count);
  ni_log(NI_LOG_DEBUG, "conf_win_top=%d\n", p_enc->conf_win_top);
  ni_log(NI_LOG_DEBUG, "conf_win_bottom=%d\n", p_enc->conf_win_bottom);
  ni_log(NI_LOG_DEBUG, "conf_win_left=%d\n", p_enc->conf_win_left);
  ni_log(NI_LOG_DEBUG, "conf_win_right=%d\n", p_enc->conf_win_right);

  ni_log(NI_LOG_DEBUG, "intra_qp=%d\n", p_enc->rc.intra_qp);
  ni_log(NI_LOG_DEBUG, "enable_mb_level_rc=%d\n", p_enc->rc.enable_mb_level_rc);

  ni_log(NI_LOG_DEBUG, "intra_period=%d\n", p_enc->intra_period);
  ni_log(NI_LOG_DEBUG, "decoding_refresh_type=%d\n", p_enc->decoding_refresh_type);

  // Rev. B: H.264 only or HEVC-shared parameters, in ni_t408_config_t
  ni_log(NI_LOG_DEBUG, "enable_transform_8x8=%d\n", p_enc->enable_transform_8x8);
  ni_log(NI_LOG_DEBUG, "slice_mode=%d\n", p_enc->slice_mode);
  ni_log(NI_LOG_DEBUG, "slice_arg=%d\n", p_enc->slice_arg);
  ni_log(NI_LOG_DEBUG, "entropy_coding_mode=%d\n", p_enc->entropy_coding_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_mode=%d\n", p_enc->intra_mb_refresh_mode);
  ni_log(NI_LOG_DEBUG, "intra_mb_refresh_arg=%d\n", p_enc->intra_mb_refresh_arg);
  ni_log(NI_LOG_DEBUG, "intra_reset_refresh=%d\n", p_enc->intra_reset_refresh);

  ni_log(NI_LOG_DEBUG, "gop_preset_index=%d\n", p_enc->gop_preset_index);
#ifndef QUADRA
  if (!QUADRA)
  {
    if (p_enc->gop_preset_index == GOP_PRESET_IDX_CUSTOM)
    {
      int i;
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
      for (i = 0; i < p_enc->custom_gop_params.custom_gop_size; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_qp=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_qp);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pic_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].num_ref_pic_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L0=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L0);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].ref_poc_L1=%d\n", i, p_enc->custom_gop_params.pic_param[i].ref_poc_L1);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_enc->custom_gop_params.pic_param[i].temporal_id);
      }
    }
  }
  else // QUADRA
#endif
  {
    if (p_enc->custom_gop_params.custom_gop_size)
    {
      int i, j;
      ni_log(NI_LOG_DEBUG, "custom_gop_params.custom_gop_size=%d\n", p_enc->custom_gop_params.custom_gop_size);
      for (i = 0; i < NI_MAX_GOP_NUM; i++)
      {
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].poc_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].poc_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_offset=%d\n", i, p_enc->custom_gop_params.pic_param[i].qp_offset);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].qp_factor=%lf\n", i, p_enc->custom_gop_params.pic_param[i].qp_factor);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].temporal_id=%d\n", i, p_enc->custom_gop_params.pic_param[i].temporal_id);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].pic_type=%d\n", i, p_enc->custom_gop_params.pic_param[i].pic_type);
        ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].num_ref_pics=%d\n", i, p_enc->custom_gop_params.pic_param[i].num_ref_pics);
        for (j = 0; j < NI_MAX_REF_PIC; j++)
        {
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic=%d\n", i, j, p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic);
          ni_log(NI_LOG_DEBUG, "custom_gop_params.pic_param[%d].rps[%d].ref_pic_used=%d\n", i, j, p_enc->custom_gop_params.pic_param[i].rps[j].ref_pic_used);
        }
      }
    }
  }

  return;
}

/*!******************************************************************************
 *  \brief  decoder keep alive thread function triggers every 1 second
 *
 *  \param void thread args
 *
 *  \return void
 *******************************************************************************/
void *ni_session_keep_alive_thread(void *arguments)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_thread_arg_struct_t *args = (ni_thread_arg_struct_t *)arguments;
    ni_session_stats_t inst_info = {0};
    ni_session_context_t ctx = {0};
    uint64_t endtime = ni_gettime_ns();
    uint64_t current_time;
    ni_pthread_mutex_t *p_mutex;
    //interval(nanoseconds) is equals to ctx.keep_alive_timeout/3(330,000,000ns approximately equal to 1/3 second).
    uint64_t interval = args->keep_alive_timeout * 330000000LL;
#ifndef _ANDROID
#ifdef __linux__
    struct sched_param sched_param;

    // Linux has a wide variety of signals, Windows has a few.
    // A large number of signals will interrupt the thread, which will cause heartbeat command interval more than 1 second.
    // So just mask the unuseful signals in Linux
    sigset_t signal;
    sigfillset(&signal);
    ni_pthread_sigmask(SIG_BLOCK, &signal, NULL);

    /* set up schedule priority
     * first try to run with RR mode.
     * if fails, try to set nice value.
     * if fails either, ignore it and run with default priority.
     * Note: Scheduling requires root permission. App is probably exectued
     *       without root so the priority for this thread might just end up
     *       being default.
     */
    if (((sched_param.sched_priority = sched_get_priority_max(SCHED_RR)) ==
         -1) ||
        sched_setscheduler(syscall(SYS_gettid), SCHED_RR, &sched_param) < 0)
    {
        ni_log(NI_LOG_DEBUG, "%s cannot set scheduler: %s\n", __func__,
               strerror(NI_ERRNO));
        if (setpriority(PRIO_PROCESS, 0, -20) != 0)
        {
            ni_log(NI_LOG_DEBUG, "%s cannot set nice value: %s\n", __func__,
                   strerror(NI_ERRNO));
        }
    }

#elif defined(_WIN32)
    /* set up schedule priority.
   * try to set the current thread to time critical level which is the highest prioriy
   * level.
   */
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) ==
        0)
    {
        ni_log(NI_LOG_DEBUG, "%s cannot set priority: %d.\n", __func__,
               GetLastError());
    }
#endif
#endif
#ifndef _WIN32
    // Set thread name, name is KAT + hw_id + session_id
    // hw_id need at most 2 bytes, session_id at most 4 bytes.
    char name[16] = {0};
    snprintf(name, sizeof(name), "%s%.2x%.4x", "KAT", args->hw_id, args->session_id);
#if __linux__
    prctl(PR_SET_NAME, name);
#elif __APPLE__
    pthread_setname_np(name);
#endif
#endif
    // Initializes the session context variables that keep alive command and query status command need.
    ni_device_session_context_init(&ctx);
    ctx.last_access_time = endtime;
    ctx.hw_id = args->hw_id;
    ctx.session_id = args->session_id;
    ctx.session_timestamp = args->session_timestamp;
    ctx.device_type = args->device_type;
    ctx.blk_io_handle = args->device_handle;
    ctx.event_handle = args->thread_event_handle;
    ctx.p_all_zero_buf = args->p_buffer;
    ctx.keep_alive_timeout = args->keep_alive_timeout;
    volatile uint64_t * plast_access_time = args->plast_access_time;
    if((ctx.last_access_time - *plast_access_time) >=
            ctx.keep_alive_timeout * 1000000000LL)
    {
      ni_log(NI_LOG_ERROR,
            "%s creation timeout. session_id=0x%X requested timeout: %" PRIu64
            "ns, ping time delta: %" PRIu64 "ns\n ",
            __func__, ctx.session_id,
            (uint64_t)ctx.keep_alive_timeout * 1000000000LL,
            ctx.last_access_time - *plast_access_time);
    }
    ni_log(NI_LOG_DEBUG, "%s ctx.keep_alive_timeout: %us.\n", __func__,
           ctx.keep_alive_timeout);
    p_mutex = args->p_mutex;

    for (;;)
    {
        ni_pthread_mutex_lock(p_mutex);

        retval =
            ni_send_session_keep_alive(ctx.session_id, ctx.blk_io_handle,
                                       ctx.event_handle, ctx.p_all_zero_buf);

        retval = ni_query_session_stats(&ctx,
                               ctx.device_type,
                               &inst_info,
                               retval,
                               nvme_admin_cmd_xcoder_config);

        if (NI_RETCODE_SUCCESS == retval)
        {
            retval = ni_nvme_check_error_code(inst_info.ui32LastTransactionCompletionStatus,
                                            nvme_admin_cmd_xcoder_config,
                                            ctx.device_type,
                                            ctx.hw_id,
                                            &(ctx.session_id));
        }

        ni_pthread_mutex_unlock(p_mutex);

        if(retval)
        {
            uint32_t error_status = inst_info.ui32LastTransactionCompletionStatus;
            if(error_status == NI_RETCODE_SUCCESS)
            {
                /* QDFWSH-971: Error is sometimes captured by keep_alive_thread
                 but LastTransactionCompletionStatus may be overwrited and cause
                 incorrect log. In this case, check LastErrorStatus.*/
                ni_log(NI_LOG_ERROR, "session_no 0x%x inst_err_no may be overwrited!\n",
                                  ctx.session_id);
                ni_nvme_check_error_code(inst_info.ui32LastErrorStatus,
                                  nvme_admin_cmd_xcoder_config,
                                  ctx.device_type,
                                  ctx.hw_id,
                                  &(ctx.session_id));
                error_status = inst_info.ui32LastErrorStatus;
            }
            ni_log(NI_LOG_ERROR,
                   "Persistent failures detected, %s() line-%d: session_no 0x%x sess_err_no %u "
                   "inst_err_no %u\n",
                   __func__, __LINE__, ctx.session_id, inst_info.ui16ErrorCount, error_status);
            LRETURN;
        }
        current_time = ni_gettime_ns();
        /*If the interval between two heartbeats is greater then expected(interval) or
        acceptable(timeout) then the thread might have been blocked.*/
        if ((current_time - ctx.last_access_time) >= (2 * interval) ||   //*2 is for safety
            (current_time - ctx.last_access_time) >=
                args->keep_alive_timeout * 1000000000LL)
        {
            ni_log(
                NI_LOG_INFO,
                "%s was possibly blocked. session_id=0x%X requested timeout: %" PRIu64
                "ns, ping time delta: %" PRIu64 "ns\n ",
                __func__, ctx.session_id,
                (uint64_t)ctx.keep_alive_timeout * 1000000000LL,
                current_time - ctx.last_access_time);
        }
        *plast_access_time = ctx.last_access_time = current_time;
        if (ctx.session_id == NI_INVALID_SESSION_ID)
        {
            retval = NI_RETCODE_ERROR_INVALID_SESSION;
        }

        // 1. If received failure, set the close_thread flag to TRUE, and exit,
        //    then main thread will check this flag and return failure directly;
        // 2. skip checking VPU recovery.
        //    If keep_alive thread detect the VPU RECOVERY before main thread,
        //    the close_thread flag may damage the vpu recovery handling process.
        if ((NI_RETCODE_SUCCESS != retval) &&
            (NI_RETCODE_NVME_SC_VPU_RECOVERY != retval))
        {
            LRETURN;
        }
        endtime += interval;
        while (ni_gettime_ns() < endtime)
        {
            if (args->close_thread)
            {
                LRETURN;
            }
            ni_usleep(10000);   // 10ms per loop
        }
    }

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "%s abnormal closed:%d\n", __func__, retval);
        // changing the value to be True here means the thread has been closed.
        args->close_thread = true;
    }

    ni_device_session_context_clear(&ctx);

    ni_log(NI_LOG_DEBUG, "%s(): exit\n", __func__);

    return NULL;
}

/*!******************************************************************************
*  \brief  Open a xcoder upload instance
*
*  \param   p_ctx   - pointer to caller allocated uploader session context
*
*  \return
*           On success
*               NI_RETCODE_SUCCESS
*
*           On failure
*               NI_RETCODE_INVALID_PARAM
*               NI_RETCODE_ERROR_MEM_ALOC
*               NI_RETCODE_ERROR_INVALID_SESSION
*               NI_RETCODE_FAILURE
*******************************************************************************/
ni_retcode_t ni_uploader_session_open(ni_session_context_t* p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  void * p_buffer = NULL;
  uint32_t ui32LBA = 0;
  uint32_t modelled_load;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  //Create the session if the create session flag is set
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    p_ctx->device_type = NI_DEVICE_TYPE_UPLOAD;
    p_ctx->pts_table = NULL;
    p_ctx->dts_queue = NULL;
    p_ctx->p_leftover = NULL;
    p_ctx->buffer_pool = NULL;
    p_ctx->prev_size = 0;
    p_ctx->sent_size = 0;
    p_ctx->status = 0;
    p_ctx->key_frame_type = 0;
    p_ctx->ready_to_close = 0;
    p_ctx->rc_error_count = 0;
    p_ctx->frame_num = 0;
    p_ctx->pkt_num = 0;
    p_ctx->pkt_index = 0;

    //malloc zero data buffer
    if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                          NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
    //In case we can open sesison, the session id would become valid.
    ((ni_session_stats_t *)p_buffer)->ui16SessionId =
        (uint16_t)NI_INVALID_SESSION_ID;

    // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
    ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_ENCODER, ni_htonl(p_ctx->codec_format), 1/*1 for uploadMode*/);
    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
        LRETURN;
    }
    //Open will return a session status structure with a valid session id if it worked.
    //Otherwise the invalid session id set before the open command will stay
    p_ctx->session_id = ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp = ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_high);
    p_ctx->session_timestamp = (p_ctx->session_timestamp << 32) |
        ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_low);
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);
        ni_encoder_session_close(p_ctx, 0);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,
           "Uploader open session ID:0x%x,timestamp:%" PRIu64 "\n",
           p_ctx->session_id, p_ctx->session_timestamp);

    //Send keep alive timeout Info
    uint64_t keep_alive_timeout =
        p_ctx->keep_alive_timeout * 1000000;   //send us to FW
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
    memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s keep_alive_timeout %" PRIx64 "\n", __func__,
           keep_alive_timeout);
    ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    CHECK_VPU_RECOVERY(retval);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR %s(): nvme write keep_alive_timeout command "
               "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    // only update firmware with pixel rate if firmware >= 6rf
    if (ni_cmp_fw_api_ver(
         (char *) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rf") >= 0)
    {
      /* Add the modelled load of the hwuploader */
      if (p_ctx->framerate.framerate_denom == 0)
      {
        // unknown or variable frame rate. Assume 25 fps
        modelled_load = (p_ctx->active_video_width * p_ctx->active_video_height) * 25;
      }
      else
      {
        modelled_load = (p_ctx->active_video_width * p_ctx->active_video_height *
                         p_ctx->framerate.framerate_num / p_ctx->framerate.framerate_denom);
      }

      // modelled_load can overflow a 4-byte value so we will downscale it to
      // kilopixels per sec by shifting the modelled load down by 10 bits.
      modelled_load >>= 10;

      ui32LBA = CONFIG_INSTANCE_UploadModel_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

      memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
      memcpy(p_buffer, &modelled_load, 4);

      retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
                   p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
      CHECK_VPU_RECOVERY(retval);

      if (NI_RETCODE_SUCCESS != retval)
      {
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
      }
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "Open session completed\n");
  }

  // init for frame pts calculation
  p_ctx->is_first_frame = 1;
  p_ctx->last_pts = 0;
  p_ctx->last_dts = 0;

  ni_timestamp_init(p_ctx, &p_ctx->pts_table, "dec_pts");
  ni_timestamp_init(p_ctx, &p_ctx->dts_queue, "dec_dts");

  //p_ctx->active_video_width = 0;
  //p_ctx->active_video_height = 0;

  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_ctx->device_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
         "p_ctx->session_id=%d\n",
         __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
         p_ctx->session_id);

  p_ctx->hw_action = NI_CODEC_HW_NONE;
#ifndef _WIN32
  // If this is a P2P upload session, open the Netint kernel driver
  if (p_ctx->isP2P)
  {
      retval = p2p_fill_pcie_address(p_ctx);
      if(retval != NI_RETCODE_SUCCESS)
      {
        LRETURN;
      }
  }
#endif

END:

  ni_aligned_free(p_buffer);
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
  return retval;
}

/*!******************************************************************************
*  \brief  Copy a xcoder decoder worker thread info
*
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_copy_internal(ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx)
{
  if (!src_p_ctx || !dst_p_ctx)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): passed parameters are null!, return\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  dst_p_ctx->pext_mutex = src_p_ctx->pext_mutex; //for hwdl
  dst_p_ctx->max_nvme_io_size = src_p_ctx->max_nvme_io_size;
  dst_p_ctx->device_handle = src_p_ctx->device_handle;
  dst_p_ctx->blk_io_handle = src_p_ctx->blk_io_handle;
  dst_p_ctx->hw_id = src_p_ctx->hw_id;
  dst_p_ctx->session_timestamp = src_p_ctx->session_timestamp;
  memcpy(dst_p_ctx->fw_rev, src_p_ctx->fw_rev, sizeof(src_p_ctx->fw_rev));
  if (src_p_ctx->isP2P)
  {
      dst_p_ctx->isP2P = src_p_ctx->isP2P;
      dst_p_ctx->ddr_config = src_p_ctx->ddr_config;
      dst_p_ctx->domain = src_p_ctx->domain;
      dst_p_ctx->bus = src_p_ctx->bus;
      dst_p_ctx->dev = src_p_ctx->dev;
      dst_p_ctx->fn = src_p_ctx->fn;
      dst_p_ctx->netint_fd = src_p_ctx->netint_fd;
  }

  return NI_RETCODE_SUCCESS;
}


/*!******************************************************************************
 *  \brief  Query and acquire buffer from xcoder scaler instance
 *
 *  \param[in]      p_ctx           pointer to scaler session context
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_scaler_session_query_buffer_avail(ni_session_context_t *p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  ni_instance_buf_info_t sInstanceBuf = {0};
  int query_retry = 0;

  if (p_ctx->session_id == NI_INVALID_SESSION_ID)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      return NI_RETCODE_ERROR_INVALID_SESSION;
  }

  if (p_ctx->buffered_frame_index > 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,
              "Session=0x%x: %s buffered_frame_index=%d return %d\n",
              p_ctx->session_id,
              __func__,
              p_ctx->buffered_frame_index,
              retval);
      return retval;
  }

  for (;;)
  {
    query_sleep(p_ctx);
    ni_log2(p_ctx, NI_LOG_DEBUG, "%s: query by ni_query_instance_buf_info INST_BUF_INFO_RW_UPLOAD\n", __func__);
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                      NI_DEVICE_TYPE_SCALER, &sInstanceBuf);

    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_3);

    /* A frame index of zero is invalid, the memory acquisition failed */
    if (NI_RETCODE_SUCCESS != retval || (sInstanceBuf.hw_inst_ind.frame_index == 0 && query_retry >=500))
    {
            if (query_retry >=500)
            {
                ni_log2(p_ctx, NI_LOG_DEBUG,
                    "scaler write exceeded max query retries. rc=%d try=%d\n",
                    retval, query_retry);
            }
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
    }
    if (sInstanceBuf.hw_inst_ind.frame_index == 0 && query_retry < 500)
    {
            query_retry++;
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(100);
            ni_pthread_mutex_lock(&p_ctx->mutex);
    }
    else
    {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: FID %d\n", __func__,
                sInstanceBuf.hw_inst_ind.frame_index);
            p_ctx->buffered_frame_index = sInstanceBuf.hw_inst_ind.frame_index;
            break;
    }
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,
         "Session=0x%x: %s got FrameIndex=%u buffered_frame_index=%d\n",
         p_ctx->session_id,
         __func__,
         sInstanceBuf.hw_inst_ind.frame_index,p_ctx->buffered_frame_index);

END:

  if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s Warning scalar read hwdesc fail rc %d or ind !\n",
             __func__, retval);
  }
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

  return retval;
}


/*!******************************************************************************
*  \brief   Query and acquire buffer from xcoder upload instance
*
*  \param   p_ctx   pointer to uploader session context
*
*  \return  NI_RETCODE_SUCCESS
*           NI_RETCODE_ERROR_INVALID_SESSION
*           NI_RETCODE_ERROR_MEM_ALOC
*******************************************************************************/
ni_retcode_t ni_hwupload_session_query_buffer_avail(ni_session_context_t *p_ctx)
{
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  //uint32_t metadata_size = NI_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t frame_size_bytes = 0;
  uint32_t retry_count = 0;
  ni_instance_buf_info_t buf_info = { 0 };

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

//#ifdef MEASURE_LATENCY
#if 0
  if ((p_frame->pts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      uint64_t abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_frame->pts);
  }
#endif

  //p_ctx->status = 0;

  if (p_ctx->buffered_frame_index > 0)
    return retval;

  for (;;)
  {
      query_sleep(p_ctx);

      retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                          NI_DEVICE_TYPE_ENCODER, &buf_info);
      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
      if (NI_RETCODE_SUCCESS != retval ||
          (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count >= 500))
      {
          if (retry_count >= 500)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,
                     "hwupload write exceeded max query retries. rc=%d try=%d"
                     "\n",
                     retval, retry_count);
          }
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          //p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
          LRETURN;
      }
      if (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count < 500)
      {
          retry_count++;
          ni_pthread_mutex_unlock(&p_ctx->mutex);
          ni_usleep(100);
          ni_pthread_mutex_lock(&p_ctx->mutex);
      }
      else
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: avail %d, FID %d\n", __func__,
                 buf_info.hw_inst_ind.buffer_avail,
                 buf_info.hw_inst_ind.frame_index);

          p_ctx->buffered_frame_index = buf_info.hw_inst_ind.frame_index;
          break;
      }
  }
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Info hwupload write query success, available buf "
                 "size %u >= frame size %u , retry %u\n",
                 buf_info.buf_avail_size, frame_size_bytes, retry_count);

//#ifdef MEASURE_LATENCY
#if 0
  if ((p_frame->pts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
    uint64_t abs_time_ns = ni_gettime_ns();
    ni_lat_meas_q_t *q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
    ni_log(NI_LOG_INFO, "PTS:%" PRId64 ",DELTA:%" PRId64 ",uLAT:%" PRIu64 ";\n",
           p_frame->pts, abs_time_ns - q->last_benchmark_time,
           ni_lat_meas_q_check_latency(q, abs_time_ns, p_frame->pts));
    q->last_benchmark_time = abs_time_ns;
  }
#endif

  //retval = size;

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}


/*!******************************************************************************
*  \brief  Send a YUV p_frame to upload session
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwupload_session_write(ni_session_context_t *p_ctx, ni_frame_t *p_frame,
                              niFrameSurface1_t *hwdesc)
{
  int retval = 0;
  uint32_t size = 0;
  //uint32_t metadata_size = NI_APP_ENC_FRAME_META_DATA_SIZE;
  uint32_t i = 0;
  uint32_t sent_size = 0;
  uint32_t frame_size_bytes = 0;
  uint32_t retry_count = 0;
  ni_instance_buf_info_t buf_info = { 0 };

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);
  //niFrameSurface1_t* p_data3 = (niFrameSurface1_t*)((uint8_t*)p_frame->p_data[3]);
  ////p_data3->rsvd = p_meta->hwdesc->rsvd;
  //ni_log2(p_ctx, NI_LOG_DEBUG,  "%s:mar16 HW=%d ui16FrameIdx=%d i8InstID=%d device_handle=%d\n",
  //               ishwframe, p_data3->ui16FrameIdx, p_data3->i8InstID, p_data3->device_handle);

  if (!p_ctx || !p_frame)
  {
    ni_log(NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  uint8_t separate_metadata = p_frame->separate_metadata;
  uint8_t separate_start = (p_frame->separate_start && p_frame->total_start_len) ? 1 : 0;

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->pts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
      uint64_t abs_time_ns = ni_gettime_ns();
      ni_lat_meas_q_add_entry((ni_lat_meas_q_t *)p_ctx->frame_time_q,
                              abs_time_ns, p_frame->pts);
  }
#endif

  frame_size_bytes = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2];// +p_frame->data_len[3] + p_frame->extra_data_len;
  ni_log2(p_ctx, NI_LOG_DEBUG,  "frame size bytes =%u  %d is metadata!\n", frame_size_bytes,
                 0);
  p_ctx->status = 0;

  if (p_frame->end_of_stream)
  {
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
  }

 if (p_ctx->buffered_frame_index > 0)
 {
   buf_info.hw_inst_ind.frame_index = p_ctx->buffered_frame_index;
   p_ctx->buffered_frame_index = 0;
 }
 else
 {
  for (;;)
  {
      query_sleep(p_ctx);

      retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                          NI_DEVICE_TYPE_ENCODER, &buf_info);
      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
      if (NI_RETCODE_SUCCESS != retval ||
          (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count >= 500))
      {
          if (retry_count >= 500)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,
                     "hwupload write exceeded max query retries. rc=%d try=%d"
                     "\n",
                     retval, retry_count);
          }
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
          LRETURN;
      }
      if (buf_info.hw_inst_ind.buffer_avail == 0 && retry_count < 500)
      {
          retry_count++;
          ni_pthread_mutex_unlock(&p_ctx->mutex);
          ni_usleep(100);
          ni_pthread_mutex_lock(&p_ctx->mutex);
      } else   //available
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: avail %d, FID %d\n", __func__,
                 buf_info.hw_inst_ind.buffer_avail,
                 buf_info.hw_inst_ind.frame_index);
          break;
      }
  }
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Info hwupload write query success, available buf "
                 "size %u >= frame size %u , retry %u\n",
                 buf_info.buf_avail_size, frame_size_bytes, retry_count);
 }

  {
#ifdef XCODER_TIMESTAMP_DTS_ENABLED
      retval = ni_timestamp_register(p_ctx->buffer_pool, p_ctx->dts_queue,
                                     p_frame->dts, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
          ni_log2(p_ctx, NI_LOG_ERROR,
                 "ERROR %s(): ni_timestamp_register() for dts "
                 "returned: %d\n",
                 __func__, retval);
      }
#endif

      //Apply write configuration here
      retval = ni_config_session_rw(p_ctx, SESSION_WRITE_CONFIG, 1,
                                    NI_CODEC_HW_UPLOAD, 0);
      CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                   p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
      CHECK_VPU_RECOVERY(retval);

      if (separate_metadata)
      {
          if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                "6S") < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): uploader separated metadata not supported on device with FW api version < 6.S\n",
                     __func__);
              retval = NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
              LRETURN;
          }

          if (!p_frame->p_metadata_buffer)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): p_metadata_buffer is NULL, allocation failed?\n",
                     __func__);
              retval = NI_RETCODE_ERROR_MEM_ALOC;
              LRETURN;
          }

          // fill in metadata
          ni_metadata_enc_frame_t *p_meta;
          p_meta = (ni_metadata_enc_frame_t *)p_frame->p_metadata_buffer;

          if (separate_start)
          {
              for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
                  p_meta->start_len[i] = p_frame->start_len[i];
          }
          else
          {
              memset(p_meta->start_len, 0, sizeof(p_meta->start_len));
          }
          p_meta->inconsecutive_transfer = p_frame->inconsecutive_transfer;

          ni_log(
              NI_LOG_DEBUG,
              "%s(): %d.%u p_ctx->frame_num=%" PRIu64 ", "
              "p_frame->video_width=%u, p_frame->video_height=%u, "
              "start_len [%u,%u,%u] inconsecutive_transfer %u\n",
              __func__, p_ctx->hw_id, p_ctx->session_id, p_ctx->frame_num,
              p_frame->video_width, p_frame->video_height,
              p_meta->start_len[0], p_meta->start_len[1], p_meta->start_len[2],
              p_meta->inconsecutive_transfer);

          uint32_t ui32LBA_metadata =
              WRITE_METADATA_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_metadata_buffer = %p, metadata_buffer_size "
                 "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_metadata_buffer,
                 p_frame->metadata_buffer_size, p_ctx->frame_num,
                 ui32LBA_metadata);

          sent_size =
              ((p_frame->metadata_buffer_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(
              p_ctx->blk_io_handle, p_ctx->event_handle,
              p_frame->p_metadata_buffer, sent_size,
              ui32LBA_metadata);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n",
                     __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      if (separate_start)
      {
          if (!p_frame->p_start_buffer)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): p_start_buffer is NULL, allocation failed?\n",
                     __func__);
              retval = NI_RETCODE_ERROR_MEM_ALOC;
              LRETURN;
          }

          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_start_buffer = %p, p_frame->start_buffer_size "
                 "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_start_buffer, p_frame->start_buffer_size, p_ctx->frame_num,
                 ui32LBA);

          sent_size =
              ((p_frame->start_buffer_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                          p_frame->p_start_buffer, sent_size, ui32LBA);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      if (p_frame->inconsecutive_transfer)
      {
          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

          for (i = 0; i < NI_MAX_NUM_SW_FRAME_DATA_POINTERS; i++)
          {
              ni_log2(p_ctx, NI_LOG_DEBUG,
                     "%s: p_data = %p, p_frame->buffer_size "
                     "= %u, p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                     __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
                     ui32LBA);

              if (p_frame->data_len[i])
              {
                  sent_size = p_frame->data_len[i];
                  if (separate_start)
                    sent_size -= p_frame->start_len[i];

                  sent_size =
                      ((sent_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

                  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                                  p_frame->p_data[i]+p_frame->start_len[i], sent_size, ui32LBA);
                  CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
                  CHECK_VPU_RECOVERY(retval);
                  if (retval < 0)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                      LRETURN;
                  }
              }
          }
      }
      else
      {
          uint32_t ui32LBA =
              WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: p_data = %p, p_frame->buffer_size = %u, "
                 "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                 __func__, p_frame->p_data, p_frame->buffer_size, p_ctx->frame_num,
                 ui32LBA);

          sent_size = frame_size_bytes;
          if (separate_start)
            sent_size -= p_frame->total_start_len;

          sent_size =
              ((sent_size + (NI_MEM_PAGE_ALIGNMENT-1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;

          retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                          p_frame->p_buffer+p_frame->total_start_len, sent_size, ui32LBA);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                       p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);
          if (retval < 0)
          {
              ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
              retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
              LRETURN;
          }
      }

      hwdesc->ui16FrameIdx = buf_info.hw_inst_ind.frame_index;
      hwdesc->ui16session_ID = p_ctx->session_id;
      hwdesc->device_handle =
          (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
      hwdesc->bit_depth = p_ctx->bit_depth_factor;
      hwdesc->src_cpu = (uint8_t)NI_DEVICE_TYPE_ENCODER;
      hwdesc->output_idx = hwdesc->ui32nodeAddress = 0;

      p_ctx->frame_num++;
      size = frame_size_bytes;

#ifdef XCODER_DUMP_DATA
      char dump_file[256];
      snprintf(dump_file, sizeof(dump_file), "%ld-%u-hwup-fme/fme-%04ld.yuv",
               (long)getpid(), p_ctx->session_id, (long)p_ctx->frame_num);

      FILE *f = fopen(dump_file, "wb");
      fwrite(p_frame->p_buffer,
             p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2],
             1, f);
      fflush(f);
      fclose(f);
#endif
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->pts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
    uint64_t abs_time_ns = ni_gettime_ns();
    ni_lat_meas_q_t *q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
    ni_log(NI_LOG_INFO, "PTS:%" PRId64 ",DELTA:%" PRId64 ",uLAT:%" PRIu64 ";\n",
           p_frame->pts, abs_time_ns - q->last_benchmark_time,
           ni_lat_meas_q_check_latency(q, abs_time_ns, p_frame->pts));
    q->last_benchmark_time = abs_time_ns;
  }
#endif

  retval = size;

END:

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
*  \brief  Retrieve a HW descriptor of uploaded frame
*
*  \param   p_ctx           pointer to uploader session context
*           hwdesc          pointer to hw descriptor
*
*  \return
*           On success
*               NI_RETCODE_SUCCESS
*           On failure
*               NI_RETCODE_INVALID_PARAM
*               NI_RETCODE_ERROR_INVALID_SESSION
*               NI_RETCODE_FAILURE
*******************************************************************************/
int ni_hwupload_session_read_hwdesc(ni_session_context_t *p_ctx,
                                    niFrameSurface1_t *hwdesc)
{
  int retval = 0;
  ni_instance_buf_info_t hwdesc_info = { 0 };
  int query_retry = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx || !hwdesc)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
         __func__);
    retval = NI_RETCODE_INVALID_PARAM;
    LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  for (;;)
  {
    query_sleep(p_ctx);

    query_retry++;
#ifndef _WIN32
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_R_ACQUIRE,
                                        NI_DEVICE_TYPE_ENCODER, &hwdesc_info);
#else
    retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                        NI_DEVICE_TYPE_ENCODER, &hwdesc_info);
#endif
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
      p_ctx->device_type, p_ctx->hw_id,
      &(p_ctx->session_id), OPT_1);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning upload read hwdesc fail rc %d or ind "
        "!\n", retval);

      if (query_retry >= 1000)
      {
        retval = NI_RETCODE_FAILURE;
        LRETURN;
      }
      ni_usleep(100);
    }
    else
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "Info hwupload read hwdesc success, "
             "frame_ind=%d !\n", hwdesc_info.hw_inst_ind.frame_index);

      hwdesc->ui16FrameIdx = hwdesc_info.hw_inst_ind.frame_index;
      hwdesc->ui16session_ID = p_ctx->session_id;
      hwdesc->device_handle =
          (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
      hwdesc->bit_depth = p_ctx->bit_depth_factor;
      hwdesc->src_cpu = (uint8_t)NI_DEVICE_TYPE_ENCODER;
      hwdesc->output_idx = 0;
      LRETURN;
    }
  }

END:
    return retval;
}

/*!*****************************************************************************
*  \brief  clear a particular xcoder instance buffer/data
*
*  \param   ni_session_context_t p_ctx - xcoder Context
*  \param   ni_instance_buf_info_rw_type_t rw_type
*  \param   ni_device_type_t device_type - xcoder type Encoder or Decoder
*  \param   ni_instance_buf_info_t *out - Struct preallocated from the caller
*           where the resulting data will be placed
*
*  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION,
*            NI_RETCODE_ERROR_MEM_ALOC or NI_RETCODE_ERROR_NVME_CMD_FAILED on
*            failure
******************************************************************************/
ni_retcode_t ni_clear_instance_buf(niFrameSurface1_t *surface)
{
  void* p_buffer = NULL;
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;

  ni_log(NI_LOG_TRACE, "%s(): enter - device_handle %d\n", __func__,
         surface->device_handle);

  if ((uint16_t)NI_INVALID_SESSION_ID == surface->ui16session_ID)
  {
    ni_log(NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  //malloc data buffer
  if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
  {
    ni_log(NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
           NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
  ni_log(NI_LOG_DEBUG, "%s(): FID = %d\n", __func__, surface->ui16FrameIdx);
  ui32LBA = CLEAR_INSTANCE_BUF_W(((uint16_t)surface->ui16FrameIdx));
  retval = ni_nvme_send_write_cmd((ni_device_handle_t)(int64_t)surface->device_handle,
                                  NI_INVALID_DEVICE_HANDLE, p_buffer,
                                  NI_DATA_BUFFER_LEN, ui32LBA);
  //Cannot check sessio stats here since this isn't a session command.
  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "[session_id=0x%x,time_stamp=%" PRIu64 "] " "%s(): NVME command Failed\n", surface->ui16session_ID, ni_log_get_utime(), __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

END:

    ni_aligned_free(p_buffer);
    ni_log(NI_LOG_TRACE, "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
*  \brief  Retrieve a hw desc p_frame from decoder
*  \param
*
*  \return
*******************************************************************************/
ni_retcode_t ni_decoder_session_read_desc(ni_session_context_t* p_ctx, ni_frame_t* p_frame)
{
    //Needs serious editing to support hwdesc read again, this is currently vanilla read
    //queue_info decoder_read_workerqueue;
    ni_instance_mgr_stream_info_t data = {0};
    int rx_size = 0;
    uint64_t frame_offset = 0;
    uint8_t *p_data_buffer = NULL;
    int i = 0;
    int retval = NI_RETCODE_SUCCESS;
    int metadata_hdr_size = NI_FW_META_DATA_SZ -
        NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
    int sei_size = 0;
    uint32_t total_bytes_to_read = 0;
    uint32_t total_yuv_met_size = 0;
    uint32_t read_size_bytes = 0;
    ni_instance_buf_info_t buf_info = {0};
    int query_retry = 0;
    uint32_t ui32LBA = 0;
    unsigned int bytes_read_so_far = 0;
    int query_type = INST_BUF_INFO_RW_READ;
    int low_delay_notify = 0;
    ni_session_statistic_t sessionStatistic = {0};
    uint32_t frames_dropped = 0;
    ni_xcoder_params_t *p_param;
    uint8_t get_first_metadata = 0;
    uint8_t sequence_change = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_frame)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

start:
  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_param = (ni_xcoder_params_t *)p_ctx->p_session_config;
  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  // p_frame->p_data[] can be NULL before actual resolution is returned by
  // decoder and buffer pool is allocated, so no checking here.

  total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;
  total_yuv_met_size = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + metadata_hdr_size;
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "Total bytes to read %u total_yuv_met_size %u, low_delay %u\n",
         total_bytes_to_read, total_yuv_met_size, p_ctx->decoder_low_delay);
  if (p_ctx->decoder_low_delay > 0 && !p_ctx->ready_to_close)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "frame_num = %" PRIu64 ", pkt_num = %" PRIu64 "\n",
             p_ctx->frame_num, p_ctx->pkt_num);
      frames_dropped = p_ctx->session_statistic.ui32FramesDropped;
      if (p_ctx->force_low_delay && (p_ctx->force_low_delay_cnt < frames_dropped)) {
          p_ctx->force_low_delay_cnt = frames_dropped;
      }
      if (p_ctx->frame_num + p_ctx->force_low_delay_cnt
          >= p_ctx->pkt_num)
      {
          //nothing to query, leave
          retval = NI_RETCODE_SUCCESS;
          LRETURN;
      }
      query_type = INST_BUF_INFO_RW_READ_BUSY;
  }
  for (;;)
  {
    query_sleep(p_ctx);

    query_retry++;

    if (ni_cmp_fw_api_ver(
            (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
            "6r3") >= 0)
    {
        retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_DECODER,
                                                 &sessionStatistic);
        CHECK_ERR_RC(p_ctx, retval, &sessionStatistic,
                      nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                      p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        CHECK_VPU_RECOVERY(retval);

        buf_info.buf_avail_size = sessionStatistic.ui32RdBufAvailSize;
    } else
    {
        retval = ni_query_instance_buf_info(p_ctx, query_type,
                                            NI_DEVICE_TYPE_DECODER, &buf_info);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);
    }

    ni_log2(p_ctx, NI_LOG_TRACE,  "Dec read desc query buf_info.size = %u\n",
           buf_info.buf_avail_size);

    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_TRACE,  "Dec read desc failed. Retry %d\n", query_retry);

      if (query_retry >= 1000)
      {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning: dec read desc failed %d retries. rc=%d"
               "\n", query_retry, retval);
        p_ctx->max_retry_fail_count[1]++;
        low_delay_notify = 1;
        retval = (p_ctx->max_retry_fail_count[1] >= NI_XCODER_FAILURES_MAX) ? NI_RETCODE_FAILURE : NI_RETCODE_SUCCESS;
        LRETURN;
      }
      ni_pthread_mutex_unlock(&p_ctx->mutex);
      ni_usleep(NI_RETRY_INTERVAL_100US);
      ni_pthread_mutex_lock(&p_ctx->mutex);
    } else if (buf_info.buf_avail_size == DP_IPC_PASSTHRU)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s(): Bad available buffer size %u\n", __FUNCTION__, buf_info.buf_avail_size);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    } else if (buf_info.buf_avail_size == metadata_hdr_size)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "Dec read desc only hdr metadata is available. Seq change may "
               "have occured.\n");
        total_bytes_to_read = metadata_hdr_size;
        sequence_change = 1;
        break;
    } else if (buf_info.buf_avail_size < total_yuv_met_size)
    {
      ni_log2(p_ctx, NI_LOG_TRACE,  "Dec read desc buf_size < frame_size. Retry %d\n", query_retry);

      // query to see if it is eos now, if we have sent it
      if (p_ctx->ready_to_close)
      {
          ni_log2(p_ctx, NI_LOG_TRACE,
                 "Dec read desc query, ready_to_close %u, query eos\n",
                 p_ctx->ready_to_close);
          retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
          CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                       p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
          CHECK_VPU_RECOVERY(retval);

          if (data.is_flushed ||
              query_retry >=
                  NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   // 15000 retries
          {
              if (query_retry >=
                  NI_MAX_DEC_SESSION_READ_QUERY_EOS_RETRIES)   //15000 retries
              {
                  ni_log2(p_ctx, NI_LOG_ERROR,
                         "WARNING: Dec read desc query eos reached but exceeded max "
                         "retries. is_flushed=%u try=%d.\n",
                         data.is_flushed, query_retry);
              } else
              {
                  ni_log2(p_ctx, NI_LOG_DEBUG,
                         "Dec read desc query eos reached. is_flushed=%u try=%d"
                         "\n",
                         data.is_flushed, query_retry);
              }
              p_frame->end_of_stream = 1;
              low_delay_notify = 1;
              retval = NI_RETCODE_SUCCESS;
              LRETURN;
        }
        else
        {
            ni_log2(p_ctx, NI_LOG_TRACE,
                   "Dec read desc available buf size == %d, query try %d, "
                   "retrying...\n",
                   buf_info.buf_avail_size, query_retry);
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(NI_RETRY_INTERVAL_200US);  // 200 us
            ni_pthread_mutex_lock(&p_ctx->mutex);
            continue;
        }
      }

      if ((NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status ||
           (p_ctx->decoder_low_delay > 0 &&
            ((p_ctx->frame_num + p_ctx->force_low_delay_cnt)
            < p_ctx->pkt_num))) &&
          query_retry < 1000 / 2)
      {
          if (p_ctx->decoder_low_delay && p_ctx->force_low_delay) {
              if (p_ctx->session_statistic.ui32FramesDropped > frames_dropped) {
                  // last pkt sent to decoder marked as dropped, no output,
                  // so just stop query and return
                  p_ctx->force_low_delay_cnt++;
                  low_delay_signal(p_ctx);
                  retval = NI_RETCODE_SUCCESS;
                  LRETURN;
              }
          } else if (NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL == p_ctx->status &&
                     sessionStatistic.ui32WrBufAvailSize > p_ctx->required_buf_size)
          {
              ni_log2(p_ctx, NI_LOG_TRACE, "Info dec write buffer is enough, available buf "
                      "size %u >= required size %u !\n",
                      sessionStatistic.ui32WrBufAvailSize, p_ctx->required_buf_size);
              p_ctx->status = 0;
              p_ctx->required_buf_size = 0;
              retval = NI_RETCODE_SUCCESS;
              LRETURN;
          }
          ni_pthread_mutex_unlock(&p_ctx->mutex);
          ni_usleep(NI_RETRY_INTERVAL_100US);
          ni_pthread_mutex_lock(&p_ctx->mutex);

          continue;
      } else
      {
          if (p_ctx->decoder_low_delay > 0)
          {
              if (p_ctx->force_low_delay) {
                  p_ctx->force_low_delay_cnt++;
                  low_delay_signal(p_ctx);
              } else {
                  ni_log2(p_ctx, NI_LOG_ERROR,"Warning: low delay mode with non sequential "
                        "input (B frames)? Just cancel the low delay mode then\n");
                  // Here it should be the last signal to release the send thread
                  // holding the low delay mutex.
                  low_delay_signal(p_ctx);
                  p_ctx->decoder_low_delay = 0;
              }
          }

          if ((p_param->dec_input_params.min_packets_delay && p_ctx->pkt_delay_cnt))
          {
              if(p_ctx->pkt_num >= (p_ctx->frame_num + p_ctx->pkt_delay_cnt +
                                    p_ctx->session_statistic.ui32FramesDropped))
              {
                  if(query_retry <= 2000)
                  {
                      ni_pthread_mutex_unlock(&p_ctx->mutex);
                      ni_usleep(25);
                      ni_pthread_mutex_lock(&p_ctx->mutex);
                      continue;
                  } else {
                      p_ctx->pkt_delay_cnt++;
                      ni_log2(p_ctx, NI_LOG_ERROR,
                            "Warning: decoder pkt_num %u frame_num %u "
                            "timeout, increasing pkt_delay_cnt to %u\n",
                            p_ctx->pkt_num, p_ctx->frame_num,
                            p_ctx->pkt_delay_cnt);
                  }
              }
          }
          ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning: dec read desc failed %d retries. rc=%d"
                 "\n", query_retry, retval);
      }
      retval = NI_RETCODE_SUCCESS;
      LRETURN;
    }
    else
    {
        // We have to ensure there are adequate number of DTS for picture
        // reorder delay otherwise wait for more packets to be sent to decoder.
        ni_timestamp_table_t *p_dts_queue = p_ctx->dts_queue;
        if ((int)p_dts_queue->list.count < p_ctx->pic_reorder_delay + 1 &&
            !p_ctx->ready_to_close &&
            NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL != p_ctx->status)
        {
            retval = NI_RETCODE_SUCCESS;
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "At least %d packets should be sent before reading the "
                   "first frame!\n",
                   p_ctx->pic_reorder_delay + 1);
            LRETURN;
        }
        p_ctx->max_retry_fail_count[1] = 0;

      // get actual YUV transfer size if this is the stream's very first read
      if (0 == p_ctx->active_video_width || 0 == p_ctx->active_video_height)
      {
        retval = ni_query_stream_info(p_ctx, NI_DEVICE_TYPE_DECODER, &data);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
          p_ctx->device_type, p_ctx->hw_id,
          &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec YUV query, pic size %ux%u xfer frame size "
          "%ux%u frame-rate %u is_flushed %u\n",
          data.picture_width, data.picture_height,
          data.transfer_frame_stride, data.transfer_frame_height,
          data.frame_rate, data.is_flushed);
        p_ctx->active_video_width = data.transfer_frame_stride;
        p_ctx->active_video_height = data.transfer_frame_height;
        p_ctx->actual_video_width = data.picture_width;
        p_ctx->pixel_format = data.pix_format;
        p_ctx->bit_depth_factor = ni_get_bitdepth_factor_from_pixfmt(p_ctx->pixel_format);
        //p_ctx->bit_depth_factor = data.transfer_frame_stride / data.picture_width;
        p_ctx->is_first_frame = 1;
        p_ctx->pixel_format_changed = 0;

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec YUV, adjust frame size from %ux%u to "
          "%ux%u\n", p_frame->video_width, p_frame->video_height,
          p_ctx->active_video_width, p_ctx->active_video_height);

        retval = ni_frame_buffer_alloc(
            p_frame, p_ctx->actual_video_width, p_ctx->active_video_height,
            p_ctx->codec_format == NI_CODEC_FORMAT_H264, 1,
            p_ctx->bit_depth_factor,
            3,   // Alloc space for write to data[3] and metadata
            1);

        if (NI_RETCODE_SUCCESS != retval)
        {
          LRETURN;
        }
        total_bytes_to_read = p_frame->data_len[3] + metadata_hdr_size;
        p_data_buffer = (uint8_t*)p_frame->p_buffer;
        // make sure we don't read more than available
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Info dec buf size: %u YUV frame + meta-hdr size: %u "
          "available: %u\n", p_frame->buffer_size,
          total_bytes_to_read, buf_info.buf_avail_size);
      }
      break;
    }
  }// end while1 query retry

  ni_log2(p_ctx, NI_LOG_DEBUG,  "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "p_frame->data_len[3] = %u\n", p_frame->data_len[3]);
  if (buf_info.buf_avail_size < total_bytes_to_read)
  {
      ni_pthread_mutex_unlock(&p_ctx->mutex);

      ni_log2(p_ctx, NI_LOG_ERROR,
             "ERROR %s() avaliable size(%u) less than "
             "needed (%u)\n",
             __func__, buf_info.buf_avail_size, total_bytes_to_read);
      abort();
  } else if (total_bytes_to_read == metadata_hdr_size && !p_ctx->frame_num)
  {
      if (ni_cmp_fw_api_ver(
          (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
          "6rE") >= 0)
      {
        // allocate p_data_buffer to read the first metadata
        void *p_metadata_buffer = NULL;
        int buffer_size = ((metadata_hdr_size + (NI_MEM_PAGE_ALIGNMENT - 1)) /
                            NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
        if (ni_posix_memalign(&p_metadata_buffer, sysconf(_SC_PAGESIZE), buffer_size))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate metadata buffer.\n",
                  NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        p_data_buffer = (uint8_t *)p_metadata_buffer;
        get_first_metadata = 1;
        sequence_change = 0;
      }
  }

  //Apply read configuration here
  retval = ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 1,
                                NI_CODEC_HW_ENABLE, 0);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);

  read_size_bytes = total_bytes_to_read;
  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);
  if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
  {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                 p_data_buffer, read_size_bytes, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else if (get_first_metadata) {
      // got first metadata alone
      ni_metadata_dec_frame_t *p_meta =
          (ni_metadata_dec_frame_t *)((uint8_t *)p_data_buffer);
      ni_log2(p_ctx, NI_LOG_DEBUG, "Got first pkt_delay_cnt %u\n",
              p_meta->metadata_common.pkt_delay_cnt);
      if (p_ctx->pkt_delay_cnt < p_meta->metadata_common.pkt_delay_cnt)
          p_ctx->pkt_delay_cnt = p_meta->metadata_common.pkt_delay_cnt;
      get_first_metadata = 0;
      ni_aligned_free(p_data_buffer);
      goto start;
  } else
  {
      // command issued successfully, now exit
      ni_metadata_dec_frame_t *p_meta;
      p_meta =
          (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                      p_frame->data_len[0] +
                                      p_frame->data_len[1] +
                                      p_frame->data_len[2] +
                                      p_frame->data_len[3]);

      if (buf_info.buf_avail_size != metadata_hdr_size)
      {
          low_delay_notify = 1;
          // shift metadata to end of triple output
#ifdef _WIN32
          p_data_buffer = (uint8_t *)p_frame->p_buffer +
              sizeof(niFrameSurface1_t) * NI_MAX_NUM_OF_DECODER_OUTPUTS;
          memcpy(p_meta, p_data_buffer, metadata_hdr_size);
#else
          memcpy(p_meta,
                 p_frame->p_buffer +
                     sizeof(niFrameSurface1_t) * NI_MAX_NUM_OF_DECODER_OUTPUTS,
                 metadata_hdr_size);
#endif
          sei_size = p_meta->sei_size;
          niFrameSurface1_t *p_data3 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    p_frame->data_len[0] +
                                    p_frame->data_len[1] +
                                    p_frame->data_len[2]);

          niFrameSurface1_t *p_data3_1 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    sizeof(niFrameSurface1_t));
          niFrameSurface1_t *p_data3_2 =
              (niFrameSurface1_t *)((uint8_t *)p_frame->p_buffer +
                                    2 * sizeof(niFrameSurface1_t));
          // Libxcoder knows the handle so overwrite here
          p_data3->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3_1->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3_2->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
          p_data3->ui16session_ID = p_data3_1->ui16session_ID =
              p_data3_2->ui16session_ID = (uint16_t)p_ctx->session_id;
          p_data3->src_cpu = p_data3_1->src_cpu = p_data3_2->src_cpu =
              (uint8_t)NI_DEVICE_TYPE_DECODER;

          p_data3->output_idx = 0;
          p_data3_1->output_idx = 1;
          p_data3_2->output_idx = 2;

          ni_log2(p_ctx, NI_LOG_DEBUG,
              "p_data3_1:sei_size=%d device_handle=%d == hw_id=%d ses_id=%d\n",
              sei_size, p_data3_1->device_handle, p_ctx->hw_id,
              p_data3_1->ui16session_ID);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "p_data3_1: ui16FrameIdx=%d NodeAddre=0x%x planar=%d bd=%d\n",
                 p_data3_1->ui16FrameIdx, p_data3_1->ui32nodeAddress,
                 p_data3_1->encoding_type, p_data3_1->bit_depth);
          ni_log2(p_ctx,
              NI_LOG_DEBUG,
              "p_data3_2:sei_size=%d device_handle=%d == hw_id=%d ses_id=%d\n",
              sei_size, p_data3_2->device_handle, p_ctx->hw_id,
              p_data3_2->ui16session_ID);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "p_data3_2: ui16FrameIdx=%d NodeAddre=0x%x planar=%d bd=%d\n",
                 p_data3_2->ui16FrameIdx, p_data3_2->ui32nodeAddress,
                 p_data3_2->encoding_type, p_data3_2->bit_depth);

          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s:sei_size=%d device_handle=%d == hw_id=%d "
                 "ses_id=%d\n",
                 __func__, sei_size, p_data3->device_handle, p_ctx->hw_id,
                 p_data3->ui16session_ID);
          ni_log2(p_ctx, NI_LOG_DEBUG,
                 "%s: session=0x%x ui16FrameIdx=%u NodeAddress=0x%x, "
                 "planar=%d bd=%d\n",
                 __func__, p_ctx->session_id, p_data3->ui16FrameIdx, p_data3->ui32nodeAddress,
                 p_data3->encoding_type, p_data3->bit_depth);
      } else if (ni_cmp_fw_api_ver(
                (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                "6rE") >= 0)
      {
        p_meta =
            (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer);
        ni_log2(p_ctx, NI_LOG_DEBUG, "Got pkt_delay_cnt %u\n",
                p_meta->metadata_common.pkt_delay_cnt);
        if (p_ctx->pkt_delay_cnt < p_meta->metadata_common.pkt_delay_cnt)
            p_ctx->pkt_delay_cnt = p_meta->metadata_common.pkt_delay_cnt;
      }

      total_bytes_to_read = total_bytes_to_read + sei_size;
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "%s decoder read desc success, retval %d "
             "total_bytes_to_read include sei %u sei_size %d\n",
             __func__, retval, total_bytes_to_read, sei_size);

      if (total_bytes_to_read > NI_MEM_PAGE_ALIGNMENT)
      {
          ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Oversized metadata!\n", __func__);
          retval = NI_RETCODE_ERROR_MEM_ALOC;
          LRETURN;
      }
  }

  //bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition
  //total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + metadata_hdr_size + sei_size; //since only HW desc
  bytes_read_so_far = total_bytes_to_read;
  //bytes_read_so_far = p_frame->data_len[0] + p_frame->data_len[1] + p_frame->data_len[2] + p_frame->data_len[3] + metadata_hdr_size + sei_size; //since only HW desc
  rx_size = ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, true);
  p_ctx->frame_pkt_offset = frame_offset;
  if (p_ctx->decoder_low_delay > 0 && buf_info.buf_avail_size == metadata_hdr_size &&
      p_ctx->enable_low_delay_check)
  {
    ni_log2(p_ctx, NI_LOG_TRACE, "Low delay mode amd check header if has b frame\n");

    ni_metadata_dec_frame_t *p_meta =
        (ni_metadata_dec_frame_t *)((uint8_t *)p_frame->p_buffer +
                                    p_frame->data_len[0] +
                                    p_frame->data_len[1] +
                                    p_frame->data_len[2]);
    if (p_meta->metadata_common.has_b_frame == 1)
    {
      ni_log2(p_ctx, NI_LOG_ERROR,"Warning: session 0x%x decoder lowDelay mode "
                          "is cancelled due to has_b_frames, frame_num %u\n",
                          p_ctx->session_id, p_ctx->frame_num);
      p_ctx->decoder_low_delay = 0;
    }
  }

  if (rx_size > 0)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): s-state %d first_frame %d\n", __func__,
             p_ctx->session_run_state, p_ctx->is_first_frame);

      int64_t tmp_dts, prev_dts = INT64_MIN, ts_diff = 0;
      int nb_diff = 0;
      if (p_ctx->is_first_frame)
      {
          ni_log2(p_ctx, NI_LOG_DEBUG,
                  "%s(): First frame : session_id 0x%x, pic_reorder_delay: %d "
                  "total frames input:%u buffered: %u completed: %u output: %u "
                  "dropped: %u error: %u\n",
                  __func__, p_ctx->session_id, p_ctx->pic_reorder_delay,
                  p_ctx->session_statistic.ui32FramesInput,
                  p_ctx->session_statistic.ui32FramesBuffered,
                  p_ctx->session_statistic.ui32FramesCompleted,
                  p_ctx->session_statistic.ui32FramesOutput,
                  p_ctx->session_statistic.ui32FramesDropped,
                  p_ctx->session_statistic.ui32InstErrors);

          if (p_ctx->session_statistic.ui32FramesDropped > 0)
          {
              for (i = 0; i < p_ctx->session_statistic.ui32FramesDropped; i++)
              {
                  if (ni_timestamp_get_with_threshold(
                          p_ctx->dts_queue, 0, &tmp_dts,
                          XCODER_FRAME_OFFSET_DIFF_THRES, 0,
                          p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld failed !\n",
                              __func__, i, tmp_dts);
                      break;
                  } else
                  {
                      ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld success !\n",
                              __func__, i, tmp_dts);
                      if (prev_dts != INT64_MIN) {
                          ts_diff += labs(tmp_dts - prev_dts);
                          nb_diff++;
                      }
                      prev_dts = tmp_dts;
                  }
              }
              if (nb_diff > 1)
              {
                  ts_diff = ts_diff / nb_diff;
              }
              p_ctx->decoder_last_drop_frame_num = p_ctx->session_statistic.ui32FramesDropped;
              ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop dts "
                      "average diff: %ld\n", __func__, ts_diff);
          }
      }
      else
      {
          if (p_ctx->decoder_last_drop_frame_num < p_ctx->session_statistic.ui32FramesDropped)
          {
              for(i = p_ctx->decoder_last_drop_frame_num; i < p_ctx->session_statistic.ui32FramesDropped; i++)
              {
                  if (ni_timestamp_get_with_threshold(
                          p_ctx->dts_queue, 0, &tmp_dts,
                          XCODER_FRAME_OFFSET_DIFF_THRES, 0,
                          p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
                  {
                      ni_log2(p_ctx, NI_LOG_ERROR, "%s(): FramesDropped pop "
                              "decoder dts queue %d %ld failed !\n",
                              __func__, i, tmp_dts);
                      break;
                  }
              }
              p_ctx->decoder_last_drop_frame_num = p_ctx->session_statistic.ui32FramesDropped;
              ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): FramesDropped pop dts %d\n", __func__, p_ctx->decoder_last_drop_frame_num);
          }
      }

      if (ni_timestamp_get_with_threshold(
              p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
              XCODER_FRAME_OFFSET_DIFF_THRES, 0,
              p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
      {
          if (p_ctx->last_dts != NI_NOPTS_VALUE && !p_ctx->ready_to_close)
          {
              p_ctx->pic_reorder_delay++;
              p_frame->dts = p_ctx->last_dts + p_ctx->last_dts_interval;
              ni_log2(p_ctx, NI_LOG_DEBUG,  "Padding DTS: %" PRId64 "\n", p_frame->dts);
          } else
          {
              p_frame->dts = NI_NOPTS_VALUE;
          }
      }

      if (p_ctx->is_first_frame)
      {
          for (i = 0; i < p_ctx->pic_reorder_delay; i++)
          {
              if (p_ctx->last_pts == NI_NOPTS_VALUE &&
                  p_ctx->last_dts == NI_NOPTS_VALUE)
              {
                  // If the p_frame->pts is unknown in the very beginning we assume
                  // p_frame->pts == 0 as well as DTS less than PTS by 1000 * 1/timebase
                  if (p_frame->pts >= p_frame->dts &&
                      p_frame->pts - p_frame->dts < 1000)
                  {
                      break;
                  }
              }

              if (ni_timestamp_get_with_threshold(
                      p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                      XCODER_FRAME_OFFSET_DIFF_THRES,
                      p_ctx->frame_num % 500 == 0,
                      p_ctx->buffer_pool) != NI_RETCODE_SUCCESS)
              {
                  p_frame->dts = NI_NOPTS_VALUE;
              } else
              {
                  ni_log2(p_ctx, NI_LOG_DEBUG, "%s(): pic_reorder_delay pop "
                          "decoder dts queue %d %ld success !\n",
                          __func__, i, p_frame->dts);
              }
          }
          // Reset for DTS padding counting
          p_ctx->pic_reorder_delay = 0;
      }
    if (p_ctx->codec_format == NI_CODEC_FORMAT_JPEG)//fw won't save frameoffset when decoding jpeg.
    {
        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
        p_frame->pts = p_ctx->pts_offsets[p_ctx->frame_num % NI_FIFO_SZ];
        p_frame->flags = p_ctx->flags_array[p_ctx->frame_num % NI_FIFO_SZ];
        p_frame->pkt_pos = p_ctx->pkt_pos[p_ctx->frame_num % NI_FIFO_SZ];
        ni_log2(p_ctx, NI_LOG_DEBUG,  "p_frame->pts = %u, frame_num = %d, p_frame->dts = %u\n",
               p_frame->pts, p_ctx->frame_num, p_frame->dts);
    }
    else
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: frame_offset %" PRIu64 "\n", __func__,
               frame_offset);

        // search for the pkt_offsets of received frame according to frame_offset.
        // here we get the index(i) which promises (p_ctx->pkt_offsets_index_min[i] <= frame_offset && p_ctx->pkt_offsets_index[i] > frame_offset)
        // i = -1 if not found
        i = rotated_array_binary_search(p_ctx->pkt_offsets_index_min,
                                        p_ctx->pkt_offsets_index, NI_FIFO_SZ,
                                        frame_offset);
        if (i >= 0)
        {
            p_frame->pts = p_ctx->pts_offsets[i];
            p_frame->flags = p_ctx->flags_array[i];
            p_frame->pkt_pos = p_ctx->pkt_pos[i];
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "%s: (found pts) dts %" PRId64 " pts "
                   "%" PRId64 " frame_offset %" PRIu64 " i %d "
                   "pkt_offsets_index_min %" PRIu64 " "
                   "pkt_offsets_index %" PRIu64 " pkt_pos %" PRIu64 "\n",
                   __func__, p_frame->dts, p_frame->pts, frame_offset, i,
                   p_ctx->pkt_offsets_index_min[i],
                   p_ctx->pkt_offsets_index[i],
                   p_ctx->pkt_pos[i]);

            if (p_ctx->is_first_frame)
            {
                // if the first frame is I frame and there are dropped frames,
                // find the dts closest to its pts using the average of dts diff
                if (p_frame->dts != NI_NOPTS_VALUE &&
                    p_frame->pts != NI_NOPTS_VALUE &&
                    PIC_TYPE_I == p_frame->ni_pict_type &&
                    p_ctx->session_statistic.ui32FramesDropped > 0 &&
                    ts_diff > 0)
                {
                    while (p_frame->dts < p_frame->pts &&
                           labs(p_frame->pts - p_frame->dts) > ts_diff)
                    {
                        ni_log2(p_ctx, NI_LOG_DEBUG, "%s: First I frame pts %ld "
                                "dts %ld diff. %ld > ts_diff %ld\n",
                                __func__, p_frame->pts, p_frame->dts,
                                labs(p_frame->pts - p_frame->dts), ts_diff);

                        if (ni_timestamp_get_with_threshold(
                                p_ctx->dts_queue, 0, (int64_t *)&p_frame->dts,
                                XCODER_FRAME_OFFSET_DIFF_THRES, 0, p_ctx->buffer_pool))
                        {
                            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): First I frame "
                                    "pop decoder dts queue error.\n", __func__);
                            break;
                        }
                    }
                }
            }

            p_frame->p_custom_sei_set = p_ctx->pkt_custom_sei_set[i];
            p_ctx->pkt_custom_sei_set[i] = NULL;
        } else
        {
            //backup solution pts
            if (p_param->dec_input_params.skip_pts_guess && p_ctx->last_pts != NI_NOPTS_VALUE)
            {
                // if skip guess_correct_pts, use pts interval to get the correct pts
                p_frame->pts = p_ctx->last_pts + (p_ctx->last_pts_interval > 0 ? p_ctx->last_pts_interval : 1);
            }
            else
            {
                p_frame->pts = p_ctx->last_pts + (p_frame->dts - p_ctx->last_dts);
            }
            p_frame->pkt_pos = p_ctx->last_pkt_pos + (frame_offset - p_ctx->last_frame_offset);
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: Frame pts %" PRId64 " not found for offset "
                   "%" PRIu64 "\n", p_frame->pts, frame_offset);
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "%s: (not found use default) dts %" PRId64 " pts %" PRId64
                   "\n",
                   __func__, p_frame->dts, p_frame->pts);
        }

        if (p_ctx->is_first_frame)
        {
            p_ctx->is_first_frame = 0;
        }
    }

    p_frame->orignal_pts = p_frame->pts;
    p_ctx->last_pkt_pos = p_frame->pkt_pos;
    p_ctx->last_frame_offset = frame_offset;
    if (!p_param->dec_input_params.skip_pts_guess)
        p_frame->pts = guess_correct_pts(p_ctx, p_frame->pts, p_frame->dts);
    if (p_frame->pts != NI_NOPTS_VALUE && p_ctx->last_pts != NI_NOPTS_VALUE)
        p_ctx->last_pts_interval = p_frame->pts - p_ctx->last_pts;
    p_ctx->last_pts = p_frame->pts;
    if (p_frame->dts != NI_NOPTS_VALUE && p_ctx->last_dts != NI_NOPTS_VALUE)
        p_ctx->last_dts_interval = p_frame->dts - p_ctx->last_dts;
    p_ctx->last_dts = p_frame->dts;
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: (best_effort_timestamp) pts %" PRId64 "\n",
           __func__, p_frame->pts);
    p_ctx->frame_num++;

    p_frame->error_ratio = p_ctx->session_statistic.ui32FramesErrorRatio;
    if (p_frame->error_ratio > 0)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: frame number = %d, error_ratio = %u\n",
                        __func__, p_ctx->frame_num, p_frame->error_ratio);
    }

#ifdef MEASURE_LATENCY
#ifndef XCODER_311
    ni_log2(p_ctx, NI_LOG_INFO, "DEC pkt_num %d, fme_num %d, latecy is %d\n",
                        p_ctx->pkt_num, p_ctx->frame_num, p_ctx->pkt_num - p_ctx->frame_num);
#endif
#endif

  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): received data: [0x%08x]\n", __func__, rx_size);
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): p_frame->data_len[0/1/2]=%u/%u/%u\n", __func__,
         p_frame->data_len[0], p_frame->data_len[1], p_frame->data_len[2]);

  if (p_ctx->frame_num % 500 == 0)
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "Decoder pts queue size = %d  dts queue size = %d\n\n",
             p_ctx->pts_table->list.count, p_ctx->dts_queue->list.count);
      // scan and clean up
      ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue,
                                p_ctx->buffer_pool);
  }

#ifdef MEASURE_LATENCY
  if ((p_frame->dts != NI_NOPTS_VALUE) && (p_ctx->frame_time_q != NULL))
  {
    uint64_t abs_time_ns = ni_gettime_ns();
    ni_lat_meas_q_t *q = (ni_lat_meas_q_t *)p_ctx->frame_time_q;
    ni_log2(p_ctx, NI_LOG_INFO, "DTS:%" PRId64 ",DELTA:%" PRId64 ",dLAT:%" PRIu64 ";\n",
           p_frame->dts, abs_time_ns - q->last_benchmark_time,
           ni_lat_meas_q_check_latency(q, abs_time_ns, p_frame->dts));
    q->last_benchmark_time = abs_time_ns;
  }
#endif

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (get_first_metadata && p_data_buffer)
        ni_aligned_free(p_data_buffer);
    if (sequence_change && p_ctx->frame_num)
    {
        if (p_ctx->actual_video_width ==  p_frame->video_width &&
            p_ctx->active_video_height == p_frame->video_height)
        {
            p_ctx->pixel_format_changed = 1;
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): format changed\n", __func__);
       }
    }
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): bad exit, retval = %d\n", __func__, retval);
        if (retval == NI_RETCODE_ERROR_VPU_RECOVERY)
        {
            low_delay_signal(p_ctx);
        }

        return retval;
    } else
    {
        ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit, rx_size = %d\n", __func__, rx_size);
        if (low_delay_notify)
        {
            low_delay_signal(p_ctx);
        }

        return rx_size;
    }
}

/*!******************************************************************************
*  \brief  Retrieve a YUV p_frame from decoder
*
*  \param
*
*  \return
*******************************************************************************/
int ni_hwdownload_session_read(ni_session_context_t* p_ctx, ni_frame_t* p_frame, niFrameSurface1_t* hwdesc)
{
  int retval = NI_RETCODE_SUCCESS;
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint8_t *p_data_buffer;
  int metadata_hdr_size = NI_FW_META_DATA_SZ -
      NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t ui32LBA = 0;

  //ni_log2(p_ctx, NI_LOG_DEBUG,  "hwcontext.c:ni_hwdl_frame() hwdesc %d %d %d\n",
  //    hwdesc->ui16FrameIdx,
  //    hwdesc->i8InstID,
  //    hwdesc->ui16session_ID);

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if ((!p_ctx) || (!p_frame))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
           __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  if (!p_frame->p_data[0] || !p_data_buffer)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): No receive buffer allocated.\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (0 == p_frame->data_len[0])
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): p_frame->data_len[0] = 0!.\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (hwdesc->encoding_type == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR)
  {
      p_frame->data_len[2] = 0;
  } else if (hwdesc->encoding_type == NI_PIXEL_PLANAR_FORMAT_TILED4X4)
  {
      ni_log2(p_ctx, NI_LOG_ERROR,
             "ERROR %s(): NI_PIXEL_PLANAR_FORMAT_TILED4X4 not supported in download.\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
    p_frame->data_len[2];// +metadata_hdr_size;
  unsigned int bytes_read_so_far = 0;
  uint32_t output_chunk_offset = hwdesc->ui32nodeAddress / FRAME_CHUNK_INDEX_SIZE; //for reading output1 or output2
  uint32_t output_minor_offset = hwdesc->ui32nodeAddress - output_chunk_offset * FRAME_CHUNK_INDEX_SIZE;
  ni_log2(p_ctx, NI_LOG_DEBUG,  "Total bytes to download %u, start offset = %u, chunkOffset "
                 "%u, minorOffset %u\n",
                 total_bytes_to_read, hwdesc->ui32nodeAddress,
                 output_chunk_offset, output_minor_offset);

  ni_log2(p_ctx, NI_LOG_DEBUG,  "total_bytes_to_read %u max_nvme_io_size %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read, p_ctx->max_nvme_io_size,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  //Apply read configuration here
  retval =
      ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 1,
                           (output_minor_offset << NI_CODEC_HW_PAYLOAD_OFFSET) |
                               NI_CODEC_HW_ENABLE | NI_CODEC_HW_DOWNLOAD,
                           hwdesc->ui16FrameIdx);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "Config HW download read desc success, retval %d total_bytes_to_read %u\n",
             retval, total_bytes_to_read);
  }

  read_size_bytes = total_bytes_to_read;
  ui32LBA = READ_INSTANCE_R(p_ctx->session_id, hwdesc->src_cpu);
  ui32LBA += output_chunk_offset;
  if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
  {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }

  retval = ni_nvme_send_read_cmd(
      (ni_device_handle_t)(int64_t)hwdesc->device_handle,
      NI_INVALID_DEVICE_HANDLE, p_data_buffer, read_size_bytes, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "HW download read desc success, retval %d total_bytes_to_read %u\n",
             retval, total_bytes_to_read);
  }

  //Unset applied read configuration here
  retval = ni_config_session_rw(p_ctx, SESSION_READ_CONFIG, 0, 0, 0);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config, hwdesc->src_cpu,
               p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
  CHECK_VPU_RECOVERY(retval);

  if (retval < 0)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  } else
  {
      ni_log2(p_ctx, NI_LOG_DEBUG,
             "Unconfig HW download read desc success, retval %d total_bytes_to_read %u\n",
             retval, total_bytes_to_read);
  }

  bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  if (p_ctx->is_auto_dl)
      rx_size = (int)bytes_read_so_far;
  else
  {
      rx_size =
          ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
      p_ctx->frame_pkt_offset = frame_offset;
  }

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): received data: [0x%08x]\n",
         __func__, rx_size);
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_frame->start_of_stream=%u, "
         "p_frame->end_of_stream=%u, p_frame->video_width=%u, "
         "p_frame->video_height=%u\n",
         __func__, p_frame->start_of_stream, p_frame->end_of_stream,
         p_frame->video_width, p_frame->video_height);
  ni_log2(p_ctx, NI_LOG_DEBUG,
         "%s(): p_ctx->frame_num %" PRIu64 ", "
         "p_frame->data_len[0/1/2]=%u/%u/%u\n",
         __func__, p_ctx->frame_num, p_frame->data_len[0], p_frame->data_len[1],
         p_frame->data_len[2]);

  //if (decq_count % 500 == 0)
  //{
  //    ni_log2(p_ctx, NI_LOG_DEBUG,  "Decoder pts queue size = %d  dts queue size = %d\n\n",
  //        p_ctx->pts_table->list.count, p_ctx->dts_queue)->list.count);
  //    // scan and clean up
  //    ni_timestamp_scan_cleanup(p_ctx->pts_table, p_ctx->dts_queue, p_ctx->buffer_pool);
  //}

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __func__, retval);
        return retval;
    } else
    {
        ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit, rx_size = %d\n", __func__, rx_size);
        return rx_size;
    }
}

int ni_hwdownload_by_frame_idx(niFrameSurface1_t* hwdesc, ni_frame_t* p_frame, int is_auto_dl)
{
  int retval = NI_RETCODE_SUCCESS;
  int rx_size = 0;
  uint64_t frame_offset = 0;
  uint8_t *p_data_buffer;
  int metadata_hdr_size = NI_FW_META_DATA_SZ -
      NI_MAX_NUM_OF_DECODER_OUTPUTS * sizeof(niFrameSurface1_t);
  uint32_t total_bytes_to_read = 0;
  uint32_t read_size_bytes = 0;
  uint32_t ui32LBA = 0;
  ni_global_session_stats_t *global_state;
  const char *error_flag = "NetintQuadraErr";

  ni_log(NI_LOG_TRACE,  "%s(): enter\n", __func__);

  p_data_buffer = (uint8_t *)p_frame->p_buffer;

  if (!p_frame->p_data[0] || !p_data_buffer || 0 == p_frame->data_len[0])
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): passed parameters are null or p_frame->data_len is 0.\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (hwdesc->encoding_type == NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR)
  {
      p_frame->data_len[2] = 0;
  } else if (hwdesc->encoding_type >= NI_PIXEL_PLANAR_FORMAT_TILED4X4)
  {
      ni_log(NI_LOG_ERROR,
             "ERROR %s(): Unsupported format %d for hw download.\n",
             __func__, hwdesc->encoding_type);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }
  total_bytes_to_read = p_frame->data_len[0] + p_frame->data_len[1] +
    p_frame->data_len[2];// +metadata_hdr_size;
  unsigned int bytes_read_so_far = 0;
  uint32_t output_chunk_offset = hwdesc->ui32nodeAddress / FRAME_CHUNK_INDEX_SIZE; //for reading output1 or output2
  uint32_t output_minor_offset = hwdesc->ui32nodeAddress - output_chunk_offset * FRAME_CHUNK_INDEX_SIZE;

  ni_log(NI_LOG_DEBUG,  "total_bytes_to_read %u ylen %u cr len "
                 "%u cb len %u hdr %d\n",
                 total_bytes_to_read,
                 p_frame->data_len[0], p_frame->data_len[1],
                 p_frame->data_len[2], metadata_hdr_size);

  // preset content of buffer, if not refresh means download fail
  memset(p_data_buffer, 0, NI_DATA_BUFFER_LEN);
  global_state = (ni_global_session_stats_t *)p_data_buffer;
  memcpy(global_state->error_flag, error_flag, strlen(error_flag)+1);
  global_state->check_flag[0] = global_state->check_flag[2] = 0x0;
  global_state->check_flag[1] = global_state->check_flag[3] = 0xFF;

  read_size_bytes = total_bytes_to_read;
  ui32LBA = DOWNLOAD_FRAMEIDX_R(hwdesc->ui16FrameIdx);
  ui32LBA += output_chunk_offset;
  if (read_size_bytes % NI_MEM_PAGE_ALIGNMENT)
  {
      read_size_bytes = ( (read_size_bytes / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT) + NI_MEM_PAGE_ALIGNMENT;
  }
  ni_log(NI_LOG_DEBUG,  "Total bytes to download %u, start offset = %u, chunkOffset "
              "%u, minorOffset %u hwdesc->ui16FrameIdx %d ui32LBA 0x%x\n",
              total_bytes_to_read, hwdesc->ui32nodeAddress,
              output_chunk_offset, output_minor_offset, hwdesc->ui16FrameIdx, ui32LBA);

  retval = ni_nvme_send_read_cmd(
      (ni_device_handle_t)(int64_t)hwdesc->device_handle,
      NI_INVALID_DEVICE_HANDLE, p_data_buffer, read_size_bytes, ui32LBA);

  if (retval < 0)
  {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }
  global_state = (ni_global_session_stats_t *)p_data_buffer;
  if (!strcmp(global_state->error_flag, error_flag) &&
      global_state->check_flag[0] == 0x0 && global_state->check_flag[1] == 0xFF &&
      global_state->check_flag[2] == 0x0 && global_state->check_flag[3] == 0xFF) {
      ni_log(NI_LOG_ERROR, "ERROR %s(): nvme download failed, invalid frameidx %u or "
             "size %u + offset %u out of range\n",
             __func__, hwdesc->ui16FrameIdx, read_size_bytes, output_chunk_offset);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  bytes_read_so_far = total_bytes_to_read;
  // Note: session status is NOT reset but tracked between send
  // and recv to catch and recover from a loop condition

  if (is_auto_dl)
      rx_size = (int)bytes_read_so_far;
  else
  {
      rx_size =
          ni_create_frame(p_frame, bytes_read_so_far, &frame_offset, false);
  }

  ni_log(NI_LOG_DEBUG,
          "HW download read desc success, retval %d total_bytes_to_read %u\n",
          retval, total_bytes_to_read);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log(NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __func__, retval);
        return retval;
    } else
    {
        ni_log(NI_LOG_TRACE,  "%s(): exit, rx_size = %d\n", __func__, rx_size);
        return rx_size;
    }
}

ni_retcode_t ni_hwframe_clone(ni_session_context_t *p_ctx,
                              ni_frameclone_desc_t *p_frameclone_desc)
{
  int retval = NI_RETCODE_SUCCESS;
  uint8_t *p_data = NULL;
  uint32_t dataLen;
  uint32_t ui32LBA = 0;

  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
      retval = NI_RETCODE_ERROR_INVALID_SESSION;
      LRETURN;
  }

  dataLen = (sizeof(ni_frameclone_desc_t) + NI_MEM_PAGE_ALIGNMENT - 1) &
      ~(NI_MEM_PAGE_ALIGNMENT - 1);
  if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
              NI_ERRNO, __func__);
      retval = NI_RETCODE_ERROR_MEM_ALOC;
      LRETURN;
  }
  memset(p_data, 0, dataLen);
  memcpy(p_data, p_frameclone_desc, sizeof(ni_frameclone_desc_t));
  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): ui16DstIdx %u, ui16SrcIdx %u, size %u, offset %u\n", __func__,
          p_frameclone_desc->ui16DstIdx, p_frameclone_desc->ui16SrcIdx,
          p_frameclone_desc->ui32Size, p_frameclone_desc->ui32Offset);

  ui32LBA = CONFIG_SESSION_FRAME_COPY_W(p_ctx->session_id);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, NI_INVALID_EVENT_HANDLE,
                                  p_data, dataLen, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                OPT_1);
  CHECK_VPU_RECOVERY(retval);
  if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n",
              __func__);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
      LRETURN;
  }

END:
    ni_aligned_free(p_data);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s(): bad exit, retval = %d\n", __func__, retval);
    }
    return retval;
}

/*!******************************************************************************
*  \brief  Close an xcoder upload instance
*
*  \param   p_ctx     pointer to uploader session context
*
*  \return  NI_RETCODE_SUCCESS
*******************************************************************************/
ni_retcode_t ni_uploader_session_close(ni_session_context_t* p_ctx)
{
#ifndef _WIN32
    if (p_ctx->isP2P)
    {
        if (p_ctx->netint_fd)
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: close driver fd %d\n", __func__,
                   p_ctx->netint_fd);
            close(p_ctx->netint_fd);
        }
    }
#endif
    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
*  \brief  Send a p_config command to configure uploading parameters.
*
*  \param     ni_session_context_t p_ctx - xcoder Context
*  \param[in] pool_size                    pool size to create
*  \param[in] pool                         0 = normal pool, 1 = P2P pool
*
*  \return - NI_RETCODE_SUCCESS on success,
*            NI_RETCODE_ERROR_INVALID_SESSION
*            NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_config_instance_set_uploader_params(ni_session_context_t *p_ctx,
                                                    uint32_t pool_size,
                                                    uint32_t pool)
{
    void* p_uploader_config = NULL;
    ni_uploader_config_t* p_cfg = NULL;
    uint32_t buffer_size = sizeof(ni_encoder_config_t);
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    ni_log2(p_ctx, NI_LOG_TRACE, "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
    if (ni_posix_memalign(&p_uploader_config, sysconf(_SC_PAGESIZE),
                          buffer_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_uploader_config, 0, buffer_size);//

    p_cfg = (ni_uploader_config_t*)p_uploader_config;
    p_cfg->ui16picWidth = p_ctx->active_video_width;
    p_cfg->ui16picHeight = p_ctx->active_video_height;
    p_cfg->ui8poolSize = pool_size;
    p_cfg->ui8PixelFormat = p_ctx->pixel_format;
    p_cfg->ui8Pool = pool;
    p_cfg->ui32lumaLinesize = 0; // cannot support config linesize yet because ni_frames_init is called when filter graph is created (no frame linesize info)
    p_cfg->ui32chromaLinesize = 0;
    bool isrgb = ((NI_PIX_FMT_RGBA == p_cfg->ui8PixelFormat) || (NI_PIX_FMT_BGRA == p_cfg->ui8PixelFormat) ||
                  (NI_PIX_FMT_ARGB == p_cfg->ui8PixelFormat) || (NI_PIX_FMT_ABGR == p_cfg->ui8PixelFormat) ||
                  (NI_PIX_FMT_BGR0 == p_cfg->ui8PixelFormat));
    if(p_cfg->ui16picWidth > NI_MAX_RESOLUTION_WIDTH || p_cfg->ui16picHeight > NI_MAX_RESOLUTION_HEIGHT ||
       p_cfg->ui16picWidth < NI_MIN_RESOLUTION_WIDTH_SCALER || p_cfg->ui16picHeight < NI_MIN_RESOLUTION_HEIGHT_SCALER ||
       ((p_cfg->ui16picWidth > NI_MAX_RESOLUTION_RGBA_WIDTH || p_cfg->ui16picHeight > NI_MAX_RESOLUTION_RGBA_HEIGHT) && isrgb))
   {
       ni_log2(p_ctx, NI_LOG_ERROR, "Resolution %d x %d not supported for %d format!\n", p_cfg->ui16picWidth, p_cfg->ui16picHeight, p_cfg->ui8PixelFormat);
       retval = NI_RETCODE_INVALID_PARAM;
       LRETURN;
   }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "ni_config_instance_set_uploader_params():%d x %d x Format %d with %d framepool\n",
        p_cfg->ui16picWidth, p_cfg->ui16picHeight, p_cfg->ui8PixelFormat, p_cfg->ui8poolSize);

    //configure the session here
    ui32LBA =
        CONFIG_INSTANCE_SetEncPara_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_uploader_config, buffer_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id,
                 &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      //Close the session since we can't configure it
      retval = ni_encoder_session_close(p_ctx, 0);
      if (NI_RETCODE_SUCCESS != retval)
      {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_uploader_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
      }

      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    else
    {
      p_ctx->pool_type = p_cfg->ui8Pool ? NI_POOL_TYPE_P2P : NI_POOL_TYPE_NORMAL;
    }

END:

    ni_aligned_free(p_uploader_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!******************************************************************************
*  \brief  Send a p_config command to configure decoding parameters.
*
*  \param   ni_session_context_t p_ctx - xcoder Context
*  \param   uint32_t max_pkt_size - overwrite maximum packet size if nonzero
*
*  \return - NI_RETCODE_SUCCESS on success, NI_RETCODE_ERROR_INVALID_SESSION, NI_RETCODE_ERROR_NVME_CMD_FAILED on failure
*******************************************************************************/
ni_retcode_t ni_config_instance_set_decoder_params(ni_session_context_t* p_ctx, uint32_t max_pkt_size)
{
  void* p_decoder_config = NULL;
  uint32_t buffer_size = sizeof(ni_decoder_config_t);
  ni_retcode_t retval = NI_RETCODE_SUCCESS;
  uint32_t ui32LBA = 0;
  ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

  if (!p_ctx)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
             __func__);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  if (NI_INVALID_SESSION_ID == p_ctx->session_id)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
           __func__);
    retval = NI_RETCODE_ERROR_INVALID_SESSION;
    LRETURN;
  }

  if (ni_cmp_fw_api_ver((char*)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6rT") < 0)
  {
    ni_log2(p_ctx, NI_LOG_INFO, "%s() FW rev %s < 6rT-- load balancing might be affected\n", __func__,
            (char*)&p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX]);
  }

  buffer_size = ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) * NI_MEM_PAGE_ALIGNMENT;
  if (ni_posix_memalign(&p_decoder_config, sysconf(_SC_PAGESIZE), buffer_size))
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate p_decoder_config buffer\n",
           NI_ERRNO, __func__);
    retval = NI_RETCODE_ERROR_MEM_ALOC;
    LRETURN;
  }
  memset(p_decoder_config, 0, buffer_size);

  ni_set_custom_dec_template(p_ctx, p_decoder_config, p_ctx->p_session_config, max_pkt_size);
  retval = ni_validate_custom_dec_template(p_ctx->p_session_config, p_ctx, p_decoder_config, p_ctx->param_err_msg, sizeof(p_ctx->param_err_msg));
  if (NI_RETCODE_PARAM_WARN == retval)
  {
      ni_log2(p_ctx, NI_LOG_INFO, "WARNING: %s . %s\n", __func__, p_ctx->param_err_msg);
      fflush(stdout);
  }
  else if (NI_RETCODE_SUCCESS != retval)
  {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s failed. %s\n", __func__,
             p_ctx->param_err_msg);
      fflush(stdout);
      retval = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  // configure the session here
  ui32LBA = CONFIG_INSTANCE_SetDecPara_W(p_ctx->session_id, NI_DEVICE_TYPE_DECODER);

  //Flip the bytes!!
  //p_cfg = (ni_decoder_config_t*)p_decoder_config;
  //p_cfg->i32picWidth = ni_htonl(p_cfg->i32picWidth);
  //p_cfg->i32picHeight = ni_htonl(p_cfg->i32picHeight);
  //p_cfg->i32meBlkMode = ni_htonl(p_cfg->i32meBlkMode);
  //p_cfg->i32frameRateInfo = ni_htonl(p_cfg->i32frameRateInfo);
  //p_cfg->i32vbvBufferSize = ni_htonl(p_cfg->i32vbvBufferSize);
  //p_cfg->i32userQpMax = ni_htonl(p_cfg->i32userQpMax);
  //p_cfg->i32maxIntraSize = ni_htonl(p_cfg->i32maxIntraSize);
  //p_cfg->i32userMaxDeltaQp = ni_htonl(p_cfg->i32userMaxDeltaQp);
  //p_cfg->i32userMinDeltaQp = ni_htonl(p_cfg->i32userMinDeltaQp);
  //p_cfg->i32userQpMin = ni_htonl(p_cfg->i32userQpMin);
  //p_cfg->i32bitRate = ni_htonl(p_cfg->i32bitRate);
  //p_cfg->i32bitRateBL = ni_htonl(p_cfg->i32bitRateBL);
  //p_cfg->i32srcBitDepth = ni_htonl(p_cfg->i32srcBitDepth);
  //p_cfg->hdrEnableVUI = ni_htonl(p_cfg->hdrEnableVUI);
  //p_cfg->ui32VuiDataSizeBits = ni_htonl(p_cfg->ui32VuiDataSizeBits);
  //p_cfg->ui32VuiDataSizeBytes = ni_htonl(p_cfg->ui32VuiDataSizeBytes);
  //p_cfg->i32hwframes = ni_htonl(p_cfg->i32hwframes);
  // flip the 16 bytes of the reserved field using 32 bits pointers
  //for (i = 0; i < (16 >> 2); i++)
  //{
  //  ((uint32_t*)p_cfg->ui8Reserved)[i] = ni_htonl(((uint32_t*)p_cfg->ui8Reserved)[i]);
  //}
  //// flip the NI_MAX_VUI_SIZE bytes of the VUI field using 32 bits pointers
  //for (i = 0; i < (NI_MAX_VUI_SIZE >> 2); i++) // apply on 32 bits
  //{
  //  ((uint32_t*)p_cfg->ui8VuiRbsp)[i] = ni_htonl(((uint32_t*)p_cfg->ui8VuiRbsp)[i]);
  //}

  ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: ui32LBA  = 0x%x\n", __func__, ui32LBA);
  retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_decoder_config, buffer_size, ui32LBA);
  CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
               p_ctx->device_type, p_ctx->hw_id,
               &(p_ctx->session_id), OPT_1);
  if (NI_RETCODE_SUCCESS != retval)
  {
    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    //Close the session since we can't configure it
    retval = ni_decoder_session_close(p_ctx, 0);
    if (NI_RETCODE_SUCCESS != retval)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: ni_encoder_session_close failed: blk_io_handle: %" PRIx64 ", hw_id, %u, xcoder_inst_id: %d\n", (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
    }

    retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
  }

END:

    ni_aligned_free(p_decoder_config);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

/*!******************************************************************************
 *  \brief  read a hardware descriptor from a scaler session
 *
 *  \param[in]      p_ctx           pointer to session context
 *  \param[out]     p_frame         pointer to frame to write hw descriptor
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_scaler_session_read_hwdesc(
    ni_session_context_t *p_ctx,
    ni_frame_t *p_frame)
{
    ni_retcode_t retval;
    ni_instance_buf_info_t sInstanceBuf = {0};
    niFrameSurface1_t *pFrameSurface;
    int query_retry = 0;

    if (!p_ctx || !p_frame || !p_frame->p_data[3])
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (p_ctx->buffered_frame_index)
    {
        pFrameSurface = (niFrameSurface1_t *) p_frame->p_data[3];
        pFrameSurface->ui16FrameIdx = p_ctx->buffered_frame_index;
        pFrameSurface->ui16session_ID = p_ctx->session_id;
        pFrameSurface->device_handle =
            (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
        pFrameSurface->src_cpu = (uint8_t) NI_DEVICE_TYPE_SCALER;
        pFrameSurface->output_idx = 0;

        p_ctx->buffered_frame_index = 0;
        return NI_RETCODE_SUCCESS;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    for (;;)
    {
        query_retry++;

        ni_log2(p_ctx, NI_LOG_DEBUG, "%s: query by ni_query_instance_buf_info INST_BUF_INFO_RW_UPLOAD\n",
                __func__);
        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_UPLOAD,
                                            NI_DEVICE_TYPE_SCALER, &sInstanceBuf);

        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
            p_ctx->device_type, p_ctx->hw_id,
            &(p_ctx->session_id), OPT_3);

        if (retval == NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE)
        {
            if (query_retry >= 1000)
            {
                ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning hwdesc read fail rc %d\n", retval);
                LRETURN;
            }
        }
        else if (retval != NI_RETCODE_SUCCESS)
        {
            LRETURN;
        }
        else
        {
            pFrameSurface = (niFrameSurface1_t *) p_frame->p_data[3];
            pFrameSurface->ui16FrameIdx = sInstanceBuf.hw_inst_ind.frame_index;
            pFrameSurface->ui16session_ID = p_ctx->session_id;
            pFrameSurface->device_handle =
                (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
            pFrameSurface->src_cpu = (uint8_t) NI_DEVICE_TYPE_SCALER;
            pFrameSurface->output_idx = 0;

            /* A frame index of zero is invalid, the memory acquisition failed */
            if (pFrameSurface->ui16FrameIdx == 0)
            {
                if (query_retry >= 1000)
                {
                    ni_log2(p_ctx, NI_LOG_ERROR, "Error: 2D could not acquire frame\n");
                    retval = NI_RETCODE_FAILURE;
                    LRETURN;
                }
                ni_usleep(100);
                continue;
            }

            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Session=0x%x: %s got FrameIndex=%u\n",
                   p_ctx->session_id,
                   __func__,
                   pFrameSurface->ui16FrameIdx);

            LRETURN;
        }
    }

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "%s Warning scalar read hwdesc fail rc %d or ind !\n",
               __func__, retval);
        retval = NI_RETCODE_FAILURE;
    }

    return retval;
}

/*!******************************************************************************
*  \brief  Grab bitdepth factor from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         1 or 2 for success, -1 for error
*******************************************************************************/
int ni_get_bitdepth_factor_from_pixfmt(int pix_fmt)
{
  switch (pix_fmt)
  {
  case NI_PIX_FMT_YUV420P:
  case NI_PIX_FMT_NV12:
  case NI_PIX_FMT_8_TILED4X4:
    return 1;
  case NI_PIX_FMT_YUV420P10LE:
  case NI_PIX_FMT_P010LE:
  case NI_PIX_FMT_10_TILED4X4:
    return 2;
  default:
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() non applicable format %d\n", __func__,
               pix_fmt);
        break;
    }
  }
  return -1;
}

/*!******************************************************************************
*  \brief  Grab planar info from NI_PIX_FMT
*
*  \param[in]      pix_fmt         ni_pix_fmt_t
*
*  \return         0 or 1 for success, -1 for error
*******************************************************************************/
int ni_get_planar_from_pixfmt(int pix_fmt)
{
  switch (pix_fmt)
  {
  case NI_PIX_FMT_YUV420P:
  case NI_PIX_FMT_YUV420P10LE:
  case NI_PIX_FMT_ABGR:
  case NI_PIX_FMT_ARGB:
  case NI_PIX_FMT_RGBA:
  case NI_PIX_FMT_BGRA:
    return 1;
    break;
  case NI_PIX_FMT_NV12:
  case NI_PIX_FMT_P010LE:
    return 0;
    break;
  default:
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() non applicable format %d\n", __func__,
               pix_fmt);
        break;
    }
  }
  return -1;
}

#ifndef _WIN32
/*!*****************************************************************************
 *  \brief  Get an address offset from a hw descriptor
 *
 *  \param[in]  p_ctx     ni_session_context_t to be referenced
 *  \param[in]  hwdesc    Pointer to caller allocated niFrameSurface1_t
 *  \param[out] p_offset  Value of offset
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 ******************************************************************************/
ni_retcode_t ni_get_memory_offset(ni_session_context_t *p_ctx, const niFrameSurface1_t *hwdesc,
                                  uint32_t *p_offset)
{
    if (!hwdesc)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed null parameter\n", __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (hwdesc->ui16FrameIdx <= NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config) ||
        hwdesc->ui16FrameIdx > NI_GET_MAX_HWDESC_P2P_BUF_ID(p_ctx->ddr_config))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() pass invalid data. "
        "FrameIdx %d OOR (%d,%d]. DDR config %d \n", __func__, hwdesc->ui16FrameIdx,
        NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config), NI_GET_MAX_HWDESC_P2P_BUF_ID(p_ctx->ddr_config),
        p_ctx->ddr_config);
        return NI_RETCODE_INVALID_PARAM;
    }

    *p_offset = (hwdesc->ui16FrameIdx - NI_GET_MIN_HWDESC_P2P_BUF_ID(p_ctx->ddr_config)) *
        NI_HWDESC_UNIFIED_MEMBIN_SIZE;

    return NI_RETCODE_SUCCESS;
}
#endif

/* AI functions */
ni_retcode_t ni_config_instance_network_binary(ni_session_context_t *p_ctx,
                                               void *nb_data, uint32_t nb_size)
{
    void *p_ai_config = NULL;
    void *p_nb_data = NULL;
    uint32_t buffer_size;
    //    uint8_t *p_data;
    //    uint32_t transferred, this_size;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t config_size;
    void *p_buffer = NULL;
    uint32_t dataLen;
    ni_instance_buf_info_t buf_info = {0};
    int retry_count = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (!nb_data || nb_size == 0) {
         ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() invalid nb_data %p nb_size %u\n",
                __func__, nb_data, nb_size);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    config_size = (sizeof(ni_ai_config_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_ai_config, sysconf(_SC_PAGESIZE), config_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate ai config buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    ((ni_ai_config_t *)p_ai_config)->ui32NetworkBinarySize = nb_size;
    ni_calculate_sha256(nb_data, nb_size,
                        ((ni_ai_config_t *)p_ai_config)->ui8Sha256);

    buffer_size =
        (nb_size + (NI_MEM_PAGE_ALIGNMENT - 1)) & ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_nb_data, sysconf(_SC_PAGESIZE), buffer_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate encConf buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    memcpy(p_nb_data, nb_data, nb_size);

    /* configure network binary size to be written */
    ui32LBA = CONFIG_INSTANCE_SetAiPara_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): LBA 0x%x, nb_size %u\n", __func__, ui32LBA,
           ((ni_ai_config_t *)p_ai_config)->ui32NetworkBinarySize);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_ai_config, config_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_ai_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                   ", hw_id, %u, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    dataLen = (sizeof(ni_network_layer_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    for (;;)
    {
        /* test if the model is already exist. if not, then continue to write binary data */
        memset(p_buffer, 0, dataLen);
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6J") >= 0)
        {
            ui32LBA = QUERY_INSTANCE_NL_SIZE_V2_R(p_ctx->session_id,
                                                  NI_DEVICE_TYPE_AI);
        } else
        {
            ui32LBA =
                QUERY_INSTANCE_NL_SIZE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        }
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, dataLen, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
        if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size > 0)
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): network binary registered\n", __func__);
            LRETURN;
        }

        /* if this model is not ready, try to require nbuffer to write */
        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                            NI_DEVICE_TYPE_AI, &buf_info);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): Query Write buffer Failed\n", __func__);
            LRETURN;
        }
        if (buf_info.buf_avail_size < nb_size)
        {
            /*
             * Cannot aquire the write access to nbuffer because someone else on the go,
             * Retry to see if model is ready.
             */
            ni_log2(p_ctx, NI_LOG_TRACE,
                   "AI write query failed or buf_size %u < "
                   "frame_size %u.\n", buf_info.buf_avail_size, nb_size);
        } else
        {
            /* Grant write access to nbuffer */
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Info ai write query success, available buf "
                   "size %u >= frame size %u !\n",
                   buf_info.buf_avail_size, nb_size);
            break;
        }
        retry_count++;
        /* Wait for at least 30s */
        if (retry_count < NI_MAX_AI_NETWORK_BINARY_BUFFER_QUERY_RETRIES)
        {
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(100);
            ni_pthread_mutex_lock(&p_ctx->mutex);
        } else
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "AI network binary configuration polling timeout\n");
            retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
            LRETURN;
        }
    }

#if 1
    /* write network binary data */
    ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): write nb LBA 0x%x\n", __func__, ui32LBA);
    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_nb_data, buffer_size, ui32LBA);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "write complete retval %d\n", retval);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                 p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_ai_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                   ", hw_id, %u, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

#else
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): write nb buffer_size %u\n", __func__,
           buffer_size);
    for (transferred = 0; transferred < buffer_size; transferred += this_size)
    {
        this_size = p_ctx->max_nvme_io_size < (buffer_size - transferred) ?
            p_ctx->max_nvme_io_size :
            (buffer_size - transferred);

        if (this_size & (4096 - 1))
        {
            this_size = (this_size + (4096 - 1)) & ~(4096 - 1);
        }
        /* write network binary data */
        ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI) +
            (transferred >> 12);
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "%s(): write nb LBA 0x%x, this_size %u, page_offset %u\n",
               __func__, ui32LBA, this_size, (transferred >> 12));
        p_data = (uint8_t *)p_nb_data + transferred;
        retval = ni_nvme_send_write_cmd(
            p_ctx->blk_io_handle, p_ctx->event_handle,
            (uint8_t *)p_nb_data + transferred, this_size, ui32LBA);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): write retval %d\n", __func__, retval);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log(
                NI_LOG_ERROR,
                "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %u, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
            //Close the session since we can't configure it
            retval = ni_ai_session_close(p_ctx, 0);
            if (NI_RETCODE_SUCCESS != retval)
            {
                ni_log(
                    NI_LOG_ERROR,
                    "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                    ", hw_id, %u, xcoder_inst_id: %d\n",
                    (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                    p_ctx->session_id);
            }

            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

#endif

END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_aligned_free(p_ai_config);
    ni_aligned_free(p_nb_data);
    ni_aligned_free(p_buffer);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_config_instance_hvsplus(ni_session_context_t *p_ctx)
{
    void *p_stream_info = NULL;
    void *p_ai_config = NULL;
    void *p_nb_data = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t config_size;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    config_size = (sizeof(ni_instance_mgr_stream_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);

    if (ni_posix_memalign(&p_stream_info, sysconf(_SC_PAGESIZE), config_size))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate stream info buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    ((ni_instance_mgr_stream_info_t *)p_stream_info)->picture_width = p_ctx->active_video_width;
    ((ni_instance_mgr_stream_info_t *)p_stream_info)->picture_height = p_ctx->active_video_height;
    ((ni_instance_mgr_stream_info_t *)p_stream_info)->ai_model_type = AI_MODEL_TYPE_HVSPLUS_FILTER;
    ((ni_instance_mgr_stream_info_t *)p_stream_info)->ai_enhance_level = p_ctx->hvsplus_level;
    ((ni_instance_mgr_stream_info_t *)p_stream_info)->pix_format = p_ctx->pixel_format;

    ni_log2(p_ctx, NI_LOG_DEBUG,
            "### %s: width %d %d height %d model type %d level %d pix_format %d\n", __func__,
            p_ctx->active_video_width, p_ctx->ori_width, p_ctx->active_video_height,
            ((ni_instance_mgr_stream_info_t *)p_stream_info)->ai_model_type,
            ((ni_instance_mgr_stream_info_t *)p_stream_info)->ai_enhance_level,
            ((ni_instance_mgr_stream_info_t *)p_stream_info)->pix_format);

    /* configure AI manager to load the suitable network binary */
    ui32LBA = CONFIG_INSTANCE_SetAiHVSPlus_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): LBA 0x%x config_size %d %d\n", __func__, ui32LBA, config_size, sizeof(ni_instance_mgr_stream_info_t));

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_stream_info, config_size, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
               ", hw_id, %u, xcoder_inst_id: %d\n",
               (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        //Close the session since we can't configure it
        retval = ni_ai_session_close(p_ctx, 0);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR: ni_ai_session_close failed: blk_io_handle: %" PRIx64
                   ", hw_id, %u, xcoder_inst_id: %d\n",
                   (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
                   p_ctx->session_id);
        }

        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_aligned_free(p_ai_config);
    ni_aligned_free(p_nb_data);
    ni_aligned_free(p_stream_info);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_query_network_ready(ni_session_context_t *p_ctx)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    void *p_buffer = NULL;
    uint32_t ui32LBA = 0;
    int retry_count = 0;
    uint32_t dataLen = (sizeof(ni_network_layer_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: Cannot allocate buffer.\n");
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    for(;;)
    {
        memset(p_buffer, 0, dataLen);
        ui32LBA =
            QUERY_INSTANCE_NL_SIZE_V2_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, dataLen, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
        }
        if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size > 0)
        {
            ni_log2(p_ctx, NI_LOG_INFO, "%s(): network binary registered\n", __func__);
            break;
        }

        retry_count++;
        /* Wait for at least 30s */
        if (retry_count < NI_MAX_AI_NETWORK_BINARY_BUFFER_QUERY_RETRIES)
        {
            ni_usleep(100);
        } else
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "AI network binary configuration polling timeout\n");
            retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
            LRETURN;
        }
    }
END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;

}

ni_retcode_t ni_ai_session_write(ni_session_context_t *p_ctx,
                                 ni_frame_t *p_frame)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    ni_instance_buf_info_t buf_info = {0};
    uint32_t frame_size_bytes;
    uint32_t sent_size = 0;
    int32_t query_retry = 0;
    uint32_t dataLen;
    ni_network_buffer_info_t *buffer_info = NULL;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_frame)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): passed parameters is null\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        return retval;
    }

    if (p_frame->data_len[0] == 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() invalid data length\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        return retval;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);


    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    frame_size_bytes = p_frame->data_len[0];
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): frame_size_bytes %u\n", __func__,
           frame_size_bytes);

    for (;;)
    {
        if (p_ctx->session_statistic.ui32WrBufAvailSize >= frame_size_bytes)
        {
            buf_info.buf_avail_size =
                p_ctx->session_statistic.ui32WrBufAvailSize;
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Info ai write query success, available buf "
                   "size %u >= frame size %u !\n",
                   buf_info.buf_avail_size, frame_size_bytes);
            break;
        }

        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6K") >= 0)
        {
            retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                                                     &p_ctx->session_statistic);
            CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                          nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                          p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
            buf_info.buf_avail_size =
                p_ctx->session_statistic.ui32WrBufAvailSize;
        } else
        {
            retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_WRITE,
                                                NI_DEVICE_TYPE_AI, &buf_info);
            CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                         p_ctx->device_type, p_ctx->hw_id,
                         &(p_ctx->session_id), OPT_1);
        }
        if (NI_RETCODE_SUCCESS != retval ||
            buf_info.buf_avail_size < frame_size_bytes)
        {
            ni_log2(p_ctx, NI_LOG_TRACE,
                   "AI write query failed or buf_size < frame_size. Retry %d\n",
                   query_retry);
            // extend to 5000 retries for 8K encode on FPGA
            if (query_retry >= NI_MAX_ENCODER_QUERY_RETRIES)
            {
                ni_log2(p_ctx, NI_LOG_DEBUG,
                       "AI write query exceeded max retries: %d\n",
                       NI_MAX_ENCODER_QUERY_RETRIES);
                p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
                retval = NI_RETCODE_SUCCESS;
                LRETURN;
            }
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(NI_RETRY_INTERVAL_100US);
            ni_pthread_mutex_lock(&p_ctx->mutex);
            query_retry++;
        }
    }

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6s6")
            < 0 || p_frame->iovec_num <= 1) {
        void *p_data;
        if (p_frame->iovec_num == 1) {
            if (!p_frame->iovec) {
                ni_log2(p_ctx, NI_LOG_ERROR, "Ai session write: invaid iovec\n");
                retval = NI_RETCODE_INVALID_PARAM;
                LRETURN;
            }
            if (!p_frame->iovec[0].ptr || p_frame->iovec[0].size == 0 ||
                    ((unsigned long)p_frame->iovec[0].ptr & (NI_MEM_PAGE_ALIGNMENT - 1))) {
                ni_log2(p_ctx, NI_LOG_ERROR, "Ai session write: invalid iovec ptr 0x%lx, size %u\n",
                        (unsigned long)p_frame->iovec[0].ptr, p_frame->iovec[0].size);
                retval = NI_RETCODE_INVALID_PARAM;
                LRETURN;
            }
            p_data = p_frame->iovec[0].ptr;
        } else {
            p_data = p_frame->p_buffer; //equals to p_frame->p_data[0]
        }
        ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Ai session write: p_data = %p, p_frame->buffer_size = %u, "
                "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                p_data, p_frame->buffer_size, p_ctx->frame_num,
                ui32LBA);

        sent_size = frame_size_bytes;
        if (sent_size & (NI_MEM_PAGE_ALIGNMENT - 1))
        {
            sent_size = (sent_size + NI_MEM_PAGE_ALIGNMENT - 1) &
                ~(NI_MEM_PAGE_ALIGNMENT - 1);
        }

        retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                p_data, sent_size, ui32LBA);
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                    "6K") >= 0)
        {
            if (retval != NI_RETCODE_SUCCESS)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }
            retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                    &p_ctx->session_statistic);
            CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                    nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        } else
        {
            CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
            if (retval < 0)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }
        }
    } else { //for iovec
        //validate iovec
        int i;
        uint32_t rel_offset = 0;
        int32_t iovec_index = 0;
        int32_t iovec_left;

        if (!p_frame->iovec) {
            ni_log2(p_ctx, NI_LOG_ERROR, "Ai session write: invaid iovec\n");
            retval = NI_RETCODE_INVALID_PARAM;
            LRETURN;
        }

        for (i = 0; i < p_frame->iovec_num; i++) {
            ni_iovec_t *this_iovec = &p_frame->iovec[i];

            ni_log2(p_ctx, NI_LOG_DEBUG, "iovec %d, ptr %p, size %u\n", i,
                    this_iovec->ptr, this_iovec->size);
            if ((this_iovec->ptr == NULL) || (this_iovec->size == 0) ||
                    ((unsigned long)this_iovec->ptr & (NI_MEM_PAGE_ALIGNMENT - 1))) {
                ni_log2(p_ctx, NI_LOG_ERROR, "Ai session write: invalid iovec ptr 0x%lx, size %u\n",
                        (unsigned long)this_iovec->ptr, this_iovec->size);
                retval = NI_RETCODE_INVALID_PARAM;
                LRETURN;
            }
        }

        //1, write iovec details
        dataLen = (sizeof(ni_network_buffer_info_t) + NI_MEM_PAGE_ALIGNMENT - 1) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
        if (ni_posix_memalign((void **)&buffer_info, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
                   NI_ERRNO, __func__);
            return NI_RETCODE_ERROR_MEM_ALOC;
        }
        memset(buffer_info, 0x00, dataLen);

        //2, write iovec
        iovec_left = p_frame->iovec_num;
        do {
            int iovec_batch = iovec_left < NI_MAX_SEGMENT_NUM ? iovec_left : NI_MAX_SEGMENT_NUM;
            uint32_t rel_lba = 0;

            buffer_info->ui16Option = NI_AI_FLAG_IOVEC;
            buffer_info->ui32IovecNum = iovec_batch;
            buffer_info->ui32RelOffset = rel_offset;
            for (i = 0; i < iovec_batch; i++) {
                ni_iovec_t *iovec = &p_frame->iovec[iovec_index + i];
                buffer_info->segment[i].ui32RelLba = rel_lba;
                buffer_info->segment[i].ui32Size = iovec->size;
                rel_lba += (((iovec->size + NI_MEM_PAGE_ALIGNMENT - 1) &
                            ~(NI_MEM_PAGE_ALIGNMENT - 1)) >> LBA_BIT_OFFSET) + 1;
                rel_offset += iovec->size;
            }
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Dev alloc frame: frame_index %u\n",
                    buffer_info->ui16FrameIdx[0]);

            ui32LBA =
                CONFIG_INSTANCE_SetAiFrm_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);

            retval =
                ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                        buffer_info, dataLen, ui32LBA);
            //CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
            //        p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
            //        OPT_1);
            if (NI_RETCODE_SUCCESS != retval)
            {
                ni_log2(p_ctx,
                        NI_LOG_ERROR,
                        "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
                        ", hw_id, %u, xcoder_inst_id: %d\n",
                        (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n",
                        __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }

#ifdef __linux__
            for (i = 0; i < iovec_batch; i++) {
                ni_iovec_t *iovec = &p_frame->iovec[iovec_index + i];
                ni_segment_t *segment = &buffer_info->segment[i];
                ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI) + segment->ui32RelLba;
                ni_log2(p_ctx, NI_LOG_DEBUG,  "Ai session write: data = %p, size = %u, "
                        "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                        iovec->ptr, iovec->size, p_ctx->frame_num, ui32LBA);
                ni_nvme_setup_aio_iocb(p_ctx->blk_io_handle, p_ctx->iocbs[i], iovec->ptr,
                        (iovec->size + NI_MEM_PAGE_ALIGNMENT - 1) & ~(NI_MEM_PAGE_ALIGNMENT - 1),
                        ui32LBA, 1);
            }

            retval = ni_nvme_batch_cmd_aio(p_ctx->aio_context, p_ctx->iocbs, p_ctx->io_event, iovec_batch);
            if (retval != NI_RETCODE_SUCCESS) {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to do batch io\n", __func__);
                LRETURN;
            }
#else
            for (i = 0; i < iovec_batch; i++) {
                ni_iovec_t *iovec = &p_frame->iovec[iovec_index + i];
                ui32LBA = WRITE_INSTANCE_W(p_ctx->session_id, NI_DEVICE_TYPE_AI) + rel_lba;
                ni_log2(p_ctx, NI_LOG_DEBUG,  "Ai session write: data = %p, size = %u, "
                        "p_ctx->frame_num = %" PRIu64 ", LBA = 0x%x\n",
                        iovec->ptr, iovec->size, p_ctx->frame_num,
                        ui32LBA);

                sent_size = (iovec->size + NI_MEM_PAGE_ALIGNMENT - 1) &
                    ~(NI_MEM_PAGE_ALIGNMENT - 1);
                retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                        iovec->ptr, sent_size, ui32LBA);
                if (retval != NI_RETCODE_SUCCESS) {
                    ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                    LRETURN;
                }
                rel_lba = (((iovec->size + NI_MEM_PAGE_ALIGNMENT - 1) &
                            ~(NI_MEM_PAGE_ALIGNMENT - 1)) >> LBA_BIT_OFFSET) + 1;
            }
#endif
            iovec_index += iovec_batch;
            iovec_left -= iovec_batch;
        } while (iovec_left);

        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                    "6K") >= 0)
        {
            if (retval != NI_RETCODE_SUCCESS)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }
            retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                    &p_ctx->session_statistic);
            CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                    nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        } else
        {
            CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_write, p_ctx->device_type,
                    p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
            if (retval < 0)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }
        }
    }

    p_ctx->frame_num++;

    retval = frame_size_bytes;

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    free(buffer_info);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_session_read(ni_session_context_t *p_ctx,
                                ni_packet_t *p_packet)
{
    uint32_t actual_read_size = 0;
    int retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    ni_instance_buf_info_t buf_info = {0};

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_packet || !p_packet->p_data)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        return retval;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    for (;;)
    {
        if (p_ctx->session_statistic.ui32RdBufAvailSize >= p_packet->data_len)
        {
            buf_info.buf_avail_size =
                p_ctx->session_statistic.ui32RdBufAvailSize;
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Info ai read query success, available buf "
                   "size %u >= frame size %u !\n",
                   buf_info.buf_avail_size, p_packet->data_len);
            break;
        }
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6K") >= 0)
        {
            retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                                                     &p_ctx->session_statistic);
            CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                          nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                          p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
            buf_info.buf_avail_size =
                p_ctx->session_statistic.ui32RdBufAvailSize;
        } else
        {
            retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_READ,
                                                NI_DEVICE_TYPE_AI, &buf_info);
            CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                         p_ctx->device_type, p_ctx->hw_id,
                         &(p_ctx->session_id), OPT_1);
        }
        ni_log2(p_ctx, NI_LOG_TRACE,
               "Info ai read query rc %d, available buf size %u, "
               "frame_num=%" PRIu64 ", pkt_num=%" PRIu64 "\n",
               retval, buf_info.buf_avail_size, p_ctx->frame_num,
               p_ctx->pkt_num);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Buffer info query failed in ai read!!!!\n");
            LRETURN;
        } else if (0 == buf_info.buf_avail_size)
        {
            ni_log2(p_ctx, NI_LOG_DEBUG,  "Info ai read available buf size %u, eos %u !\n",
                           buf_info.buf_avail_size, p_packet->end_of_stream);
            retval = NI_RETCODE_SUCCESS;
            LRETURN;
        }
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "Ai read buf_avail_size %u\n", buf_info.buf_avail_size);

    assert(buf_info.buf_avail_size >= p_packet->data_len);

    ui32LBA = READ_INSTANCE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
    actual_read_size = p_packet->data_len;
    if (actual_read_size & (NI_MEM_PAGE_ALIGNMENT - 1))
    {
        actual_read_size = (actual_read_size + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
    }

    retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_packet->p_data, actual_read_size, ui32LBA);
    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6K") >= 0)
    {
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
        retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                                                 &p_ctx->session_statistic);
        CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                      nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                      p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
    } else
    {
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (retval < 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

    retval = p_packet->data_len;

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

static void ni_unreference_network_data(ni_network_data_t *network_data)
{
    if (network_data)
    {
        ni_memfree(network_data->inset);
        ni_memfree(network_data->linfo.in_param);
    }
}

ni_retcode_t ni_config_read_inout_layers(ni_session_context_t *p_ctx,
                                         ni_network_data_t *p_network)
{
    void *p_buffer = NULL;
    void *p_info = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    uint32_t ui32LBA = 0;
    uint32_t dataLen;
    int32_t query_retry = 0;
    int l;
    ni_network_layer_params_t *layer_param;
    uint32_t buffer_size;
    uint32_t this_size;
    ni_network_data_t *network_data = NULL;
    uint32_t total_io_num;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx || !p_network)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    network_data = p_ctx->network_data;
    if (network_data->linfo.in_param != NULL)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: %s(): network parameters data is already initialized\n",
               __func__);
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    /* query available size can be read. */
    dataLen = (sizeof(ni_instance_buf_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, dataLen);

    for (;;)
    {
        if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                              "6J") >= 0)
        {
            ui32LBA = QUERY_INSTANCE_NL_SIZE_V2_R(p_ctx->session_id,
                                                  NI_DEVICE_TYPE_AI);
        } else
        {
            ui32LBA =
                QUERY_INSTANCE_NL_SIZE_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        }
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, dataLen, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size > 0)
        {
            break;
        }

        query_retry++;
        if (query_retry > 50000)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): exceeded max query retries: %d\n",
                   __func__, query_retry - 1);
            retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
            LRETURN;
        }
        ni_pthread_mutex_unlock(&p_ctx->mutex);
        ni_usleep(200);
        ni_pthread_mutex_lock(&p_ctx->mutex);
        continue;
    }

    if (((ni_instance_buf_info_t *)p_buffer)->buf_avail_size == 0)
    {
        retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
        LRETURN;
    }

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6J") >= 0)
    {
        network_data->input_num =
            ((ni_instance_buf_info_t *)p_buffer)->buf_avail_size >> 16;
        network_data->output_num =
            ((ni_instance_buf_info_t *)p_buffer)->buf_avail_size & 0xFFFF;
    } else
    {
        network_data->input_num = 4;
        network_data->output_num = 4;
    }

    total_io_num = network_data->input_num + network_data->output_num;

    network_data->linfo.in_param = (ni_network_layer_params_t *)calloc(
        total_io_num, sizeof(ni_network_layer_params_t));
    if (!network_data->linfo.in_param)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s(): Unable to allocate network layer params\n",
               __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    network_data->linfo.out_param =
        network_data->linfo.in_param + network_data->input_num;

    network_data->inset = (ni_network_layer_offset_t *)calloc(
        total_io_num, sizeof(ni_network_layer_offset_t));
    if (!network_data->inset)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s(): Unable to allocate network offset\n",
               __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    network_data->outset = network_data->inset + network_data->input_num;

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6J") >= 0)
    {
        /* query the real network layer data */
        this_size = sizeof(ni_network_layer_params_t) * total_io_num;
        dataLen = (this_size + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
        if (ni_posix_memalign(&p_info, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate info buffer\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_info, 0, dataLen);

        ui32LBA = QUERY_INSTANCE_NL_V2_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_info, dataLen, ui32LBA);
        if ((int32_t)retval < 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        memcpy(network_data->linfo.in_param, p_info, this_size);
    } else
    {
        /* query the real network layer data */
        this_size = sizeof(ni_network_layer_params_t) * total_io_num;
        dataLen = (this_size + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
        if (ni_posix_memalign(&p_info, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate info buffer\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_info, 0, dataLen);

        ui32LBA = QUERY_INSTANCE_NL_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        retval = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_info, dataLen, ui32LBA);
        if ((int32_t)retval < 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
              p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        memcpy(network_data->linfo.in_param, p_info, this_size);

        for (l = 0, network_data->input_num = 0; l < 4; l++)
        {
            layer_param = &network_data->linfo.in_param[l];
            if (layer_param->num_of_dims == 0)
            {
                break;
            }
            network_data->input_num++;
        }

        for (l = 0, network_data->output_num = 0; l < 4; l++)
        {
            layer_param = &network_data->linfo.out_param[l];
            if (layer_param->num_of_dims == 0)
            {
                break;
            }
            network_data->output_num++;
        }
    }

    for (l = 0, buffer_size = 0; l < network_data->input_num; l++)
    {
        layer_param = &network_data->linfo.in_param[l];
        this_size = ni_ai_network_layer_size(layer_param);
        this_size =
            (this_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        network_data->inset[l].offset = buffer_size;
        buffer_size += this_size;

        ni_log2(p_ctx,
            NI_LOG_DEBUG,
            "%s(): network input layer %d: dims %u, %u/%u/%u/%u, f %d, q %d\n",
            __func__, l, layer_param->num_of_dims, layer_param->sizes[0],
            layer_param->sizes[1], layer_param->sizes[2], layer_param->sizes[3],
            layer_param->data_format, layer_param->quant_format);
    }

    for (l = 0, buffer_size = 0; l < network_data->output_num; l++)
    {
        layer_param = &network_data->linfo.out_param[l];
        this_size = ni_ai_network_layer_size(layer_param);
        this_size =
            (this_size + NI_AI_HW_ALIGN_SIZE - 1) & ~(NI_AI_HW_ALIGN_SIZE - 1);
        network_data->outset[l].offset = buffer_size;
        buffer_size += this_size;

        ni_log2(p_ctx,
            NI_LOG_DEBUG,
            "%s(): network output layer %d: dims %u, %u/%u/%u/%u, f %d, q %d\n",
            __func__, l, layer_param->num_of_dims, layer_param->sizes[0],
            layer_param->sizes[1], layer_param->sizes[2], layer_param->sizes[3],
            layer_param->data_format, layer_param->quant_format);
    }

    memcpy(p_network, network_data, sizeof(ni_network_data_t));

    retval = NI_RETCODE_SUCCESS;

END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);

    if (retval != NI_RETCODE_SUCCESS)
    {
        ni_unreference_network_data(network_data);
    }
    ni_aligned_free(p_buffer);
    ni_aligned_free(p_info);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

ni_retcode_t ni_ai_session_open(ni_session_context_t *p_ctx)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    void *p_buffer = NULL;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    //Check if there is an instance or we need a new one
    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        p_ctx->device_type = NI_DEVICE_TYPE_AI;
        p_ctx->pts_table = NULL;
        p_ctx->dts_queue = NULL;
        p_ctx->buffer_pool = NULL;
        p_ctx->status = 0;
        p_ctx->key_frame_type = 0;
        p_ctx->keyframe_factor = 1;
        p_ctx->frame_num = 0;
        p_ctx->pkt_num = 0;
        p_ctx->rc_error_count = 0;
        p_ctx->force_frame_type = 0;
        p_ctx->ready_to_close = 0;
        //Sequence change tracking reated stuff
        p_ctx->active_video_width = 0;
        p_ctx->active_video_height = 0;
        p_ctx->p_all_zero_buf = NULL;
        p_ctx->actual_video_width = 0;
        p_ctx->session_run_state = SESSION_RUN_STATE_NORMAL;
        memset(&(p_ctx->param_err_msg[0]), 0, sizeof(p_ctx->param_err_msg));

        //malloc zero data buffer
        if (ni_posix_memalign(&p_ctx->p_all_zero_buf, sysconf(_SC_PAGESIZE),
                              NI_DATA_BUFFER_LEN))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc all zero buffer failed\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_ctx->p_all_zero_buf, 0, NI_DATA_BUFFER_LEN);

        //malloc data buffer
        if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE),
                              NI_DATA_BUFFER_LEN))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

        //Set session ID to be invalid. In case we cannot open session, the session id wold remain invalid.
        //In case we can open sesison, the session id would become valid.
        ((ni_session_stats_t *)p_buffer)->ui16SessionId =
            (uint16_t)NI_INVALID_SESSION_ID;

        // First uint32_t is either an invaild session ID or a valid session ID, depending on if session could be opened
        ui32LBA = OPEN_SESSION_CODEC(NI_DEVICE_TYPE_AI, 0, p_ctx->hw_action);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): LBA 0x%x, hw_action %d\n", __func__,
               ui32LBA, p_ctx->hw_action);
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR ni_nvme_send_read_cmd\n");
            LRETURN;
        }
        //Open will return a session status structure with a valid session id if it worked.
        //Otherwise the invalid session id set before the open command will stay
        if ((uint16_t)NI_INVALID_SESSION_ID ==
            ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId))
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): p_ctx->device_handle=%" PRIx64
                   ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
                   __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
                   p_ctx->session_id);
            ni_ai_session_close(p_ctx, 0);
            retval = NI_RETCODE_ERROR_INVALID_SESSION;
            LRETURN;
        }
        p_ctx->session_id =
            ni_ntohs(((ni_session_stats_t *)p_buffer)->ui16SessionId);
    p_ctx->session_timestamp = ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_high);
        p_ctx->session_timestamp = (p_ctx->session_timestamp << 32) |
            ni_htonl(((ni_session_stats_t *)p_buffer)->ui32Session_timestamp_low);
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Ai open session ID:0x%x,timestamp:%" PRIu64 "\n",
               p_ctx->session_id, p_ctx->session_timestamp);

        ni_log2(p_ctx, NI_LOG_DEBUG,  "Open session completed\n");
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "%s(): p_ctx->device_handle=%" PRIx64
               ", p_ctx->hw_id=%d, p_ctx->session_id=%d\n",
               __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
               p_ctx->session_id);

        //Send keep alive timeout Info
        uint64_t keep_alive_timeout =
            p_ctx->keep_alive_timeout * 1000000;   //send us to FW
        memset(p_buffer, 0, NI_DATA_BUFFER_LEN);
        memcpy(p_buffer, &keep_alive_timeout, sizeof(keep_alive_timeout));
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s keep_alive_timeout %" PRIx64 "\n", __func__,
               keep_alive_timeout);
        ui32LBA = CONFIG_SESSION_KeepAliveTimeout_W(p_ctx->session_id);
        retval =
            ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id), OPT_1);
        CHECK_VPU_RECOVERY(retval);

        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): nvme write keep_alive_timeout command "
                   "failed, blk_io_handle: %" PRIx64 ", hw_id, %d\n",
                   __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        p_ctx->network_data =
            (ni_network_data_t *)calloc(1, sizeof(ni_network_data_t));
        if (!p_ctx->network_data)
        {
            ni_log2(p_ctx, NI_LOG_ERROR,
                   "ERROR %s(): Unable to allocate network_data memory\n");
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
    }

    //aio
#ifdef __linux__
    if (ni_aio_setup(NI_MAX_SEGMENT_NUM, &p_ctx->aio_context) != 0) {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to setup aio context\n", __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    p_ctx->iocbs = (ni_iocb_t **)malloc(sizeof(ni_iocb_t *) * NI_MAX_SEGMENT_NUM);
    if (!p_ctx->iocbs) {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to allocate iocbs\n", __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    } else {
        int i;
        for (i = 0; i < NI_MAX_SEGMENT_NUM; i++) {
            p_ctx->iocbs[i] = (ni_iocb_t *)malloc(sizeof(ni_iocb_t));
            if (!p_ctx->iocbs[i]) {
                ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to allocate iocb %d\n", __func__, i);
                retval = NI_RETCODE_ERROR_MEM_ALOC;
                LRETURN;
            }
        }
    }

    p_ctx->io_event = (ni_io_event_t *)malloc(sizeof(ni_io_event_t) * NI_MAX_SEGMENT_NUM);
    if (!p_ctx->io_event) {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to allocate ioevent %d\n", __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
#endif

    // init for frame pts calculation
    p_ctx->is_first_frame = 1;
    p_ctx->last_pts = 0;
    p_ctx->last_dts = 0;
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;

END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);
    return retval;
}

ni_retcode_t ni_ai_session_close(ni_session_context_t *p_ctx, int eos_recieved)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    void *p_buffer = NULL;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): Invalid session ID, return.\n", __func__);
        retval = NI_RETCODE_SUCCESS;
        LRETURN;
    }

    //malloc data buffer
    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() alloc data buffer failed\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    ui32LBA = CLOSE_SESSION_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);

    int retry = 0;
    while (retry < NI_SESSION_CLOSE_RETRY_MAX)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,
               "%s(): p_ctx->blk_io_handle=%" PRIx64 ", p_ctx->hw_id=%d, "
               "p_ctx->session_id=%d, close_mode=1\n",
               __func__, (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id,
               p_ctx->session_id);

        if (ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, NI_DATA_BUFFER_LEN, ui32LBA) < 0)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): command failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            p_ctx->session_id = NI_INVALID_SESSION_ID;
            break;
        } else
        {
            //Close should always succeed
            retval = NI_RETCODE_SUCCESS;
            p_ctx->session_id = NI_INVALID_SESSION_ID;
            break;
        }
        /*
    else if(((ni_session_closed_status_t *)p_buffer)->session_closed)
    {
      retval = NI_RETCODE_SUCCESS;
      p_ctx->session_id = NI_INVALID_SESSION_ID;
      break;
    }
    else
    {
      ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): wait for close\n", __func__);
      ni_usleep(NI_SESSION_CLOSE_RETRY_INTERVAL_US);
      retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
    }
    */
        retry++;
    }

END:

#ifdef __linux__
    if (p_ctx->aio_context != 0) {
        if (ni_aio_destroy(p_ctx->aio_context) != 0) {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): failed to destroy aio context\n", __func__);
        } else {
            p_ctx->aio_context = 0;
        }
    }
    if (p_ctx->iocbs) {
        for (int i = 0; i < NI_MAX_SEGMENT_NUM; i++) {
            ni_memfree(p_ctx->iocbs[i]);
        }
        ni_memfree(p_ctx->iocbs);
    }
    ni_memfree(p_ctx->io_event);
#endif

    ni_unreference_network_data(p_ctx->network_data);
    ni_memfree(p_ctx->network_data);

    ni_aligned_free(p_buffer);
    ni_aligned_free(p_ctx->p_all_zero_buf);

    //Sequence change related stuff cleanup here
    p_ctx->active_video_width = 0;
    p_ctx->active_video_height = 0;
    p_ctx->actual_video_width = 0;

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s(): CTX[Card:%" PRIx64 " / HW:%d / INST:%d]\n",
           __func__, (int64_t)p_ctx->device_handle, p_ctx->hw_id,
           p_ctx->session_id);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

ni_retcode_t ni_ai_multi_config_frame(ni_session_context_t *p_ctx,
                                      ni_frame_config_t p_cfg_in[],
                                      int numInCfgs,
                                      ni_frame_config_t *p_cfg_out)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_network_buffer_info_t *p_data = NULL;
    void *p_read_data = NULL;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }
    ni_pthread_mutex_lock(&p_ctx->mutex);
    ni_instance_buf_info_t buf_info = {0};
    int32_t query_retry = 0;
    for (;;)
    {
        if (p_ctx->session_statistic.ui32WrBufAvailSize > 0)
        {
            buf_info.buf_avail_size =
                    p_ctx->session_statistic.ui32WrBufAvailSize;
            ni_log2(p_ctx, NI_LOG_DEBUG,
                   "Info ai write query success, available buf "
                   "size %u !\n",
                   buf_info.buf_avail_size);
            break;
        }

        retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                                                 &p_ctx->session_statistic);
        CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                     nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                     p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
        buf_info.buf_avail_size =
                p_ctx->session_statistic.ui32WrBufAvailSize;
        if (NI_RETCODE_SUCCESS != retval ||
            buf_info.buf_avail_size == 0)
        {
            ni_log2(p_ctx, NI_LOG_TRACE,
                   "AI write query failed or buf_size < frame_size. Retry %d\n",
                   query_retry);
            // extend to 5000 retries for 8K encode on FPGA
            if (query_retry >= NI_MAX_ENCODER_QUERY_RETRIES)
            {
                ni_log2(p_ctx, NI_LOG_TRACE,
                       "AI write query exceeded max retries: %d\n",
                       NI_MAX_ENCODER_QUERY_RETRIES);
                p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
                retval = NI_RETCODE_SUCCESS;
                LRETURN;
            }
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(NI_RETRY_INTERVAL_100US);
            ni_pthread_mutex_lock(&p_ctx->mutex);
            query_retry++;
        }
    }

    dataLen = (sizeof(ni_network_buffer_info_t) + NI_MEM_PAGE_ALIGNMENT - 1) &
              ~(NI_MEM_PAGE_ALIGNMENT - 1);

    if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
               NI_ERRNO, __func__);
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    memset(p_data, 0x00, dataLen);
    p_data->ui16Width = p_cfg_in[0].picture_width;
    p_data->ui16Height = p_cfg_in[0].picture_height;
    p_data->ui16Option = p_cfg_in[0].options;
    p_data->ui8PoolSize = p_cfg_in[0].rgba_color;
    p_data->ui8MultiIn = 1;
    for(int i = 0; i < numInCfgs; i++){
        p_data->ui16FrameIdx[i] = p_cfg_in[i].frame_index;
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Dev alloc frame[%d]: frame_index %u, hw=%d\n",
               i,p_data->ui16FrameIdx[i], p_data->ui8MultiIn);
    }


    ui32LBA =
            CONFIG_INSTANCE_SetAiFrm_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);

    retval =
            ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data, dataLen, ui32LBA);
    CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                 p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                 OPT_1);
    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_log2(p_ctx,
                NI_LOG_ERROR,
                "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %u, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n",
               __func__);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);
    ni_aligned_free(p_data);
    if (p_read_data != NULL)
    {
        ni_aligned_free(p_read_data);
    }
    return retval;
}

ni_retcode_t ni_ai_alloc_dst_frame(ni_session_context_t *p_ctx,
                                   niFrameSurface1_t *p_out_surface)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_network_buffer_info_t *p_data = NULL;
    void *p_read_data = NULL;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    if (ni_cmp_fw_api_ver(
          (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
          "6rL") < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
                "Error: %s function not supported on device with FW API version < 6rL\n",
                __func__);
        return NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    int query_retry = 0;
    dataLen =
        (sizeof(ni_instance_buf_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
        ~(NI_MEM_PAGE_ALIGNMENT - 1);
    if (ni_posix_memalign(&p_read_data, sysconf(_SC_PAGESIZE), dataLen))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
                NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }
    memset(p_read_data, 0, dataLen);

    for (;;)
    {
        ui32LBA = QUERY_INSTANCE_HW_OUT_SIZE_R(p_ctx->session_id,
                                                NI_DEVICE_TYPE_AI);
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_read_data, dataLen, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read,
                      p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                      OPT_1);
        if (retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }

        if (((ni_instance_buf_info_t *)p_read_data)->hw_inst_ind.buffer_avail > 0)
        {
            p_out_surface->ui16FrameIdx = ((ni_instance_buf_info_t *)p_read_data)->hw_inst_ind.frame_index;
            p_out_surface->ui16session_ID = p_ctx->session_id;
            p_out_surface->device_handle =
              (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
            p_out_surface->bit_depth = p_ctx->bit_depth_factor;
            p_out_surface->src_cpu = (uint8_t)NI_DEVICE_TYPE_AI;
            p_out_surface->output_idx = 0;
            break;
        }

        query_retry++;
        if (query_retry > 2000)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): exceeded max query retries: %d\n",
                    __func__, query_retry - 1);
            retval = NI_RETCODE_EAGAIN;
            LRETURN;
        }
        ni_pthread_mutex_unlock(&p_ctx->mutex);
        ni_usleep(200);
        ni_pthread_mutex_lock(&p_ctx->mutex);
        continue;
    }

    if (((ni_instance_buf_info_t *)p_read_data)->hw_inst_ind.buffer_avail == 0)
    {
        retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
        LRETURN;
    }
END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);
    ni_aligned_free(p_data);
    if (p_read_data != NULL)
    {
        ni_aligned_free(p_read_data);
    }
    return retval;
}

ni_retcode_t ni_ai_alloc_hwframe(ni_session_context_t *p_ctx, int width,
                                 int height, int options, int pool_size,
                                 int frame_index)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_network_buffer_info_t *p_data = NULL;
    void *p_read_data = NULL;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    if (!p_ctx)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s() passed parameters are null!, return\n",
               __func__);
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_ctx->session_id == NI_INVALID_SESSION_ID)
    {
      ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
             __func__);
        return NI_RETCODE_ERROR_INVALID_SESSION;
    }

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (options == NI_AI_FLAG_IO)
    {
        int query_retry = 0;
        dataLen =
            (sizeof(ni_instance_buf_info_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
        if (ni_posix_memalign(&p_read_data, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_read_data, 0, dataLen);

        for (;;)
        {
            ui32LBA = QUERY_INSTANCE_HW_OUT_SIZE_R(p_ctx->session_id,
                                                   NI_DEVICE_TYPE_AI);
            retval =
                ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                      p_read_data, dataLen, ui32LBA);
            CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read,
                         p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                         OPT_1);
            if (retval != NI_RETCODE_SUCCESS)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
                retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
                LRETURN;
            }

            if (((ni_instance_buf_info_t *)p_read_data)->buf_avail_size > 0)
            {
                break;
            }

            query_retry++;
            if (query_retry > 2000)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "%s(): exceeded max query retries: %d\n",
                       __func__, query_retry - 1);
                retval = NI_RETCODE_EAGAIN;
                LRETURN;
            }
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(200);
            ni_pthread_mutex_lock(&p_ctx->mutex);
            continue;
        }

        if (((ni_instance_buf_info_t *)p_read_data)->buf_avail_size == 0)
        {
            retval = NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE;
            LRETURN;
        }
    } else
    {
        ni_instance_buf_info_t buf_info = {0};
        int32_t query_retry = 0;
        for (;;)
        {
            if (p_ctx->session_statistic.ui32WrBufAvailSize > 0)
            {
                buf_info.buf_avail_size =
                        p_ctx->session_statistic.ui32WrBufAvailSize;
                ni_log2(p_ctx, NI_LOG_DEBUG,
                       "Info ai write query success, available buf "
                       "size %u !\n",
                       buf_info.buf_avail_size);
                break;
            }

            retval = ni_query_session_statistic_info(p_ctx, NI_DEVICE_TYPE_AI,
                                                         &p_ctx->session_statistic);
            CHECK_ERR_RC(p_ctx, retval, &p_ctx->session_statistic,
                             nvme_admin_cmd_xcoder_query, p_ctx->device_type,
                             p_ctx->hw_id, &(p_ctx->session_id), OPT_2);
            buf_info.buf_avail_size =
                        p_ctx->session_statistic.ui32WrBufAvailSize;
            if (NI_RETCODE_SUCCESS != retval ||
                buf_info.buf_avail_size == 0)
            {
                ni_log2(p_ctx, NI_LOG_TRACE,
                       "AI write query failed or buf_size < frame_size. Retry %d\n",
                       query_retry);
                // extend to 5000 retries for 8K encode on FPGA
                if (query_retry >= NI_MAX_ENCODER_QUERY_RETRIES)
                {
                    ni_log2(p_ctx, NI_LOG_TRACE,
                           "AI write query exceeded max retries: %d\n",
                           NI_MAX_ENCODER_QUERY_RETRIES);
                    p_ctx->status = NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL;
                    retval = NI_RETCODE_SUCCESS;
                    LRETURN;
                }
                ni_pthread_mutex_unlock(&p_ctx->mutex);
                ni_usleep(NI_RETRY_INTERVAL_100US);
                ni_pthread_mutex_lock(&p_ctx->mutex);
                query_retry++;
            }
        }

        dataLen = (sizeof(ni_network_buffer_info_t) + NI_MEM_PAGE_ALIGNMENT - 1) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);

        if (ni_posix_memalign((void **)&p_data, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
                   NI_ERRNO, __func__);
            return NI_RETCODE_ERROR_MEM_ALOC;
        }

        memset(p_data, 0x00, dataLen);
        p_data->ui16FrameIdx[0] = frame_index;
        p_data->ui16Width = width;
        p_data->ui16Height = height;
        p_data->ui16Option = options;
        p_data->ui8PoolSize = pool_size;
        p_data->ui8MultiIn = 0;
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Dev alloc frame: frame_index %u\n",
               p_data->ui16FrameIdx[0]);

        ui32LBA =
            CONFIG_INSTANCE_SetAiFrm_W(p_ctx->session_id, NI_DEVICE_TYPE_AI);

        retval =
            ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                   p_data, dataLen, ui32LBA);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_config,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                     OPT_1);
        if (NI_RETCODE_SUCCESS != retval)
        {
            ni_log2(p_ctx,
                NI_LOG_ERROR,
                "ERROR: ni_nvme_send_admin_cmd failed: blk_io_handle: %" PRIx64
                ", hw_id, %u, xcoder_inst_id: %d\n",
                (int64_t)p_ctx->blk_io_handle, p_ctx->hw_id, p_ctx->session_id);
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): nvme command failed!\n",
                   __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
    }

END:
    ni_pthread_mutex_unlock(&p_ctx->mutex);
    ni_aligned_free(p_data);
    if (p_read_data != NULL)
    {
        ni_aligned_free(p_read_data);
    }
    return retval;
}

/*!******************************************************************************
 *  \brief  read a hardware descriptor from a scaler session
 *
 *  \param[in]      p_ctx           pointer to session context
 *  \param[out]     p_frame         pointer to frame to write hw descriptor
 *
 *  \return         NI_RETCODE_INVALID_PARAM
 *                  NI_RETCODE_ERROR_INVALID_SESSION
 *                  NI_RETCODE_ERROR_MEM_ALOC
 *                  NI_RETCODE_ERROR_NVME_CMD_FAILED
 *                  NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_ai_session_read_hwdesc(ni_session_context_t *p_ctx,
                                       ni_frame_t *p_frame)
{
    int retval = NI_RETCODE_SUCCESS;
    int retry_count = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    ni_pthread_mutex_lock(&p_ctx->mutex);

    if (NI_INVALID_SESSION_ID == p_ctx->session_id)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %s(): Invalid session ID, return.\n",
               __func__);
        retval = NI_RETCODE_ERROR_INVALID_SESSION;
        LRETURN;
    }

    for (;;)
    {
        retry_count++;
        ni_instance_buf_info_t sInstanceBuf = {0};
        niFrameSurface1_t *pFrameSurface;
        retval = ni_query_instance_buf_info(p_ctx, INST_BUF_INFO_RW_READ_BY_AI,
                                            NI_DEVICE_TYPE_AI, &sInstanceBuf);
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_admin_cmd_xcoder_query,
                     p_ctx->device_type, p_ctx->hw_id, &(p_ctx->session_id),
                     OPT_3);

        if (retval == NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE)
        {
            if (retry_count >= 500)
            {
                ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning hwdesc read fail rc %d\n",
                       retval);
                LRETURN;
            }
            ni_pthread_mutex_unlock(&p_ctx->mutex);
            ni_usleep(100);
            ni_pthread_mutex_lock(&p_ctx->mutex);
        }
        else if (retval != NI_RETCODE_SUCCESS)
        {
            LRETURN;
        } else
        {
            pFrameSurface = (niFrameSurface1_t *)p_frame->p_data[3];
            pFrameSurface->ui16FrameIdx = sInstanceBuf.hw_inst_ind.frame_index;
            pFrameSurface->ui16session_ID = p_ctx->session_id;
            pFrameSurface->device_handle =
                (int32_t)((int64_t)p_ctx->blk_io_handle & 0xFFFFFFFF);
            pFrameSurface->src_cpu = (uint8_t)NI_DEVICE_TYPE_AI;
            pFrameSurface->output_idx = 0;

            /* A frame index of zero is invalid, the memory acquisition failed */
            if (pFrameSurface->ui16FrameIdx == 0)
            {
                if (retry_count >= 500)
                {
                    ni_log2(p_ctx, NI_LOG_DEBUG,  "Warning hwdesc read fail rc %d\n",
                           retval);
                    retval = NI_RETCODE_EAGAIN;
                    LRETURN;
                }
                ni_pthread_mutex_unlock(&p_ctx->mutex);
                ni_usleep(100);
                ni_pthread_mutex_lock(&p_ctx->mutex);
                continue;
            }
            LRETURN;
        }
    }

END:

    ni_pthread_mutex_unlock(&p_ctx->mutex);

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Get DDR configuration of Quadra device
 *
 *  \param[in/out] p_ctx  pointer to a session context with valid file handle
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *                        NI_RETCODE_ERROR_MEM_ALOC
 *                        NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_get_ddr_configuration(ni_session_context_t *p_ctx)
{
    void *p_buffer = NULL;
    ni_nvme_identity_t *p_id_data;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_event_handle_t event_handle = NI_INVALID_EVENT_HANDLE;
    uint32_t ui32LBA = IDENTIFY_DEVICE_R;
    ni_device_handle_t device_handle = p_ctx->blk_io_handle;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    if (NI_INVALID_DEVICE_HANDLE == device_handle)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid passed parameters\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE),
                          NI_NVME_IDENTITY_CMD_DATA_SZ))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    memset(p_buffer, 0, NI_NVME_IDENTITY_CMD_DATA_SZ);

    if (ni_nvme_send_read_cmd(device_handle, event_handle, p_buffer,
                              NI_NVME_IDENTITY_CMD_DATA_SZ, ui32LBA) < 0)
    {
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

    p_id_data = (ni_nvme_identity_t *) p_buffer;

    if (p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_SR_4G)
    {
        p_ctx->ddr_config = 6;
    }
    else
    {
        if (ni_cmp_fw_api_ver(
            (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
            "6rJ") >= 0)
        {
            p_ctx->ddr_config = (p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_SR)
                ? 3 : ((p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_DR)? 4 : 5);
        }
        else if (ni_cmp_fw_api_ver(
            (char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
            "6rD") >= 0)
        {
            p_ctx->ddr_config = (p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_SR)
                ? 3 : 4;
        } else
        {
            p_ctx->ddr_config = (p_id_data->memory_cfg == NI_QUADRA_MEMORY_CONFIG_SR)
                ? 1 : 2;
        }
    }

    ni_log2(p_ctx, NI_LOG_DEBUG,  "Memory configuration %d\n",p_ctx->ddr_config);
END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): retval: %d\n", __func__, retval);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Set DDR configuration of Quadra device
 *
 *  \param[in] p_ctx  pointer to a session context with valid file handle
 *  \param[in] ddr_priority_mode  ddr priority mode
 *
 *  \return On success    NI_RETCODE_SUCCESS
 *          On failure    NI_RETCODE_INVALID_PARAM
 *                        NI_RETCODE_ERROR_MEM_ALOC
 *                        NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_set_ddr_configuration(ni_session_context_t *p_ctx,
                                             uint8_t ddr_priority_mode)
{
    void *p_buffer = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    ni_ddr_priority_config_t *p_cfg = NULL;
    uint32_t ui32LBA = CONFIG_SESSION_DDR_PRIORITY_W(p_ctx->session_id);
    ni_device_handle_t device_handle = p_ctx->blk_io_handle;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);

    ni_log2(p_ctx, NI_LOG_INFO, "set ddr priority %d\n",
           ddr_priority_mode);

    if (NI_INVALID_DEVICE_HANDLE == device_handle)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR: %s(): invalid passed parameters\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE),
                          NI_NVME_IDENTITY_CMD_DATA_SZ))
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer.\n",
               NI_ERRNO, __func__);
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    memset(p_buffer, 0, NI_NVME_IDENTITY_CMD_DATA_SZ);
    p_cfg = (ni_ddr_priority_config_t *)p_buffer;
    p_cfg->ddr_mode = ddr_priority_mode;
    if (ni_nvme_send_write_cmd(p_ctx->blk_io_handle, NI_INVALID_EVENT_HANDLE,
                               p_buffer,NI_DATA_BUFFER_LEN, ui32LBA) < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "DDR priority setting failed with mode %d\n",
               ddr_priority_mode);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }
END:
    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): retval: %d\n", __func__, retval);

    return retval;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for the metadata header and auxillary data for
  *          encoder input data.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] extra_len     Length header and auxillary data
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_metadata_buffer_alloc(ni_frame_t *p_frame,
                                              int extra_len)
{
    void *metadata_buffer = NULL;
    int retval = NI_RETCODE_SUCCESS;

    if ((!p_frame) || (extra_len <= 0))
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s passed parameters are null or not supported, "
               "p_frame %p, extra_len %d",
               __func__, p_frame, extra_len);
        return NI_RETCODE_INVALID_PARAM;
    }

    int buffer_size = extra_len;
    if (buffer_size % NI_MEM_PAGE_ALIGNMENT)
    {
        buffer_size = ((buffer_size / NI_MEM_PAGE_ALIGNMENT) + 1) *
            NI_MEM_PAGE_ALIGNMENT;
    }

    // Check if previous metadata buffer needs to be freed
    if ((p_frame->metadata_buffer_size != buffer_size) &&
        (p_frame->metadata_buffer_size > 0))
    {
        ni_log(NI_LOG_DEBUG,
               "%s: free current p_frame metadata buffer, "
               "p_frame->buffer_size=%u\n",
               __func__, p_frame->metadata_buffer_size);
        p_frame->metadata_buffer_size = 0;
        ni_aligned_free(p_frame->p_metadata_buffer);
    }

    // Check if new metadata buffer needs to be allocated
    if (p_frame->metadata_buffer_size != buffer_size)
    {
        if (ni_posix_memalign(&metadata_buffer, sysconf(_SC_PAGESIZE),
                              buffer_size))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %d: %s() Cannot allocate metadata buffer.\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }

        // init once after allocation
        memset(metadata_buffer, 0, buffer_size);
        p_frame->metadata_buffer_size = buffer_size;
        p_frame->p_metadata_buffer = metadata_buffer;

        ni_log(NI_LOG_DEBUG, "%s: allocated new metadata buffer\n", __func__);
    } else
    {
        ni_log(NI_LOG_DEBUG, "%s: reuse metadata buffer\n", __func__);
    }

    ni_log(NI_LOG_DEBUG,
           "%s: success: p_frame->p_metadata_buffer %p "
           "p_frame->metadata_buffer_size=%u\n",
           __func__, p_frame->p_metadata_buffer, p_frame->metadata_buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(metadata_buffer);
    }

    return retval;
}

/*!*****************************************************************************
  *  \brief  Allocate memory for the non-4k-aligned part at the start of YUV data for
  *          encoder input data.
  *
  *  \param[in] p_frame       Pointer to a caller allocated ni_frame_t struct
  *
  *  \param[in] start_len     Length of non-4k-aligned part at the start of YUV data
  *
  *  \return On success
  *                          NI_RETCODE_SUCCESS
  *          On failure
  *                          NI_RETCODE_INVALID_PARAM
  *                          NI_RETCODE_ERROR_MEM_ALOC
  *****************************************************************************/
ni_retcode_t ni_encoder_start_buffer_alloc(ni_frame_t *p_frame)
{
    void *start_buffer = NULL;
    int retval = NI_RETCODE_SUCCESS;

    if (!p_frame)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s passed parameters are null or not supported, "
               "p_frame %p\n",
               __func__, p_frame);
        return NI_RETCODE_INVALID_PARAM;
    }

    // Check if new start buffer needs to be allocated
    if (!p_frame->start_buffer_size)
    {
        if (ni_posix_memalign(&start_buffer, sysconf(_SC_PAGESIZE),
                              NI_MEM_PAGE_ALIGNMENT*NI_MAX_NUM_SW_FRAME_DATA_POINTERS))
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR %d: %s() Cannot allocate start buffer.\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }

        // init once after allocation
        memset(start_buffer, 0, NI_MEM_PAGE_ALIGNMENT*NI_MAX_NUM_SW_FRAME_DATA_POINTERS);
        p_frame->start_buffer_size = NI_MEM_PAGE_ALIGNMENT*NI_MAX_NUM_SW_FRAME_DATA_POINTERS;
        p_frame->p_start_buffer = start_buffer;

        ni_log(NI_LOG_DEBUG, "%s: allocated new start buffer\n", __func__);
    } else
    {
        ni_log(NI_LOG_DEBUG, "%s: reuse start buffer\n", __func__);
    }

    ni_log(NI_LOG_DEBUG,
           "%s: success: p_frame->p_start_buffer %p "
           "p_frame->start_buffer_size=%u\n",
           __func__, p_frame->p_start_buffer, p_frame->start_buffer_size);

END:

    if (NI_RETCODE_SUCCESS != retval)
    {
        ni_aligned_free(start_buffer);
    }

    return retval;
}

ni_retcode_t ni_ai_session_query_metrics(ni_session_context_t *p_ctx,
                                         ni_network_perf_metrics_t *p_metrics)
{
    ni_retcode_t retval = NI_RETCODE_SUCCESS;
    void *p_buffer = NULL;
    uint32_t dataLen;
    uint32_t ui32LBA = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): enter\n", __func__);
    if (!p_ctx || !p_metrics)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,
               "ERROR: %s() passed parameters are null!, return\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (ni_cmp_fw_api_ver((char*) &p_ctx->fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          "6N") >= 0)
    {
        dataLen =
            (sizeof(ni_network_perf_metrics_t) + (NI_MEM_PAGE_ALIGNMENT - 1)) &
            ~(NI_MEM_PAGE_ALIGNMENT - 1);
        if (ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), dataLen))
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "ERROR %d: %s() Cannot allocate buffer\n",
                   NI_ERRNO, __func__);
            retval = NI_RETCODE_ERROR_MEM_ALOC;
            LRETURN;
        }
        memset(p_buffer, 0, sizeof(ni_network_perf_metrics_t));

        ui32LBA =
            QUERY_INSTANCE_METRICS_R(p_ctx->session_id, NI_DEVICE_TYPE_AI);
        retval =
            ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                  p_buffer, dataLen, ui32LBA);
        if ((int32_t)retval != NI_RETCODE_SUCCESS)
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s(): NVME command Failed\n", __func__);
            retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
            LRETURN;
        }
        CHECK_ERR_RC(p_ctx, retval, 0, nvme_cmd_xcoder_read, p_ctx->device_type,
              p_ctx->hw_id, &(p_ctx->session_id), OPT_1);

        *p_metrics = *((ni_network_perf_metrics_t *)p_buffer);
    } else
    {
        p_metrics->total_cycles = (uint32_t)(-1);
        p_metrics->total_idle_cycles = (uint32_t)(-1);
    }
END:

    ni_aligned_free(p_buffer);
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s(): exit\n", __func__);

    return retval;
}

/*!*****************************************************************************
 *  \brief  Send namespace num / Opmode and SRIOv index/value to the device with
 *          specified logic block address.
 *
 *  \param[in] device_handle  Device handle obtained by calling ni_device_open
 *  \param[in] Key            Represents either namespace num or opmode
 *  \param[in] Value          Represents either SRIOv index or opmode value
 *
 *  \return On success
 *                          NI_RETCODE_SUCCESS
 *          On failure
 *                          NI_RETCODE_ERROR_MEM_ALOC
 *                          NI_RETCODE_ERROR_NVME_CMD_FAILED
 ******************************************************************************/
ni_retcode_t ni_device_config_ns_qos(ni_device_handle_t device_handle,
                                            uint32_t key,
                                            uint32_t value)
{
    char buf[NI_DATA_BUFFER_LEN] = {'\0'};
    uint32_t *u32_buf = (uint32_t *)buf;
    u32_buf[0] = key;
    u32_buf[1] = value;
    // event handle could be ignored
    return ni_nvme_send_write_cmd(device_handle, NI_INVALID_EVENT_HANDLE,
                                  (void *)buf, NI_DATA_BUFFER_LEN,
                                  CONFIG_GLOBAL_NAMESPACE_NUM);
}

char *ni_get_core_name(ni_core_type_t eCoreType)
{
    char *CoreName;
    switch (eCoreType)
    {
        case ALL_CORE:
            CoreName = (char *)"all";
            break;
        case NVME_CORE:
            CoreName = (char *)"np";
            break;
        case EP_CORE:
            CoreName = (char *)"ep";
            break;
        case DP_CORE:
            CoreName = (char *)"dp";
            break;
        case TP_CORE:
            CoreName = (char *)"tp";
            break;
        case FP_CORE:
            CoreName = (char *)"fp";
            break;
        default:
            CoreName = (char *)"Not Found";
            break;
    }
    return CoreName;
}

uint32_t ni_get_log_lba(ni_core_type_t eCoreType)
{
    uint32_t lba;
    switch (eCoreType)
    {
        case NVME_CORE:
            lba = NVME_LOG_OFFSET_IN_4K;
            break;
        case EP_CORE:
            lba = EP_LOG_OFFSET_IN_4K;
            break;
        case DP_CORE:
            lba = DP_LOG_OFFSET_IN_4K;
            break;
        case TP_CORE:
            lba = TP_LOG_OFFSET_IN_4K;
            break;
        case FP_CORE:
            lba = FP_LOG_OFFSET_IN_4K;
            break;
        default:
            ni_log(NI_LOG_ERROR, "%s:() Invalid core ID:%u\n", __func__, eCoreType);
            lba = 0;
            break;
    }

    return lba;
}

ni_retcode_t  ni_dump_log_single_core(ni_session_context_t *p_ctx, void* p_data, uint32_t core_id, bool gen_log_file)
{
    int32_t rc;
    uint32_t lba = 0;
    uint32_t data_len = CPU_LOG_BUFFER_SIZE;
    char *core_name = NULL;
    FILE *p_file = NULL;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_ctx || !p_data)
    {
         ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: ERROR invalid pointer p_ctx %p p_data %p\n",
                    __func__, __LINE__, p_ctx, p_data);
        return NI_RETCODE_INVALID_PARAM;
    }

    memset(p_data, 0, CPU_LOG_BUFFER_SIZE);
    *(uint8_t *)p_data = 0x55;

    lba = ni_get_log_lba((ni_core_type_t)core_id);
    if (lba == 0)
    {
         ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: ERROR core_id %u\n",
                    __func__, __LINE__, core_id);
        return NI_RETCODE_INVALID_PARAM;
    }

    core_name = ni_get_core_name((ni_core_type_t)core_id);

    rc = ni_nvme_send_read_cmd(p_ctx->blk_io_handle, p_ctx->event_handle, p_data, data_len, lba);

    if (rc != NI_RETCODE_SUCCESS)
    {
         ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: ERROR %d: nvme read %s core failed\n",
                    __func__, __LINE__, rc, core_name);
    }
    else if (gen_log_file)
    {
        //generate log file e.g. raw_dp_slot_0_0000.bin
        char filename[32] = "raw_";
        strcat(filename, core_name);
        strcat(filename, "_slot_");
#ifdef __linux__
        bool pcie_id_name = false;
        char devFilePath[1024] = {0};
        char devFDPath[1024] = {0};
        char pcie[64] = {0};
        char domain[5] = {0}, slot[3] = {0}, dev[3] = {0}, func[2] = {0};
        //p_ctx->blk_dev_name might be empty so look up the file name
        snprintf(devFDPath, sizeof(devFDPath), "/proc/self/fd/%d", p_ctx->blk_io_handle);
        ssize_t len = readlink(devFDPath, devFilePath, sizeof(devFilePath)-1);
        if (len != -1) {
            devFilePath[len] = '\0';
        }
        if (strstr(devFilePath, "/dev/nvme") != NULL)
        {
            get_dev_pcie_addr(devFilePath, pcie, domain, slot, dev, func);
            if (strlen(pcie) > 0 && strlen(slot) > 0 && strlen(domain) > 0)
            {
                strcat(filename, slot);
                strcat(filename, "_");
                strcat(filename, domain);
                pcie_id_name = true;
            }
        }
        if (!pcie_id_name)
        {
            ni_log2(p_ctx, NI_LOG_INFO, "%s:():%d: For dev %d can't look up PCI domain and slot info. Defaulting to slot=hw_id and domain=0000\n",
                    __func__, __LINE__, p_ctx->blk_io_handle);
            char num[4] = {0};
            snprintf(num, 4, "%d", p_ctx->hw_id);
            strcat(filename, num);
            strcat(filename, "_0000");
        }
        strcat(filename, ".bin");
        ni_log2(p_ctx, NI_LOG_INFO, "For dev %d %s core %s creating file %s\n",
                p_ctx->blk_io_handle, devFilePath, core_name, filename);
#else
        char num[4] = {0};
        snprintf(num, 4, "%02x", p_ctx->hw_id);
        strcat(filename, num);
        strcat(filename, "_0000");
        strcat(filename, ".bin");
        ni_log2(p_ctx, NI_LOG_INFO, "For dev %d core %s creating file %s\n",
                p_ctx->blk_io_handle, core_name, filename);
#endif
        p_file = fopen(filename, "wb");
        if (p_file)
        {
            /* Write out the stream header */
            if (fwrite((uint8_t *)p_data ,
                       data_len, 1, p_file) != 1)
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: Error: writing data %u bytes error!\n",
                          __func__, __LINE__, data_len);
                ni_log2(p_ctx, NI_LOG_ERROR, "Error: ferror rc = %d\n", ferror(p_file));
                retval = NI_RETCODE_FAILURE;
            }
            if (fflush(p_file))
            {
                ni_log2(p_ctx, NI_LOG_ERROR, "Error: writing data frame flush failed! errno %d\n",
                        ferror(p_file));
                retval = NI_RETCODE_FAILURE;
            }
            fclose(p_file);
        }
        else
        {
            ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: Error: cannot open %s\n",
                      __func__, __LINE__, filename);
            retval = NI_RETCODE_FAILURE;
        }
    }

    return retval;
}

ni_retcode_t ni_dump_log_all_cores(ni_session_context_t *p_ctx, void* p_data, bool gen_log_file)
{
    int i = 1;

    if (!p_ctx || !p_data)
    {
         ni_log2(p_ctx, NI_LOG_ERROR, "%s:():%d: ERROR invalid pointer p_ctx %p p_data %p\n",
                    __func__, __LINE__, p_ctx, p_data);
        return NI_RETCODE_INVALID_PARAM;
    }

    for (i = NVME_CORE; i< NUM_OF_CORES; i++)
    {
        ni_dump_log_single_core(p_ctx, p_data, i, gen_log_file);
        p_data = (void*)((uint8_t *)p_data + CPU_LOG_BUFFER_SIZE);
    }

    return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_send_to_target(ni_session_context_t *p_ctx,
                               niFrameSurface1_t *source,
                               uint64_t ui64DestAddr,
                               uint32_t ui32FrameSize)
{
    void *p_buffer = NULL;
    ni_retcode_t retval;
    uint32_t ui32LBA;
    uint8_t *p_data;
    uint16_t ui16Direction = NI_P2P_SEND;

    /* allocate memory aligned buffer */
    retval = ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN);
    if (retval != 0)
    {
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    p_data = (uint8_t *) p_buffer;

    /* Add payload */
    memcpy(p_data, &ui64DestAddr, sizeof(uint64_t));
    memcpy(p_data + 8, &ui32FrameSize, sizeof(uint32_t));
    memcpy(p_data + 12, &source->ui16FrameIdx, sizeof(uint16_t));
    memcpy(p_data + 14, MAGIC_P2P_VALUE, 4);
    memcpy(p_data + 18, &ui16Direction, sizeof(uint16_t));

    ui32LBA = CONFIG_INSTANCE_SetP2P_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

    retval = ni_nvme_send_write_cmd((ni_device_handle_t)(int64_t)source->device_handle,
                                    NI_INVALID_DEVICE_HANDLE, p_buffer,
                                    NI_DATA_BUFFER_LEN, ui32LBA);

    if (retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s: NVME command failed %d\n", __func__, retval);
        ni_aligned_free(p_buffer);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

END:
    if (p_buffer != NULL)
        ni_aligned_free(p_buffer);
    return retval;
}

ni_retcode_t ni_recv_from_target(
    ni_session_context_t *p_ctx,
    const ni_p2p_sgl_t *dmaAddrs,
    ni_frame_t *pDstFrame)
{
    void *p_buffer = NULL;
    ni_retcode_t retval;
    uint32_t ui32LBA = 0;
    niFrameSurface1_t *pSurface;
    uint8_t *p_data;
    uint16_t ui16Direction = NI_P2P_RECV;
    uint32_t ui32Dummy = 0;
    int i;

    if (dmaAddrs->ui32NumEntries > NI_MAX_P2P_SGL_ENTRIES)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "Too many SGL entries\n");
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    /* allocate memory aligned buffer */
    retval = ni_posix_memalign(&p_buffer, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN);
    if (retval != 0)
    {
        retval = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    pSurface = (niFrameSurface1_t *) pDstFrame->p_data[3];

    memset(p_buffer, 0, NI_DATA_BUFFER_LEN);

    p_data = (uint8_t *) p_buffer;

    /* Add payload */
    memcpy(p_data, &dmaAddrs->ui64DMAAddr[0], 8);    // dummy data, not used for p2p read
    memcpy(p_data + 8, &ui32Dummy, 4);               // dummy data, not used for p2p read
    memcpy(p_data + 12, &pSurface->ui16FrameIdx, 2); // dummy data, not used for p2p read
    memcpy(p_data + 14, MAGIC_P2P_VALUE, 4);
    memcpy(p_data + 18, &ui16Direction, 2);
    memcpy(p_data + 20, &dmaAddrs->ui32NumEntries, 4);

    for (i = 0; i < dmaAddrs->ui32NumEntries; i++)
    {
        memcpy(&p_data[24] + (i*8), &dmaAddrs->ui64DMAAddr[i], 8);
    }

    for (i = 0; i < dmaAddrs->ui32NumEntries; i++)
    {
        memcpy(&p_data[24] + (dmaAddrs->ui32NumEntries * 8) + (i * 4), &dmaAddrs->ui32DMALen[i], 4);
    }

    ui32LBA = CONFIG_INSTANCE_SetP2P_W(p_ctx->session_id, NI_DEVICE_TYPE_ENCODER);

    retval = ni_nvme_send_write_cmd(p_ctx->blk_io_handle, p_ctx->event_handle,
                                    p_buffer, NI_DATA_BUFFER_LEN, ui32LBA);

    if (retval < 0)
    {
        ni_log2(p_ctx, NI_LOG_ERROR, "%s: NVME command failed\n", __func__);
        ni_aligned_free(p_buffer);
        retval = NI_RETCODE_ERROR_NVME_CMD_FAILED;
        LRETURN;
    }

END:
    if (p_buffer != NULL)
        ni_aligned_free(p_buffer);
    return retval;
}

int lower_pixel_rate(const ni_load_query_t *pQuery, uint32_t ui32CurrentLowest)
{
    return (pQuery->total_pixel_load < ui32CurrentLowest) ? 1 : 0;
}
