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
 *  \file   ni_rsrc_priv.cpp
 *
 *  \brief  Private definitions used by ni_rsrc_api.cpp for management of
 *          NETINT video processing devices
 ******************************************************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <setjmp.h>

#if __linux__ || __APPLE__
#ifdef __OPENHARMONY__
#include <sys/ipc.h>
#include <sys/shm.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#endif

#if __APPLE__
#include <sys/syslimits.h>
#endif

#ifdef _ANDROID
#include "ni_rsrc_api_android.h"
#endif

#include "ni_device_api.h"
#include "ni_rsrc_api.h"
#include "ni_rsrc_priv.h"
#include "ni_nvme.h"
#include "ni_log.h"
#include "ni_util.h"

#if __linux__ || __APPLE__
#ifndef _ANDROID
jmp_buf env;
#endif
#endif

uint32_t g_xcoder_stop_process = 0;

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 ******************************************************************************/
ni_retcode_t ni_rsrc_fill_device_info(ni_device_info_t* p_device_info, ni_codec_t fmt, ni_device_type_t type,
  ni_hw_capability_t* p_hw_cap)
{
    int i;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_device_info)
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s() p_device_info is null\n", __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    ni_log(NI_LOG_DEBUG, "%s type %d fmt %d\n", __func__, type, fmt);

    for (i = 0; i < EN_CODEC_MAX; i++)
    {
        p_device_info->dev_cap[i].supports_codec = EN_INVALID;
    }

    if (NI_DEVICE_TYPE_DECODER == type)
    {
        p_device_info->dev_cap[0].supports_codec = EN_H264;
        p_device_info->dev_cap[0].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[0].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[0].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[0].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[0].profiles_supported,
                "Baseline, Main, High, High10", NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[0].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[1].supports_codec = EN_H265;
        p_device_info->dev_cap[1].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[1].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[1].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[1].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[1].profiles_supported, "Main, Main10",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[1].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[2].supports_codec = EN_JPEG;
        p_device_info->dev_cap[2].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[2].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[2].min_res_width = NI_MIN_RESOLUTION_WIDTH_JPEG;
        p_device_info->dev_cap[2].min_res_height =
            NI_MIN_RESOLUTION_HEIGHT_JPEG;

        strncpy(p_device_info->dev_cap[2].profiles_supported, "Baseline",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[2].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[3].supports_codec = EN_VP9;
        p_device_info->dev_cap[3].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[3].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[3].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[3].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[3].profiles_supported, "0, 2",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[3].level, "6.2", NI_LEVELS_SUPP_STR_LEN);
    } else if (NI_DEVICE_TYPE_ENCODER == type)
    { /*! encoder */
        p_device_info->dev_cap[0].supports_codec = EN_H264;
        p_device_info->dev_cap[0].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[0].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[0].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[0].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[0].profiles_supported,
                "Baseline, Main, High, High10", NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[0].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[1].supports_codec = EN_H265;
        p_device_info->dev_cap[1].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[1].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[1].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[1].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[1].profiles_supported, "Main, Main10",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[1].level, "6.2", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[2].supports_codec = EN_JPEG;
        p_device_info->dev_cap[2].max_res_width = p_hw_cap->max_video_width;
        p_device_info->dev_cap[2].max_res_height = p_hw_cap->max_video_height;
        p_device_info->dev_cap[2].min_res_width = NI_PARAM_MIN_WIDTH;
        p_device_info->dev_cap[2].min_res_height = NI_PARAM_MIN_HEIGHT;

        strncpy(p_device_info->dev_cap[2].profiles_supported, "Main",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[2].level, "5.1", NI_LEVELS_SUPP_STR_LEN);

        p_device_info->dev_cap[3].supports_codec = EN_AV1;
        p_device_info->dev_cap[3].max_res_width = NI_PARAM_AV1_MAX_WIDTH;
        p_device_info->dev_cap[3].max_res_height = NI_PARAM_AV1_MAX_HEIGHT;
        p_device_info->dev_cap[3].min_res_width = p_hw_cap->min_video_width;
        p_device_info->dev_cap[3].min_res_height = p_hw_cap->min_video_height;

        strncpy(p_device_info->dev_cap[3].profiles_supported, "Main",
                NI_PROFILES_SUPP_STR_LEN);
        strncpy(p_device_info->dev_cap[3].level, "5.1", NI_LEVELS_SUPP_STR_LEN);
    } else if (NI_DEVICE_TYPE_SCALER == type || NI_DEVICE_TYPE_AI == type)
    {
        p_device_info->dev_cap[0].supports_codec =
            p_device_info->dev_cap[1].supports_codec =
                p_device_info->dev_cap[2].supports_codec =
                    p_device_info->dev_cap[3].supports_codec = EN_INVALID;
    }
    strncpy(p_device_info->dev_cap[3].additional_info, "Set", NI_ADDITIONAL_INFO_STR_LEN);
