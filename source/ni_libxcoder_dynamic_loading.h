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
 *  \file    ni_libxcoder_dynamic_loading.h
 *
 *  \brief   Libxcoder API dynamic loading support for Linux
 *
 *  \author  Netflix, Inc. (2022)
 ******************************************************************************/

#pragma once

#ifndef _NETINTLIBXCODERAPI_H_
#define _NETINTLIBXCODERAPI_H_

#include <dlfcn.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

#ifndef _NETINT_LIBXCODER_DYNAMIC_LOADING_TEST_
#include <ni_av_codec.h>
#include <ni_util.h>
#include <ni_device_api.h>
#else
#include "ni_av_codec.h"
#include "ni_util.h"
#include "ni_device_api.h"
#endif

#pragma GCC diagnostic pop

#define LIB_API

/*
 * Defines API function pointers
 */
//
// Function pointers for ni_av_codec.h
//
typedef int (LIB_API* PNISHOULDSENDSEIWITHFRAME) (ni_session_context_t *p_enc_ctx, ni_pic_type_t pic_type, ni_xcoder_params_t *p_param);
typedef void (LIB_API* PNIDECRETRIEVEAUXDATA) (ni_frame_t *frame);
typedef void (LIB_API* PNIENCPREPAUXDATA) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame, ni_codec_format_t codec_format, int should_send_sei_with_frame, uint8_t *mdcv_data, uint8_t *cll_data, uint8_t *cc_data, uint8_t *udu_data, uint8_t *hdrp_data);
typedef void (LIB_API* PNIENCCOPYAUXDATA) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame, ni_frame_t *p_dec_frame, ni_codec_format_t codec_format, const uint8_t *mdcv_data, const uint8_t *cll_data, const uint8_t *cc_data, const uint8_t *udu_data, const uint8_t *hdrp_data, int is_hwframe, int is_semiplanar);
typedef int (LIB_API* PNIENCWRITEFROMYUVBUFFER) (ni_session_context_t *p_ctx, ni_frame_t *p_enc_frame, uint8_t *p_yuv_buffer);
typedef int (LIB_API* PNIEXTRACTCUSTOMSEI) (uint8_t *pkt_data, int pkt_size, long index, ni_packet_t *p_packet, uint8_t sei_type, int vcl_found);
typedef int (LIB_API* PNIDECPACKETPARSE) (ni_session_context_t *p_session_ctx, ni_xcoder_params_t *p_param, uint8_t *data, int size, ni_packet_t *p_packet, int low_delay, int codec_format, int pkt_nal_bitmap, int custom_sei_type, int *svct_skip_next_packet, int *is_lone_sei_pkt);
typedef int (LIB_API* PNIEXPANDFRAME) (ni_frame_t *dst, ni_frame_t *src, int dst_stride[], int raw_width, int raw_height, int ni_fmt, int nb_planes);
typedef int (LIB_API* PNIRECONFIGPPUOUTPUT) (ni_session_context_t *p_session_ctx, ni_xcoder_params_t *p_param, ni_ppu_config_t *ppu_config);
typedef int (LIB_API* PNIENCINSERTTIMECODE) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_enc_frame, ni_timecode_t *p_timecode);
//
// Function pointers for ni_util.h
//
typedef void (LIB_API* PNIGETHWYUV420PDIM) (int width, int height, int bit_depth_factor, int is_semiplanar, int plane_stride[NI_MAX_NUM_DATA_POINTERS], int plane_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNIGETFRAMEDIM) (int width, int height, ni_pix_fmt_t pix_fmt, int plane_stride[NI_MAX_NUM_DATA_POINTERS], int plane_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNIGETMINFRAMEDIM) (int width, int height, ni_pix_fmt_t pix_fmt, int plane_stride[NI_MAX_NUM_DATA_POINTERS], int plane_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNICOPYHWYUV420P) (uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS], int width, int height, int bit_depth_factor, int is_semiplanar, int conf_win_right, int dst_stride[NI_MAX_NUM_DATA_POINTERS], int dst_height[NI_MAX_NUM_DATA_POINTERS], int src_stride[NI_MAX_NUM_DATA_POINTERS], int src_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNICOPYFRAMEDATA) (uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS], int frame_width, int frame_height, int factor, ni_pix_fmt_t pix_fmt, int conf_win_right, int dst_stride[NI_MAX_NUM_DATA_POINTERS], int dst_height[NI_MAX_NUM_DATA_POINTERS], int src_stride[NI_MAX_NUM_DATA_POINTERS], int src_height[NI_MAX_NUM_DATA_POINTERS]);
typedef void (LIB_API* PNICOPYYUV444PTO420P) (uint8_t *p_dst0[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_dst1[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS], int width, int height, int factor, int mode);
typedef int (LIB_API* PNIINSERTEMULATIONPREVENTBYTES) (uint8_t *buf, int size);
typedef int (LIB_API* PNIREMOVEEMULATIONPREVENTBYTES) (uint8_t *buf, int size);
typedef int32_t (LIB_API* PNIGETTIMEOFDAY) (struct timeval *p_tp, void *p_tzp);
typedef uint64_t (LIB_API* PNIGETTIMENS) (void);
typedef void (LIB_API* PNIUSLEEP) (int64_t usec);
typedef char * (LIB_API* PNISTRTOK) (char *s, const char *delim, char **saveptr);
typedef ni_retcode_t (LIB_API* PNINETWORKLAYERCONVERTOUTPUT) (float *dst, uint32_t num, ni_packet_t *p_packet, ni_network_data_t *p_network, uint32_t layer);
typedef uint32_t (LIB_API* PNIAINETWORKLAYERSIZE) (ni_network_layer_params_t *p_param);
typedef uint32_t (LIB_API* PNIAINETWORKLAYERDIMS) (ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKLAYERCONVERTTENSOR) (uint8_t *dst, uint32_t dst_len, const char *tensor_file, ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKCONVERTTENSORTODATA) (uint8_t *dst, uint32_t dst_len, float *src, uint32_t src_len, ni_network_layer_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNINETWORKCONVERTDATATOTENSOR) (float *dst, uint32_t dst_len, uint8_t *src, uint32_t src_len, ni_network_layer_params_t *p_param);
typedef void (LIB_API* PNICALCULATESHA256) (const uint8_t aui8Data[], size_t ui32DataLength, uint8_t aui8Hash[]);
typedef void (LIB_API* PNICOPYHWDESCRIPTORS) (uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS], uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS]);
typedef char * (LIB_API* PNIGETLIBXCODERAPIVER) (void);
typedef char * (LIB_API* PNIGETCOMPATFWAPIVER) (void);
typedef void (LIB_API* PNIFMTFWAPIVERSTR) (const char ver_str[], char fmt_str[]);
typedef int (LIB_API* PNICMPFWAPIVER) (const char ver1[], const char ver2[]);
typedef char * (LIB_API* PNIGETLIBXCODERRELEASEVER) (void);
typedef const char * (LIB_API* PNIGETRCTXT) (ni_retcode_t rc);
typedef int (LIB_API* PNIPARAMGETKEYVALUE) (char *p_str, char *key, char *value);
typedef int (LIB_API* PNIRETRIEVEXCODERPARAMS) (char xcoderParams[], ni_xcoder_params_t *params, ni_session_context_t *ctx);
typedef int (LIB_API* PNIRETRIEVEXCODERGOP) (char xcoderGop[], ni_xcoder_params_t *params, ni_session_context_t *ctx);
typedef int (LIB_API* PNIRETRIEVEDECODERPARAMS) (char xcoderParams[], ni_xcoder_params_t *params, ni_session_context_t *ctx);
typedef int (LIB_API* PNIPTHREADMUTEXINIT) (ni_pthread_mutex_t *mutex);
typedef int (LIB_API* PNIPTHREADMUTEXDESTROY) (ni_pthread_mutex_t *mutex);
typedef int (LIB_API* PNIPTHREADMUTEXLOCK) (ni_pthread_mutex_t *mutex);
typedef int (LIB_API* PNIPTHREADMUTEXUNLOCK) (ni_pthread_mutex_t *mutex);
typedef int (LIB_API* PNIPTHREADCREATE) (ni_pthread_t *thread, const ni_pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
typedef int (LIB_API* PNIPTHREADJOIN) (ni_pthread_t thread, void **value_ptr);
typedef int (LIB_API* PNIPTHREADCONDINIT) (ni_pthread_cond_t *cond, const ni_pthread_condattr_t *attr);
typedef int (LIB_API* PNIPTHREADCONDDESTROY) (ni_pthread_cond_t *cond);
typedef int (LIB_API* PNIPTHREADCONDBROADCAST) (ni_pthread_cond_t *cond);
typedef int (LIB_API* PNIPTHREADCONDWAIT) (ni_pthread_cond_t *cond, ni_pthread_mutex_t *mutex);
typedef int (LIB_API* PNIPTHREADCONDSIGNAL) (ni_pthread_cond_t *cond);
typedef int (LIB_API* PNIPTHREADCONDTIMEDWAIT) (ni_pthread_cond_t *cond, ni_pthread_mutex_t *mutex, const struct timespec *abstime);
typedef int (LIB_API* PNIPTHREADSIGMASK) (int how, const ni_sigset_t *set, ni_sigset_t *oldset);
typedef int (LIB_API* PNIPOSIXMEMALIGN) (void **memptr, size_t alignment, size_t size);
typedef const char * (LIB_API* PNIAIERRNOTOSTR) (int rc);
//

//
// Function pointers for ni_device_api.h
//
typedef ni_session_context_t * (LIB_API* PNIDEVICESESSIONCONTEXTALLOCINIT) (void);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCONTEXTINIT) (ni_session_context_t *p_ctx);
typedef void (LIB_API* PNIDEVICESESSIONCONTEXTCLEAR) (ni_session_context_t *p_ctx);
typedef void (LIB_API* PNIDEVICESESSIONCONTEXTFREE) (ni_session_context_t *p_ctx);
typedef ni_event_handle_t (LIB_API* PNICREATEEVENT) (void);
typedef void (LIB_API* PNICLOSEEVENT) (ni_event_handle_t event_handle);
typedef ni_device_handle_t (LIB_API* PNIDEVICEOPEN) (const char *dev, uint32_t *p_max_io_size_out);
typedef void (LIB_API* PNIDEVICECLOSE) (ni_device_handle_t dev);
typedef ni_retcode_t (LIB_API* PNIDEVICECAPABILITYQUERY) (ni_device_handle_t device_handle, ni_device_capability_t *p_cap);
typedef ni_retcode_t (LIB_API* PNIDEVICECAPABILITYQUERY2) (ni_device_handle_t device_handle, ni_device_capability_t *p_cap, bool device_in_ctxt);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONOPEN) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCLOSE) (ni_session_context_t *p_ctx, int eos_received, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONFLUSH) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICEDECSESSIONSAVEHDRS) (ni_session_context_t *p_ctx, uint8_t *hdr_data, uint8_t hdr_size);
typedef ni_retcode_t (LIB_API* PNIDEVICEDECSESSIONFLUSH) (ni_session_context_t *p_ctx);
typedef int (LIB_API* PNIDEVICESESSIONWRITE) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef int (LIB_API* PNIDEVICESESSIONREAD) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONQUERY) (ni_session_context_t *p_ctx, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONQUERYDETAIL) (ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_detail_status_t *detail_data);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONQUERYDETAILV1) (ni_session_context_t* p_ctx, ni_device_type_t device_type, ni_instance_mgr_detail_status_v1_t *detail_data);
typedef ni_retcode_t (LIB_API* PNIDEVICECONFIGNAMESPACENUM) (ni_device_handle_t device_handle, uint32_t namespace_num, uint32_t sriov_index);
typedef ni_retcode_t (LIB_API* PNIDEVICECONFIGQOS) (ni_device_handle_t device_handle, uint32_t mode);
typedef ni_retcode_t (LIB_API* PNIDEVICECONFIGQOSOP) (ni_device_handle_t device_handle, ni_device_handle_t device_handle_t, uint32_t over_provision);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOC) (ni_frame_t *p_frame, int video_width, int video_height, int alignment, int metadata_flag, int factor, int hw_frame_count, int is_planar);
typedef ni_retcode_t (LIB_API* PNIENCFRAMEBUFFERALLOC) (ni_frame_t *p_frame, int video_width, int video_height, int alignment, int metadata_flag, int factor, int hw_frame_count, int is_planar, ni_pix_fmt_t pix_fmt);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCDL) (ni_frame_t *p_frame, int video_width, int video_height, int pixel_format);
typedef ni_retcode_t (LIB_API* PNIDECODERFRAMEBUFFERALLOC) (ni_buf_pool_t *p_pool, ni_frame_t *pframe, int alloc_mem, int video_width, int video_height, int alignment, int factor, int is_planar);
typedef ni_retcode_t (LIB_API* PNIENCODERFRAMEBUFFERALLOC) (ni_frame_t *pframe, int video_width, int video_height, int linesize[], int alignment, int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNISCALERDESTFRAMEALLOC) (ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params, niFrameSurface1_t *p_surface);
typedef ni_retcode_t (LIB_API* PNISCALERINPUTFRAMEALLOC) (ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params, niFrameSurface1_t *p_src_surface);
typedef ni_retcode_t (LIB_API* PNISCALERFRAMEPOOLALLOC) (ni_session_context_t *p_ctx, ni_scaler_input_params_t scaler_params);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCNV) (ni_frame_t *p_frame, int video_width, int video_height, int linesize[], int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNIENCODERSWFRAMEBUFFERALLOC) (bool planar, ni_frame_t *p_frame, int video_width, int video_height, int linesize[], int alignment, int extra_len, bool alignment_2pass_wa);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERFREE) (ni_frame_t *pframe);
typedef ni_retcode_t (LIB_API* PNIDECODERFRAMEBUFFERFREE) (ni_frame_t *pframe);
typedef void (LIB_API* PNIDECODERFRAMEBUFFERPOOLRETURNBUF) (ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERALLOC) (ni_packet_t *ppacket, int packet_size);
typedef ni_retcode_t (LIB_API* PNICUSTOMPACKETBUFFERALLOC) (void *p_buffer, ni_packet_t *p_packet, int buffer_size);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERFREE) (ni_packet_t *ppacket);
typedef ni_retcode_t (LIB_API* PNIPACKETBUFFERFREEAV1) (ni_packet_t *ppacket);
typedef int (LIB_API* PNIPACKETCOPY) (void *p_destination, const void *const p_source, int cur_size, void *p_leftover, int *p_prev_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMENEWAUXDATA) (ni_frame_t *frame, ni_aux_data_type_t type, int data_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMENEWAUXDATAFROMRAWDATA) (ni_frame_t *frame, ni_aux_data_type_t type, const uint8_t *raw_data, int data_size);
typedef ni_aux_data_t * (LIB_API* PNIFRAMEGETAUXDATA) (const ni_frame_t *frame, ni_aux_data_type_t type);
typedef void (LIB_API* PNIFRAMEFREEAUXDATA) (ni_frame_t *frame, ni_aux_data_type_t type);
typedef void (LIB_API* PNIFRAMEWIPEAUXDATA) (ni_frame_t *frame);
typedef ni_retcode_t (LIB_API* PNIENCODERINITDEFAULTPARAMS) (ni_xcoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height, ni_codec_format_t codec_format);
typedef ni_retcode_t (LIB_API* PNIDECODERINITDEFAULTPARAMS) (ni_xcoder_params_t *p_param, int fps_num, int fps_denom, long bit_rate, int width, int height);
typedef ni_retcode_t (LIB_API* PNIENCODERPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, const char *value);
typedef ni_retcode_t (LIB_API* PNIDECODERPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, char *value);
typedef ni_retcode_t (LIB_API* PNIENCODERGOPPARAMSSETVALUE) (ni_xcoder_params_t *p_params, const char *name, const char *value);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONCOPY) (ni_session_context_t *src_p_ctx, ni_session_context_t *dst_p_ctx);
typedef int (LIB_API* PNIDEVICESESSIONINITFRAMEPOOL) (ni_session_context_t *p_ctx, uint32_t pool_size, uint32_t pool);
typedef int (LIB_API* PNIDEVICESESSIONREADHWDESC) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, ni_device_type_t device_type);
typedef int (LIB_API* PNIDEVICESESSIONHWDL) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data, niFrameSurface1_t *hwdesc);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONQUERYBUFFERAVAIL) (ni_session_context_t* p_ctx, ni_device_type_t device_type);
typedef int (LIB_API* PNIDEVICESESSIONHWUP) (ni_session_context_t* p_ctx, ni_session_data_io_t *p_src_data, niFrameSurface1_t* hwdesc);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCHWENC) (ni_frame_t *pframe, int video_width, int video_height, int extra_len);
typedef ni_retcode_t (LIB_API* PNIHWFRAMEBUFFERRECYCLE) (niFrameSurface1_t *surface, int32_t device_handle);
typedef ni_retcode_t (LIB_API* PNIHWFRAMEBUFFERRECYCLE2) (niFrameSurface1_t *surface);
typedef ni_retcode_t (LIB_API* PNISCALERSETPARAMS) (ni_session_context_t *p_ctx, ni_scaler_params_t *p_params);
typedef ni_retcode_t (LIB_API* PNIDEVICEALLOCFRAME) (ni_session_context_t* p_ctx, int width, int height, int format, int options, int rectangle_width, int rectangle_height, int rectangle_x, int rectangle_y, int rgba_color, int frame_index, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICEALLOCDSTFRAME) (ni_session_context_t *p_ctx, niFrameSurface1_t *p_out_surface, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIDEVICECLONEHWFRAME) (ni_session_context_t *p_ctx, ni_frameclone_desc_t *p_frameclone_desc);
typedef ni_retcode_t (LIB_API* PNIDEVICECONFIGFRAME) (ni_session_context_t *p_ctx, ni_frame_config_t *p_cfg);
typedef ni_retcode_t (LIB_API* PNISCALERSETDRAWBOXPARAMS) (ni_session_context_t *p_ctx, ni_scaler_drawbox_params_t *p_params);
typedef ni_retcode_t (LIB_API* PNISCALERSETWATERMARKPARAMS) (ni_session_context_t *p_ctx, ni_scaler_watermark_params_t *p_params);
typedef ni_retcode_t (LIB_API* PNIDEVICEMULTICONFIGFRAME) (ni_session_context_t *p_ctx, ni_frame_config_t p_cfg_in[], int numInCfgs, ni_frame_config_t *p_cfg_out);
typedef ni_retcode_t (LIB_API* PNIFRAMEBUFFERALLOCPIXFMT) (ni_frame_t *pframe, int pixel_format, int video_width, int video_height, int linesize[], int alignment, int extra_len);
typedef ni_retcode_t (LIB_API* PNIAICONFIGNETWORKBINARY) (ni_session_context_t *p_ctx, ni_network_data_t *p_network, const char *file);
typedef ni_retcode_t (LIB_API* PNIAICONFIGHVSPLUS) (ni_session_context_t *p_ctx, ni_network_data_t *p_network);
typedef ni_retcode_t (LIB_API* PNIAIFRAMEBUFFERALLOC) (ni_frame_t *p_frame, ni_network_data_t *p_network);
typedef ni_retcode_t (LIB_API* PNIAIPACKETBUFFERALLOC) (ni_packet_t *p_packet, ni_network_data_t *p_network);
typedef ni_retcode_t (LIB_API* PNIRECONFIGBITRATE) (ni_session_context_t *p_ctx, int32_t bitrate);
typedef ni_retcode_t (LIB_API* PNIRECONFIGVUI) (ni_session_context_t *p_ctx, ni_vui_hrd_t *vui);
typedef ni_retcode_t (LIB_API* PNIFORCEIDRFRAMETYPE) (ni_session_context_t *p_ctx);
typedef ni_retcode_t (LIB_API* PNISETLTR) (ni_session_context_t *p_ctx, ni_long_term_ref_t *ltr);
typedef ni_retcode_t (LIB_API* PNISETLTRINTERVAL) (ni_session_context_t *p_ctx, int32_t ltr_interval);
typedef ni_retcode_t (LIB_API* PNISETFRAMEREFINVALID) (ni_session_context_t *p_ctx, int32_t frame_num);
typedef ni_retcode_t (LIB_API* PNIRECONFIGFRAMERATE) (ni_session_context_t *p_ctx, ni_framerate_t *framerate);
typedef ni_retcode_t (LIB_API* PNIRECONFIGMAXFRAMESIZE) (ni_session_context_t *p_ctx, int32_t max_frame_size);
typedef ni_retcode_t (LIB_API* PNIRECONFIGMINMAXQP) (ni_session_context_t *p_ctx, ni_rc_min_max_qp *p_min_max_qp);
typedef int (LIB_API* PNIDEVICESESSIONACQUIRE) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef int (LIB_API* PNIDEVICESESSIONACQUIREFORREAD) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERFRAMEBUFFERLOCK) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERFRAMEBUFFERUNLOCK) (ni_session_context_t *p_upl_ctx, ni_frame_t *p_frame);
typedef ni_retcode_t (LIB_API* PNIUPLOADERP2PTESTSEND) (ni_session_context_t *p_upl_ctx, uint8_t *p_data, uint32_t len, ni_frame_t *p_hwframe);
typedef ni_retcode_t (LIB_API* PNIUPLOADERP2PTESTLOAD) (ni_session_context_t *p_upl_ctx, uint8_t *p_data, uint32_t len, ni_frame_t *p_hwframe);
typedef ni_retcode_t (LIB_API* PNIENCODERSETINPUTFRAMEFORMAT) (ni_session_context_t *p_enc_ctx, ni_xcoder_params_t *p_enc_params, int width, int height, int bit_depth, int src_endian, int planar);
typedef ni_retcode_t (LIB_API* PNIUPLOADERSETFRAMEFORMAT) (ni_session_context_t *p_upl_ctx, int width, int height, ni_pix_fmt_t pixel_format, int isP2P);
typedef ni_retcode_t (LIB_API* PNISCALERP2PFRAMEACQUIRE) (ni_session_context_t *p_ctx, niFrameSurface1_t *p_surface, int data_len);
typedef ni_retcode_t (LIB_API* PNIHWFRAMEP2PBUFFERRECYCLE) (ni_frame_t *p_frame);
typedef int (LIB_API* PNIENCODERSESSIONREADSTREAMHEADER) (ni_session_context_t *p_ctx, ni_session_data_io_t *p_data);
typedef int32_t (LIB_API* PNIGETDMABUFFILEDESCRIPTOR) (const ni_frame_t* p_frame);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONSEQUENCECHANGE) (ni_session_context_t *p_ctx, int width, int height, int bit_depth_factor, ni_device_type_t device_type);
typedef ni_retcode_t (LIB_API* PNIAISESSIONREADMETRICS) (ni_session_context_t *p_ctx, ni_network_perf_metrics_t *p_metrics);
typedef ni_retcode_t (LIB_API* PNIQUERYNVMESTATUS) (ni_session_context_t *p_ctx, ni_load_query_t *p_load_query);
typedef ni_retcode_t (LIB_API* PNIQUERYFLFWVERSIONS) (ni_device_handle_t device_handle, ni_device_info_t *p_dev_info);
typedef ni_retcode_t (LIB_API* PNIQUERYVFNSID) (ni_device_handle_t device_handle, ni_device_vf_ns_id_t *p_dev_ns_vf, uint8_t fw_rev[]);
typedef ni_retcode_t (LIB_API* PNIQUERYTEMPERATURE) (ni_device_handle_t device_handle, ni_device_temp_t *p_dev_temp, uint8_t fw_rev[]);
typedef ni_retcode_t (LIB_API* PNIQUERYEXTRAINFO) (ni_device_handle_t device_handle, ni_device_extra_info_t *p_dev_extra_info, uint8_t fw_rev[]);
typedef ni_retcode_t (LIB_API* PNIENCODERFRAMEZEROCOPYCHECK) (ni_session_context_t *p_enc_ctx, ni_xcoder_params_t *p_enc_params, int width, int height, const int linesize[], bool set_linesize);
typedef ni_retcode_t (LIB_API* PNIENCODERFRAMEZEROCOPYBUFFERALLOC) (ni_frame_t *p_frame, int video_width, int video_height, const int linesize[], const uint8_t *data[], int extra_len);
typedef ni_retcode_t (LIB_API* PNIUPLOADERFRAMEZEROCOPYCHECK) (ni_session_context_t *p_upl_ctx, int width, int height, const int linesize[], int pixel_format);
typedef ni_retcode_t (LIB_API* PNIRECONFIGCRF) (ni_session_context_t *p_ctx, int32_t crf);
typedef ni_retcode_t (LIB_API* PNIRECONFIGCRF2) (ni_session_context_t *p_ctx, float crf);
typedef ni_retcode_t (LIB_API* PNIDEVICEALLOCANDGETFIRMWARELOGS) (ni_session_context_t *p_ctx, void** p_log_buffer, bool gen_log_file);
typedef ni_retcode_t (LIB_API* PNIRECONFIGVBVVALUE) (ni_session_context_t *p_ctx, int32_t vbvMaxRate, int32_t vbvBufferSize);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONUPDATEFRAMEPOOL) (ni_session_context_t *p_ctx, uint32_t pool_size);
typedef ni_retcode_t (LIB_API* PNISETDEMOROIMAP) (ni_session_context_t *p_enc_ctx);
typedef ni_retcode_t (LIB_API* PNIENCPREPRECONFDEMODATA) (ni_session_context_t *p_enc_ctx, ni_frame_t *p_frame);
typedef void (LIB_API* PNIGOPPARAMSCHECKSET) (ni_xcoder_params_t *p_param, char *value);
typedef bool (LIB_API* PNIGOPPARAMSCHECK) (ni_xcoder_params_t *p_param);
typedef ni_retcode_t (LIB_API* PNIRECONFIGMAXFRAMESIZERATIO) (ni_session_context_t *p_ctx, int32_t max_frame_size_ratio);
typedef ni_retcode_t (LIB_API* PNIRECONFIGINTRAPRD) (ni_session_context_t *p_ctx, int32_t intra_period);
typedef ni_retcode_t (LIB_API* PNIP2PXFER) (ni_session_context_t *pSession, niFrameSurface1_t *source, uint64_t ui64DestAddr, uint32_t ui32FrameSize);
typedef ni_retcode_t (LIB_API* PNIP2PSEND) (ni_session_context_t *pSession, niFrameSurface1_t *source, uint64_t ui64DestAddr, uint32_t ui32FrameSize);
typedef int (LIB_API* PNICALCULATETOTALFRAMESIZE) (const ni_session_context_t *p_upl_ctx, const int linesize[]);
typedef ni_retcode_t (LIB_API* PNIRECONFIGSLICEARG) (ni_session_context_t *p_ctx, int16_t sliceArg);
typedef ni_retcode_t (LIB_API* PNIP2PRECV) (ni_session_context_t *pSession, const ni_p2p_sgl_t *dmaAddrs, ni_frame_t *pDstFrame);
typedef ni_retcode_t (LIB_API* PNIDEVICESESSIONRESTART) (ni_session_context_t *p_ctx, int video_width, int video_height, ni_device_type_t device_type);