END:

    return retval;
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_strcmp(const void * p_str, const void* p_str1)
{
    const char *l = (const char *)p_str;
    const char *r = (const char *)p_str1;
    int vl, vr;

    while (!isdigit(*l) && (*l) != '\0')
    {
        l++;
    }
  while (!isdigit(*r) && (*r) != '\0')
  {
    r++;
  }
  vl = atoi(l);
  vr = atoi(r);
  if (vl == vr)
  {
    return 0;
  }
  else if (vl < vr)
  {
    return -1;
  }
  else
  {
    return 1;
  }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_lock_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
    char type = g_device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    if (NULL != p_name)
    {
        if (strcmp(CODERS_SHM_NAME, "NI_QUADRA_SHM_CODERS") == 0)
        {
          snprintf(p_name, max_name_len, "%s/NI_QUADRA_lck_%c%d", LOCK_DIR, type, guid);
        }
        else
        {
          snprintf(p_name, max_name_len, "%s/NI_lck_%c%d", LOCK_DIR, type, guid);
        }
    }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_shm_name(ni_device_type_t device_type, int32_t guid, char* p_name, size_t max_name_len)
{
    char type = g_device_type_chr[GET_XCODER_DEVICE_TYPE(device_type)];
    /*! assume there is enough space allocated in name */
    if (NULL != p_name)
    {
        if (strcmp(CODERS_SHM_NAME, "NI_QUADRA_SHM_CODERS") == 0)
        {
          snprintf(p_name, max_name_len, "NI_QUADRA_shm_%c%d", type, guid);
        }
        else
        {
          snprintf(p_name, max_name_len, "NI_shm_%c%d", type, guid);
        }
    }
}

static void fill_device_info(ni_device_info_t *device_info,
                             const int module_id,
                             const ni_device_handle_t device_handle,
                             const char device_name[NI_MAX_DEVICE_NAME_LEN],
                             const int fw_ver_compat_warning,
                             ni_device_capability_t *device_capability,
                             const ni_device_type_t device_type)
{
    ni_hw_capability_t *hw_capability;

    hw_capability = &device_capability->xcoder_devices[device_type];

    strncpy(device_info->dev_name, device_name, (MAX_CHAR_IN_DEVICE_NAME-1));
    strncpy(device_info->blk_name, device_name, (MAX_CHAR_IN_DEVICE_NAME-1));
    device_info->hw_id = hw_capability->hw_id;
    device_info->module_id = module_id;
    device_info->fw_ver_compat_warning = fw_ver_compat_warning;

    memcpy(device_info->fw_rev,
           device_capability->fw_rev,
           sizeof(device_info->fw_rev));
    memcpy(device_info->fw_branch_name,
           device_capability->fw_branch_name,
           sizeof(device_info->fw_branch_name) - 1);
    memcpy(device_info->fw_commit_time,
           device_capability->fw_commit_time,
           sizeof(device_info->fw_commit_time) - 1);
    memcpy(device_info->fw_commit_hash,
           device_capability->fw_commit_hash,
           sizeof(device_info->fw_commit_hash) - 1);
    memcpy(device_info->fw_build_time,
           device_capability->fw_build_time,
           sizeof(device_info->fw_build_time) - 1);
    memcpy(device_info->fw_build_id,
           device_capability->fw_build_id,
           sizeof(device_info->fw_build_id) - 1);
    memcpy(device_info->serial_number,
           device_capability->serial_number,
           sizeof(device_info->serial_number));
    memcpy(device_info->model_number,
           device_capability->model_number,
           sizeof(device_info->model_number));

    ni_query_fl_fw_versions(device_handle, device_info);

    device_info->max_fps_4k = hw_capability->max_4k_fps;
    device_info->max_instance_cnt = hw_capability->max_number_of_contexts;
    device_info->device_type = device_type;

    ni_rsrc_fill_device_info(device_info,
                             (ni_codec_t)hw_capability->codec_format,
                             device_type,
                             hw_capability);
}

static void fill_shared_memory(ni_device_queue_t *device_queue,
                               const int should_match_rev,
                               const int existing_number_of_devices,
                               const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    int i, j, compatible_device_counter;

    memset(device_queue->xcoder_cnt, 0, sizeof(device_queue->xcoder_cnt));
    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        for (j = 0; j < NI_MAX_DEVICE_CNT; j++)
        {
            device_queue->xcoders[i][j] = -1;
        }
    }

    compatible_device_counter = 0;
    for (i = 0; i < existing_number_of_devices; i++)
    {
        if (!add_to_shared_memory(device_names[i],
                                 false,
                                 should_match_rev,
                                 device_queue))
        {
            continue;
        }
        compatible_device_counter++;
        if (compatible_device_counter >= NI_MAX_DEVICE_CNT)
        {
            ni_log(NI_LOG_INFO,
                   "Maximum number of supported and compatible devices "
                   "reached. Ignoring other supported and compatible "
                   "devices.\n");
            break;
        }
    }
}

bool find_available_guid(ni_device_queue_t *device_queue, int device_type, int *guidn)
{
    /*
    Create a mask of all 128 guids, mark used by 1 and unused by 0.
    */
    int32_t temp_guid;
    int32_t i, j;
    uint32_t guid_mask[4] = {0};

    for (i = 0; i < NI_MAX_DEVICE_CNT; i++)
    {
        temp_guid = device_queue->xcoders[device_type][i];
        if (temp_guid >= 0 && temp_guid < NI_MAX_DEVICE_CNT)
        {
            guid_mask[temp_guid / 32] |= (1u << ((uint32_t)temp_guid % 32));
        }
    }
    //from the masks find the first available guidn
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 32; j++)
        {
            if ((guid_mask[i] & (1u << j)) == 0)
            {
                *guidn = (i * 32) + j;
                return true;
            }
        }
    }

    return false;
}

bool add_to_shared_memory(const char device_name[NI_MAX_DEVICE_NAME_LEN],
                          const bool device_open_should_succeed,
                          const int should_match_rev,
                          ni_device_queue_t *device_queue)
{
    int32_t guid;
    int i, j, fw_compat_cmp;
    uint32_t max_io_size;
    char fw_api_ver_str[5];

    ni_device_capability_t device_capability = {0};
    ni_device_handle_t device_handle;
    ni_device_info_t device_info = {0};
    bool success = true;

    max_io_size = NI_INVALID_IO_SIZE;
    device_handle = ni_device_open(device_name, &max_io_size);
    if (device_handle == NI_INVALID_DEVICE_HANDLE)
    {
        if (device_open_should_succeed)
        {
            ni_log(NI_LOG_ERROR,
                   "ERROR: %s(): Failed to add %s\n: Failed ni_device_open()\n",
                   __FUNCTION__,
                   device_name);
            return false;
        }
        return true;
    }

    if (ni_device_capability_query2(device_handle, &device_capability, false) != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_INFO, "Skipping %s init: unable to query capability\n",
               device_name);
        LRETURN;
    }
    if (!is_supported_xcoder(device_capability.device_is_xcoder))
    {
        ni_log(NI_LOG_INFO, "Skipping %s init: model not supported\n", device_name);
        LRETURN;
    }
    ni_fmt_fw_api_ver_str((char*) &device_capability.fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                          &fw_api_ver_str[0]);
    if (should_match_rev && \
        ((uint8_t) device_capability.fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] != \
         (uint8_t) NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX]))
    {
        ni_log(NI_LOG_INFO,
               "Skipping %s init: device FW v%s incompatible with this version of Libxcoder\n",
               device_name,
               fw_api_ver_str);
        LRETURN;
    }
    fw_compat_cmp = ni_cmp_fw_api_ver((char*) &device_capability.fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
                                      (char*) &NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX]);

    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        if (!device_capability.xcoder_cnt[i])
        {
            ni_log(NI_LOG_DEBUG,
                   "%s %s disabled...\n",
                   device_name,
                   GET_XCODER_DEVICE_TYPE_STR(i));
            continue;
        }

        j = device_queue->xcoder_cnt[i];
        guid = j ? device_queue->xcoders[i][j-1] + 1 : 0;
        if (guid >= NI_MAX_DEVICE_CNT && !find_available_guid(device_queue, i, &guid))
        {
            ni_log(NI_LOG_ERROR, "ERROR: %s(): Skipping %s init: number of "
                   "initialized devices exceeds %d\n", __FUNCTION__,
                   device_name, NI_MAX_DEVICE_CNT);
            success = false;
            LRETURN;
        }
        device_queue->xcoders[i][j] = guid;
        device_queue->xcoder_cnt[i]++;

        fill_device_info(&device_info,
                         guid,
                         device_handle,
                         device_name,
                         (should_match_rev && fw_compat_cmp) ? 1 : 0,
                         &device_capability,
                         (ni_device_type_t)i);

        ni_rsrc_get_one_device_info(&device_info);
    }

    if (fw_compat_cmp < 0) {
        ni_log(NI_LOG_INFO, "Initialized %s with FW API v%s that is older than "
               "Libxcoder supported FW API version\n", device_name,
               fw_api_ver_str);
    } else if (fw_compat_cmp > 0) {
        ni_log(NI_LOG_INFO, "Initialized %s with FW API v%s that is newer than "
               "Libxcoder supported FW API version\n", device_name,
               fw_api_ver_str);
    } else {
        ni_log(NI_LOG_INFO, "Initialized %s\n", device_name, fw_api_ver_str);
    }

END:
    ni_device_close(device_handle);

    return success;
}

ni_retcode_t ni_rsrc_create_retry_lck()
{
#ifdef _WIN32
  return NI_RETCODE_SUCCESS;
#else
  size_t i;
  int retry_shm_fd;
  for(i = 0; i < sizeof(XCODERS_RETRY_LCK_NAME) / sizeof(XCODERS_RETRY_LCK_NAME[0]); ++i)
  {
    retry_shm_fd = open(XCODERS_RETRY_LCK_NAME[i], O_RDWR | O_CREAT | O_CLOEXEC | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if(retry_shm_fd < 0)
    {
      if(errno != EEXIST)
      {
        ni_log(NI_LOG_ERROR, "Failed to create %s ERROR: %s\n", XCODERS_RETRY_LCK_NAME[i], strerror(errno));
        return NI_RETCODE_ERROR_LOCK_DOWN_DEVICE;
      }
      else
      {
        ni_log(NI_LOG_DEBUG, "%s already exists\n", XCODERS_RETRY_LCK_NAME[i]);
      }
    }
    else
    {
      ni_log(NI_LOG_DEBUG, "Create XCODERS_RETRY_LCK: %s\n", XCODERS_RETRY_LCK_NAME[i]);
      close(retry_shm_fd);
    }
  }

  return NI_RETCODE_SUCCESS;

#endif
}

#ifdef _WIN32

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int ni_rsrc_enumerate_devices(
    char   ni_devices[][NI_MAX_DEVICE_NAME_LEN],
    int    max_handles
)
{
    return ni_nvme_enumerate_devices(ni_devices, max_handles);
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_one_device_info(ni_device_info_t* p_device_info)
{
  char shm_name[32] = { 0 };
    char lck_name[32] = { 0 };
  DWORD  rc = 0;
  ni_device_info_t * p_coder_info_map = NULL;
  HANDLE map_file_handle = NULL;
  ni_lock_handle_t mutex_handle = NULL;
  SECURITY_DESCRIPTOR security_descriptor = { 0 };

  if(!p_device_info)
  {
    return;
  }

  ni_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));
  ni_log(NI_LOG_DEBUG, "%s(): shm_name %s, lck_name %s\n", __func__, shm_name, lck_name);

  //Create a mutex for protecting the memory area
  mutex_handle = CreateMutex(NULL,        // default security attributes
                             FALSE,       // initially owned
                             lck_name);   // unnamed mutex
  if (NULL == mutex_handle)
  {
    ni_log(NI_LOG_ERROR, "CreateMutex error: %d\n", NI_ERRNO);
    LRETURN;
  }

  if (WAIT_ABANDONED == WaitForSingleObject(mutex_handle, INFINITE))
  {
    ni_log(NI_LOG_ERROR, "ERROR: ni_rsrc_get_device_context() failed to "
             "obtain mutex: %p\n", mutex_handle);
    LRETURN;
  }

  InitializeSecurityDescriptor(&security_descriptor, SECURITY_DESCRIPTOR_REVISION);
  //security_descriptor.Control

  map_file_handle = CreateFileMapping(
                      INVALID_HANDLE_VALUE,    // use paging file
                      NULL,                    // default security
                      PAGE_READWRITE,          // read/write access
                      0,                       // maximum object size (high-order DWORD)
                      sizeof(ni_device_info_t),// maximum object size (low-order DWORD)
                      (LPCSTR)shm_name         // name of mapping object
                    );

  if (NULL == map_file_handle)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_ERROR, "ERROR: CreateFileMapping returned (%d) for %s\n",
           rc, shm_name);
    LRETURN;
  }
  else
  {
    rc = NI_ERRNO;
    if (ERROR_ALREADY_EXISTS == rc)
    {
        ni_log(NI_LOG_ERROR, "CreateFileMapping returned existing handle for"
              " %s\n", shm_name);
    }
    else
    {
      ni_log(NI_LOG_INFO, "CreateFileMapping created a new mapFile for %s, handle: %p  ..\n", shm_name, map_file_handle);
    }
  }

  p_coder_info_map = (ni_device_info_t *) MapViewOfFile(
                              map_file_handle,   // handle to map object
                              FILE_MAP_ALL_ACCESS, // read/write permission
                              0,
                              0,
                              sizeof(ni_device_info_t)
                            );

  if (NULL == p_coder_info_map)
  {
    rc = NI_ERRNO;
    ni_log(NI_LOG_INFO, "Could not map view of file, p_last error (%d).\n", rc);
    LRETURN;
  }

  memcpy(p_coder_info_map, p_device_info, sizeof(ni_device_info_t));

END:
  if (p_coder_info_map)
  {
    UnmapViewOfFile(p_coder_info_map);
  }

  if (mutex_handle) {
    ReleaseMutex(mutex_handle);
    mutex_handle = NULL;
  }
}





/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t* p_device_context, ni_session_context_t* p_session_context)
{
    uint32_t i = 0;

    if ((!p_device_context) || (!p_session_context))
    {
        return;
    }

  p_device_context->p_device_info->load = p_session_context->load_query.current_load;
  p_device_context->p_device_info->active_num_inst = p_session_context->load_query.total_contexts;
  // Now we get the model load from the FW
  p_device_context->p_device_info->model_load = p_session_context->load_query.fw_model_load;
  if ( 0 == p_device_context->p_device_info->active_num_inst )
  {
    p_device_context->p_device_info->load = 0;
  }

  for (i = 0; i < p_device_context->p_device_info->active_num_inst; i++)
  {
    p_device_context->p_device_info->sw_instance[i].id =
      p_session_context->load_query.context_status[i].context_id;
    p_device_context->p_device_info->sw_instance[i].status = (ni_sw_instance_status_t)
      p_session_context->load_query.context_status[i].context_status;
    p_device_context->p_device_info->sw_instance[i].codec = (ni_codec_t)
      p_session_context->load_query.context_status[i].codec_format;
    p_device_context->p_device_info->sw_instance[i].width =
      p_session_context->load_query.context_status[i].video_width;
    p_device_context->p_device_info->sw_instance[i].height =
      p_session_context->load_query.context_status[i].video_height;
    p_device_context->p_device_info->sw_instance[i].fps =
      p_session_context->load_query.context_status[i].fps;
  }
}