/* End API function pointers */
 
 
 
/*
 * Definition of _NETINT_LIBXCODER_API_FUNCTION_LIST
 */
typedef struct _NETINT_LIBXCODER_API_FUNCTION_LIST
{
    //
    // API function list for ni_av_codec.h
    //
    PNISHOULDSENDSEIWITHFRAME            niShouldSendSeiWithFrame;             /** Client should access ::ni_should_send_sei_with_frame API through this pointer */
    PNIDECRETRIEVEAUXDATA                niDecRetrieveAuxData;                 /** Client should access ::ni_dec_retrieve_aux_data API through this pointer */
    PNIENCPREPAUXDATA                    niEncPrepAuxData;                     /** Client should access ::ni_enc_prep_aux_data API through this pointer */
    PNIENCCOPYAUXDATA                    niEncCopyAuxData;                     /** Client should access ::ni_enc_copy_aux_data API through this pointer */
    PNIENCWRITEFROMYUVBUFFER             niEncWriteFromYuvBuffer;              /** Client should access ::ni_enc_write_from_yuv_buffer API through this pointer */
    PNIEXTRACTCUSTOMSEI                  niExtractCustomSei;                   /** Client should access ::ni_extract_custom_sei API through this pointer */
    PNIDECPACKETPARSE                    niDecPacketParse;                     /** Client should access ::ni_dec_packet_parse API through this pointer */
    PNIEXPANDFRAME                       niExpandFrame;                        /** Client should access ::ni_expand_frame API through this pointer */
    PNIRECONFIGPPUOUTPUT                 niReconfigPpuOutput;                  /** Client should access ::ni_reconfig_ppu_output API through this pointer */
    PNIENCINSERTTIMECODE                 niEncInsertTimecode;                  /** Client should access ::ni_enc_insert_timecode API through this pointer */
    //
    // API function list for ni_util.h
    //
    PNIGETHWYUV420PDIM                   niGetHwYuv420PDim;                    /** Client should access ::ni_get_hw_yuv420p_dim API through this pointer */
    PNICOPYHWYUV420P                     niCopyHwYuv420P;                      /** Client should access ::ni_copy_hw_yuv420p API through this pointer */
    PNICOPYYUV444PTO420P                 niCopyYuv444PTo420P;                  /** Client should access ::ni_copy_yuv_444p_to_420p API through this pointer */
    PNIINSERTEMULATIONPREVENTBYTES       niInsertEmulationPreventBytes;        /** Client should access ::ni_insert_emulation_prevent_bytes API through this pointer */
    PNIREMOVEEMULATIONPREVENTBYTES       niRemoveEmulationPreventBytes;        /** Client should access ::ni_remove_emulation_prevent_bytes API through this pointer */
    PNIGETTIMEOFDAY                      niGettimeofday;                       /** Client should access ::ni_gettimeofday API through this pointer */
    PNIGETTIMENS                         niGettimeNs;                          /** Client should access ::ni_gettime_ns API through this pointer */
    PNIUSLEEP                            niUsleep;                             /** Client should access ::ni_usleep API through this pointer */
    PNISTRTOK                            niStrtok;                             /** Client should access ::ni_strtok API through this pointer */
    PNINETWORKLAYERCONVERTOUTPUT         niNetworkLayerConvertOutput;          /** Client should access ::ni_network_layer_convert_output API through this pointer */
    PNIAINETWORKLAYERSIZE                niAiNetworkLayerSize;                 /** Client should access ::ni_ai_network_layer_size API through this pointer */
    PNIAINETWORKLAYERDIMS                niAiNetworkLayerDims;                 /** Client should access ::ni_ai_network_layer_dims API through this pointer */
    PNINETWORKLAYERCONVERTTENSOR         niNetworkLayerConvertTensor;          /** Client should access ::ni_network_layer_convert_tensor API through this pointer */
    PNINETWORKCONVERTTENSORTODATA        niNetworkConvertTensorToData;         /** Client should access ::ni_network_convert_tensor_to_data API through this pointer */
    PNINETWORKCONVERTDATATOTENSOR        niNetworkConvertDataToTensor;         /** Client should access ::ni_network_convert_data_to_tensor API through this pointer */
    PNICALCULATESHA256                   niCalculateSha256;                    /** Client should access ::ni_calculate_sha256 API through this pointer */
    PNICOPYHWDESCRIPTORS                 niCopyHwDescriptors;                  /** Client should access ::ni_copy_hw_descriptors API through this pointer */
    PNIGETLIBXCODERAPIVER                niGetLibxcoderApiVer;                 /** Client should access ::ni_get_libxcoder_api_ver API through this pointer */
    PNIGETCOMPATFWAPIVER                 niGetCompatFwApiVer;                  /** Client should access ::ni_get_compat_fw_api_ver API through this pointer */
    PNIFMTFWAPIVERSTR                    niFmtFwApiVerStr;                     /** Client should access ::ni_fmt_fw_api_ver_str API through this pointer */
    PNICMPFWAPIVER                       niCmpFwApiVer;                        /** Client should access ::ni_cmp_fw_api_ver API through this pointer */
    PNIGETLIBXCODERRELEASEVER            niGetLibxcoderReleaseVer;             /** Client should access ::ni_get_libxcoder_release_ver API through this pointer */
    PNIGETRCTXT                          niGetRcTxt;                           /** Client should access ::ni_get_rc_txt API through this pointer */
    PNIPARAMGETKEYVALUE                  niParamGetKeyValue;                   /** Client should access ::ni_param_get_key_value API through this pointer */
    PNIRETRIEVEXCODERPARAMS              niRetrieveXcoderParams;               /** Client should access ::ni_retrieve_xcoder_params API through this pointer */
    PNIRETRIEVEXCODERGOP                 niRetrieveXcoderGop;                  /** Client should access ::ni_retrieve_xcoder_gop API through this pointer */
    PNIRETRIEVEDECODERPARAMS             niRetrieveDecoderParams;              /** Client should access ::ni_retrieve_decoder_params API through this pointer */
    PNIPTHREADMUTEXINIT                  niPthreadMutexInit;                   /** Client should access ::ni_pthread_mutex_init API through this pointer */
    PNIPTHREADMUTEXDESTROY               niPthreadMutexDestroy;                /** Client should access ::ni_pthread_mutex_destroy API through this pointer */
    PNIPTHREADMUTEXLOCK                  niPthreadMutexLock;                   /** Client should access ::ni_pthread_mutex_lock API through this pointer */
    PNIPTHREADMUTEXUNLOCK                niPthreadMutexUnlock;                 /** Client should access ::ni_pthread_mutex_unlock API through this pointer */
    PNIPTHREADCREATE                     niPthreadCreate;                      /** Client should access ::ni_pthread_create API through this pointer */
    PNIPTHREADJOIN                       niPthreadJoin;                        /** Client should access ::ni_pthread_join API through this pointer */
    PNIPTHREADCONDINIT                   niPthreadCondInit;                    /** Client should access ::ni_pthread_cond_init API through this pointer */
    PNIPTHREADCONDDESTROY                niPthreadCondDestroy;                 /** Client should access ::ni_pthread_cond_destroy API through this pointer */
    PNIPTHREADCONDBROADCAST              niPthreadCondBroadcast;               /** Client should access ::ni_pthread_cond_broadcast API through this pointer */
    PNIPTHREADCONDWAIT                   niPthreadCondWait;                    /** Client should access ::ni_pthread_cond_wait API through this pointer */
    PNIPTHREADCONDSIGNAL                 niPthreadCondSignal;                  /** Client should access ::ni_pthread_cond_signal API through this pointer */
    PNIPTHREADCONDTIMEDWAIT              niPthreadCondTimedwait;               /** Client should access ::ni_pthread_cond_timedwait API through this pointer */
    PNIPTHREADSIGMASK                    niPthreadSigmask;                     /** Client should access ::ni_pthread_sigmask API through this pointer */
    PNIPOSIXMEMALIGN                     niPosixMemalign;                      /** Client should access ::ni_posix_memalign API through this pointer */
    PNIAIERRNOTOSTR                      niAiErrnoToStr;                       /** Client should access ::ni_ai_errno_to_str API through this pointer */
    //
    // API function list for ni_device_api.h
    //
    PNIDEVICESESSIONCONTEXTALLOCINIT     niDeviceSessionContextAllocInit;      /** Client should access ::ni_device_session_context_alloc_init API through this pointer */
    PNIDEVICESESSIONCONTEXTINIT          niDeviceSessionContextInit;           /** Client should access ::ni_device_session_context_init API through this pointer */
    PNIDEVICESESSIONCONTEXTCLEAR         niDeviceSessionContextClear;          /** Client should access ::ni_device_session_context_clear API through this pointer */
    PNIDEVICESESSIONCONTEXTFREE          niDeviceSessionContextFree;           /** Client should access ::ni_device_session_context_free API through this pointer */
    PNICREATEEVENT                       niCreateEvent;                        /** Client should access ::ni_create_event API through this pointer */
    PNICLOSEEVENT                        niCloseEvent;                         /** Client should access ::ni_close_event API through this pointer */
    PNIDEVICEOPEN                        niDeviceOpen;                         /** Client should access ::ni_device_open API through this pointer */
    PNIDEVICECLOSE                       niDeviceClose;                        /** Client should access ::ni_device_close API through this pointer */
    PNIDEVICECAPABILITYQUERY             niDeviceCapabilityQuery;              /** Client should access ::ni_device_capability_query API through this pointer */
    PNIDEVICECAPABILITYQUERY2            niDeviceCapabilityQuery2;             /** Client should access ::ni_device_capability_query2 API through this pointer */
    PNIDEVICESESSIONOPEN                 niDeviceSessionOpen;                  /** Client should access ::ni_device_session_open API through this pointer */
    PNIDEVICESESSIONCLOSE                niDeviceSessionClose;                 /** Client should access ::ni_device_session_close API through this pointer */
    PNIDEVICESESSIONFLUSH                niDeviceSessionFlush;                 /** Client should access ::ni_device_session_flush API through this pointer */
    PNIDEVICEDECSESSIONSAVEHDRS          niDeviceDecSessionSaveHdrs;           /** Client should access ::ni_device_dec_session_save_hdrs API through this pointer */
    PNIDEVICEDECSESSIONFLUSH             niDeviceDecSessionFlush;              /** Client should access ::ni_device_dec_session_flush API through this pointer */
    PNIDEVICESESSIONWRITE                niDeviceSessionWrite;                 /** Client should access ::ni_device_session_write API through this pointer */
    PNIDEVICESESSIONREAD                 niDeviceSessionRead;                  /** Client should access ::ni_device_session_read API through this pointer */
    PNIDEVICESESSIONQUERY                niDeviceSessionQuery;                 /** Client should access ::ni_device_session_query API through this pointer */
    PNIDEVICESESSIONQUERYDETAIL          niDeviceSessionQueryDetail;           /** Client should access ::ni_device_session_query_detail API through this pointer */
    PNIDEVICESESSIONQUERYDETAILV1        niDeviceSessionQueryDetailV1;         /** Client should access ::ni_device_session_query_detail_v1 API through this pointer */
    PNIDEVICECONFIGNAMESPACENUM          niDeviceConfigNamespaceNum;           /** Client should access ::ni_device_config_namespace_num API through this pointer */
    PNIDEVICECONFIGQOS                   niDeviceConfigQos;                    /** Client should access ::ni_device_config_qos API through this pointer */
    PNIDEVICECONFIGQOSOP                 niDeviceConfigQosOp;                  /** Client should access ::ni_device_config_qos_op API through this pointer */
    PNIFRAMEBUFFERALLOC                  niFrameBufferAlloc;                   /** Client should access ::ni_frame_buffer_alloc API through this pointer */
    PNIFRAMEBUFFERALLOCDL                niFrameBufferAllocDl;                 /** Client should access ::ni_frame_buffer_alloc_dl API through this pointer */
    PNIDECODERFRAMEBUFFERALLOC           niDecoderFrameBufferAlloc;            /** Client should access ::ni_decoder_frame_buffer_alloc API through this pointer */
    PNIENCODERFRAMEBUFFERALLOC           niEncoderFrameBufferAlloc;            /** Client should access ::ni_encoder_frame_buffer_alloc API through this pointer */
    PNISCALERDESTFRAMEALLOC              niScalerDestFrameAlloc;               /** Client should access ::ni_scaler_dest_frame_alloc API through this pointer */
    PNISCALERINPUTFRAMEALLOC             niScalerInputFrameAlloc;              /** Client should access ::ni_scaler_input_frame_alloc API through this pointer */
    PNISCALERFRAMEPOOLALLOC              niScalerFramePoolAlloc;               /** Client should access ::ni_scaler_frame_pool_alloc API through this pointer */
    PNIFRAMEBUFFERALLOCNV                niFrameBufferAllocNv;                 /** Client should access ::ni_frame_buffer_alloc_nv API through this pointer */
    PNIENCODERSWFRAMEBUFFERALLOC         niEncoderSwFrameBufferAlloc;          /** Client should access ::ni_encoder_sw_frame_buffer_alloc API through this pointer */
    PNIFRAMEBUFFERFREE                   niFrameBufferFree;                    /** Client should access ::ni_frame_buffer_free API through this pointer */
    PNIDECODERFRAMEBUFFERFREE            niDecoderFrameBufferFree;             /** Client should access ::ni_decoder_frame_buffer_free API through this pointer */
    PNIDECODERFRAMEBUFFERPOOLRETURNBUF   niDecoderFrameBufferPoolReturnBuf;    /** Client should access ::ni_decoder_frame_buffer_pool_return_buf API through this pointer */
    PNIPACKETBUFFERALLOC                 niPacketBufferAlloc;                  /** Client should access ::ni_packet_buffer_alloc API through this pointer */
    PNICUSTOMPACKETBUFFERALLOC           niCustomPacketBufferAlloc;            /** Client should access ::ni_custom_packet_buffer_alloc API through this pointer */
    PNIPACKETBUFFERFREE                  niPacketBufferFree;                   /** Client should access ::ni_packet_buffer_free API through this pointer */
    PNIPACKETBUFFERFREEAV1               niPacketBufferFreeAv1;                /** Client should access ::ni_packet_buffer_free_av1 API through this pointer */
    PNIPACKETCOPY                        niPacketCopy;                         /** Client should access ::ni_packet_copy API through this pointer */
    PNIFRAMENEWAUXDATA                   niFrameNewAuxData;                    /** Client should access ::ni_frame_new_aux_data API through this pointer */
    PNIFRAMENEWAUXDATAFROMRAWDATA        niFrameNewAuxDataFromRawData;         /** Client should access ::ni_frame_new_aux_data_from_raw_data API through this pointer */
    PNIFRAMEGETAUXDATA                   niFrameGetAuxData;                    /** Client should access ::ni_frame_get_aux_data API through this pointer */
    PNIFRAMEFREEAUXDATA                  niFrameFreeAuxData;                   /** Client should access ::ni_frame_free_aux_data API through this pointer */
    PNIFRAMEWIPEAUXDATA                  niFrameWipeAuxData;                   /** Client should access ::ni_frame_wipe_aux_data API through this pointer */
    PNIENCODERINITDEFAULTPARAMS          niEncoderInitDefaultParams;           /** Client should access ::ni_encoder_init_default_params API through this pointer */
    PNIDECODERINITDEFAULTPARAMS          niDecoderInitDefaultParams;           /** Client should access ::ni_decoder_init_default_params API through this pointer */
    PNIENCODERPARAMSSETVALUE             niEncoderParamsSetValue;              /** Client should access ::ni_encoder_params_set_value API through this pointer */
    PNIDECODERPARAMSSETVALUE             niDecoderParamsSetValue;              /** Client should access ::ni_decoder_params_set_value API through this pointer */
    PNIENCODERGOPPARAMSSETVALUE          niEncoderGopParamsSetValue;           /** Client should access ::ni_encoder_gop_params_set_value API through this pointer */
    PNIDEVICESESSIONCOPY                 niDeviceSessionCopy;                  /** Client should access ::ni_device_session_copy API through this pointer */
    PNIDEVICESESSIONINITFRAMEPOOL        niDeviceSessionInitFramepool;         /** Client should access ::ni_device_session_init_framepool API through this pointer */
    PNIDEVICESESSIONREADHWDESC           niDeviceSessionReadHwdesc;            /** Client should access ::ni_device_session_read_hwdesc API through this pointer */
    PNIDEVICESESSIONHWDL                 niDeviceSessionHwdl;                  /** Client should access ::ni_device_session_hwdl API through this pointer */
    PNIDEVICESESSIONHWUP                 niDeviceSessionHwup;                  /** Client should access ::ni_device_session_hwup API through this pointer */
    PNIFRAMEBUFFERALLOCHWENC             niFrameBufferAllocHwenc;              /** Client should access ::ni_frame_buffer_alloc_hwenc API through this pointer */
    PNIHWFRAMEBUFFERRECYCLE              niHwframeBufferRecycle;               /** Client should access ::ni_hwframe_buffer_recycle API through this pointer */
    PNIHWFRAMEBUFFERRECYCLE2             niHwframeBufferRecycle2;              /** Client should access ::ni_hwframe_buffer_recycle2 API through this pointer */
    PNISCALERSETPARAMS                   niScalerSetParams;                    /** Client should access ::ni_scaler_set_params API through this pointer */
    PNIDEVICEALLOCFRAME                  niDeviceAllocFrame;                   /** Client should access ::ni_device_alloc_frame API through this pointer */
    PNIDEVICEALLOCDSTFRAME               niDeviceAllocDstFrame;                /** Client should access ::ni_device_alloc_dst_frame API through this pointer */
    PNIDEVICECLONEHWFRAME                niDeviceCloneHwframe;                 /** Client should access ::ni_device_clone_hwframe API through this pointer */
    PNIDEVICECONFIGFRAME                 niDeviceConfigFrame;                  /** Client should access ::ni_device_config_frame API through this pointer */
    PNIDEVICEMULTICONFIGFRAME            niDeviceMultiConfigFrame;             /** Client should access ::ni_device_multi_config_frame API through this pointer */
    PNIFRAMEBUFFERALLOCPIXFMT            niFrameBufferAllocPixfmt;             /** Client should access ::ni_frame_buffer_alloc_pixfmt API through this pointer */
    PNIAICONFIGNETWORKBINARY             niAiConfigNetworkBinary;              /** Client should access ::ni_ai_config_network_binary API through this pointer */
    PNIAICONFIGHVSPLUS                   niAiConfigHvsplus;                    /** Client should access ::ni_ai_config_hvsplus API through this pointer */
    PNIAIFRAMEBUFFERALLOC                niAiFrameBufferAlloc;                 /** Client should access ::ni_ai_frame_buffer_alloc API through this pointer */
    PNIAIPACKETBUFFERALLOC               niAiPacketBufferAlloc;                /** Client should access ::ni_ai_packet_buffer_alloc API through this pointer */
    PNIRECONFIGBITRATE                   niReconfigBitrate;                    /** Client should access ::ni_reconfig_bitrate API through this pointer */
    PNIRECONFIGVUI                       niReconfigVui;                        /** Client should access ::ni_reconfig_vui API through this pointer */
    PNIFORCEIDRFRAMETYPE                 niForceIdrFrameType;                  /** Client should access ::ni_force_idr_frame_type API through this pointer */
    PNISETLTR                            niSetLtr;                             /** Client should access ::ni_set_ltr API through this pointer */
    PNISETLTRINTERVAL                    niSetLtrInterval;                     /** Client should access ::ni_set_ltr_interval API through this pointer */
    PNISETFRAMEREFINVALID                niSetFrameRefInvalid;                 /** Client should access ::ni_set_frame_ref_invalid API through this pointer */
    PNIRECONFIGFRAMERATE                 niReconfigFramerate;                  /** Client should access ::ni_reconfig_framerate API through this pointer */
    PNIRECONFIGMAXFRAMESIZE              niReconfigMaxFrameSize;               /** Client should access ::ni_reconfig_max_frame_size API through this pointer */
    PNIRECONFIGMINMAXQP                  niReconfigMinMaxQp;                   /** Client should access ::ni_reconfig_min_max_qp API through this pointer */
    PNIDEVICESESSIONACQUIRE              niDeviceSessionAcquire;               /** Client should access ::ni_device_session_acquire API through this pointer */
    PNIDEVICESESSIONACQUIREFORREAD       niDeviceSessionAcquireForRead;        /** Client should access ::ni_device_session_acquire_for_read API through this pointer */
    PNIUPLOADERFRAMEBUFFERLOCK           niUploaderFrameBufferLock;            /** Client should access ::ni_uploader_frame_buffer_lock API through this pointer */
    PNIUPLOADERFRAMEBUFFERUNLOCK         niUploaderFrameBufferUnlock;          /** Client should access ::ni_uploader_frame_buffer_unlock API through this pointer */
    PNIUPLOADERP2PTESTSEND               niUploaderP2PTestSend;                /** Client should access ::ni_uploader_p2p_test_send API through this pointer */
    PNIUPLOADERP2PTESTLOAD               niUploaderP2PTestLoad;                /** Client should access ::ni_uploader_p2p_test_load API through this pointer */
    PNIENCODERSETINPUTFRAMEFORMAT        niEncoderSetInputFrameFormat;         /** Client should access ::ni_encoder_set_input_frame_format API through this pointer */
    PNIUPLOADERSETFRAMEFORMAT            niUploaderSetFrameFormat;             /** Client should access ::ni_uploader_set_frame_format API through this pointer */
    PNISCALERP2PFRAMEACQUIRE             niScalerP2PFrameAcquire;              /** Client should access ::ni_scaler_p2p_frame_acquire API through this pointer */
    PNIHWFRAMEP2PBUFFERRECYCLE           niHwframeP2PBufferRecycle;            /** Client should access ::ni_hwframe_p2p_buffer_recycle API through this pointer */
    PNIENCODERSESSIONREADSTREAMHEADER    niEncoderSessionReadStreamHeader;     /** Client should access ::ni_encoder_session_read_stream_header API through this pointer */
    PNIGETDMABUFFILEDESCRIPTOR           niGetDmaBufFileDescriptor;            /** Client should access ::ni_get_dma_buf_file_descriptor API through this pointer */
    PNIDEVICESESSIONSEQUENCECHANGE       niDeviceSessionSequenceChange;        /** Client should access ::ni_device_session_sequence_change API through this pointer */
    PNISCALERSETDRAWBOXPARAMS            niScalerSetDrawboxParams;             /** Client should access ::ni_scaler_set_drawbox_params API through this pointer */
    PNISCALERSETWATERMARKPARAMS          niScalerSetWatermarkParams;           /** Client should access ::ni_scaler_set_watermark_params API through this pointer */
    PNIAISESSIONREADMETRICS              niAiSessionReadMetrics;               /** Client should access ::ni_ai_session_read_metrics API through this pointer */
    PNIQUERYNVMESTATUS                   niQueryNvmeStatus;                    /** Client should access ::ni_query_nvme_status API through this pointer */
    PNIQUERYFLFWVERSIONS                 niQueryFlFwVersions;                  /** Client should access ::ni_query_fl_fw_versions API through this pointer */
    PNIQUERYVFNSID                       niQueryVfNsId;                        /** Client should access ::ni_query_vf_ns_id API through this pointer */
    PNIQUERYTEMPERATURE                  niQueryTemperature;                   /** Client should access ::ni_query_temperature API through this pointer */
    PNIQUERYEXTRAINFO                    niQueryExtraInfo;                     /** Client should access ::ni_query_extra_info API through this pointer */
    PNIENCODERFRAMEZEROCOPYCHECK         niEncoderFrameZerocopyCheck;          /** Client should access ::ni_encoder_frame_zerocopy_check API through this pointer */
    PNIENCODERFRAMEZEROCOPYBUFFERALLOC   niEncoderFrameZerocopyBufferAlloc;    /** Client should access ::ni_encoder_frame_zerocopy_buffer_alloc API through this pointer */
    PNIUPLOADERFRAMEZEROCOPYCHECK        niUploaderFrameZerocopyCheck;         /** Client should access ::ni_uploader_frame_zerocopy_check API through this pointer */
    PNIRECONFIGCRF                       niReconfigCrf;                        /** Client should access ::ni_reconfig_crf API through this pointer */
    PNIRECONFIGCRF2                      niReconfigCrf2;                       /** Client should access ::ni_reconfig_crf2 API through this pointer */
    PNIDEVICEALLOCANDGETFIRMWARELOGS     niDeviceAllocAndGetFirmwareLogs;      /** Client should access ::ni_device_alloc_and_get_firmware_logs API through this pointer */
    PNIRECONFIGVBVVALUE                  niReconfigVbvValue;                   /** Client should access ::ni_reconfig_vbv_value API through this pointer */
    PNIDEVICESESSIONUPDATEFRAMEPOOL      niDeviceSessionUpdateFramepool;       /** Client should access ::ni_device_session_update_framepool API through this pointer */
    PNIGETFRAMEDIM                       niGetFrameDim;                        /** Client should access ::ni_get_frame_dim API through this pointer */
    PNIGETMINFRAMEDIM                    niGetMinFrameDim;                     /** Client should access ::ni_get_min_frame_dim API through this pointer */
    PNICOPYFRAMEDATA                     niCopyFrameData;                      /** Client should access ::ni_copy_frame_data API through this pointer */
    PNIENCFRAMEBUFFERALLOC               niEncFrameBufferAlloc;                /** Client should access ::ni_enc_frame_buffer_alloc API through this pointer */
    PNISETDEMOROIMAP                     niSetDemoRoiMap;                      /** Client should access ::ni_set_demo_roi_map API through this pointer */
    PNIENCPREPRECONFDEMODATA             niEncPrepReconfDemoData;              /** Client should access ::ni_enc_prep_reconf_demo_data API through this pointer */
    PNIGOPPARAMSCHECKSET                 niGopParamsCheckSet;                  /** Client should access ::ni_gop_params_check_set API through this pointer */
    PNIGOPPARAMSCHECK                    niGopParamsCheck;                     /** Client should access ::ni_gop_params_check API through this pointer */
    PNIRECONFIGMAXFRAMESIZERATIO         niReconfigMaxFrameSizeRatio;          /** Client should access ::ni_reconfig_max_frame_size_ratio API through this pointer */
    PNIRECONFIGINTRAPRD                  niReconfigIntraprd;                   /** Client should access ::ni_reconfig_intraprd API through this pointer */
    PNIP2PXFER                           niP2PXfer;                            /** Client should access ::ni_p2p_xfer API through this pointer */
    PNIP2PSEND                           niP2PSend;                            /** Client should access ::ni_p2p_send API through this pointer */
    PNICALCULATETOTALFRAMESIZE           niCalculateTotalFrameSize;            /** Client should access ::ni_calculate_total_frame_size API through this pointer */
    PNIRECONFIGSLICEARG                  niReconfigSliceArg;                   /** Client should access ::ni_reconfig_slice_arg API through this pointer */
    PNIP2PRECV                           niP2PRecv;                            /** Client should access ::ni_p2p_recv API through this pointer */
    PNIDEVICESESSIONRESTART              niDeviceSessionRestart;               /** Client should access ::ni_device_session_restart API through this pointer */
    PNIDEVICESESSIONQUERYBUFFERAVAIL     niDeviceSessionQueryBufferAvail;      /** Client should access ::ni_device_session_query_buffer_avail API through this pointer */
} NETINT_LIBXCODER_API_FUNCTION_LIST;