/*!******************************************************************************
 *  \brief   Initialize and create all resources required to work with NETINT NVMe
 *           transcoder devices. This is a high level API function which is used
 *           mostly with user application like FFmpeg that relies on those resources.
 *           In case of custom application integration, revised functionality might
 *           be necessary utilizing corresponding API functions.
 *
 *  \param[in]   should_match_rev  0: transcoder firmware revision matching the
 *                             library's version is NOT required for placing
 *                             the transcoder into resource pool; 1: otherwise
 *               timeout_seconds   0: No timeout amount, loop until init success
 *                              or fail; else: timeout will fail init once reached
 *
 *  \return
 *           NI_RETCODE_SUCCESS on success
 *           NI_RETCODE_FAILURE on failure
 *
 *******************************************************************************/
int ni_rsrc_init_priv(const int should_match_rev,
                      const int existing_number_of_devices,
                      const char existing_device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    DWORD rc = 0;
    HANDLE lock = NULL;
    HANDLE map_file_handle = NULL;
    ni_device_queue_t* p_device_queue = NULL;
    map_file_handle = CreateFileMapping(
        INVALID_HANDLE_VALUE,     // use paging file
        NULL,                     // default security
        PAGE_READWRITE,           // read/write access
        0,                        // maximum object size (high-order DWORD)
        sizeof(ni_device_queue_t),// maximum object size (low-order DWORD)
        CODERS_SHM_NAME           // name of mapping object
    );

    if (NULL == map_file_handle)
    {
        rc = NI_ERRNO;
        ni_log(NI_LOG_ERROR, "ERROR: CreateFileMapping returned: %d\n", rc);
        return NI_RETCODE_FAILURE;
    }
    else
    {
        rc = NI_ERRNO;
        if(ERROR_ALREADY_EXISTS == rc)
        {
            ni_log(NI_LOG_INFO, "NETINT resources have been initialized already, exiting ..\n");
            CloseHandle(map_file_handle);
            return NI_RETCODE_SUCCESS;
        }
        else
        {
            ni_log(NI_LOG_INFO, "NETINT resources not initialized, starting initialization ..\n");
        }
    }

    p_device_queue = (ni_device_queue_t*)MapViewOfFile(
        map_file_handle,   // handle to map object
        FILE_MAP_ALL_ACCESS, // read/write permission
        0,
        0,
        sizeof(ni_device_queue_t)
    );

    if (NULL == p_device_queue)
    {
        ni_log(NI_LOG_ERROR, "Could not map view of file, p_last error (%d).\n",
            NI_ERRNO);
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
    }

    lock = CreateMutex(NULL, FALSE, CODERS_LCK_NAME);
    if (NULL == lock)
    {
        ni_log(NI_LOG_ERROR, "Init CreateMutex %s failed: %d\n", CODERS_LCK_NAME,
                NI_ERRNO);
        UnmapViewOfFile(p_device_queue);
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
    }

    if (WAIT_ABANDONED == WaitForSingleObject(lock, INFINITE))
    {
        ni_log(NI_LOG_ERROR, "ERROR %d: failed to obtain mutex: %p\n",
                NI_ERRNO, lock);
        ReleaseMutex(lock);
        UnmapViewOfFile(p_device_queue);
        CloseHandle(map_file_handle);
        return NI_RETCODE_FAILURE;
    }

    fill_shared_memory(p_device_queue,
                       should_match_rev,
                       existing_number_of_devices,
                       existing_device_names);

    UnmapViewOfFile(p_device_queue);
    ReleaseMutex(lock);
    return NI_RETCODE_SUCCESS;
}

#elif __linux__ || __APPLE__

/**
 * @brief This function is used to check if the existing device queue matches the expectation.
*/
static bool check_correctness_count(const ni_device_queue_t *existing_device_queue,
                                    const int should_match_rev,
                                    const int existing_number_of_devices,
                                    const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    uint32_t max_io_size;
    int i, j, k;

    ni_device_capability_t device_capability;
    ni_device_handle_t device_handle;
    ni_device_queue_t device_queue;

    memset(device_queue.xcoder_cnt, 0, sizeof(device_queue.xcoder_cnt));

    max_io_size = NI_INVALID_IO_SIZE;
    for (i = 0; i < existing_number_of_devices; i++)
    {
        device_handle = ni_device_open(device_names[i], &max_io_size);
        if (device_handle == NI_INVALID_DEVICE_HANDLE)
        {
            continue;
        }

        memset(&device_capability, 0, sizeof(ni_device_capability_t));
        if (ni_device_capability_query2(device_handle, &device_capability, false) != NI_RETCODE_SUCCESS || \
            !is_supported_xcoder(device_capability.device_is_xcoder) ||
            (should_match_rev && \
             ((uint8_t) device_capability.fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX] != \
              (uint8_t) NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX])))
        {
            goto NEXT;
        }

        for (j = NI_DEVICE_TYPE_DECODER; j < NI_DEVICE_TYPE_XCODER_MAX; j++)
        {
            //Don't count if not in queue.
            if (existing_device_queue->xcoders[j][i] == -1 && device_capability.xcoder_cnt[j] != 0)
            {
                //If not in queue then it shouldn't be in any device module.
                for (k = NI_DEVICE_TYPE_DECODER; k < NI_DEVICE_TYPE_XCODER_MAX; k++)
                {
                    if (existing_device_queue->xcoders[k][i] != -1)
                    {
                        ni_log(NI_LOG_ERROR,
                               "ERROR: %s(): Discovered device %s is not in queue for module %s but is in %s\n",
                               __func__,
                               device_names[i],
                               GET_XCODER_DEVICE_TYPE_STR(j), GET_XCODER_DEVICE_TYPE_STR(k));
                        ni_device_close(device_handle);
                        return false;
                    }
                }
                continue;
            }
            device_queue.xcoder_cnt[j] += device_capability.xcoder_cnt[j];
        }
NEXT:
        ni_device_close(device_handle);
    }

    for (i = NI_DEVICE_TYPE_DECODER; i < NI_DEVICE_TYPE_XCODER_MAX; i++)
    {
        if (device_queue.xcoder_cnt[i] == existing_device_queue->xcoder_cnt[i])
        {
            continue;
        }
        ni_log(NI_LOG_ERROR,
               "WARNING: %s(): Discovered %u %s, expected %u\n",
               __func__,
               device_queue.xcoder_cnt[i],
               GET_XCODER_DEVICE_TYPE_STR(i),
               existing_device_queue->xcoder_cnt[i]);
        return false;
    }

    return true;
}