class NETINTLibxcoderAPI {
public:
    // NiLibxcoderAPICreateInstance
    /**
     * Creates an instance of the NiLibxcoderAPI interface, and populates the
     * pFunctionList with function pointers to the API routines implemented by the
     * NiLibxcoderAPI interface.
     */
    static void NiLibxcoderAPICreateInstance(void *lib, NETINT_LIBXCODER_API_FUNCTION_LIST *functionList)
    {
        //
        // Function/symbol loading for ni_av_codec.h
        //
        functionList->niShouldSendSeiWithFrame = reinterpret_cast<decltype(ni_should_send_sei_with_frame)*>(dlsym(lib,"ni_should_send_sei_with_frame"));
        functionList->niDecRetrieveAuxData = reinterpret_cast<decltype(ni_dec_retrieve_aux_data)*>(dlsym(lib,"ni_dec_retrieve_aux_data"));
        functionList->niEncPrepAuxData = reinterpret_cast<decltype(ni_enc_prep_aux_data)*>(dlsym(lib,"ni_enc_prep_aux_data"));
        functionList->niEncCopyAuxData = reinterpret_cast<decltype(ni_enc_copy_aux_data)*>(dlsym(lib,"ni_enc_copy_aux_data"));
        functionList->niEncWriteFromYuvBuffer = reinterpret_cast<decltype(ni_enc_write_from_yuv_buffer)*>(dlsym(lib,"ni_enc_write_from_yuv_buffer"));
        functionList->niExtractCustomSei = reinterpret_cast<decltype(ni_extract_custom_sei)*>(dlsym(lib,"ni_extract_custom_sei"));
        functionList->niDecPacketParse = reinterpret_cast<decltype(ni_dec_packet_parse)*>(dlsym(lib,"ni_dec_packet_parse"));
        functionList->niExpandFrame = reinterpret_cast<decltype(ni_expand_frame)*>(dlsym(lib,"ni_expand_frame"));
        functionList->niReconfigPpuOutput = reinterpret_cast<decltype(ni_reconfig_ppu_output)*>(dlsym(lib,"ni_reconfig_ppu_output"));
        functionList->niEncInsertTimecode = reinterpret_cast<decltype(ni_enc_insert_timecode)*>(dlsym(lib,"ni_enc_insert_timecode"));
        //
        // Function/symbol loading for ni_util.h
        //
        functionList->niGetHwYuv420PDim = reinterpret_cast<decltype(ni_get_hw_yuv420p_dim)*>(dlsym(lib,"ni_get_hw_yuv420p_dim"));
        functionList->niCopyHwYuv420P = reinterpret_cast<decltype(ni_copy_hw_yuv420p)*>(dlsym(lib,"ni_copy_hw_yuv420p"));
        functionList->niCopyYuv444PTo420P = reinterpret_cast<decltype(ni_copy_yuv_444p_to_420p)*>(dlsym(lib,"ni_copy_yuv_444p_to_420p"));
        functionList->niInsertEmulationPreventBytes = reinterpret_cast<decltype(ni_insert_emulation_prevent_bytes)*>(dlsym(lib,"ni_insert_emulation_prevent_bytes"));
        functionList->niRemoveEmulationPreventBytes = reinterpret_cast<decltype(ni_remove_emulation_prevent_bytes)*>(dlsym(lib,"ni_remove_emulation_prevent_bytes"));
        functionList->niGettimeofday = reinterpret_cast<decltype(ni_gettimeofday)*>(dlsym(lib,"ni_gettimeofday"));
        functionList->niGettimeNs = reinterpret_cast<decltype(ni_gettime_ns)*>(dlsym(lib,"ni_gettime_ns"));
        functionList->niUsleep = reinterpret_cast<decltype(ni_usleep)*>(dlsym(lib,"ni_usleep"));
        functionList->niStrtok = reinterpret_cast<decltype(ni_strtok)*>(dlsym(lib,"ni_strtok"));
        functionList->niNetworkLayerConvertOutput = reinterpret_cast<decltype(ni_network_layer_convert_output)*>(dlsym(lib,"ni_network_layer_convert_output"));
        functionList->niAiNetworkLayerSize = reinterpret_cast<decltype(ni_ai_network_layer_size)*>(dlsym(lib,"ni_ai_network_layer_size"));
        functionList->niAiNetworkLayerDims = reinterpret_cast<decltype(ni_ai_network_layer_dims)*>(dlsym(lib,"ni_ai_network_layer_dims"));
        functionList->niNetworkLayerConvertTensor = reinterpret_cast<decltype(ni_network_layer_convert_tensor)*>(dlsym(lib,"ni_network_layer_convert_tensor"));
        functionList->niNetworkConvertTensorToData = reinterpret_cast<decltype(ni_network_convert_tensor_to_data)*>(dlsym(lib,"ni_network_convert_tensor_to_data"));
        functionList->niNetworkConvertDataToTensor = reinterpret_cast<decltype(ni_network_convert_data_to_tensor)*>(dlsym(lib,"ni_network_convert_data_to_tensor"));
        functionList->niCalculateSha256 = reinterpret_cast<decltype(ni_calculate_sha256)*>(dlsym(lib,"ni_calculate_sha256"));
        functionList->niCopyHwDescriptors = reinterpret_cast<decltype(ni_copy_hw_descriptors)*>(dlsym(lib,"ni_copy_hw_descriptors"));
        functionList->niGetLibxcoderApiVer = reinterpret_cast<decltype(ni_get_libxcoder_api_ver)*>(dlsym(lib,"ni_get_libxcoder_api_ver"));
        functionList->niGetCompatFwApiVer = reinterpret_cast<decltype(ni_get_compat_fw_api_ver)*>(dlsym(lib,"ni_get_compat_fw_api_ver"));
        functionList->niFmtFwApiVerStr = reinterpret_cast<decltype(ni_fmt_fw_api_ver_str)*>(dlsym(lib,"ni_fmt_fw_api_ver_str"));
        functionList->niCmpFwApiVer = reinterpret_cast<decltype(ni_cmp_fw_api_ver)*>(dlsym(lib,"ni_cmp_fw_api_ver"));
        functionList->niGetLibxcoderReleaseVer = reinterpret_cast<decltype(ni_get_libxcoder_release_ver)*>(dlsym(lib,"ni_get_libxcoder_release_ver"));
        functionList->niGetRcTxt = reinterpret_cast<decltype(ni_get_rc_txt)*>(dlsym(lib,"ni_get_rc_txt"));
        functionList->niParamGetKeyValue = reinterpret_cast<decltype(ni_param_get_key_value)*>(dlsym(lib,"ni_param_get_key_value"));
        functionList->niRetrieveXcoderParams = reinterpret_cast<decltype(ni_retrieve_xcoder_params)*>(dlsym(lib,"ni_retrieve_xcoder_params"));
        functionList->niRetrieveXcoderGop = reinterpret_cast<decltype(ni_retrieve_xcoder_gop)*>(dlsym(lib,"ni_retrieve_xcoder_gop"));
        functionList->niRetrieveDecoderParams = reinterpret_cast<decltype(ni_retrieve_decoder_params)*>(dlsym(lib,"ni_retrieve_decoder_params"));
        functionList->niPthreadMutexInit = reinterpret_cast<decltype(ni_pthread_mutex_init)*>(dlsym(lib,"ni_pthread_mutex_init"));
        functionList->niPthreadMutexDestroy = reinterpret_cast<decltype(ni_pthread_mutex_destroy)*>(dlsym(lib,"ni_pthread_mutex_destroy"));
        functionList->niPthreadMutexLock = reinterpret_cast<decltype(ni_pthread_mutex_lock)*>(dlsym(lib,"ni_pthread_mutex_lock"));
        functionList->niPthreadMutexUnlock = reinterpret_cast<decltype(ni_pthread_mutex_unlock)*>(dlsym(lib,"ni_pthread_mutex_unlock"));
        functionList->niPthreadCreate = reinterpret_cast<decltype(ni_pthread_create)*>(dlsym(lib,"ni_pthread_create"));
        functionList->niPthreadJoin = reinterpret_cast<decltype(ni_pthread_join)*>(dlsym(lib,"ni_pthread_join"));
        functionList->niPthreadCondInit = reinterpret_cast<decltype(ni_pthread_cond_init)*>(dlsym(lib,"ni_pthread_cond_init"));
        functionList->niPthreadCondDestroy = reinterpret_cast<decltype(ni_pthread_cond_destroy)*>(dlsym(lib,"ni_pthread_cond_destroy"));
        functionList->niPthreadCondBroadcast = reinterpret_cast<decltype(ni_pthread_cond_broadcast)*>(dlsym(lib,"ni_pthread_cond_broadcast"));
        functionList->niPthreadCondWait = reinterpret_cast<decltype(ni_pthread_cond_wait)*>(dlsym(lib,"ni_pthread_cond_wait"));
        functionList->niPthreadCondSignal = reinterpret_cast<decltype(ni_pthread_cond_signal)*>(dlsym(lib,"ni_pthread_cond_signal"));
        functionList->niPthreadCondTimedwait = reinterpret_cast<decltype(ni_pthread_cond_timedwait)*>(dlsym(lib,"ni_pthread_cond_timedwait"));
        functionList->niPthreadSigmask = reinterpret_cast<decltype(ni_pthread_sigmask)*>(dlsym(lib,"ni_pthread_sigmask"));
        functionList->niPosixMemalign = reinterpret_cast<decltype(ni_posix_memalign)*>(dlsym(lib,"ni_posix_memalign"));
        functionList->niAiErrnoToStr = reinterpret_cast<decltype(ni_ai_errno_to_str)*>(dlsym(lib,"ni_ai_errno_to_str"));
        //
        // Function/symbol loading for ni_device_api.h
        //
        functionList->niDeviceSessionContextAllocInit = reinterpret_cast<decltype(ni_device_session_context_alloc_init)*>(dlsym(lib,"ni_device_session_context_alloc_init"));
        functionList->niDeviceSessionContextInit = reinterpret_cast<decltype(ni_device_session_context_init)*>(dlsym(lib,"ni_device_session_context_init"));
        functionList->niDeviceSessionContextClear = reinterpret_cast<decltype(ni_device_session_context_clear)*>(dlsym(lib,"ni_device_session_context_clear"));
        functionList->niDeviceSessionContextFree = reinterpret_cast<decltype(ni_device_session_context_free)*>(dlsym(lib,"ni_device_session_context_free"));
        functionList->niCreateEvent = reinterpret_cast<decltype(ni_create_event)*>(dlsym(lib,"ni_create_event"));
        functionList->niCloseEvent = reinterpret_cast<decltype(ni_close_event)*>(dlsym(lib,"ni_close_event"));
        functionList->niDeviceOpen = reinterpret_cast<decltype(ni_device_open)*>(dlsym(lib,"ni_device_open"));
        functionList->niDeviceClose = reinterpret_cast<decltype(ni_device_close)*>(dlsym(lib,"ni_device_close"));
        functionList->niDeviceCapabilityQuery = reinterpret_cast<decltype(ni_device_capability_query)*>(dlsym(lib,"ni_device_capability_query"));
        functionList->niDeviceCapabilityQuery2 = reinterpret_cast<decltype(ni_device_capability_query2)*>(dlsym(lib,"ni_device_capability_query2"));
        functionList->niDeviceSessionOpen = reinterpret_cast<decltype(ni_device_session_open)*>(dlsym(lib,"ni_device_session_open"));
        functionList->niDeviceSessionClose = reinterpret_cast<decltype(ni_device_session_close)*>(dlsym(lib,"ni_device_session_close"));
        functionList->niDeviceSessionFlush = reinterpret_cast<decltype(ni_device_session_flush)*>(dlsym(lib,"ni_device_session_flush"));
        functionList->niDeviceDecSessionSaveHdrs = reinterpret_cast<decltype(ni_device_dec_session_save_hdrs)*>(dlsym(lib,"ni_device_dec_session_save_hdrs"));
        functionList->niDeviceDecSessionFlush = reinterpret_cast<decltype(ni_device_dec_session_flush)*>(dlsym(lib,"ni_device_dec_session_flush"));
        functionList->niDeviceSessionWrite = reinterpret_cast<decltype(ni_device_session_write)*>(dlsym(lib,"ni_device_session_write"));
        functionList->niDeviceSessionRead = reinterpret_cast<decltype(ni_device_session_read)*>(dlsym(lib,"ni_device_session_read"));
        functionList->niDeviceSessionQuery = reinterpret_cast<decltype(ni_device_session_query)*>(dlsym(lib,"ni_device_session_query"));
        functionList->niDeviceSessionQueryDetail = reinterpret_cast<decltype(ni_device_session_query_detail)*>(dlsym(lib,"ni_device_session_query_detail"));
        functionList->niDeviceSessionQueryDetailV1 = reinterpret_cast<decltype(ni_device_session_query_detail_v1)*>(dlsym(lib,"ni_device_session_query_detail_v1"));
        functionList->niDeviceConfigNamespaceNum = reinterpret_cast<decltype(ni_device_config_namespace_num)*>(dlsym(lib,"ni_device_config_namespace_num"));
        functionList->niDeviceConfigQos = reinterpret_cast<decltype(ni_device_config_qos)*>(dlsym(lib,"ni_device_config_qos"));
        functionList->niDeviceConfigQosOp = reinterpret_cast<decltype(ni_device_config_qos_op)*>(dlsym(lib,"ni_device_config_qos_op"));
        functionList->niFrameBufferAlloc = reinterpret_cast<decltype(ni_frame_buffer_alloc)*>(dlsym(lib,"ni_frame_buffer_alloc"));
        functionList->niFrameBufferAllocDl = reinterpret_cast<decltype(ni_frame_buffer_alloc_dl)*>(dlsym(lib,"ni_frame_buffer_alloc_dl"));
        functionList->niDecoderFrameBufferAlloc = reinterpret_cast<decltype(ni_decoder_frame_buffer_alloc)*>(dlsym(lib,"ni_decoder_frame_buffer_alloc"));
        functionList->niEncoderFrameBufferAlloc = reinterpret_cast<decltype(ni_encoder_frame_buffer_alloc)*>(dlsym(lib,"ni_encoder_frame_buffer_alloc"));
        functionList->niScalerDestFrameAlloc = reinterpret_cast<decltype(ni_scaler_dest_frame_alloc)*>(dlsym(lib,"ni_scaler_dest_frame_alloc"));
        functionList->niScalerInputFrameAlloc = reinterpret_cast<decltype(ni_scaler_input_frame_alloc)*>(dlsym(lib,"ni_scaler_input_frame_alloc"));
        functionList->niScalerFramePoolAlloc = reinterpret_cast<decltype(ni_scaler_frame_pool_alloc)*>(dlsym(lib,"ni_scaler_frame_pool_alloc"));
        functionList->niFrameBufferAllocNv = reinterpret_cast<decltype(ni_frame_buffer_alloc_nv)*>(dlsym(lib,"ni_frame_buffer_alloc_nv"));
        functionList->niEncoderSwFrameBufferAlloc = reinterpret_cast<decltype(ni_encoder_sw_frame_buffer_alloc)*>(dlsym(lib,"ni_encoder_sw_frame_buffer_alloc"));
        functionList->niFrameBufferFree = reinterpret_cast<decltype(ni_frame_buffer_free)*>(dlsym(lib,"ni_frame_buffer_free"));
        functionList->niDecoderFrameBufferFree = reinterpret_cast<decltype(ni_decoder_frame_buffer_free)*>(dlsym(lib,"ni_decoder_frame_buffer_free"));
        functionList->niDecoderFrameBufferPoolReturnBuf = reinterpret_cast<decltype(ni_decoder_frame_buffer_pool_return_buf)*>(dlsym(lib,"ni_decoder_frame_buffer_pool_return_buf"));
        functionList->niPacketBufferAlloc = reinterpret_cast<decltype(ni_packet_buffer_alloc)*>(dlsym(lib,"ni_packet_buffer_alloc"));
        functionList->niCustomPacketBufferAlloc = reinterpret_cast<decltype(ni_custom_packet_buffer_alloc)*>(dlsym(lib,"ni_custom_packet_buffer_alloc"));
        functionList->niPacketBufferFree = reinterpret_cast<decltype(ni_packet_buffer_free)*>(dlsym(lib,"ni_packet_buffer_free"));
        functionList->niPacketBufferFreeAv1 = reinterpret_cast<decltype(ni_packet_buffer_free_av1)*>(dlsym(lib,"ni_packet_buffer_free_av1"));
        functionList->niPacketCopy = reinterpret_cast<decltype(ni_packet_copy)*>(dlsym(lib,"ni_packet_copy"));
        functionList->niFrameNewAuxData = reinterpret_cast<decltype(ni_frame_new_aux_data)*>(dlsym(lib,"ni_frame_new_aux_data"));
        functionList->niFrameNewAuxDataFromRawData = reinterpret_cast<decltype(ni_frame_new_aux_data_from_raw_data)*>(dlsym(lib,"ni_frame_new_aux_data_from_raw_data"));
        functionList->niFrameGetAuxData = reinterpret_cast<decltype(ni_frame_get_aux_data)*>(dlsym(lib,"ni_frame_get_aux_data"));
        functionList->niFrameFreeAuxData = reinterpret_cast<decltype(ni_frame_free_aux_data)*>(dlsym(lib,"ni_frame_free_aux_data"));
        functionList->niFrameWipeAuxData = reinterpret_cast<decltype(ni_frame_wipe_aux_data)*>(dlsym(lib,"ni_frame_wipe_aux_data"));
        functionList->niEncoderInitDefaultParams = reinterpret_cast<decltype(ni_encoder_init_default_params)*>(dlsym(lib,"ni_encoder_init_default_params"));
        functionList->niDecoderInitDefaultParams = reinterpret_cast<decltype(ni_decoder_init_default_params)*>(dlsym(lib,"ni_decoder_init_default_params"));
        functionList->niEncoderParamsSetValue = reinterpret_cast<decltype(ni_encoder_params_set_value)*>(dlsym(lib,"ni_encoder_params_set_value"));
        functionList->niDecoderParamsSetValue = reinterpret_cast<decltype(ni_decoder_params_set_value)*>(dlsym(lib,"ni_decoder_params_set_value"));
        functionList->niEncoderGopParamsSetValue = reinterpret_cast<decltype(ni_encoder_gop_params_set_value)*>(dlsym(lib,"ni_encoder_gop_params_set_value"));
        functionList->niDeviceSessionCopy = reinterpret_cast<decltype(ni_device_session_copy)*>(dlsym(lib,"ni_device_session_copy"));
        functionList->niDeviceSessionInitFramepool = reinterpret_cast<decltype(ni_device_session_init_framepool)*>(dlsym(lib,"ni_device_session_init_framepool"));
        functionList->niDeviceSessionReadHwdesc = reinterpret_cast<decltype(ni_device_session_read_hwdesc)*>(dlsym(lib,"ni_device_session_read_hwdesc"));
        functionList->niDeviceSessionHwdl = reinterpret_cast<decltype(ni_device_session_hwdl)*>(dlsym(lib,"ni_device_session_hwdl"));
        functionList->niDeviceSessionQueryBufferAvail = reinterpret_cast<decltype(ni_device_session_query_buffer_avail)*>(dlsym(lib,"ni_device_session_query_buffer_avail"));
        functionList->niDeviceSessionHwup = reinterpret_cast<decltype(ni_device_session_hwup)*>(dlsym(lib,"ni_device_session_hwup"));
        functionList->niFrameBufferAllocHwenc = reinterpret_cast<decltype(ni_frame_buffer_alloc_hwenc)*>(dlsym(lib,"ni_frame_buffer_alloc_hwenc"));
        functionList->niHwframeBufferRecycle = reinterpret_cast<decltype(ni_hwframe_buffer_recycle)*>(dlsym(lib,"ni_hwframe_buffer_recycle"));
        functionList->niHwframeBufferRecycle2 = reinterpret_cast<decltype(ni_hwframe_buffer_recycle2)*>(dlsym(lib,"ni_hwframe_buffer_recycle2"));
        functionList->niScalerSetParams = reinterpret_cast<decltype(ni_scaler_set_params)*>(dlsym(lib,"ni_scaler_set_params"));
        functionList->niDeviceAllocFrame = reinterpret_cast<decltype(ni_device_alloc_frame)*>(dlsym(lib,"ni_device_alloc_frame"));
        functionList->niDeviceAllocDstFrame = reinterpret_cast<decltype(ni_device_alloc_dst_frame)*>(dlsym(lib,"ni_device_alloc_dst_frame"));
        functionList->niDeviceCloneHwframe = reinterpret_cast<decltype(ni_device_clone_hwframe)*>(dlsym(lib,"ni_device_clone_hwframe"));
        functionList->niDeviceConfigFrame = reinterpret_cast<decltype(ni_device_config_frame)*>(dlsym(lib,"ni_device_config_frame"));
        functionList->niDeviceMultiConfigFrame = reinterpret_cast<decltype(ni_device_multi_config_frame)*>(dlsym(lib,"ni_device_multi_config_frame"));
        functionList->niFrameBufferAllocPixfmt = reinterpret_cast<decltype(ni_frame_buffer_alloc_pixfmt)*>(dlsym(lib,"ni_frame_buffer_alloc_pixfmt"));
        functionList->niAiConfigNetworkBinary = reinterpret_cast<decltype(ni_ai_config_network_binary)*>(dlsym(lib,"ni_ai_config_network_binary"));
        functionList->niAiConfigHvsplus = reinterpret_cast<decltype(ni_ai_config_hvsplus)*>(dlsym(lib,"ni_ai_config_hvsplus"));
        functionList->niAiFrameBufferAlloc = reinterpret_cast<decltype(ni_ai_frame_buffer_alloc)*>(dlsym(lib,"ni_ai_frame_buffer_alloc"));
        functionList->niAiPacketBufferAlloc = reinterpret_cast<decltype(ni_ai_packet_buffer_alloc)*>(dlsym(lib,"ni_ai_packet_buffer_alloc"));
        functionList->niReconfigBitrate = reinterpret_cast<decltype(ni_reconfig_bitrate)*>(dlsym(lib,"ni_reconfig_bitrate"));
        functionList->niReconfigVui = reinterpret_cast<decltype(ni_reconfig_vui)*>(dlsym(lib,"ni_reconfig_vui"));
        functionList->niForceIdrFrameType = reinterpret_cast<decltype(ni_force_idr_frame_type)*>(dlsym(lib,"ni_force_idr_frame_type"));
        functionList->niSetLtr = reinterpret_cast<decltype(ni_set_ltr)*>(dlsym(lib,"ni_set_ltr"));
        functionList->niSetLtrInterval = reinterpret_cast<decltype(ni_set_ltr_interval)*>(dlsym(lib,"ni_set_ltr_interval"));
        functionList->niSetFrameRefInvalid = reinterpret_cast<decltype(ni_set_frame_ref_invalid)*>(dlsym(lib,"ni_set_frame_ref_invalid"));
        functionList->niReconfigFramerate = reinterpret_cast<decltype(ni_reconfig_framerate)*>(dlsym(lib,"ni_reconfig_framerate"));
        functionList->niReconfigMaxFrameSize = reinterpret_cast<decltype(ni_reconfig_max_frame_size)*>(dlsym(lib,"ni_reconfig_max_frame_size"));
        functionList->niReconfigMinMaxQp = reinterpret_cast<decltype(ni_reconfig_min_max_qp)*>(dlsym(lib,"ni_reconfig_min_max_qp"));
        functionList->niDeviceSessionAcquire = reinterpret_cast<decltype(ni_device_session_acquire)*>(dlsym(lib,"ni_device_session_acquire"));
        functionList->niDeviceSessionAcquireForRead = reinterpret_cast<decltype(ni_device_session_acquire_for_read)*>(dlsym(lib,"ni_device_session_acquire_for_read"));
        functionList->niUploaderFrameBufferLock = reinterpret_cast<decltype(ni_uploader_frame_buffer_lock)*>(dlsym(lib,"ni_uploader_frame_buffer_lock"));
        functionList->niUploaderFrameBufferUnlock = reinterpret_cast<decltype(ni_uploader_frame_buffer_unlock)*>(dlsym(lib,"ni_uploader_frame_buffer_unlock"));
        functionList->niUploaderP2PTestSend = reinterpret_cast<decltype(ni_uploader_p2p_test_send)*>(dlsym(lib,"ni_uploader_p2p_test_send"));
        functionList->niUploaderP2PTestLoad = reinterpret_cast<decltype(ni_uploader_p2p_test_load)*>(dlsym(lib,"ni_uploader_p2p_test_load"));
        functionList->niEncoderSetInputFrameFormat = reinterpret_cast<decltype(ni_encoder_set_input_frame_format)*>(dlsym(lib,"ni_encoder_set_input_frame_format"));
        functionList->niUploaderSetFrameFormat = reinterpret_cast<decltype(ni_uploader_set_frame_format)*>(dlsym(lib,"ni_uploader_set_frame_format"));
        functionList->niScalerP2PFrameAcquire = reinterpret_cast<decltype(ni_scaler_p2p_frame_acquire)*>(dlsym(lib,"ni_scaler_p2p_frame_acquire"));
        functionList->niHwframeP2PBufferRecycle = reinterpret_cast<decltype(ni_hwframe_p2p_buffer_recycle)*>(dlsym(lib,"ni_hwframe_p2p_buffer_recycle"));
        functionList->niEncoderSessionReadStreamHeader = reinterpret_cast<decltype(ni_encoder_session_read_stream_header)*>(dlsym(lib,"ni_encoder_session_read_stream_header"));
        functionList->niGetDmaBufFileDescriptor = reinterpret_cast<decltype(ni_get_dma_buf_file_descriptor)*>(dlsym(lib,"ni_get_dma_buf_file_descriptor"));
        functionList->niDeviceSessionSequenceChange = reinterpret_cast<decltype(ni_device_session_sequence_change)*>(dlsym(lib,"ni_device_session_sequence_change"));
        functionList->niScalerSetDrawboxParams = reinterpret_cast<decltype(ni_scaler_set_drawbox_params)*>(dlsym(lib,"ni_scaler_set_drawbox_params"));
        functionList->niScalerSetWatermarkParams = reinterpret_cast<decltype(ni_scaler_set_watermark_params)*>(dlsym(lib,"ni_scaler_set_watermark_params"));
        functionList->niAiSessionReadMetrics = reinterpret_cast<decltype(ni_ai_session_read_metrics)*>(dlsym(lib,"ni_ai_session_read_metrics"));
        functionList->niQueryNvmeStatus = reinterpret_cast<decltype(ni_query_nvme_status)*>(dlsym(lib,"ni_query_nvme_status"));
        functionList->niQueryFlFwVersions = reinterpret_cast<decltype(ni_query_fl_fw_versions)*>(dlsym(lib,"ni_query_fl_fw_versions"));
        functionList->niQueryVfNsId = reinterpret_cast<decltype(ni_query_vf_ns_id)*>(dlsym(lib,"ni_query_vf_ns_id"));
        functionList->niQueryTemperature = reinterpret_cast<decltype(ni_query_temperature)*>(dlsym(lib,"ni_query_temperature"));
        functionList->niQueryExtraInfo = reinterpret_cast<decltype(ni_query_extra_info)*>(dlsym(lib,"ni_query_extra_info"));
        functionList->niEncoderFrameZerocopyCheck = reinterpret_cast<decltype(ni_encoder_frame_zerocopy_check)*>(dlsym(lib,"ni_encoder_frame_zerocopy_check"));
        functionList->niEncoderFrameZerocopyBufferAlloc = reinterpret_cast<decltype(ni_encoder_frame_zerocopy_buffer_alloc)*>(dlsym(lib,"ni_encoder_frame_zerocopy_buffer_alloc"));
        functionList->niUploaderFrameZerocopyCheck = reinterpret_cast<decltype(ni_uploader_frame_zerocopy_check)*>(dlsym(lib,"ni_uploader_frame_zerocopy_check"));
        functionList->niReconfigCrf = reinterpret_cast<decltype(ni_reconfig_crf)*>(dlsym(lib,"ni_reconfig_crf"));
        functionList->niReconfigCrf2 = reinterpret_cast<decltype(ni_reconfig_crf2)*>(dlsym(lib,"ni_reconfig_crf2"));
        functionList->niDeviceAllocAndGetFirmwareLogs = reinterpret_cast<decltype(ni_device_alloc_and_get_firmware_logs)*>(dlsym(lib,"ni_device_alloc_and_get_firmware_logs"));
        functionList->niReconfigVbvValue = reinterpret_cast<decltype(ni_reconfig_vbv_value)*>(dlsym(lib,"ni_reconfig_vbv_value"));
        functionList->niDeviceSessionUpdateFramepool = reinterpret_cast<decltype(ni_device_session_update_framepool)*>(dlsym(lib,"ni_device_session_update_framepool"));
        functionList->niGetFrameDim = reinterpret_cast<decltype(ni_get_frame_dim)*>(dlsym(lib,"ni_get_frame_dim"));
        functionList->niGetMinFrameDim = reinterpret_cast<decltype(ni_get_min_frame_dim)*>(dlsym(lib,"ni_get_min_frame_dim"));
        functionList->niCopyFrameData = reinterpret_cast<decltype(ni_copy_frame_data)*>(dlsym(lib,"ni_copy_frame_data"));
        functionList->niEncFrameBufferAlloc = reinterpret_cast<decltype(ni_enc_frame_buffer_alloc)*>(dlsym(lib,"ni_enc_frame_buffer_alloc"));
        functionList->niSetDemoRoiMap = reinterpret_cast<decltype(ni_set_demo_roi_map)*>(dlsym(lib,"ni_set_demo_roi_map"));
        functionList->niEncPrepReconfDemoData = reinterpret_cast<decltype(ni_enc_prep_reconf_demo_data)*>(dlsym(lib,"ni_enc_prep_reconf_demo_data"));
        functionList->niGopParamsCheckSet = reinterpret_cast<decltype(ni_gop_params_check_set)*>(dlsym(lib,"ni_gop_params_check_set"));
        functionList->niGopParamsCheck = reinterpret_cast<decltype(ni_gop_params_check)*>(dlsym(lib,"ni_gop_params_check"));
        functionList->niReconfigMaxFrameSizeRatio = reinterpret_cast<decltype(ni_reconfig_max_frame_size_ratio)*>(dlsym(lib,"ni_reconfig_max_frame_size_ratio"));
        functionList->niReconfigIntraprd = reinterpret_cast<decltype(ni_reconfig_intraprd)*>(dlsym(lib,"ni_reconfig_intraprd"));
        functionList->niP2PXfer = reinterpret_cast<decltype(ni_p2p_xfer)*>(dlsym(lib,"ni_p2p_xfer"));
        functionList->niP2PSend = reinterpret_cast<decltype(ni_p2p_send)*>(dlsym(lib,"ni_p2p_send"));
        functionList->niCalculateTotalFrameSize = reinterpret_cast<decltype(ni_calculate_total_frame_size)*>(dlsym(lib,"ni_calculate_total_frame_size"));
        functionList->niReconfigSliceArg = reinterpret_cast<decltype(ni_reconfig_slice_arg)*>(dlsym(lib,"ni_reconfig_slice_arg"));
        functionList->niP2PRecv = reinterpret_cast<decltype(ni_p2p_recv)*>(dlsym(lib,"ni_p2p_recv"));
        functionList->niDeviceSessionRestart = reinterpret_cast<decltype(ni_device_session_restart)*>(dlsym(lib,"ni_device_session_restart"));
    }
};


#endif // _NETINTLIBXCODERAPI_H_