static bool check_device_queue(const ni_device_queue_t *existing_device_queue,
                               const int existing_number_of_devices,
                               const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    int32_t module_id;
    int i, j;

    ni_device_context_t *device_context;

    for (i = 0; i < existing_number_of_devices; i++)
    {
        for (j = NI_DEVICE_TYPE_DECODER; j < NI_DEVICE_TYPE_XCODER_MAX; j++)
        {
            module_id = existing_device_queue->xcoders[j][i];
            if (module_id == -1)
            {
                break;
            }
            device_context = ni_rsrc_get_device_context((ni_device_type_t)j,
                                                        module_id);
            if (!device_context)
            {
               ni_log(NI_LOG_ERROR,
                      "WARNING: %s(): Missing device context for %s %s\n",
                      __func__,
                      device_names[i],
                      GET_XCODER_DEVICE_TYPE_STR(j));
               return false;
            }
            if (strcmp(device_context->p_device_info->dev_cap[3].additional_info, "Set") != 0)
            {
              ni_log(NI_LOG_ERROR, "ERROR %s() device info not populated %s\n", __func__, device_context->p_device_info->dev_cap[3].additional_info);
              return false;
            }
            ni_rsrc_free_device_context(device_context);
        }
    }

    return true;
}

#ifndef _ANDROID
static void sigbus_handler(int signal)
{
    siglongjmp(env, 1);
}

static void setup_signal_handler(struct sigaction *p, const int signum)
{
    struct sigaction c;

    memset(&c, 0, sizeof(struct sigaction));
    if (sigemptyset(&c.sa_mask) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not initialize signal set: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
    c.sa_handler = sigbus_handler;

    if (sigaction(signum, NULL, p) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not save previous signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }

    if (sigaction(signum, &c, NULL) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not register signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
}
#endif

static bool check_correctness(const ni_device_queue_t *existing_device_queue,
                              const int should_match_rev,
                              const int existing_number_of_devices,
                              const char device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN])
{
    bool result = false;
#ifndef _ANDROID
    const int signum = SIGBUS;
    struct sigaction p;

    setup_signal_handler(&p, signum);

    if (sigsetjmp(env, 1))
    {
        LRETURN;
    }
#endif

    if (!check_correctness_count(existing_device_queue,
                                 should_match_rev,
                                 existing_number_of_devices,
                                 device_names))
    {
        LRETURN;
    }

    if (!check_device_queue(existing_device_queue,
                            existing_number_of_devices,
                            device_names))
    {
        LRETURN;
    }

    result = true;
    ni_log(NI_LOG_INFO, "%s ok\n", CODERS_SHM_NAME);

end:
#ifndef _ANDROID
    if (sigaction(signum, &p, NULL) == -1)
    {
        ni_log(NI_LOG_ERROR,
               "ERROR: %s(): Could not restore previous signal handler: %d\n",
               __func__,
               NI_ERRNO);
        exit(EXIT_FAILURE);
    }
#endif
    return result;
}

int ni_rsrc_init_priv(const int should_match_rev,
                      const int existing_number_of_devices,
                      const char existing_device_names[NI_MAX_DEVICE_CNT][NI_MAX_DEVICE_NAME_LEN],
                      int limit_depth)
{
    int return_value = 0;
    int lck_fd = -1;
    int shm_fd = -1;
    ni_rsrc_shm_state state = NI_RSRC_SHM_IS_INVALID;
    int flags = O_CREAT | O_RDWR | O_CLOEXEC;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    ni_device_queue_t *p_device_queue = NULL;

    if ((ni_rsrc_try_get_shm_lock(CODERS_LCK_NAME, flags, mode, &lck_fd) < 0) ||
        (ni_rsrc_open_shm(CODERS_SHM_NAME, sizeof(ni_device_queue_t), &state, &shm_fd) < 0)) {
        return_value = 1;
        LRETURN;
    }

    if ((ni_rsrc_mmap_shm(CODERS_SHM_NAME,
                          shm_fd,
                          sizeof(ni_device_queue_t),
                          (void **)&p_device_queue)) < 0) {
        ni_log(NI_LOG_ERROR, "%s(): Failed to ni_rsrc_mmap_shm\n", __func__);
        return_value = 1;
        LRETURN;
    }

    //share momery exist
    if (NI_RSRC_SHM_IS_EXISTED == state) {
        if (check_correctness(p_device_queue,
                              should_match_rev,
                              existing_number_of_devices,
                              existing_device_names))
        {
            LRETURN;
        }

        ni_rsrc_munmap_shm((void *)p_device_queue, sizeof(ni_device_queue_t));
        ni_log(NI_LOG_DEBUG, "in %s do munmap for %s, shm_flag is O_RDWR\n", __func__, CODERS_SHM_NAME);

        if (lockf(lck_fd, F_ULOCK, 0) < 0) {
          ni_log(NI_LOG_ERROR, "%s(): Failed to unlock lck_fd for %s\n", __func__, CODERS_SHM_NAME);
        }

        close(lck_fd);

#ifndef __OPENHARMONY__
        close(shm_fd);
#endif
        ni_rsrc_remove_all_shm();

        if (limit_depth <= 0)
        {
            return 1;
        }
        return ni_rsrc_init_priv(should_match_rev,
                                 existing_number_of_devices,
                                 existing_device_names,
                                 limit_depth - 1);
    }

    fill_shared_memory(p_device_queue,
                       should_match_rev,
                       existing_number_of_devices,
                       existing_device_names);

end:
    if (p_device_queue && p_device_queue != MAP_FAILED) {
        ni_rsrc_munmap_shm((void *)p_device_queue, sizeof(ni_device_queue_t));
        p_device_queue = NULL;
        ni_log(NI_LOG_DEBUG, "in %s do munmap for %s\n", __func__, CODERS_SHM_NAME);
    }

    if (lck_fd != -1) {
      if (lockf(lck_fd, F_ULOCK, 0) < 0) {
        ni_log(NI_LOG_ERROR, "Will exit from %s(), but failed to unlock lck_fd for %s\n", __func__, CODERS_SHM_NAME);
      }

      close(lck_fd);
    }

#ifndef __OPENHARMONY__
    if (shm_fd >= 0) {
        close(shm_fd);
    }
#endif

    return return_value;
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_get_one_device_info (ni_device_info_t * p_device_info)
{
  int32_t shm_fd = -1;
  ni_rsrc_shm_state state = NI_RSRC_SHM_IS_INVALID;
  int32_t lock = -1;
  int flags = O_CREAT | O_RDWR | O_CLOEXEC;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
  char shm_name[32] = { 0 };
  char lck_name[32] = { 0 };
  ni_device_info_t * p_coder_info_dst = NULL;

  if(! p_device_info) {
    return;
  }

  ni_rsrc_get_shm_name(p_device_info->device_type, p_device_info->module_id, shm_name, sizeof(shm_name));
  ni_rsrc_get_lock_name(p_device_info->device_type, p_device_info->module_id, lck_name, sizeof(lck_name));

  ni_log(NI_LOG_DEBUG, "%s(): shm_name %s, lck_name %s\n", __func__, shm_name, lck_name);

  if (ni_rsrc_try_get_shm_lock(lck_name, flags, mode, (int *)&lock) < 0) {
    ni_log(NI_LOG_ERROR, "%s: Failed to get lock\n", __func__);
    return ;
  }

  if (ni_rsrc_open_shm(shm_name,
                       sizeof(ni_device_info_t),
                       &state,
                       (int *)&shm_fd) < 0) {
    ni_log(NI_LOG_ERROR, "%s: Failed to ni_rsrc_open_shm\n", __func__);
    LRETURN;
  }

  if ((ni_rsrc_mmap_shm(shm_name,
                        (int)shm_fd,
                        sizeof(ni_device_info_t),
                        (void **)&p_coder_info_dst)) < 0) {
    ni_log(NI_LOG_ERROR, "%s(): Failed to ni_rsrc_mmap_shm\n", __func__);
    LRETURN;
  }

  memcpy(p_coder_info_dst, p_device_info, sizeof(ni_device_info_t));

#ifndef __OPENHARMONY__
  if (msync((void*)p_coder_info_dst, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE)) {
      ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_coder_info_dst: %s\n",
             __func__, strerror(NI_ERRNO));
  } else {
    ni_log(NI_LOG_DEBUG, "%s(): written out\n", __func__);
  }
#endif

END:
  if (p_coder_info_dst && p_coder_info_dst != MAP_FAILED) {
    ni_rsrc_munmap_shm((void *)p_coder_info_dst, sizeof(ni_device_info_t));
    p_coder_info_dst = NULL;
    ni_log(NI_LOG_DEBUG, "in %s do munmap for %s\n", __func__, shm_name);
  }

#ifndef __OPENHARMONY__
  if (shm_fd >= 0) {
    close(shm_fd);
  }
#endif

  if (lockf(lock, F_ULOCK, 0) < 0) {
    ni_log(NI_LOG_ERROR, "Will exit from %s(), but failed to unlock lck_fd for %s\n", __func__, shm_name);
  }

  if (lock >= 0) {
      close(lock);
  }
}

/*!******************************************************************************
 *  \brief
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
void ni_rsrc_update_record(ni_device_context_t *p_device_context, ni_session_context_t *p_session_context)
{
    uint32_t j;

    if ((!p_device_context) || (!p_session_context))
    {
        return;
    }

  p_device_context->p_device_info->load = p_session_context->load_query.current_load;
  p_device_context->p_device_info->active_num_inst = p_session_context->load_query.total_contexts;
  // Now we get the model load from the FW
  p_device_context->p_device_info->model_load = p_session_context->load_query.fw_model_load;
  if (0 == p_device_context->p_device_info->active_num_inst)
  {
    p_device_context->p_device_info->load = 0;
  }
  for (j = 0; j < p_device_context->p_device_info->active_num_inst; j++)
  {
    p_device_context->p_device_info->sw_instance[j].id =
        p_session_context->load_query.context_status[j].context_id;
    p_device_context->p_device_info->sw_instance[j].status = (ni_sw_instance_status_t)
                                             p_session_context->load_query.context_status[j]
                                                 .context_status;
    p_device_context->p_device_info->sw_instance[j].codec = (ni_codec_t)
                                            p_session_context->load_query.context_status[j]
                                                .codec_format;
    p_device_context->p_device_info->sw_instance[j].width =
        p_session_context->load_query.context_status[j].video_width;
    p_device_context->p_device_info->sw_instance[j].height =
        p_session_context->load_query.context_status[j].video_height;
    p_device_context->p_device_info->sw_instance[j].fps =
        p_session_context->load_query.context_status[j].fps;
  }
  if (msync((void *)p_device_context->p_device_info, sizeof(ni_device_info_t), MS_SYNC | MS_INVALIDATE))
  {
      ni_log(NI_LOG_ERROR, "ERROR %s() msync() p_device_context->"
             "p_device_info: %s\n", __func__, strerror(NI_ERRNO));
  }
}


/*!******************************************************************************
 *  \brief     get PCIe address information from device name
 *
 *  \param[in] char *device_name e.g. /dev/nvme0n1.
 *  \param[out] char *pcie e.g. 0000:0a:00.0. Should be at least 13 bytes including null terminator
 *  \param[out] char *domain, optional. Should be at least 5 bytes including null terminator
 *  \param[out] char *slot, optional. Should be at least 3 bytes including null terminator
 *  \param[out] char *dev, optional. Should be at least 3 bytes including null terminator
 *  \param[out] char *func, optional. Should be at least 2 bytes including null terminator
 *
 *  \return    void
 *  *******************************************************************************/
void get_dev_pcie_addr(char *device_name,
                       char *pcie,
                       char *domain, char *slot, char *dev, char *func)
{
#ifndef __linux__
  return;
#else
  int i=0;
  char *ptr = NULL;
  // path to nvme drive
  char path[PATH_MAX];
  int ret;

  if(!device_name || !strstr(device_name, "/dev/nvme") || !pcie)
  {
    return ;
  }

  // we need to skip '/dev/' in the device name
  char *start = device_name + 5;

  // construct the path to /sys/block
  snprintf(path, sizeof(path), "/sys/block/%s", start);
  ni_log2(NULL, NI_LOG_DEBUG,"path:%s\n", path);

  // read the target of the symbolic link
  char target[PATH_MAX];
  //e.g.: ../devices/pci0000:00/0000:00:03.1/0000:09:00.0/nvme/nvme0/nvme0n1
  ssize_t len = readlink(path, target, sizeof(target) - 1);
  if (len == -1) {
      perror("readlink");
      return;
  }
  target[len] = '\0';  // set the null-terminating character
  ni_log2(NULL, NI_LOG_DEBUG,"target:%s\n", target);

  // and find domain and slot from it
  char *saveptr = NULL;
  ptr = ni_strtok(target, "/", &saveptr);
  pcie[4] = pcie[7] = ':';
  pcie[10] = '.';
  //last pcie info is for the device
  while(ptr != NULL) {
      ni_log2(NULL, NI_LOG_DEBUG, "===%d ptr:%s\n", ++i, ptr);
      if (strlen(ptr) == 12)//e.g.: 0000:09:00.0
      {
          ret = sscanf(ptr, "%4c:%2c:%2c.%1c", pcie, pcie+5,pcie+8,pcie+11);
          if (ret != 4)
          {
            ni_log2(NULL, NI_LOG_DEBUG, "\tsscanf error %d errno %d %s\n", ret, errno, strerror(errno));
          }
      }
      ni_log2(NULL, NI_LOG_DEBUG, "=====\n");
      ptr = ni_strtok(NULL, "/", &saveptr);
  }
  pcie[12] = '\0';
  ni_log2(NULL, NI_LOG_DEBUG, "PCIE:%s\n", pcie);
  if (!domain || !slot || !dev || !func)
  {
    goto end;
  }
  domain[4] = slot[2] = dev[2] = func[1] = '\0';
  sscanf(pcie, "%4[^:]:%2[^:]:%2[^.].%1s", domain, slot, dev, func);
  ni_log2(NULL, NI_LOG_DEBUG, "\t%d: Domain: %s, Slot: %s, Device: %s, Function: %s\n", i, domain, slot, dev, func);
end:
  return;
#endif
}

/*!******************************************************************************
 *  \brief   try to get lock for specified shared memory
 *
 *  \param[in] const char *lck_name, specified lock name for shared memory
 *  \param[in] int flags, open lock with specified flags
 *  \param[in] const mode_t mode, open lock with specified mode
 *  \param[out] int *lck_fd, Pointer to opened file handle for lock
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_rsrc_try_get_shm_lock(const char *lck_name,
                                      int flags,
                                      const mode_t mode,
                                      int *lck_fd)
{
  int lock = -1;

  if (!lck_name || !lck_fd) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef _ANDROID
  if (0 != access(LOCK_DIR, F_OK)) {
    if (0 != mkdir(LOCK_DIR, S_IRWXU | S_IRWXG | S_IRWXO)) {
      ni_log(NI_LOG_ERROR, "ERROR: Could not mkdir : %s directory", LOCK_DIR);
      return NI_RETCODE_FAILURE;
    }
  }
#endif

  lock = open(lck_name, flags, mode);
  if (lock < 0) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() open() %s fail: %s\n",
        __func__, lck_name, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  int retry_cnt = 0;
  //use non blocking F_TLOCK in case broken instance has indefinitely locked it
  while (lockf(lock, F_TLOCK, 0) != 0)
  {
    retry_cnt++;
    ni_usleep(LOCK_WAIT);    //10ms
    if (retry_cnt >= 900)   //10s
    {
      ni_log(NI_LOG_ERROR, "ERROR %s() lockf() %s fail: %s\n", __func__, lck_name, strerror(NI_ERRNO));
      ni_log(NI_LOG_ERROR, "ERROR %s() If persists, stop traffic and run rm /dev/shm/NI_*\n", __func__);
      close(lock);
      return NI_RETCODE_FAILURE;
    }
  }

  *lck_fd = lock;

  return NI_RETCODE_SUCCESS;
}

#if defined(__OPENHARMONY__)
static ni_retcode_t openharmony_open_shm(const char *shm_name,
                                         int shm_size,
                                         ni_rsrc_shm_state *state,
                                         int *shm_fd)
{
  int shm_id = -1;
  int flag = IPC_CREAT | IPC_EXCL;
  char shm_path[PATH_MAX];

  if (!shm_name || !shm_fd) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  memset(shm_path, 0, PATH_MAX);
  snprintf(shm_path, PATH_MAX, "%s/%s", LOCK_DIR, shm_name);

  //If file not exist, create the file for ftok
  if (0 != access(shm_path, F_OK)) {
    int fd = open(shm_path, O_RDWR | O_CREAT | O_CLOEXEC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (fd < 0) {
      ni_log(NI_LOG_ERROR, "ERROR: %s() open() %s fail: %s\n", __func__, shm_name, strerror(NI_ERRNO));
      return NI_RETCODE_FAILURE;
    }

    close(fd);
  }

  //create unique key for share memory
  key_t key = ftok(shm_path, PROJ_ID);
  if (key == -1) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() ftok() fail: %s\n", __func__, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  *state = NI_RSRC_SHM_IS_CREATED;

  //create share memory
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  shm_id = shmget(key, shm_size, mode | flag);
  if (shm_id < 0) {
    if (EEXIST == NI_ERRNO) {
      *state = NI_RSRC_SHM_IS_EXISTED;
      flag = IPC_CREAT;
      shm_id = shmget(key, shm_size, mode | flag);
    }

    if (shm_id < 0) {
      ni_log(NI_LOG_ERROR, "ERROR: %s() shmget() fail: %s\n", __func__, strerror(NI_ERRNO));
      return NI_RETCODE_FAILURE;
    }
  }
  *shm_fd = shm_id;

  return NI_RETCODE_SUCCESS;
}

static ni_retcode_t openharmony_remove_shm(const char *shm_name,
                                           int shm_size)
{
  int shm_id = -1;
  char shm_path[PATH_MAX];

  if (!shm_name) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  memset(shm_path, 0, PATH_MAX);
  snprintf(shm_path, PATH_MAX, "%s/%s", LOCK_DIR, shm_name);

  //If file not exist, exit directly
  if (0 != access(shm_path, F_OK)) {
    return NI_RETCODE_SUCCESS;
  }

  //create unique key for share memory
  key_t key = ftok(shm_path, PROJ_ID);
  if (key == -1) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() ftok() fail: %s\n", __func__, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  //create share memory
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  shm_id = shmget(key, shm_size, mode);
  if (shm_id < 0) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() shmget() fail: %s\n", __func__, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  shmctl(shm_id, IPC_RMID, nullptr);

  return NI_RETCODE_SUCCESS;
}

#elif defined(_ANDROID)
static ni_retcode_t android_open_shm(const char *shm_name,
                                     int shm_size,
                                     ni_rsrc_shm_state *state,
                                     int *shm_fd)
{
  int shm_fd_tmp = -1;

  if (!shm_name || !shm_fd) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  int ret = ni_rsrc_android_init();
  if (ret < 0) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed to get android service\n", __func__);
    return NI_RETCODE_FAILURE;
  }

  *state = NI_RSRC_SHM_IS_CREATED;

  string param = shm_name;
  Return<void> retvalue =
      service->GetAppFlag(param, [&](int32_t ret, hidl_handle handle) {
        if (ret > 0) {
          *state = NI_RSRC_SHM_IS_EXISTED;
          shm_fd_tmp = dup(handle->data[0]);
        } else {
          ni_log(NI_LOG_ERROR, "ERROR: failed to get shm_fd when call GetAppFlag\n");
        }
      });

  if (!retvalue.isOk()) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() maybe something is wrong with Android service\n");
    return NI_RETCODE_FAILURE;
  }

  if (shm_fd_tmp < 0) {
    int fd = ashmem_create_region(shm_name, shm_size);
    if (fd >= 0) {
      native_handle_t *native_handle = native_handle_create(1, 0);
      if (!native_handle) {
        close(fd);
        return NI_RETCODE_FAILURE;
      }
      native_handle->data[0] = fd;

      hidl_handle handle;
      handle.setTo(native_handle, true);
      service->SetAppFlag(param, handle);
      shm_fd_tmp = dup(fd);
    } else {
      ni_log(NI_LOG_ERROR, "Could not create ashmem under Android\n");
      return NI_RETCODE_FAILURE;
    }
  }
  *shm_fd = shm_fd_tmp;

  return NI_RETCODE_SUCCESS;
}

static ni_retcode_t android_remove_shm(const char *shm_name,
                                       int shm_size)
{
  if (!shm_name) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  int ret = ni_rsrc_android_init();
  if (ret < 0) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed to get android service\n", __func__);
    return NI_RETCODE_FAILURE;
  }

  string param = shm_name;
  Return<void> retvalue = service->RemoveAppFlag(param);
  if (!retvalue.isOk()) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed to remove shm\n", __func__);
    return NI_RETCODE_FAILURE;
  }

  return NI_RETCODE_SUCCESS;
}

static ni_retcode_t android_remove_all_shm()
{
  int ret = ni_rsrc_android_init();
  if (ret < 0) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed to get android service\n", __func__);
    return NI_RETCODE_FAILURE;
  }

  Return<void> retvalue = service->RemoveAllAppFlags();
  if (!retvalue.isOk()) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() failed to remove all shm\n", __func__);
    return NI_RETCODE_FAILURE;
  }

  return NI_RETCODE_SUCCESS;
}

#else
static ni_retcode_t linux_open_shm(const char *shm_name,
                                   int shm_size,
                                   ni_rsrc_shm_state *state,
                                   int *shm_fd)
{
  int shm_fd_tmp = -1;
  const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
  int flag = O_CREAT | O_EXCL | O_RDWR;
  bool skip_ftruncate = false;

  if (!shm_name || !shm_fd) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

  *state = NI_RSRC_SHM_IS_CREATED;

  //create share memory
  shm_fd_tmp = shm_open(shm_name, flag, mode);
  if (shm_fd_tmp < 0) {
    if (EEXIST == NI_ERRNO) {
      skip_ftruncate = true;
      *state = NI_RSRC_SHM_IS_EXISTED;
      flag = O_RDWR;
      shm_fd_tmp = shm_open(shm_name, flag, mode);
    }

    if (shm_fd_tmp < 0) {
      ni_log(NI_LOG_ERROR, "ERROR: %s() %s shm_open() fail: %s\n",
         __func__, shm_name, strerror(NI_ERRNO));
      return NI_RETCODE_FAILURE;
    }
  }

  //set share memory size
  if (!skip_ftruncate && ftruncate(shm_fd_tmp, shm_size) < 0) {
    close(shm_fd_tmp);
    shm_unlink(shm_name);
    return NI_RETCODE_FAILURE;
  }

  *shm_fd = shm_fd_tmp;

  return NI_RETCODE_SUCCESS;
}
#endif

/*!******************************************************************************
 *  \brief    open shared memory for specified shm_name
 *
 *  \param[in] const char *shm_name, specified shared memory name
 *  \param[in] int shm_size, the size of shared memory
 *  \param[out] ni_rsrc_shm_state *state, Pointer to the shared memory's state
 *              if the shared memor isn't created, it will be created at first and
 *              then open it, param "state" will be set to 0
 *              if the shared memor has been created yet, only open it,
 *              param "state" will be set to 1
 *  \param[out] int *shm_fd, Pointer to opened file handle for shared memory
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_rsrc_open_shm(const char *shm_name,
                              int shm_size,
                              ni_rsrc_shm_state *state,
                              int *shm_fd)
{
#if defined(__OPENHARMONY__)
  return openharmony_open_shm(shm_name, shm_size, state, shm_fd);
#elif defined(_ANDROID)
  return android_open_shm(shm_name, shm_size, state, shm_fd);
#else
  return linux_open_shm(shm_name, shm_size, state, shm_fd);
#endif
}

/*!******************************************************************************
 *  \brief   map shared memory to the address space of the calling process
 *
 *  \param[in] const char *shm_name, specified shared memory name
 *  \param[in] int shm_fd, file handle for shared memory
 *  \param[in] int shm_size, the size of shared memory
 *  \param[out] int *shm_addr, pointer to the mapped area
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_rsrc_mmap_shm(const char *shm_name,
                              int shm_fd,
                              int shm_size,
                              void **shm_addr)
{
  if (!shm_name || !shm_addr) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef __OPENHARMONY__
  *shm_addr = shmat(shm_fd, nullptr, 0);
  if ((void *)(-1) == *shm_addr) {
    ni_log(NI_LOG_ERROR, "%s(): %s shmat() fail: %s\n", __func__,
           shm_name, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }
#else
  *shm_addr = mmap(0, shm_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, shm_fd, 0);
  if (MAP_FAILED == *shm_addr) {
    ni_log(NI_LOG_ERROR, "%s(): %s mmap() fail: %s\n", __func__,
           shm_name, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }
#endif

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief   do munmap for shared memory
 *
 *  \param[in] int *shm_addr, pointer to the mapped area
 *  \param[in] int shm_size, the size of shared memory
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_rsrc_munmap_shm(void *shm_addr,
                                int shm_size)
{
  if (!shm_addr) {
    ni_log(NI_LOG_ERROR, "ERROR: %s() input params is invalid\n", __func__);
    return NI_RETCODE_INVALID_PARAM;
  }

#ifdef __OPENHARMONY__
  shmdt(shm_addr);
#else
  munmap(shm_addr, shm_size);
#endif

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief   delete shared memory
 *
 *  \param[in] const char *shm_name, specified shared memory name
 *  \param[in] int shm_size, the size of shared memory
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_rsrc_remove_shm(const char *shm_name,
                                int shm_size)
{
#ifdef __OPENHARMONY__
  return openharmony_remove_shm(shm_name, shm_size);
#elif defined(_ANDROID)
  return android_remove_shm(shm_name, shm_size);
#else
  shm_unlink(shm_name);
#endif

  return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief   delete all shared memory
 *
 *
 *  \return On success
 *                     NI_RETCODE_SUCCESS
 *          On failure
 *                     NI_RETCODE_INVALID_PARAM
 *                     NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_rsrc_remove_all_shm()
{
  DIR *dir;
  struct dirent *dirent;
  char path_to_remove[PATH_MAX];

  ni_log(NI_LOG_ERROR, "Deleting shared memory files in %s\n", LOCK_DIR);

  dir = opendir(LOCK_DIR);
  if (!dir) {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): opendir failed for %s: %s\n",
         __func__, LOCK_DIR, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  while ((dirent = readdir(dir)) != NULL) {
    if (strncmp(dirent->d_name, "NI_", 3) != 0) {
      continue;
    }

    snprintf(path_to_remove, PATH_MAX, "%s/%s", LOCK_DIR, dirent->d_name);

#ifndef _ANDROID
    if (strncasecmp(dirent->d_name, "NI_SHM", 6) == 0) {
#ifdef __OPENHARMONY__
      openharmony_remove_shm(dirent->d_name, 1); //for created share mem we can ignore size
#else
      shm_unlink(dirent->d_name);
#endif
    }
#endif

    remove(path_to_remove);
  }

#ifdef _ANDROID
  android_remove_all_shm();
#endif

  if (closedir(dir) == -1) {
    ni_log(NI_LOG_ERROR, "ERROR: %s(): closedir failed for %s: %s\n",
           __func__, LOCK_DIR, strerror(NI_ERRNO));
    return NI_RETCODE_FAILURE;
  }

  ni_log(NI_LOG_INFO, "Deleted shared memory files in %s\n", LOCK_DIR);

  return NI_RETCODE_SUCCESS;
}

#endif
