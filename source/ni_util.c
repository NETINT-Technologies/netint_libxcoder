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
 *  \file   ni_util.c
 *
 *  \brief  Utility definitions
 ******************************************************************************/

#if __linux__ || __APPLE__
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ni_nvme.h"
#include "ni_util.h"

typedef struct _ni_err_rc_txt_entry
{
    ni_retcode_t rc;
    const char *txt;
} ni_err_rc_txt_entry_t;

static const ni_err_rc_txt_entry_t ni_err_rc_description[] = {
    {NI_RETCODE_SUCCESS, "SUCCESS"},
    {NI_RETCODE_FAILURE, "FAILURE"},
    {NI_RETCODE_INVALID_PARAM, "INVALID_PARAM"},
    {NI_RETCODE_ERROR_MEM_ALOC, "ERROR_MEM_ALOC"},
    {NI_RETCODE_ERROR_NVME_CMD_FAILED, "ERROR_NVME_CMD_FAILED"},
    {NI_RETCODE_ERROR_INVALID_SESSION, "ERROR_INVALID_SESSION"},
    {NI_RETCODE_ERROR_RESOURCE_UNAVAILABLE, "ERROR_RESOURCE_UNAVAILABLE"},
    {NI_RETCODE_PARAM_INVALID_NAME, "PARAM_INVALID_NAME"},
    {NI_RETCODE_PARAM_INVALID_VALUE, "PARAM_INVALID_VALUE"},
    {NI_RETCODE_PARAM_ERROR_FRATE, "PARAM_ERROR_FRATE"},
    {NI_RETCODE_PARAM_ERROR_BRATE, "PARAM_ERROR_BRATE"},
    {NI_RETCODE_PARAM_ERROR_TRATE, "PARAM_ERROR_TRATE"},
    {NI_RETCODE_PARAM_ERROR_VBV_BUFFER_SIZE, "PARAM_ERROR_VBV_BUFFER_SIZE"},
    {NI_RETCODE_PARAM_ERROR_INTRA_PERIOD, "PARAM_ERROR_INTRA_PERIOD"},
    {NI_RETCODE_PARAM_ERROR_INTRA_QP, "PARAM_ERROR_INTRA_QP"},
    {NI_RETCODE_PARAM_ERROR_GOP_PRESET, "PARAM_ERROR_GOP_PRESET"},
    {NI_RETCODE_PARAM_ERROR_CU_SIZE_MODE, "PARAM_ERROR_CU_SIZE_MODE"},
    {NI_RETCODE_PARAM_ERROR_MX_NUM_MERGE, "PARAM_ERROR_MX_NUM_MERGE"},
    {NI_RETCODE_PARAM_ERROR_DY_MERGE_8X8_EN, "PARAM_ERROR_DY_MERGE_8X8_EN"},
    {NI_RETCODE_PARAM_ERROR_DY_MERGE_16X16_EN, "PARAM_ERROR_DY_MERGE_16X16_EN"},
    {NI_RETCODE_PARAM_ERROR_DY_MERGE_32X32_EN, "PARAM_ERROR_DY_MERGE_32X32_EN"},
    {NI_RETCODE_PARAM_ERROR_CU_LVL_RC_EN, "PARAM_ERROR_CU_LVL_RC_EN"},
    {NI_RETCODE_PARAM_ERROR_HVS_QP_EN, "PARAM_ERROR_HVS_QP_EN"},
    {NI_RETCODE_PARAM_ERROR_HVS_QP_SCL, "PARAM_ERROR_HVS_QP_SCL"},
    {NI_RETCODE_PARAM_ERROR_MN_QP, "PARAM_ERROR_MN_QP"},
    {NI_RETCODE_PARAM_ERROR_MX_QP, "PARAM_ERROR_MX_QP"},
    {NI_RETCODE_PARAM_ERROR_MX_DELTA_QP, "PARAM_ERROR_MX_DELTA_QP"},
    {NI_RETCODE_PARAM_ERROR_CONF_WIN_TOP, "PARAM_ERROR_CONF_WIN_TOP"},
    {NI_RETCODE_PARAM_ERROR_CONF_WIN_BOT, "PARAM_ERROR_CONF_WIN_BOT"},
    {NI_RETCODE_PARAM_ERROR_CONF_WIN_L, "PARAM_ERROR_CONF_WIN_L"},
    {NI_RETCODE_PARAM_ERROR_CONF_WIN_R, "PARAM_ERROR_CONF_WIN_R"},
    {NI_RETCODE_PARAM_ERROR_USR_RMD_ENC_PARAM, "PARAM_ERROR_USR_RMD_ENC_PARAM"},
    {NI_RETCODE_PARAM_ERROR_BRATE_LT_TRATE, "PARAM_ERROR_BRATE_LT_TRATE"},
    {NI_RETCODE_PARAM_ERROR_RCENABLE, "PARAM_ERROR_RCENABLE"},
    {NI_RETCODE_PARAM_ERROR_MAXNUMMERGE, "PARAM_ERROR_MAXNUMMERGE"},
    {NI_RETCODE_PARAM_ERROR_CUSTOM_GOP, "PARAM_ERROR_CUSTOM_GOP"},
    {NI_RETCODE_PARAM_ERROR_PIC_WIDTH, "PARAM_ERROR_PIC_WIDTH"},
    {NI_RETCODE_PARAM_ERROR_PIC_HEIGHT, "PARAM_ERROR_PIC_HEIGHT"},
    {NI_RETCODE_PARAM_ERROR_DECODING_REFRESH_TYPE, "PARAM_ERROR_DECODING_REFRESH_TYPE"},
    {NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_8X8_EN, "PARAM_ERROR_CUSIZE_MODE_8X8_EN"},
    {NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_16X16_EN, "PARAM_ERROR_CUSIZE_MODE_16X16_EN"},
    {NI_RETCODE_PARAM_ERROR_CUSIZE_MODE_32X32_EN, "PARAM_ERROR_CUSIZE_MODE_32X32_EN"},
    {NI_RETCODE_PARAM_ERROR_TOO_BIG, "PARAM_ERROR_TOO_BIG"},
    {NI_RETCODE_PARAM_ERROR_TOO_SMALL, "PARAM_ERROR_TOO_SMALL"},
    {NI_RETCODE_PARAM_ERROR_ZERO, "PARAM_ERROR_ZERO"},
    {NI_RETCODE_PARAM_ERROR_OOR, "PARAM_ERROR_OOR"},
    {NI_RETCODE_PARAM_ERROR_WIDTH_TOO_BIG, "PARAM_ERROR_WIDTH_TOO_BIG"},
    {NI_RETCODE_PARAM_ERROR_WIDTH_TOO_SMALL, "PARAM_ERROR_WIDTH_TOO_SMALL"},
    {NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_BIG, "PARAM_ERROR_HEIGHT_TOO_BIG"},
    {NI_RETCODE_PARAM_ERROR_HEIGHT_TOO_SMALL, "PARAM_ERROR_HEIGHT_TOO_SMALL"},
    {NI_RETCODE_PARAM_ERROR_AREA_TOO_BIG, "PARAM_ERROR_AREA_TOO_BIG"},
    {NI_RETCODE_ERROR_EXCEED_MAX_NUM_SESSIONS, "ERROR_EXCEED_MAX_NUM_SESSIONS"},
    {NI_RETCODE_ERROR_GET_DEVICE_POOL, "ERROR_GET_DEVICE_POOL"},
    {NI_RETCODE_ERROR_LOCK_DOWN_DEVICE, "ERROR_LOCK_DOWN_DEVICE"},
    {NI_RETCODE_ERROR_UNLOCK_DEVICE, "ERROR_UNLOCK_DEVICE"},
    {NI_RETCODE_ERROR_OPEN_DEVICE, "ERROR_OPEN_DEVICE"},
    {NI_RETCODE_ERROR_INVALID_HANDLE, "ERROR_INVALID_HANDLE"},
    {NI_RETCODE_ERROR_INVALID_ALLOCATION_METHOD, "ERROR_INVALID_ALLOCATION_METHOD"},
    {NI_RETCODE_ERROR_VPU_RECOVERY, "ERROR_VPU_RECOVERY"},
    {NI_RETCODE_ERROR_STREAM_ERROR, "ERROR_STREAM_ERROR"},

    {NI_RETCODE_PARAM_WARNING_DEPRECATED, "PARAM_WARNING_DEPRECATED"},
    {NI_RETCODE_PARAM_ERROR_LOOK_AHEAD_DEPTH, "PARAM_ERROR_LOOK_AHEAD_DEPTH"},
    {NI_RETCODE_PARAM_ERROR_FILLER, "PARAM_ERROR_FILLER"},
    {NI_RETCODE_PARAM_ERROR_PICSKIP, "PARAM_ERROR_PICSKIP"},

    {NI_RETCODE_PARAM_WARN, "PARAM_WARN"},

    {NI_RETCODE_NVME_SC_WRITE_BUFFER_FULL, "NVME_SC_WRITE_BUFFER_FULL"},
    {NI_RETCODE_NVME_SC_RESOURCE_UNAVAILABLE, "NVME_SC_RESOURCE_UNAVAILABLE"},
    {NI_RETCODE_NVME_SC_RESOURCE_IS_EMPTY, "NVME_SC_RESOURCE_IS_EMPTY"},
    {NI_RETCODE_NVME_SC_RESOURCE_NOT_FOUND, "NVME_SC_RESOURCE_NOT_FOUND"},
    {NI_RETCODE_NVME_SC_REQUEST_NOT_COMPLETED, "NVME_SC_REQUEST_NOT_COMPLETED"},
    {NI_RETCODE_NVME_SC_REQUEST_IN_PROGRESS, "NVME_SC_REQUEST_IN_PROGRESS"},
    {NI_RETCODE_NVME_SC_INVALID_PARAMETER, "NVME_SC_INVALID_PARAMETER"},
    {NI_RETCODE_NVME_SC_STREAM_ERROR, "NVME_SC_STREAM_ERROR"},
    {NI_RETCODE_NVME_SC_INTERLACED_NOT_SUPPORTED, "NVME_SC_INTERLACED_NOT_SUPPORTED"},
    {NI_RETCODE_NVME_SC_VPU_RECOVERY, "NVME_SC_VPU_RECOVERY"},
    {NI_RETCODE_NVME_SC_VPU_RSRC_INSUFFICIENT, "NVME_SC_VPU_RSRC_INSUFFICIENT"},
    {NI_RETCODE_NVME_SC_VPU_GENERAL_ERROR, "NVME_SC_VPU_GENERAL_ERROR"},
};

/*!*****************************************************************************
 *  \brief Get time for logs with microsecond timestamps
 *
 *  \param[in/out] p_tp   timeval struct
 *  \param[in] p_tzp      void *
 *
 *  \return return 0 for success, -1 for error
 ******************************************************************************/
int32_t ni_gettimeofday(struct timeval *p_tp, void *p_tzp)
{
#ifdef _WIN32
    FILETIME file_time;
    SYSTEMTIME system_time;
    ULARGE_INTEGER ularge;
    /*! FILETIME of Jan 1 1970 00:00:00. */
    static const unsigned __int64 epoch =
        ((unsigned __int64)116444736000000000ULL);

    // timezone information is stored outside the kernel so tzp isn't used
    (void *)p_tzp; /*!Remove compiler warnings*/

    // Note: this function is not a precision timer. See elapsed_time().
    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    ularge.LowPart = file_time.dwLowDateTime;
    ularge.HighPart = file_time.dwHighDateTime;
    // Surpress cppcheck
    (void)ularge.LowPart;
    (void)ularge.HighPart;
    p_tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
    p_tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

    return 0;
#else
    return gettimeofday(p_tp, p_tzp);
#endif
}

uint32_t ni_round_up(uint32_t number_to_round, uint32_t multiple)
{
    if (0 == multiple)
    {
        return number_to_round;
    }

    uint32_t remainder = number_to_round % multiple;
    if (0 == remainder)
    {
        return number_to_round;
    }

    return (number_to_round + multiple - remainder);
}

/*!*****************************************************************************
 *  \brief Allocate aligned memory
 *
 *  \param[in/out] memptr  The address of the allocated memory will be a
 *                         multiple of alignment, which must be a power of two
 *                         and a multiple of sizeof(void *).  If size is 0, then
 *                         the value placed is either NULL, or a unique pointer
 *                         value that can later be successfully passed to free.
 *  \param[in] alignment   The alignment value of the allocated value.
 *  \param[in] size        The allocated memory size.
 *
 *  \return                0 for success, ENOMEM for error
 ******************************************************************************/
int ni_posix_memalign(void **memptr, size_t alignment, size_t size)
{
#ifdef _WIN32
    *memptr = _aligned_malloc(size, alignment);
    if (NULL == *memptr)
    {
        return ENOMEM;
    } else
    {
        ZeroMemory(*memptr, size);
        return 0;
    }
#else
    return posix_memalign(memptr, alignment, size);
#endif
}

#ifdef __linux__
/*!******************************************************************************
 *  \brief  Get max io transfer size from the kernel
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
uint32_t ni_get_kernel_max_io_size(const char * p_dev)
{
    FILE *p_file = NULL; /* file pointer*/
    char file_name[KERNEL_NVME_FILE_NAME_MAX_SZ];
    int max_segments = 0, min_io_size = 0, max_hw_sectors_kb = 0;
    uint32_t io_size = DEFAULT_IO_TRANSFER_SIZE;
    size_t len = 0;
    int err = 0;

    memset(file_name, 0, KERNEL_NVME_FILE_NAME_MAX_SZ);

    if (!p_dev)
    {
        ni_log(NI_LOG_ERROR, "Invalid Arguments\n");
        LRETURN;
    }

    len = strlen(p_dev) - 5;
    if (len < MIN_NVME_DEV_NAME_LEN)
    {
        ni_log(NI_LOG_DEBUG, "p_dev length is %zu\n", len);
        LRETURN;
    }

    // Get Max number of segments from /sys
    memset(file_name, 0, sizeof(file_name));
    strcpy(file_name, SYS_PARAMS_PREFIX_PATH);
#if defined(_ANDROID) || defined(__OPENHARMONY__)
    //start from 11 chars ahead to not copy the "/dev/block/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 11), sizeof(file_name) - SYS_PREFIX_SZ);
#else
    //start from 5 chars ahead to not copy the "/dev/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 5), sizeof(file_name) - SYS_PREFIX_SZ);
#endif
    strncat(file_name, KERNEL_NVME_MAX_SEG_PATH,
            sizeof(file_name) - SYS_PREFIX_SZ - len);
    ni_log(NI_LOG_DEBUG, "file_name  is %s\n", file_name);
    p_file = fopen(file_name, "r");
    if (!p_file)
    {
        ni_log(NI_LOG_ERROR, "file_name failed to open: %s\n", file_name);
        LRETURN;
    }

    err = fscanf(p_file, "%d", &max_segments);
    if (EOF == err)
    {
        ni_log(NI_LOG_ERROR, "fscanf failed on: %s max_segments\n", file_name);
        LRETURN;
    }

    fclose(p_file);
    p_file = NULL;
    // Get Max segment size from /sys
    memset(file_name, 0, sizeof(file_name));
    strcpy(file_name, SYS_PARAMS_PREFIX_PATH);
#if defined(_ANDROID) || defined(__OPENHARMONY__)
    //start from 11 chars ahead to not copy the "/dev/block/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 11), sizeof(file_name) - SYS_PREFIX_SZ);
#else
    //start from 5 chars ahead to not copy the "/dev/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 5), sizeof(file_name) - SYS_PREFIX_SZ);
#endif
    strncat(file_name, KERNEL_NVME_MIN_IO_SZ_PATH,
            sizeof(file_name) - SYS_PREFIX_SZ - len);
    ni_log(NI_LOG_DEBUG, "file_name  is %s\n", file_name);
    p_file = fopen(file_name, "r");
    if (!p_file)
    {
        ni_log(NI_LOG_ERROR, "file_name  failed to open: %s\n", file_name);
        LRETURN;
    }

    err = fscanf(p_file, "%d", &min_io_size);
    if (EOF == err)
    {
        ni_log(NI_LOG_ERROR, "fscanf failed on: %s min_io_size\n", file_name);
        LRETURN;
    }

    fclose(p_file);
    p_file = NULL;
    //Now get max_hw_sectors_kb
    memset(file_name, 0, sizeof(file_name));
    strcpy(file_name, SYS_PARAMS_PREFIX_PATH);
#if defined(_ANDROID) || defined(__OPENHARMONY__)
    //start from 11 chars ahead to not copy the "/dev/block/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 11), sizeof(file_name) - SYS_PREFIX_SZ);
#else
    //start from 5 chars ahead to not copy the "/dev/" since we only need whats after it
    strncat(file_name, (char *)(p_dev + 5), sizeof(file_name) - SYS_PREFIX_SZ);
#endif
    strncat(file_name, KERNEL_NVME_MAX_HW_SEC_KB_PATH,
            sizeof(file_name) - SYS_PREFIX_SZ - len);
    ni_log(NI_LOG_DEBUG, "file_name  is %s\n", file_name);
    p_file = fopen(file_name, "r");
    if (!p_file)
    {
        ni_log(NI_LOG_ERROR, "file_name  failed to open: %s\n", file_name);
        LRETURN;
    }

    err = fscanf(p_file, "%d", &max_hw_sectors_kb);
    if (EOF == err)
    {
        ni_log(NI_LOG_ERROR, "fscanf failed on: %s min_io_size\n", file_name);
        LRETURN;
    }

    if (ni_min(min_io_size * max_segments, max_hw_sectors_kb * 1024) >
        MAX_IO_TRANSFER_SIZE)
    {
        io_size = MAX_IO_TRANSFER_SIZE;
        //ni_log(NI_LOG_INFO, "max_io_size is set to: %d because its bigger than maximum limit of: %d\n",io_size, MAX_IO_TRANSFER_SIZE);
    } else
    {
        io_size = ni_min(min_io_size * max_segments, max_hw_sectors_kb * 1024);
    }

    // ni_log(NI_LOG_INFO, "\nMAX NVMe IO Size of %d was calculated for this platform and will
    // be used unless overwritten by user settings\n",io_size);
    fflush(stdout);

END:

    if (p_file)
    {
        fclose(p_file);
    }

    return io_size;
}

#endif

void ni_usleep(int64_t usec)
{
#ifdef _WIN32
    if (usec < 5000)   //this will be more accurate when less than 5000
    {
        LARGE_INTEGER StartCount;
        LARGE_INTEGER StopCount;
        LARGE_INTEGER Frequency;
        QueryPerformanceCounter(&StartCount);
        QueryPerformanceFrequency(&Frequency);
        StopCount.QuadPart =
            StartCount.QuadPart + usec * (Frequency.QuadPart / 1000000);
        do
        {
            QueryPerformanceCounter(&StartCount);
        } while (StartCount.QuadPart < StopCount.QuadPart);
    } else
    {
        HANDLE timer = NULL;
        LARGE_INTEGER ft = {0};
        BOOL retval;

        ft.QuadPart = -(
            10 *
            usec);   // Convert to 100 nanosecond interval, negative value indicates relative time

        timer = CreateWaitableTimer(NULL, TRUE, NULL);
        if (NULL != timer)
        {
            retval = SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
            if (retval)
            {
                WaitForSingleObject(timer, INFINITE);
            }
            CloseHandle(timer);
        } else
        {
            ni_log(NI_LOG_ERROR, "failed to create waitable timer\n");
            LARGE_INTEGER StartCount;
            LARGE_INTEGER StopCount;
            LARGE_INTEGER Frequency;
            QueryPerformanceCounter(&StartCount);
            QueryPerformanceFrequency(&Frequency);
            StopCount.QuadPart =
                StartCount.QuadPart + usec * (Frequency.QuadPart / 1000000);
            do
            {
                QueryPerformanceCounter(&StartCount);
            } while (StartCount.QuadPart < StopCount.QuadPart);
        }
    }
#else
    if (usec < 0xFFFFFFFF)   // to avoid overflow
    {
        usleep(usec);
    } else
    {
        sleep(usec >> 32);
    }
#endif
}

char *ni_strtok(char *s, const char *delim, char **saveptr)
{
    char *tok;

    if (!s && !(s = *saveptr))
        return NULL;

    /* skip leading delimiters */
    s += strspn(s, delim);

    /* s now points to the first non delimiter char, or to the end of the string */
    if (!*s) {
        *saveptr = NULL;
        return NULL;
    }
    tok = s++;

    /* skip non delimiters */
    s += strcspn(s, delim);
    if (*s) {
        *s = '\0';
        *saveptr = s+1;
    } else {
        *saveptr = NULL;
    }

    return tok;
}

// memory buffer pool operations (one use is for decoder frame buffer pool)
// expand buffer pool by a pre-defined size
ni_buf_t *ni_buf_pool_expand(ni_buf_pool_t *pool)
{
  int32_t i;
  for (i = 0; i < NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND; i++)
  {
    if( NULL == ni_buf_pool_allocate_buffer(pool, pool->buf_size) )
    {
        ni_log(NI_LOG_FATAL, "FATAL: Failed to expand ni_buf_pool buffer: %p, "
               "current size: %u\n", pool, pool->number_of_buffers);
        return NULL;
    }
  }
  pool->number_of_buffers += NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND;
  return pool->p_free_head;
}

// get a free memory buffer from the pool
ni_buf_t *ni_buf_pool_get_buffer(ni_buf_pool_t *p_buffer_pool)
{
    ni_buf_t *buf = NULL;

    if (NULL == p_buffer_pool)
    {
        return NULL;
    }

    ni_pthread_mutex_lock(&p_buffer_pool->mutex);
    buf = p_buffer_pool->p_free_head;

    // find and return a free buffer
    if (NULL == buf)
    {
        ni_log(NI_LOG_INFO, "Expanding dec fme buffer_pool from %u to %u\n",
               p_buffer_pool->number_of_buffers,
               p_buffer_pool->number_of_buffers +
                   NI_DEC_FRAME_BUF_POOL_SIZE_EXPAND);

        buf = ni_buf_pool_expand(p_buffer_pool);
        if (NULL == buf)
        {
            ni_pthread_mutex_unlock(&p_buffer_pool->mutex);
            return NULL;
        }
    }

    // remove it from free list head; reconnect the linked list, the p_next
    // will become the new head now
    p_buffer_pool->p_free_head = buf->p_next_buffer;

    if (NULL != buf->p_next_buffer)
    {
        buf->p_next_buffer->p_previous_buffer = NULL;
    } else
    {
        p_buffer_pool->p_free_tail = NULL;
    }

    // add it to the used list tail
    buf->p_previous_buffer = p_buffer_pool->p_used_tail;
    buf->p_next_buffer = NULL;

    if (NULL != p_buffer_pool->p_used_tail)
    {
        p_buffer_pool->p_used_tail->p_next_buffer = buf;
    } else
    {
        p_buffer_pool->p_used_head = buf;
    }

    p_buffer_pool->p_used_tail = buf;

    ni_pthread_mutex_unlock(&p_buffer_pool->mutex);

    ni_log(NI_LOG_DEBUG, "%s ptr %p  buf %p\n", __func__, buf->buf, buf);
    return buf;
}

// return a used memory buffer to the pool
void ni_buf_pool_return_buffer(ni_buf_t *buf, ni_buf_pool_t *p_buffer_pool)
{
  // p_buffer_pool could be null in case of delayed buffer return after pool
  // has been freed
  if (!buf)
  {
      return;
  }

  ni_log(NI_LOG_DEBUG, "%s ptr %p  buf %p\n", __func__, buf->buf, buf);

  if (!p_buffer_pool)
  {
      ni_log(NI_LOG_DEBUG, "%s: pool already freed, self destroy\n", __func__);
      ni_aligned_free(buf->buf);
      free(buf);
      return;
  }

  ni_pthread_mutex_lock(&p_buffer_pool->mutex);

  // remove buf from the used list
  if (NULL != buf->p_previous_buffer)
  {
      buf->p_previous_buffer->p_next_buffer = buf->p_next_buffer;
  } else
  {
      p_buffer_pool->p_used_head = buf->p_next_buffer;
  }

  if (NULL != buf->p_next_buffer)
  {
      buf->p_next_buffer->p_previous_buffer = buf->p_previous_buffer;
  } else
  {
      p_buffer_pool->p_used_tail = buf->p_previous_buffer;
  }

  // put it on the tail of free buffers list
  buf->p_previous_buffer = p_buffer_pool->p_free_tail;
  buf->p_next_buffer = NULL;

  if (NULL != p_buffer_pool->p_free_tail)
  {
      p_buffer_pool->p_free_tail->p_next_buffer = buf;
  } else
  {
      p_buffer_pool->p_free_head = buf;
  }

  p_buffer_pool->p_free_tail = buf;

  ni_pthread_mutex_unlock(&p_buffer_pool->mutex);
}

// allocate a memory buffer and place it in the pool
ni_buf_t *ni_buf_pool_allocate_buffer(ni_buf_pool_t *p_buffer_pool,
                                      int buffer_size)
{
    ni_buf_t *p_buffer = NULL;
    void *p_buf = NULL;

    if (NULL != p_buffer_pool &&
        (p_buffer = (ni_buf_t *)malloc(sizeof(ni_buf_t))) != NULL)
    {
        // init the struct
        memset(p_buffer, 0, sizeof(ni_buf_t));

        if (ni_posix_memalign(&p_buf, sysconf(_SC_PAGESIZE), buffer_size))
        {
            ni_aligned_free(p_buffer);
            return NULL;
        }
        ni_log(NI_LOG_DEBUG, "%s ptr %p  buf %p\n", __func__, p_buf, p_buffer);
        p_buffer->buf = p_buf;
        p_buffer->pool = p_buffer_pool;

        // add buffer to the buf pool list
        p_buffer->p_prev = NULL;
        p_buffer->p_next = NULL;
        p_buffer->p_previous_buffer = p_buffer_pool->p_free_tail;

        if (p_buffer_pool->p_free_tail != NULL)
        {
            p_buffer_pool->p_free_tail->p_next_buffer = p_buffer;
        } else
        {
            p_buffer_pool->p_free_head = p_buffer;
        }

        p_buffer_pool->p_free_tail = p_buffer;
    }

    return p_buffer;
}

// decoder frame buffer pool init & free
int32_t ni_dec_fme_buffer_pool_initialize(ni_session_context_t* p_ctx,
                                          int32_t number_of_buffers,
                                          int width, int height,
                                          int height_align, int factor)
{
    int32_t i;
    int width_aligned;
    int height_aligned;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: enter\n", __func__);

    if (QUADRA)
    {
        width_aligned = ((((width * factor) + 127) / 128) * 128) / factor;
        height_aligned = height;
    } else
    {
        width_aligned = ((width + 31) / 32) * 32;
        height_aligned = ((height + 7) / 8) * 8;
        if (height_align)
        {
            height_aligned = ((height + 15) / 16) * 16;
        }
    }

    int luma_size = width_aligned * height_aligned * factor;
    int chroma_b_size;
    int chroma_r_size;
    if (QUADRA)
    {
        int chroma_width_aligned =
            ((((width / 2 * factor) + 127) / 128) * 128) / factor;
        int chroma_height_aligned = height_aligned / 2;
        chroma_b_size = chroma_r_size =
            chroma_width_aligned * chroma_height_aligned * factor;
    } else
    {
        chroma_b_size = luma_size / 4;
        chroma_r_size = chroma_b_size;
    }
    uint32_t buffer_size = luma_size + chroma_b_size + chroma_r_size +
        NI_FW_META_DATA_SZ + NI_MAX_SEI_DATA;

    // added 2 blocks of 512 bytes buffer space to handle any extra metadata
    // retrieval from fw
    buffer_size =
        ((buffer_size + (NI_MEM_PAGE_ALIGNMENT - 1)) / NI_MEM_PAGE_ALIGNMENT) *
            NI_MEM_PAGE_ALIGNMENT +
        NI_MEM_PAGE_ALIGNMENT * 3;

    if (p_ctx->dec_fme_buf_pool != NULL)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG, 
               "Warning init dec_fme Buf pool already with size %u\n",
               p_ctx->dec_fme_buf_pool->number_of_buffers);

        if (buffer_size > p_ctx->dec_fme_buf_pool->buf_size)
        {
            ni_log(NI_LOG_INFO,
                   "Warning resolution %dx%d memory buffer size %u "
                   "> %u (existing buffer size), re-allocating !\n",
                   width, height, buffer_size,
                   p_ctx->dec_fme_buf_pool->buf_size);

            ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
        } else
        {
            ni_log(NI_LOG_INFO,
                   "INFO resolution %dx%d memory buffer size %u <= "
                   "%u (existing buffer size), continue !\n",
                   width, height, buffer_size,
                   p_ctx->dec_fme_buf_pool->buf_size);
            return 0;
        }
    }

    if ((p_ctx->dec_fme_buf_pool =
             (ni_buf_pool_t *)malloc(sizeof(ni_buf_pool_t))) == NULL)
    {
        ni_log2(p_ctx, NI_LOG_ERROR,  "Error alloc for dec fme buf pool\n");
        return -1;
    }

    // init the struct
    memset(p_ctx->dec_fme_buf_pool, 0, sizeof(ni_buf_pool_t));
    ni_pthread_mutex_init(&p_ctx->dec_fme_buf_pool->mutex);
    p_ctx->dec_fme_buf_pool->number_of_buffers = number_of_buffers;

    ni_log2(p_ctx, NI_LOG_DEBUG, 
           "ni_dec_fme_buffer_pool_initialize: entries %d  entry size "
           "%d\n",
           number_of_buffers, buffer_size);

    p_ctx->dec_fme_buf_pool->buf_size = buffer_size;
    for (i = 0; i < number_of_buffers; i++)
    {
        if (NULL ==
            ni_buf_pool_allocate_buffer(p_ctx->dec_fme_buf_pool, buffer_size))
        {
            // release everything we have allocated so far and exit
            ni_dec_fme_buffer_pool_free(p_ctx->dec_fme_buf_pool);
            return -1;
        }
    }

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: exit\n", __func__);
    return 0;
}

void ni_dec_fme_buffer_pool_free(ni_buf_pool_t *p_buffer_pool)
{
    ni_buf_t *buf, *p_next;

    if (p_buffer_pool)
    {
        ni_log(NI_LOG_TRACE, "%s: enter.\n", __func__);

        // mark used buf not returned at pool free time by setting pool ptr in used
        // buf to NULL, so they will self-destroy when time is due eventually
        ni_pthread_mutex_lock(&p_buffer_pool->mutex);
        buf = p_buffer_pool->p_used_head;
        if (buf)
        {
            while (buf)
            {
                p_next = buf->p_next_buffer;
                ni_log(NI_LOG_DEBUG, "Release ownership of ptr %p buf %p\n",
                       buf->buf, buf);
                buf->pool = NULL;
                buf = p_next;
            }
        }
        ni_pthread_mutex_unlock(&p_buffer_pool->mutex);

        buf = p_buffer_pool->p_free_head;
        // free all the buffers in the free list
        int32_t count_free = 0;
        while (buf)
        {
            p_next = buf->p_next_buffer;
            ni_aligned_free(buf->buf);
            free(buf);
            buf = p_next;
            count_free++;
        }
        // NOLINTNEXTLINE(bugprone-branch-clone)
        if (count_free != p_buffer_pool->number_of_buffers)
        {
            ni_log(NI_LOG_DEBUG, "%s free %d  != number_of_buffers %u\n",
                   __func__, count_free, p_buffer_pool->number_of_buffers);
        } else
        {
            ni_log(NI_LOG_DEBUG, "%s all buffers freed: %d.\n", __func__,
                   count_free);
        }
        free(p_buffer_pool);
    }
    else
    {
        ni_log(NI_LOG_INFO, "%s: NOT allocated\n", __func__);
    }
}

void ni_buffer_pool_free(ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *buf, *p_next;

    ni_log(NI_LOG_TRACE, "%s: enter.\n", __func__);

    if (p_buffer_pool)
    {
        buf = p_buffer_pool->p_free_head;
        // free all the buffers in the free and used list
        int32_t count = 0;
        while (buf)
        {
            p_next = buf->p_next_buffer;
            free(buf);
            buf = p_next;
            count++;
        }

        buf = p_buffer_pool->p_used_head;
        while (buf)
        {
            p_next = buf->p_next_buffer;
            free(buf);
            buf = p_next;
            count++;
        }

        if (count != p_buffer_pool->number_of_buffers)
        {
            ni_log(NI_LOG_ERROR, "??? freed %d != number_of_buffers %u\n",
                   count, p_buffer_pool->number_of_buffers);
        } else
        {
            ni_log(NI_LOG_DEBUG, "p_buffer_pool freed %d buffers.\n", count);
        }
        free(p_buffer_pool);
    }
    else
    {
        ni_log(NI_LOG_INFO, "%s: NOT allocated\n", __func__);
    }
}

ni_queue_node_t *ni_buffer_pool_allocate_buffer(ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *p_buffer = NULL;

    if (NULL != p_buffer_pool &&
        (p_buffer = (ni_queue_node_t *)malloc(sizeof(ni_queue_node_t))) != NULL)
    {
        //Inititalise the struct
        memset(p_buffer, 0, sizeof(ni_queue_node_t));
        // add buffer to the buf pool list
        p_buffer->p_prev = NULL;
        p_buffer->p_next = NULL;
        p_buffer->p_previous_buffer = p_buffer_pool->p_free_tail;

        if (p_buffer_pool->p_free_tail != NULL)
        {
            p_buffer_pool->p_free_tail->p_next_buffer = p_buffer;
        } else
        {
            p_buffer_pool->p_free_head = p_buffer;
        }

        p_buffer_pool->p_free_tail = p_buffer;
    }

    return p_buffer;
}

int32_t ni_buffer_pool_initialize(ni_session_context_t* p_ctx, int32_t number_of_buffers)
{
    int i;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: enter\n", __func__);

    if (p_ctx->buffer_pool != NULL)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "Warn init Buf pool already with size %u\n",
               p_ctx->buffer_pool->number_of_buffers);
        return -1;
    }

    if ((p_ctx->buffer_pool = (ni_queue_buffer_pool_t *)malloc(
             sizeof(ni_queue_buffer_pool_t))) == NULL)
    {
        ni_log(NI_LOG_ERROR, "Error alloc for pool\n");
        return -1;
    }

    //initialise the struct
    memset(p_ctx->buffer_pool, 0, sizeof(ni_queue_buffer_pool_t));
    p_ctx->buffer_pool->number_of_buffers = number_of_buffers;
    //p_buffer_pool->p_free_head = NULL;
    //p_buffer_pool->p_free_tail = NULL;
    //p_buffer_pool->p_used_head = NULL;
    //p_buffer_pool->p_used_tail = NULL;

    for (i = 0; i < number_of_buffers; i++)
    {
        if (NULL == ni_buffer_pool_allocate_buffer(p_ctx->buffer_pool))
        {
            //Release everything we have allocated so far and exit
            ni_buffer_pool_free(p_ctx->buffer_pool);
            return -1;
        }
    }

    return 0;
}

ni_queue_node_t *ni_buffer_pool_expand(ni_queue_buffer_pool_t *pool)
{
    int i;

    for (i = 0; i < 200; i++)
    {
        if (NULL == ni_buffer_pool_allocate_buffer(pool))
        {
            ni_log(NI_LOG_FATAL,
                   "FATAL ERROR: Failed to allocate pool buffer for pool :%p\n",
                   pool);
            return NULL;
        }
    }
    pool->number_of_buffers += 200;
    return pool->p_free_head;
}

ni_queue_node_t *ni_buffer_pool_get_queue_buffer(ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *buf = NULL;

    if (NULL == p_buffer_pool)
    {
        return NULL;
    }

    // find and return a free buffer
    buf = p_buffer_pool->p_free_head;
    if (NULL == buf)
    {
        ni_log(NI_LOG_INFO, "Expanding p_buffer_pool from %u to %u \n",
               p_buffer_pool->number_of_buffers,
               p_buffer_pool->number_of_buffers + 200);
        buf = ni_buffer_pool_expand(p_buffer_pool);
        if (NULL == buf)
        {
            return NULL;   //return null otherwise there will be null derefferencing later
        }
    }

    buf->checkout_timestamp = time(NULL);
    // remove it from free list head; reconnect the linked list, the p_next
    // will become the new head now
    p_buffer_pool->p_free_head = buf->p_next_buffer;

    if (NULL != buf->p_next_buffer)
    {
        buf->p_next_buffer->p_previous_buffer = NULL;
    } else
    {
        p_buffer_pool->p_free_tail = NULL;
    }

    // add it to the used list tail
    buf->p_previous_buffer = p_buffer_pool->p_used_tail;
    buf->p_next_buffer = NULL;

    if (NULL != p_buffer_pool->p_used_tail)
    {
        p_buffer_pool->p_used_tail->p_next_buffer = buf;
    } else
    {
        p_buffer_pool->p_used_head = buf;
    }

    p_buffer_pool->p_used_tail = buf;

    return buf;
}

void ni_buffer_pool_return_buffer(ni_queue_node_t *buf,
                                  ni_queue_buffer_pool_t *p_buffer_pool)
{
    if (!buf || !p_buffer_pool)
    {
        return;
    }

    // remove buf from the used list
    if (NULL != buf->p_previous_buffer)
    {
        buf->p_previous_buffer->p_next_buffer = buf->p_next_buffer;
    } else
    {
        p_buffer_pool->p_used_head = buf->p_next_buffer;
    }

    if (NULL != buf->p_next_buffer)
    {
        buf->p_next_buffer->p_previous_buffer = buf->p_previous_buffer;
    } else
    {
        p_buffer_pool->p_used_tail = buf->p_previous_buffer;
    }

    // put it on the tail of free buffers list
    buf->p_previous_buffer = p_buffer_pool->p_free_tail;
    buf->p_next_buffer = NULL;

    if (NULL != p_buffer_pool->p_free_tail)
    {
        p_buffer_pool->p_free_tail->p_next_buffer = buf;
    } else
    {
        p_buffer_pool->p_free_head = buf;
    }

    p_buffer_pool->p_free_tail = buf;
}

/*!******************************************************************************
 *  \brief  Get xcoder instance id
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_get_frame_index(uint32_t *value)
{
    ni_nvme_write_complete_dw0_t *p_value =
        (ni_nvme_write_complete_dw0_t *)value;

    return p_value->frame_index;
}

#ifdef _ANDROID
/*!******************************************************************************
 *	\brief	use cmd to search nvme block file
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[in] search cmd
 *  \param[in] cmd_ret length
 *  \param[out] cmd_ret search result for nvme block file
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE
 *          On failure returns NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
static ni_retcode_t ni_search_file(const char *p_dev, char *cmd, char *cmd_ret,
                                   int cmd_ret_len)
{
    FILE *cmd_fp;
    ni_retcode_t ret = NI_RETCODE_SUCCESS;

    if (access(p_dev, F_OK) == -1)
    {
        return NI_RETCODE_FAILURE;
    }

    // look for child block in sysfs mapping tree
    cmd_fp = popen(cmd, "r");
    if (!cmd_fp)
    {
        return NI_RETCODE_FAILURE;
    }

    if (fgets(cmd_ret, cmd_ret_len, cmd_fp) == 0)
    {
        ret = NI_RETCODE_INVALID_PARAM;
    }

    pclose(cmd_fp);
    return ret;
}
#endif
/*!******************************************************************************
*  \brief  Remove a string-pattern from a string in-place.
*
*  \param[in,out] main_str Null terminated array of characters to operate upon in-place.
*  \param[in] pattern Null terminated array of characters to remove from main_str.
*                     Supports special characters '#' and '+' for digit matching and
*                     repeated matching respectively. Note, there is no way to \a escape
*                     the special characters.
*                     \b Example:
*                     char a_str[10] = "aaa123qwe";
*                     char b_str[5] = "a+#+";
*                     remove_substring_pattern(a_str, b_str);
*                     printf("%s\n", a_str);
*                     \b Output:
*                     qwe
*
*  \return If characters removed, returns 1
*          If no characters removed, returns 0
*******************************************************************************/
NI_UNUSED static uint32_t remove_substring_pattern(char *main_str, const char *pattern)
{
  uint32_t i, j;                     // for loop counters
  uint32_t match_length;             // length of matching substring
  uint32_t matched_chr;              // boolean flag for when match is found for a character in the pattern
  char char_match_pattern[11] = "";  // what characters to look for when evaluating a character in main_str
  uint32_t pattern_matched = 0;      // boolean flag for when who pattern match is found
  uint32_t pattern_start = 0;        // starting index in main_str of substring matching pattern
  const char digit_match_pattern[11] = "0123456789";  // set of numeric digits for expansion of special character '#'

  // do not accept zero length main_str or pattern
  if (!main_str || !pattern || !*main_str || !*pattern)
  {
    return 0;
  }

  // iterate over all characters in main_str looking for starting index of matching pattern
  for (i = 0; i < strlen(main_str) && !pattern_matched; i++)
  {
    pattern_matched = 0;
    match_length = 0;
    // iterate over the characters of the pattern
    for (j = 0; j < strlen(pattern); j++)
    {
      matched_chr = 0;
      // set which characters to look for, special cases for special control characters
      if (pattern[j] == '+')
      {
        // immediately fail as entering this branch means either the first character is a '+', or a '+" following a "+'
        return 0;
      }
      else if (pattern[j] == '#')
      {
        memcpy(char_match_pattern, digit_match_pattern, strlen(digit_match_pattern) + 1);
      }
      else
      {
        memcpy(char_match_pattern, pattern + j, 1);
        memset(char_match_pattern + 1, 0, 1);
      }
      // check if char is in match_pattern
      if (pattern[j+1] == '+')
      {
        while (main_str[i + match_length] && strchr(char_match_pattern, (int) main_str[i + match_length]))
        {
          match_length++;
          matched_chr = 1;
        }
        j++;
      }
      else if (main_str[i + match_length] && strchr(char_match_pattern, (int) main_str[i + match_length]))
      {
        match_length++;
        matched_chr = 1;
      }
      // if no matches were found, then this segment is not the sought pattern
      if (!matched_chr)
      {
        break;
      }
      // if all of pattern has been processed and matched, mark sucessful whole pattern match
      else if ((j + 1) >= strlen(pattern))
      {
        pattern_matched = 1;
        pattern_start = i;
      }
    }
  }

  // remove sub-string if its pattern was found in main_str
  if (pattern_matched)
  {
    uint32_t orig_main_str_len = (uint32_t)strlen(main_str);
    memmove(main_str + pattern_start, main_str + pattern_start + match_length,
    strlen(main_str + pattern_start + match_length));
    main_str[orig_main_str_len - match_length] = 0;
    return 1;
  }
  else
  {
    return 0;
  }
}

/*!******************************************************************************
 *  \brief  Find NVMe name space block from device name
 *          If none is found, assume nvme multi-pathing is disabled and return /dev/nvmeXn1
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvme0"
 *  \param[in] out_buf Output buffer to put NVMe name space block. Must be at least length 21
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE
 *******************************************************************************/
ni_retcode_t ni_find_blk_name(const char *p_dev, char *p_out_buf, int out_buf_len)
{
    if (!p_dev || !p_out_buf)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

#ifdef _WIN32
    ni_log(NI_LOG_DEBUG,
           "Automatic namespaceID discovery not supported in Windows. Using "
           "guess.\n");
    snprintf(p_out_buf, out_buf_len, "%s", p_dev);
    return NI_RETCODE_SUCCESS;
#elif __APPLE__
    /* 
    Using smartctl /dev/rdisk4 -i | grep 'PCI Vendor' command check if output 
    contains Quadra. Model number could be duplicated in other devices so 
    should use PCI Vendor instead but smartctl is not preinstalled on mac
    snprintf(command, sizeof(command) - 1,
              "smartctl %s -i | grep 'PCI Vendor'", p_dev);
     */
    FILE *cmd_fp;
    char cmd_ret[60] = {0};
    char command[128] = {0};
    snprintf(command, sizeof(command) - 1,
             "diskutil info %s | grep 'Media Name'", p_dev);
    ni_log(NI_LOG_TRACE, "Using %s to find quadra device\n", command);
    cmd_fp = popen(command, "r");
    if (cmd_fp == NULL)
    {
        ni_log(NI_LOG_ERROR, "Failed to execute %s\n", command);
        return NI_RETCODE_FAILURE;
    }
    if (fgets(cmd_ret, sizeof(cmd_ret) - 1, cmd_fp) == NULL)
    {
        ni_log(NI_LOG_ERROR, "Failed to read %s output.\n", command);
        pclose(cmd_fp);
        return NI_RETCODE_FAILURE;
    }
    pclose(cmd_fp);
    ni_log(NI_LOG_TRACE, "Got '%s' from the command\n", cmd_ret);
    // if (strcasestr(cmd_ret, "0x1d82") != NULL)
    if (strcasestr(cmd_ret, "Quadra") != NULL)
    {
        snprintf(p_out_buf, out_buf_len, "%s", p_dev);
        return NI_RETCODE_SUCCESS;
    } else
    {
        //This is a soft error so trace level is fine
        ni_log(NI_LOG_TRACE, "%s is not a quadra device\n", p_dev);
        return NI_RETCODE_INVALID_PARAM;
    }
#elif defined(XCODER_LINUX_VIRTIO_DRIVER_ENABLED)
    ni_log(NI_LOG_TRACE, "The device is already considered as a block divice in Linux virtual machine with VirtIO driver.\n");
    snprintf(p_out_buf, out_buf_len, "%s", p_dev);
    return NI_RETCODE_SUCCESS;
#else
    ni_log(NI_LOG_DEBUG, "Set NVMe device name equal to NVMe block name\n");
    snprintf(p_out_buf, out_buf_len, "%s", p_dev);
    return NI_RETCODE_SUCCESS;
#endif
}

/*!******************************************************************************
 *  \brief  check dev name
 *
 *  \param[in] p_dev Device name represented as c string. ex: "/dev/nvmeXnY"
 *
 *  \return On success returns NI_RETCODE_SUCCESS
 *          On failure returns NI_RETCODE_FAILURE or NI_RETCODE_INVALID_PARAM
 *******************************************************************************/
ni_retcode_t ni_check_dev_name(const char *p_dev)
{
    if (!p_dev)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

#ifdef __APPLE__
    /* 
    Using smartctl /dev/rdisk4 -i | grep 'PCI Vendor' command check if output 
    contains Quadra. Model number could be duplicated in other devices so 
    should use PCI Vendor instead but smartctl is not preinstalled on mac
    snprintf(command, sizeof(command) - 1,
              "smartctl %s -i | grep 'PCI Vendor'", p_dev);
     */
    FILE *cmd_fp;
    char cmd_ret[60] = {0};
    char command[128] = {0};
    snprintf(command, sizeof(command) - 1,
             "diskutil info %s | grep 'Media Name'", p_dev);
    ni_log(NI_LOG_TRACE, "Using %s to find quadra device\n", command);
    cmd_fp = popen(command, "r");
    if (cmd_fp == NULL)
    {
        ni_log(NI_LOG_ERROR, "Failed to execute %s\n", command);
        return NI_RETCODE_FAILURE;
    }
    if (fgets(cmd_ret, sizeof(cmd_ret) - 1, cmd_fp) == NULL)
    {
        ni_log(NI_LOG_ERROR, "Failed to read %s output.\n", command);
        pclose(cmd_fp);
        return NI_RETCODE_FAILURE;
    }
    pclose(cmd_fp);
    ni_log(NI_LOG_TRACE, "Got '%s' from the command\n", cmd_ret);
    // if (strcasestr(cmd_ret, "0x1d82") != NULL)
    if (strcasestr(cmd_ret, "Quadra") != NULL)
    {
        return NI_RETCODE_SUCCESS;
    } else
    {
        //This is a soft error so trace level is fine
        ni_log(NI_LOG_TRACE, "%s is not a quadra device\n", p_dev);
        return NI_RETCODE_INVALID_PARAM;
    }
#endif
    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Initialize timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_init(ni_session_context_t* p_ctx, ni_timestamp_table_t **pp_table, const char *name)
{
    ni_timestamp_table_t *ptemp;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: enter\n", __func__);

    if (*pp_table != NULL)
    {
        ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: previously allocated, reallocating now\n",
               __func__);
        ni_queue_free(&(*pp_table)->list, p_ctx->buffer_pool);
        free(*pp_table);
    }
    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: Malloc\n", __func__);
    ptemp = (ni_timestamp_table_t *)malloc(sizeof(ni_timestamp_table_t));
    if (!ptemp)
    {
        return NI_RETCODE_ERROR_MEM_ALOC;
    }

    //initialise the struct
    memset(ptemp, 0, sizeof(ni_timestamp_table_t));

    ni_queue_init(p_ctx, &ptemp->list,
                  name);   //buffer_pool_initialize runs in here

    *pp_table = ptemp;

    ni_log2(p_ctx, NI_LOG_DEBUG,  "%s: success\n", __func__);

    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Clean up timestamp handling
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_done(ni_timestamp_table_t *p_table,
                               ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_log(NI_LOG_TRACE, "%s: enter\n", __func__);

    if (!p_table)
    {
        ni_log(NI_LOG_DEBUG, "%s: no pts table to free\n", __func__);
        return NI_RETCODE_SUCCESS;
    }
    ni_queue_free(&p_table->list, p_buffer_pool);

    free(p_table);
    ni_log(NI_LOG_DEBUG, "%s: success\n", __func__);

    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Register timestamp in timestamp/frameoffset table
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_register(ni_queue_buffer_pool_t *p_buffer_pool,
                                   ni_timestamp_table_t *p_table,
                                   int64_t timestamp, uint64_t data_info)
{
    ni_retcode_t err = NI_RETCODE_SUCCESS;

    err = ni_queue_push(p_buffer_pool, &p_table->list, data_info, timestamp);

    if (NI_RETCODE_SUCCESS == err)   // NOLINT(bugprone-branch-clone)
    {
        ni_log(NI_LOG_DEBUG, "%s: success\n", __func__);
    } else
    {
        ni_log(NI_LOG_ERROR, "ERROR: %s: FAILED\n", __func__);
    }

    return err;
}

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frameoffset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get(ni_timestamp_table_t *p_table,
                              uint64_t frame_info, int64_t *p_timestamp,
                              int32_t threshold, int32_t print,
                              ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_retcode_t err = NI_RETCODE_SUCCESS;

    ni_log(NI_LOG_DEBUG, "%s: getting timestamp with frame_info=%" PRId64 "\n",
           __func__, frame_info);

    err = ni_queue_pop(&p_table->list, frame_info, p_timestamp, threshold,
                       print, p_buffer_pool);
    if (NI_RETCODE_SUCCESS != err)
    {
        ni_log(NI_LOG_ERROR, "%s: error getting timestamp\n", __func__);
    }

    ni_log(NI_LOG_DEBUG,
           "%s: timestamp=%" PRId64 ", frame_info=%" PRId64 ", err=%d\n",
           __func__, *p_timestamp, frame_info, err);

    return err;
}

ni_retcode_t ni_timestamp_get_with_threshold(
    ni_timestamp_table_t *p_table, uint64_t frame_info, int64_t *p_timestamp,
    int32_t threshold, int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
    return ni_queue_pop_threshold(&p_table->list, frame_info, p_timestamp,
                                  threshold, print, p_buffer_pool);
}

void ni_timestamp_scan_cleanup(ni_timestamp_table_t *pts_list,
                               ni_timestamp_table_t *dts_list,
                               ni_queue_buffer_pool_t *p_buffer_pool)
{
    if (!pts_list || !dts_list)
    {
        return;
    }

    // currently, only have dts list.
    // if pts list is added back, this should be modified.
    ni_queue_t *p_queue = &dts_list->list;
    ni_queue_node_t *p = p_queue->p_first;
    time_t now = time(NULL);

    while (p)
    {
        if (now - p->checkout_timestamp <= 30)
        {
            break;
        }
        if (p_queue->p_first == p_queue->p_last)
        {
            ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);
            p = p_queue->p_first = p_queue->p_last = NULL;
        } else
        {
            p_queue->p_first = p->p_next;
            p->p_next->p_prev = NULL;
            ni_buffer_pool_return_buffer(p, p_buffer_pool);

            p = p_queue->p_first;
        }
        p_queue->count--;
    }
}

/*!******************************************************************************
 *  \brief  Retrieve timestamp from table based on frameoffset info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_timestamp_get_v2(ni_timestamp_table_t *p_table,
                                 uint64_t frame_offset, int64_t *p_timestamp,
                                 int32_t threshold,
                                 ni_queue_buffer_pool_t *p_buffer_pool)
{
  ni_retcode_t err = NI_RETCODE_SUCCESS;

  if (!p_table || !p_timestamp || !p_buffer_pool)
  {
      err = NI_RETCODE_INVALID_PARAM;
      LRETURN;
  }

  ni_log(NI_LOG_DEBUG, "%s: getting timestamp with frame_offset=%" PRId64 "\n",
         __func__, frame_offset);

  err = ni_queue_pop(&p_table->list, frame_offset, p_timestamp, threshold, 0, p_buffer_pool);
  if( NI_RETCODE_SUCCESS != err)
  {
      ni_log(NI_LOG_ERROR, "%s: error getting timestamp\n", __func__);
  }

  ni_log(NI_LOG_DEBUG,
         "%s: timestamp=%" PRId64 ", frame_offset=%" PRId64 ", err=%d\n",
         __func__, *p_timestamp, frame_offset, err);

END:

    return err;
}

/*!******************************************************************************
 *  \brief  Initialize xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_init(ni_session_context_t* p_ctx, ni_queue_t *p_queue, const char *name)
{
    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: enter\n", __func__);

    if (!p_queue || !name)
    {
        return NI_RETCODE_INVALID_PARAM;
    }
    strcpy(p_queue->name, name);
    ni_buffer_pool_initialize(p_ctx, BUFFER_POOL_SZ_PER_CONTEXT);

    p_queue->p_first = NULL;
    p_queue->p_last = NULL;
    p_queue->count = 0;

    ni_log2(p_ctx, NI_LOG_TRACE,  "%s: exit\n", __func__);

    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Push into xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_push(ni_queue_buffer_pool_t *p_buffer_pool,
                           ni_queue_t *p_queue, uint64_t frame_info,
                           int64_t timestamp)
{
    ni_retcode_t err = NI_RETCODE_SUCCESS;
    ni_queue_node_t *temp = NULL;

    if (!p_queue)
    {
        ni_log(NI_LOG_ERROR, "%s: error, null pointer parameters passed\n",
               __func__);
        err = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    temp = ni_buffer_pool_get_queue_buffer(p_buffer_pool);

    if (!temp)
    {
        ni_log(NI_LOG_ERROR, "%s: error, cannot allocate memory\n", __func__);
        err = NI_RETCODE_ERROR_MEM_ALOC;
        LRETURN;
    }

    //ni_log(NI_LOG_TRACE, "%s enter: p_first=%"PRId64", p_last=%"PRId64", count=%d\n", __func__, p_queue->p_first, p_queue->p_last, p_queue->count);

    temp->timestamp = timestamp;
    temp->frame_info = frame_info;

    temp->p_next = NULL;

    if (!p_queue->p_first)
    {
        p_queue->p_first = p_queue->p_last = temp;
        p_queue->p_first->p_prev = NULL;
        p_queue->count++;
    } else
    {
        p_queue->p_last->p_next = temp;
        temp->p_prev = p_queue->p_last;
        p_queue->p_last = temp;
        p_queue->count++;

        // Assume the oldest one is useless when reaching this situation.
        if (p_queue->count > XCODER_MAX_NUM_QUEUE_ENTRIES)
        {
            ni_log(NI_LOG_DEBUG,
                   "%s: queue overflow, remove oldest entry, count=%u\n",
                   __func__, p_queue->count);
            //Remove oldest one
            temp = p_queue->p_first->p_next;
            // free(p_queue->p_first);
            ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);
            p_queue->p_first = temp;
            p_queue->p_first->p_prev = NULL;
            p_queue->count--;
        }
    }

END:

    return err;
}

/*!******************************************************************************
 *  \brief  Pop from the xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_pop(ni_queue_t *p_queue, uint64_t frame_info,
                          int64_t *p_timestamp, int32_t threshold,
                          int32_t print, ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *temp;
    ni_queue_node_t *temp_prev = NULL;
    int32_t found = 0;
    int32_t count = 0;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_queue || !p_timestamp)
    {
        ni_log(NI_LOG_ERROR, "%s: error, null pointer parameters passed\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (NULL == p_queue->p_first)
    {
        ni_log(NI_LOG_DEBUG, "%s: queue is empty...\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    if (p_queue->p_first == p_queue->p_last)
    {
        /*! If only one entry, retrieve timestamp without checking */
        *p_timestamp = p_queue->p_first->timestamp;
        // free(p_queue->p_first);
        ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);

        p_queue->p_first = NULL;
        p_queue->p_last = NULL;
        p_queue->count--;
        ni_assert(p_queue->count == 0);
        found = 1;
    } else
    {
        temp = p_queue->p_first;
        while (temp && !found)
        {
            if (frame_info < temp->frame_info)
            {
                if (!temp->p_prev)
                {
                    ni_log(NI_LOG_DEBUG, "First in ts list, return it\n");
                    *p_timestamp = temp->timestamp;

                    p_queue->p_first = temp->p_next;
                    temp->p_next->p_prev = NULL;

                    ni_buffer_pool_return_buffer(temp, p_buffer_pool);
                    p_queue->count--;
                    found = 1;
                    break;
                }

                // retrieve from p_prev and delete p_prev !
                *p_timestamp = temp->p_prev->timestamp;
                temp = temp->p_prev;
                temp_prev = temp->p_prev;

                if (temp_prev)
                {
                    temp_prev->p_next = temp->p_next;
                    if (temp->p_next)
                    {
                        temp->p_next->p_prev = temp_prev;
                    } else
                    {
                        p_queue->p_last = temp_prev;
                    }
                } else
                {
                    p_queue->p_first = temp->p_next;
                    temp->p_next->p_prev = NULL;
                }
                //free(temp);
                ni_buffer_pool_return_buffer(temp, p_buffer_pool);
                p_queue->count--;
                found = 1;
                break;
            }
            temp = temp->p_next;
            count++;
      }
    }

    if (print)
    {
        ni_log(NI_LOG_DEBUG, "%s %s %d iterations ..\n", __func__,
               p_queue->name, count);
    }

    if (!found)
    {
        retval = NI_RETCODE_FAILURE;
    }

END:

    return retval;
}

ni_retcode_t ni_queue_pop_threshold(ni_queue_t *p_queue, uint64_t frame_info,
                                    int64_t *p_timestamp, int32_t threshold,
                                    int32_t print,
                                    ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *temp;
    ni_queue_node_t *temp_prev = NULL;
    int32_t found = 0;
    int32_t count = 0;
    ni_retcode_t retval = NI_RETCODE_SUCCESS;

    if (!p_queue || !p_timestamp)
    {
        ni_log(NI_LOG_ERROR, "%s: error, null pointer parameters passed\n",
               __func__);
        retval = NI_RETCODE_INVALID_PARAM;
        LRETURN;
    }

    if (p_queue->p_first == NULL)
    {
        ni_log(NI_LOG_DEBUG, "%s: queue is empty...\n", __func__);
        retval = NI_RETCODE_FAILURE;
        LRETURN;
    }

    if (p_queue->p_first == p_queue->p_last)
    {
        /*! If only one entry, retrieve timestamp without checking */
        *p_timestamp = p_queue->p_first->timestamp;
        // free(p_queue->p_first);
        ni_buffer_pool_return_buffer(p_queue->p_first, p_buffer_pool);

        p_queue->p_first = NULL;
        p_queue->p_last = NULL;
        p_queue->count--;
        found = 1;
    } else
    {
        temp = p_queue->p_first;
        while ((temp) && (!found))
        {
            if (llabs((int)frame_info - (int)temp->frame_info) <= threshold)
            {
                *p_timestamp = temp->timestamp;
                if (temp_prev)
                {
                    temp_prev->p_next = temp->p_next;
                    if (temp->p_next)
                    {
                        temp->p_next->p_prev = temp_prev;
                    } else
                    {
                        p_queue->p_last = temp_prev;
                    }
                } else
                {
                    p_queue->p_first = temp->p_next;
                    temp->p_next->p_prev = NULL;
                }
                // free(temp);
                ni_buffer_pool_return_buffer(temp, p_buffer_pool);
                p_queue->count--;
                found = 1;
                break;
            }
            temp_prev = temp;
            temp = temp->p_next;
            count++;
        }
    }

    if (print)
    {
        ni_log(NI_LOG_DEBUG, "%s %s %d iterations ..\n", __func__,
               p_queue->name, count);
    }

    if (!found)
    {
        retval = NI_RETCODE_FAILURE;
    }

END:

    return retval;
}

/*!******************************************************************************
 *  \brief  Free xcoder queue
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_free(ni_queue_t *p_queue, ni_queue_buffer_pool_t *p_buffer_pool)
{
    ni_queue_node_t *temp = NULL;
    ni_queue_node_t *temp_next;
    int32_t left = 0;

    if (!p_queue)
    {
        return NI_RETCODE_SUCCESS;
    }

    ni_log(NI_LOG_DEBUG, "Entries before clean up: \n");
    ni_queue_print(p_queue);

    temp = p_queue->p_first;
    while (temp)
    {
        temp_next = temp->p_next;
        //free(temp);
        ni_buffer_pool_return_buffer(temp, p_buffer_pool);
        temp = temp_next;
        left++;
    }
    ni_log(NI_LOG_DEBUG, "Entries cleaned up at ni_queue_free: %d, count: %u\n",
           left, p_queue->count);

    //ni_queue_print(p_queue);

    p_queue->count = 0;

    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Print xcoder queue info
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
ni_retcode_t ni_queue_print(ni_queue_t *p_queue)
{
    ni_queue_node_t *temp = NULL;
    struct tm *ltime = NULL;
    char buff[20] = {0};

    if (!p_queue)
    {
        return NI_RETCODE_SUCCESS;
    }

    ni_log(NI_LOG_DEBUG, "Queue [%s] Count: %u\n", p_queue->name,
           p_queue->count);

    ni_log(NI_LOG_DEBUG, "\nForward:\n");

    temp = p_queue->p_first;

    ni_log(NI_LOG_DEBUG, "%s enter: p_first=%p, p_last=%p, count=%u, temp=%p\n",
           __func__, p_queue->p_first, p_queue->p_last, p_queue->count, temp);

    //  ni_log(NI_LOG_TRACE, "%s enter: p_first=%" PRId64 ", p_last=%" PRId64 ", count=%d, temp=%" PRId64 "\n", __func__, p_queue->p_first, p_queue->p_last, p_queue->count, temp);

    while (temp)
    {
        ltime = localtime(&temp->checkout_timestamp);
        if (ltime)
        {
            strftime(buff, 20, "%Y-%m-%d %H:%M:%S", ltime);
            ni_log(NI_LOG_TRACE, " %s [%" PRId64 ", %" PRId64 "]", buff,
                   temp->timestamp, temp->frame_info);
        }
        temp = temp->p_next;
    }

    ni_log(NI_LOG_DEBUG, "\nBackward:");

    temp = p_queue->p_last;
    while (temp)
    {
        ni_log(NI_LOG_TRACE, " [%" PRId64 ", %" PRId64 "]\n", temp->timestamp,
               temp->frame_info);
        temp = temp->p_prev;
    }
    ni_log(NI_LOG_DEBUG, "\n");

    return NI_RETCODE_SUCCESS;
}

/*!******************************************************************************
 *  \brief  Convert string to boolean
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_atobool(const char *p_str, bool *b_error)
{
    if (!strcmp(p_str, "1") || !strcmp(p_str, "true") || !strcmp(p_str, "yes"))
    {
        return 1;
    }

    if (!strcmp(p_str, "0") || !strcmp(p_str, "false") || !strcmp(p_str, "no"))
    {
        return 0;
    }

    *b_error = true;
    return 0;
}

/*!******************************************************************************
 *  \brief  Convert string to integer
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_atoi(const char *p_str, bool *b_error)
{
    char *end;
    int32_t v = strtol(p_str, &end, 0);

    if (end == p_str || *end != '\0')
    {
        *b_error = true;
    }

    return v;
}

/*!******************************************************************************
 *  \brief  Convert string to floating
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
double ni_atof(const char *p_str, bool *b_error)
{
    char *end;
    double v = strtod(p_str, &end);

    if (end == p_str || *end != '\0')
    {
        *b_error = true;
    }

    return v;
}

/*!******************************************************************************
 *  \brief  Parse name
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
int32_t ni_parse_name(const char *arg, const char *const *names, bool *b_error)
{
    int32_t i;
    for (i = 0; names[i]; i++)
    {
        if (!strcmp(arg, names[i]))
        {
            return i;
        }
    }

    return ni_atoi(arg, b_error);
}

/*!******************************************************************************
 *  \brief  Get system time for log
 *
 *  \param
 *
 *  \return
 *******************************************************************************/
uint64_t ni_get_utime(void)
{
    struct timeval tv;
    (void)ni_gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000LL + tv.tv_usec);
}

uint64_t ni_gettime_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER count;
    uint64_t time_sec, time_nsec;

    // Get frequency firstly
    QueryPerformanceFrequency(&frequency);

    QueryPerformanceCounter(&count);

    time_sec = count.QuadPart / frequency.QuadPart;
    time_nsec = (count.QuadPart - time_sec * frequency.QuadPart) *
        1000000000LL / frequency.QuadPart;

    return (time_sec * 1000000000LL + time_nsec);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

// Netint HW YUV420p data layout related utility functions

/*!*****************************************************************************
 *  \brief  Get dimension information of Netint HW YUV420p frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *
 *  \param[in]  width   source YUV frame width
 *  \param[in]  height  source YUV frame height
 *  \param[in]  factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_semiplanar  1 for semiplanar frame, 0 otherwise
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return Y/Cb/Cr stride and height info
 *
 ******************************************************************************/
void ni_get_hw_yuv420p_dim(int width, int height, int factor,
                           int is_semiplanar,
                           int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                           int plane_height[NI_MAX_NUM_DATA_POINTERS])
{
    // strides are multiples of 128
    if (width < NI_MIN_WIDTH)
    {
        plane_stride[0] = ((NI_MIN_WIDTH * factor + 127) / 128) * 128;
        plane_stride[1] =
            (((NI_MIN_WIDTH / (is_semiplanar ? 1 : 2) * factor) + 127) /
             128) *
            128;
        plane_stride[2] = (is_semiplanar ? 0 : plane_stride[1]);
    } else
    {
        width = ((width + 1) / 2) * 2; // pad odd resolution
        plane_stride[0] = ((width * factor + 127) / 128) * 128;
        plane_stride[1] =
            (((width / (is_semiplanar ? 1 : 2) * factor) + 127) / 128) *
            128;
        plane_stride[2] = (is_semiplanar ? 0 : plane_stride[1]);
    }

    // height (in lines) just needs to be even number
    if (height < NI_MIN_HEIGHT)
    {
        plane_height[0] = NI_MIN_HEIGHT;
    } else
    {
        plane_height[0] = ((height + 1) / 2) * 2;
    }
    plane_height[1] = plane_height[2] = plane_height[0] / 2;
}

/*!*****************************************************************************
 *  \brief  Get dimension information of frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation.
 *          The returned stride and height info will take alignment 
 *          requirements into account.
 *
 *  \param[in]  width   source frame width
 *  \param[in]  height  source frame height
 *  \param[in]  pix_fmt  ni pixel format
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return stride and height info
 *
 ******************************************************************************/
void ni_get_frame_dim(int width, int height,
                      ni_pix_fmt_t pix_fmt,
                      int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                      int plane_height[NI_MAX_NUM_DATA_POINTERS])
{
    plane_height[0] = ((height + 1) / 2) * 2;
    plane_height[1] = plane_height[2] = plane_height[0] / 2;

    switch (pix_fmt)
    {
        /* 8-bit YUV420 planar */
        case NI_PIX_FMT_YUV420P:
            plane_stride[0] = NI_VPU_ALIGN128(width);
            plane_stride[1] = NI_VPU_ALIGN128((width / 2));
            plane_stride[2] = plane_stride[1];
            plane_stride[3] = 0;
            break;
        /* 10-bit YUV420 planar, little-endian, least significant bits */
        case NI_PIX_FMT_YUV420P10LE:
            plane_stride[0] = NI_VPU_ALIGN128(width * 2);
            plane_stride[1] = NI_VPU_ALIGN128(width);
            plane_stride[2] = plane_stride[1];
            plane_stride[3] = 0;
            break;
        /* 8-bit YUV420 semi-planar */
        case NI_PIX_FMT_NV12:
            plane_stride[0] = NI_VPU_ALIGN128(width);
            plane_stride[1] = plane_stride[0];
            plane_stride[2] = 0;
            plane_stride[3] = 0;
            break;
        /* 8-bit yuv422 semi-planar */
        case NI_PIX_FMT_NV16:
            plane_stride[0] = NI_VPU_ALIGN64(width);
            plane_stride[1] = plane_stride[0];
            plane_stride[2] = 0;
            plane_stride[3] = 0;
            break;
        /*8-bit yuv422 planar */
        case NI_PIX_FMT_YUYV422:
        case NI_PIX_FMT_UYVY422:
            plane_stride[0] = NI_VPU_ALIGN16(width) * 2;
            plane_stride[1] = 0;
            plane_stride[2] = 0;
            plane_stride[3] = 0;
            break;
        /* 10-bit YUV420 semi-planar, little endian, most significant bits */
        case NI_PIX_FMT_P010LE:
            plane_stride[0] = NI_VPU_ALIGN128(width * 2);
            plane_stride[1] = plane_stride[0];
            plane_stride[2] = 0;
            plane_stride[3] = 0;
            break;
        /* 32-bit RGBA packed */
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
        case NI_PIX_FMT_BGR0:
            plane_height[1] = plane_height[2] = 0;

            plane_stride[0] = NI_VPU_ALIGN16(width) * 4;
            plane_stride[1] = 0;
            plane_stride[2] = 0;
            plane_stride[3] = 0;
            break;
        default:
            break;
    }

}   

/*!*****************************************************************************
 *  \brief  Get dimension information of frame to be sent
 *          to encoder for encoding. Caller usually retrieves this info and
 *          uses it in the call to ni_encoder_frame_buffer_alloc for buffer
 *          allocation. 
 *          The returned stride and height info will take into account both min
 *          resolution and alignment requirements.
 *
 *  \param[in]  width   source frame width
 *  \param[in]  height  source frame height
 *  \param[in]  pix_fmt  ni pixel format
 *  \param[out] plane_stride  size (in bytes) of each plane width
 *  \param[out] plane_height  size of each plane height
 *
 *  \return stride and height info
 *
 ******************************************************************************/
void ni_get_min_frame_dim(int width, int height,
                      ni_pix_fmt_t pix_fmt,
                      int plane_stride[NI_MAX_NUM_DATA_POINTERS],
                      int plane_height[NI_MAX_NUM_DATA_POINTERS])
{
    
    if (height < NI_MIN_HEIGHT)
    {
        height = NI_MIN_HEIGHT;
    }
    if (width < NI_MIN_WIDTH)
    {
        width = NI_MIN_WIDTH;
    }
    else
    {
        width = ((width + 1) / 2) * 2; // pad odd resolution
    }

    ni_get_frame_dim(width, height, pix_fmt, plane_stride, plane_height);

    ni_log(NI_LOG_DEBUG,
           "%s dst_stride %d/%d/%d height %d/%d/%d pix_fmt %d\n",
           __func__, plane_stride[0], plane_stride[1], plane_stride[2],
           plane_height[0], plane_height[1], plane_height[2], pix_fmt);
}

/*!*****************************************************************************
 *  \brief  Copy RGBA or YUV data to Netint HW frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers to which data is copied
 *  \param[in]  p_src  pointers from which data is copied
 *  \param[in]  width  source frame width
 *  \param[in]  height source frame height
 *  \param[in]  factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_semiplanar  non-0 for semiplanar frame, 0 otherwise
 *  \param[in]  conf_win_right  right offset of conformance window
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *  \param[in]  i  index to plane to be copied
 *
 *  \return copied data
 *
 ******************************************************************************/
void ni_copy_plane_data(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                          uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                          int frame_width, int frame_height, int factor,
                          int is_semiplanar, int conf_win_right,
                          int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                          int dst_height[NI_MAX_NUM_DATA_POINTERS],
                          int src_stride[NI_MAX_NUM_DATA_POINTERS],
                          int src_height[NI_MAX_NUM_DATA_POINTERS],
                          int i)
{
    if (i >= NI_MAX_NUM_DATA_POINTERS)
    {
        ni_log(NI_LOG_ERROR, "%s: error, invalid plane index %d\n", __func__,
               i);
        return;
    }
    if (p_dst[i] == p_src[i])
    {
        ni_log(NI_LOG_DEBUG, "%s: src and dst identical, return\n", __func__);
        return;
    }

    int height =
        (src_height[i] < dst_height[i] ? src_height[i] : dst_height[i]);
    uint8_t *dst = p_dst[i];
    const uint8_t *src = (const uint8_t *)p_src[i];

    // width padding length in bytes, if needed
    int pad_len_bytes;

    if (0 == i || is_semiplanar) // Y
    {
        pad_len_bytes = dst_stride[i] - frame_width * factor;
    }
    else
    {
        // U/V share the same padding length
        pad_len_bytes = dst_stride[i] - frame_width / 2 * factor;
    }

    if (0 == pad_len_bytes && conf_win_right > 0)
    {
        if (0 == i) // Y
        {
            pad_len_bytes = conf_win_right * factor;
        }
        else
        {
            // U/V share the same padding length
            pad_len_bytes = conf_win_right * factor / 2;
        }
    }

    ni_log(NI_LOG_DEBUG,
           "%s plane %d stride padding: %d pixel (%d bytes), copy height: "
           "%d.\n",
           __func__, i, pad_len_bytes / factor, pad_len_bytes,
           height);

    for (; height > 0; height--)
    {
        memcpy(dst, src,
               (src_stride[i] < dst_stride[i] ? src_stride[i] : dst_stride[i]));
        dst += dst_stride[i];

        // dst is now at the line end
        if (pad_len_bytes)
        {
            // repeat last pixel
            if (factor > 1)
            {
                // for 10 bit it's 2 bytes
                int j;
                uint8_t *tmp_dst = dst - pad_len_bytes;
                for (j = 0; j < pad_len_bytes / factor; j++)
                {
                    memcpy(tmp_dst, dst - pad_len_bytes - factor, factor);
                    tmp_dst += factor;
                }
            }
            else
            {
                memset(dst - pad_len_bytes, *(dst - pad_len_bytes - 1),
                       pad_len_bytes);
            }
        }
        src += src_stride[i];
    }

    // height padding/cropping if needed
    int padding_height = dst_height[i] - src_height[i];
    if (padding_height > 0)
    {
        ni_log(NI_LOG_DEBUG, "%s plane %d padding height: %d\n", __func__,
               i, padding_height);
        src = dst - dst_stride[i];
        for (; padding_height > 0; padding_height--)
        {
            memcpy(dst, src, dst_stride[i]);
            dst += dst_stride[i];
        }
    }
}
/*!*****************************************************************************
 *  \brief  Copy YUV data to Netint HW YUV420p frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers of Y/Cb/Cr to which data is copied
 *  \param[in]  p_src  pointers of Y/Cb/Cr from which data is copied
 *  \param[in]  width  source YUV frame width
 *  \param[in]  height source YUV frame height
 *  \param[in]  factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  is_semiplanar  non-0 for semiplanar frame, 0 otherwise
 *  \param[in]  conf_win_right  right offset of conformance window
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *
 *  \return Y/Cb/Cr data
 *
 ******************************************************************************/
void ni_copy_hw_yuv420p(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                        uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                        int frame_width, int frame_height, int factor,
                        int is_semiplanar, int conf_win_right,
                        int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                        int dst_height[NI_MAX_NUM_DATA_POINTERS],
                        int src_stride[NI_MAX_NUM_DATA_POINTERS],
                        int src_height[NI_MAX_NUM_DATA_POINTERS])
{
    ni_log(NI_LOG_DEBUG,
           "%s dst_stride %d/%d/%d src_stride %d/%d/%d dst_height "
           "%d/%d/%d src_height %d/%d/%d\n",
           __func__, dst_stride[0], dst_stride[1], dst_stride[2], src_stride[0],
           src_stride[1], src_stride[2], dst_height[0], dst_height[1],
           dst_height[2], src_height[0], src_height[1], src_height[2]);

    int i;

    for (i = 0; i < NI_MAX_NUM_DATA_POINTERS - 1; i++)
    {
        ni_copy_plane_data(p_dst, p_src, frame_width, frame_height, factor,
                              is_semiplanar, conf_win_right, dst_stride,
                              dst_height, src_stride, src_height, i);
    }
}

/*!*****************************************************************************
 *  \brief  Copy RGBA or YUV data to Netint HW frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out] p_dst  pointers to which data is copied
 *  \param[in]  p_src  pointers from which data is copied
 *  \param[in]  width  source frame width
 *  \param[in]  height source frame height
 *  \param[in]  factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]  pix_fmt  pixel format to distinguish between planar types and/or components
 *  \param[in]  conf_win_right  right offset of conformance window
 *  \param[in]  dst_stride  size (in bytes) of each plane width in destination
 *  \param[in]  dst_height  size of each plane height in destination
 *  \param[in]  src_stride  size (in bytes) of each plane width in source
 *  \param[in]  src_height  size of each plane height in source
 *
 *  \return copied data
 *
 ******************************************************************************/
void ni_copy_frame_data(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                          uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                          int frame_width, int frame_height,
                          int factor, ni_pix_fmt_t pix_fmt,
                          int conf_win_right,
                          int dst_stride[NI_MAX_NUM_DATA_POINTERS],
                          int dst_height[NI_MAX_NUM_DATA_POINTERS],
                          int src_stride[NI_MAX_NUM_DATA_POINTERS],
                          int src_height[NI_MAX_NUM_DATA_POINTERS])
{
    ni_log(NI_LOG_DEBUG,
           "%s frame_width %d frame_height %d factor %d conf_win_right %d "
           "dst_stride %d/%d/%d src_stride %d/%d/%d dst_height "
           "%d/%d/%d src_height %d/%d/%d pix_fmt %d\n",
           __func__, frame_width, frame_height, factor, conf_win_right, 
           dst_stride[0], dst_stride[1], dst_stride[2], src_stride[0], 
           src_stride[1], src_stride[2], dst_height[0], dst_height[1],
           dst_height[2], src_height[0], src_height[1], src_height[2], 
           pix_fmt);
    
    int is_rgba = 0;
    int is_semiplanar = 0;
    switch (pix_fmt)
    {
        case NI_PIX_FMT_NV12:
        case NI_PIX_FMT_P010LE:
            is_semiplanar = 1;
            break;
        case NI_PIX_FMT_ARGB:
        case NI_PIX_FMT_ABGR:
        case NI_PIX_FMT_RGBA:
        case NI_PIX_FMT_BGRA:
            is_rgba = 1;
            break;
        default:
            break;
    }

    if (is_rgba)
    {
        ni_copy_plane_data(p_dst, p_src,
                             frame_width, frame_height, 4,
                             is_semiplanar, conf_win_right,
                             dst_stride, dst_height,
                             src_stride, src_height,
                             0); // just one plane for rgba
    }
    else
    {
        ni_copy_hw_yuv420p(p_dst, p_src, frame_width, frame_height, factor,
                           is_semiplanar, conf_win_right, dst_stride,
                           dst_height, src_stride, src_height);
    }
}

/*!*****************************************************************************
 *  \brief  Copy yuv444p data to yuv420p frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc.
 *
 *  \param[out]    p_dst0  pointers of Y/Cb/Cr as yuv420p output0
 *  \param[out]    p_dst1  pointers of Y/Cb/Cr as yuv420p output1
 *  \param[in]     p_src  pointers of Y/Cb/Cr as yuv444p intput
 *  \param[in]     width  source YUV frame width
 *  \param[in]     height source YUV frame height
 *  \param[in]     factor  1 for 8 bit, 2 for 10 bit
 *  \param[in]     mode 0 for
 *                 out0 is Y+1/2V, with the original input as the out0, 1/4V
 *                 copy to data[1] 1/4V copy to data[2]
 *                 out1 is U+1/2V, U copy to data[0], 1/4V copy to data[1], 1/4V
 *                 copy to data[2]
 *                 mode 1 for
 *                 out0 is Y+1/2u+1/2v, with the original input as the output0,
 *                 1/4U copy to data[1] 1/4V copy to data[2]
 *                 out1 is (1/2U+1/2V)+1/4U+1/4V, 1/2U & 1/2V copy to data[0],
 *                 1/4U copy to data[1], 1/4V copy to data[2]
 *
 *  \return Y/Cb/Cr data
 *
 ******************************************************************************/
void ni_copy_yuv_444p_to_420p(uint8_t *p_dst0[NI_MAX_NUM_DATA_POINTERS],
                              uint8_t *p_dst1[NI_MAX_NUM_DATA_POINTERS],
                              uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS],
                              int frame_width, int frame_height,
                              int factor, int mode)
{
    int i, j;
    int y_444p_linesize = frame_width * factor;
    int uv_444p_linesize = y_444p_linesize;
    int y_420p_linesize = NI_VPU_ALIGN128(y_444p_linesize);
    int uv_420p_linesize = NI_VPU_ALIGN128(uv_444p_linesize / 2);

    // return to avoid self copy
    if (p_dst0[0] == p_dst1[0] && p_dst0[1] == p_dst1[1] &&
        p_dst0[2] == p_dst1[2])
    {
        ni_log(NI_LOG_DEBUG, "%s: src and dst identical, return\n", __func__);
        return;
    }

    // Y component
    for (i = 0; i < frame_height; i++)
    {
        memcpy(&p_dst0[0][i * y_420p_linesize], &p_src[0][i * y_444p_linesize],
               y_444p_linesize);
    }

    if (mode == 0)
    {
        // out0 data[0]: Y  data[1]: 0.25V  data[2]: 0.25V
        // out1 data[0]: U  data[1]: 0.25V  data[2]: 0.25V
        // U component
        for (i = 0; i < frame_height; i++)
        {
            memcpy(&p_dst1[0][i * y_420p_linesize],
                   &p_src[1][i * y_444p_linesize], y_444p_linesize);
        }

        for (i = 0; i < frame_height / 2; i++)
        {
            for (j = 0; j < frame_width * factor / 2; j += factor)
            {
                // V component
                // even line
                memcpy(&p_dst0[1][i * uv_420p_linesize + j],
                       &p_src[2][2 * i * uv_444p_linesize + 2 * j],
                       factor);
                memcpy(&p_dst0[2][i * uv_420p_linesize + j],
                       &p_src[2][2 * i * uv_444p_linesize + (2 * j + factor)],
                       factor);
                // odd line
                memcpy(&p_dst1[1][i * uv_420p_linesize + j],
                       &p_src[2][(2 * i + 1) * uv_444p_linesize + 2 * j],
                       factor);
                memcpy(&p_dst1[2][i * uv_420p_linesize + j],
                       &p_src[2][(2 * i + 1) * uv_444p_linesize + (2 * j + factor)],
                       factor);
            }
        }
    } else
    {
        // out0 data[0]:  Y           data[1]: 0.25U  data[2]: 0.25V
        // out1 data[0]:  0.5U + 0.5V data[1]: 0.25U  data[2]: 0.25V
        for (i = 0; i < frame_height / 2; i++)
        {
            for (j = 0; j < frame_width * factor / 2; j += factor)
            {
                // U component
                // even line 0.25U
                memcpy(&p_dst1[1][i * uv_420p_linesize + j],
                       &p_src[1][2 * i * uv_444p_linesize + (2 * j + factor)],
                       factor);
                // odd line 0.5U
                memcpy(&p_dst1[0][2 * i * uv_444p_linesize + 2 * j],
                       &p_src[1][(2 * i + 1) * uv_444p_linesize + 2 * j],
                       factor * 2);
                // even line 0.25U
                memcpy(&p_dst0[1][i * uv_420p_linesize + j],
                       &p_src[1][2 * i * uv_444p_linesize + 2 * j],
                       factor);

                // V component
                // even line 0.25V
                memcpy(&p_dst1[2][i * uv_420p_linesize + j],
                       &p_src[2][2 * i * uv_444p_linesize + (2 * j + factor)],
                       factor);
                // odd line 0.5V
                memcpy(&p_dst1[0][(2 * i + 1) * uv_444p_linesize + 2 * j],
                       &p_src[2][(2 * i + 1) * uv_444p_linesize + 2 * j],
                       factor * 2);
                // even line 0.25V
                memcpy(&p_dst0[2][i * uv_420p_linesize + j],
                       &p_src[2][2 * i * uv_444p_linesize + 2 * j],
                       factor);
            }
        }
    }
}

// NAL operations

/*!*****************************************************************************
 *  \brief  Insert emulation prevention byte(s) as needed into the data buffer
 *
 *  \param  buf   data buffer to be worked on - new byte(s) will be inserted
 *          size  number of bytes starting from buf to check
 *
 *  \return the number of emulation prevention bytes inserted into buf, 0 if
 *          none.
 *
 *  Note: caller *MUST* ensure for newly inserted bytes, buf has enough free
 *        space starting from buf + size
 ******************************************************************************/
int ni_insert_emulation_prevent_bytes(uint8_t *buf, int size)
{
    int insert_bytes = 0;
    uint8_t *buf_curr = buf;
    uint8_t *buf_end = buf + size - 1;
    int zeros = 0, insert_ep3_byte;

    ni_log(NI_LOG_TRACE, "%s: enter\n", __func__);

    for (; buf_curr <= buf_end; buf_curr++)
    {
        if (zeros == 2)
        {
            insert_ep3_byte = (*buf_curr <= 3);
            if (insert_ep3_byte)
            {
                // move bytes from curr to end 1 position to make space for ep3
                memmove(buf_curr + 1, buf_curr, buf_end - buf_curr + 1);
                *buf_curr = 0x3;

                buf_curr++;
                buf_end++;
                insert_bytes++;
            }

            zeros = 0;
        }

        if (!*buf_curr)
        {
            zeros++;
        } else
        {
            zeros = 0;
        }
    }

    ni_log(NI_LOG_TRACE, "%s: %d, exit\n", __func__, insert_bytes);
    return insert_bytes;
}

/*!*****************************************************************************
 *  \brief  Remove emulation prevention byte(s) as needed from the data buffer
 *
 *  \param  buf   data buffer to be worked on - emu prevent byte(s) will be
 *                removed from.
 *          size  number of bytes starting from buf to check
 *
 *  \return the number of emulation prevention bytes removed from buf, 0 if
 *          none.
 *
 *  Note: buf will be modified if emu prevent byte(s) found and removed.
 ******************************************************************************/
int ni_remove_emulation_prevent_bytes(uint8_t *buf, int size)
{
    int remove_bytes = 0;
    uint8_t *buf_curr = buf;
    uint8_t *buf_end = buf + size - 1;
    int zeros = 0, remove_ep3_byte;

    ni_log(NI_LOG_TRACE, "%s: enter\n", __func__);

    for (; buf_curr < buf_end; buf_curr++)
    {
        if (zeros == 2)
        {
            remove_ep3_byte = (*buf_curr == 0x03 && *(buf_curr + 1) <= 3);
            if (remove_ep3_byte)
            {
                // move bytes after curr to end, 1 position forward to overwrite ep3
                memmove(buf_curr, buf_curr + 1, buf_end - buf_curr);

                // buf_curr unchanged
                buf_end--;
                remove_bytes++;
            }

            zeros = 0;
        }

        if (!*buf_curr)
        {
            zeros++;
        } else
        {
            zeros = 0;
        }
    }

    ni_log(NI_LOG_TRACE, "%s: %d, exit\n", __func__, remove_bytes);
    return remove_bytes;
}

/******************************************************************************
 *
 * Ai utils apis
 *
 ******************************************************************************/
static uint32_t ni_ai_type_get_bytes(const uint32_t type)
{
    switch (type)
    {
        case NI_AI_BUFFER_FORMAT_INT8:
        case NI_AI_BUFFER_FORMAT_UINT8:
            return 1;
        case NI_AI_BUFFER_FORMAT_INT16:
        case NI_AI_BUFFER_FORMAT_UINT16:
        case NI_AI_BUFFER_FORMAT_FP16:
        case NI_AI_BUFFER_FORMAT_BFP16:
            return 2;
        case NI_AI_BUFFER_FORMAT_FP32:
        case NI_AI_BUFFER_FORMAT_INT32:
        case NI_AI_BUFFER_FORMAT_UINT32:
            return 4;
        case NI_AI_BUFFER_FORMAT_FP64:
        case NI_AI_BUFFER_FORMAT_INT64:
        case NI_AI_BUFFER_FORMAT_UINT64:
            return 8;

        default:
            return 0;
    }
}

static void ni_ai_integer_convert(const void *src, void *dest,
                                  ni_ai_buffer_format_e src_dtype,
                                  ni_ai_buffer_format_e dst_dtype)
{
    unsigned char all_zeros[] = {0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00};
    unsigned char all_ones[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t src_sz = ni_ai_type_get_bytes(src_dtype);
    uint32_t dest_sz = ni_ai_type_get_bytes(dst_dtype);
    unsigned char *buffer = all_zeros;

    if (((int8_t *)src)[src_sz - 1] & 0x80)
    {
        buffer = all_ones;
    }
    memcpy(buffer, src, src_sz);
    memcpy(dest, buffer, dest_sz);
}

static float ni_ai_int8_to_fp32(signed char val, signed char fixedPointPos)
{
    float result = 0.0f;

    if (fixedPointPos > 0)
    {
        result = (float)val * (1.0f / ((float)(1 << fixedPointPos)));
    } else
    {
        result = (float)val * ((float)(1 << -fixedPointPos));
    }

    return result;
}

static float ni_ai_int16_to_fp32(int16_t val, signed char fixedPointPos)
{
    float result = 0.0f;

    if (fixedPointPos > 0)
    {
        result = (float)val * (1.0f / ((float)(1 << fixedPointPos)));
    } else
    {
        result = (float)val * ((float)(1 << -fixedPointPos));
    }

    return result;
}

static float ni_ai_uint8_to_fp32(uint8_t val, int32_t zeroPoint, float scale)
{
    float result = 0.0f;

    result = (float)(val - (uint8_t)zeroPoint) * scale;

    return result;
}

static float ni_ai_fp16_to_fp32(const short in)
{
    typedef union
    {
        unsigned int u;
        float f;
    } _fp32_t;

    const _fp32_t magic = {(254 - 15) << 23};
    const _fp32_t infnan = {(127 + 16) << 23};
    _fp32_t o;
    // Non-sign bits
    o.u = (in & 0x7fff) << 13;
    o.f *= magic.f;
    if (o.f >= infnan.f)
    {
        o.u |= 255 << 23;
    }
    //Sign bit
    o.u |= (in & 0x8000) << 16;
    return o.f;
}

static float ni_ai_affine_to_fp32(int32_t val, int32_t zeroPoint, float scale)
{
    float result = 0.0f;
    result = ((float)val - zeroPoint) * scale;
    return result;
}

static uint32_t ni_ai_get_tensor_size(int32_t *shape, uint32_t dim_num,
                                      int32_t type)
{
    uint32_t sz;
    uint32_t i;
    sz = 0;
    if (NULL == shape || 0 == dim_num)
    {
        return sz;
    }
    sz = 1;
    for (i = 0; i < dim_num; i++)
    {
        sz *= shape[i];
    }
    sz *= ni_ai_type_get_bytes(type);

    return sz;
}

static uint32_t ni_ai_get_element_num(int32_t *sizes, uint32_t num_of_dims,
                                      int32_t data_format)
{
    uint32_t num;
    uint32_t sz;
    uint32_t dsize;

    sz = ni_ai_get_tensor_size(sizes, num_of_dims, data_format);
    dsize = ni_ai_type_get_bytes(data_format);
    if (dsize)
    {
        num = (uint32_t)(sz / dsize);
    } else
    {
        num = 0;
    }

    return num;
}

static int32_t ni_ai_type_is_integer(const int32_t type)
{
    int32_t ret = 0;
    switch (type)
    {
        case NI_AI_BUFFER_FORMAT_INT8:
        case NI_AI_BUFFER_FORMAT_INT16:
        case NI_AI_BUFFER_FORMAT_INT32:
        case NI_AI_BUFFER_FORMAT_UINT8:
        case NI_AI_BUFFER_FORMAT_UINT16:
        case NI_AI_BUFFER_FORMAT_UINT32:
            ret = 1;
            break;
        default:
            break;
    }

    return ret;
}

static int32_t ni_ai_type_is_signed(const int32_t type)
{
    int32_t ret = 0;
    switch (type)
    {
        case NI_AI_BUFFER_FORMAT_INT8:
        case NI_AI_BUFFER_FORMAT_INT16:
        case NI_AI_BUFFER_FORMAT_INT32:
        case NI_AI_BUFFER_FORMAT_BFP16:
        case NI_AI_BUFFER_FORMAT_FP16:
        case NI_AI_BUFFER_FORMAT_FP32:
            ret = 1;
            break;
        default:
            break;
    }

    return ret;
}

static void ni_ai_type_get_range(int32_t type, double *max_range,
                                 double *min_range)
{
    int32_t bits;
    double from, to;
    from = 0.0;
    to = 0.0;
    bits = ni_ai_type_get_bytes(type) * 8;
    if (ni_ai_type_is_integer(type))
    {
        if (ni_ai_type_is_signed(type))
        {
            from = (double)(-(1L << (bits - 1)));
            to = (double)((1UL << (bits - 1)) - 1);
        } else
        {
            from = 0.0;
            to = (double)((1UL << bits) - 1);
        }
    }
    if (NULL != max_range)
    {
        *max_range = to;
    }
    if (NULL != min_range)
    {
        *min_range = from;
    }
}

static inline double ni_abs(double x)
{
    return x < 0 ? -x : x;
}

static double ni_ai_copy_sign(double number, double sign)
{
    double value = ni_abs(number);
    return (sign > 0) ? value : (-value);
}

static inline int ni_ai_math_floorf(double x)
{
    return x >= 0 ? (int)x : (int)x - 1;
}

static double ni_ai_rint(double x)
{
#define _EPSILON 1e-8
    double decimal;
    double inter;
    int intpart;

    intpart = (int)x;
    decimal = x - intpart;
    inter = (double)intpart;

    if (ni_abs((ni_abs(decimal) - 0.5f)) < _EPSILON)
    {
        inter += (int32_t)(inter) % 2;
    } else
    {
        return ni_ai_copy_sign(ni_ai_math_floorf(ni_abs(x) + 0.5f), x);
    }

    return inter;
}

static int32_t ni_ai_fp32_to_dfp(const float in, const signed char fl,
                                 const int32_t type)
{
    int32_t data;
    double max_range;
    double min_range;
    ni_ai_type_get_range(type, &max_range, &min_range);
    if (fl > 0)
    {
        data = (int32_t)ni_ai_rint(in * (float)(1 << fl));
    } else
    {
        data = (int32_t)ni_ai_rint(in * (1.0f / (float)(1 << -fl)));
    }
    data = ni_min(data, (int32_t)max_range);
    data = ni_max(data, (int32_t)min_range);

    return data;
}

static int32_t ni_ai_fp32_to_affine(const float in, const float scale,
                                    const int zero_point, const int32_t type)
{
    int32_t data;
    double max_range;
    double min_range;
    ni_ai_type_get_range(type, &max_range, &min_range);
    data = (int32_t)(ni_ai_rint(in / scale) + zero_point);
    data = ni_max((int32_t)min_range, ni_min((int32_t)max_range, data));
    return data;
}

static unsigned short ni_ai_fp32_to_bfp16_rtne(float in)
{
    /*
    Convert a float point to bfloat16, with round-nearest-to-even as rounding method.
    */
    unsigned short out;

    uint32_t fp32;
    memcpy(&fp32, &in, sizeof(uint32_t));

    uint32_t lsb =
        (fp32 >> 16) & 1; /* Least significant bit of resulting bfloat. */
    uint32_t rounding_bias = 0x7fff + lsb;

    if ((float)0x7FC00000 == in)
    {
        out = 0x7fc0;
    } else
    {
        fp32 += rounding_bias;
        out = (unsigned short)(fp32 >> 16);
    }

    return out;
}

static unsigned short ni_ai_fp32_to_fp16(float in)
{
    uint32_t fp32 = 0;
    uint32_t t1 = 0;
    uint32_t t2 = 0;
    uint32_t t3 = 0;
    uint32_t fp16 = 0u;

    memcpy((uint8_t *)&fp32, (uint8_t *)&in, sizeof(uint32_t));

    t1 = (fp32 & 0x80000000u) >> 16; /* sign bit. */
    t2 = (fp32 & 0x7F800000u) >> 13; /* Exponent bits */
    t3 = (fp32 & 0x007FE000u) >> 13; /* Mantissa bits, no rounding */

    if (t2 >= 0x023c00u)
    {
        fp16 = t1 | 0x7BFF; /* Don't round to infinity. */
    } else if (t2 <= 0x01c000u)
    {
        fp16 = t1;
    } else
    {
        t2 -= 0x01c000u;
        fp16 = t1 | t2 | t3;
    }

    return (unsigned short)fp16;
}

static void ni_ai_float32_to_dtype(float src, unsigned char *dst,
                                   const int32_t data_type,
                                   const int32_t quant_format,
                                   signed char fixed_point_pos, float tf_scale,
                                   int32_t tf_zerop)
{
    switch (data_type)
    {
        case NI_AI_BUFFER_FORMAT_FP32:
            *(float *)((void *)dst) = src;
            break;
        case NI_AI_BUFFER_FORMAT_FP16:
            *(int16_t *)dst = ni_ai_fp32_to_fp16(src);
            break;
        case NI_AI_BUFFER_FORMAT_BFP16:
            *(int16_t *)dst = ni_ai_fp32_to_bfp16_rtne(src);
            break;
        case NI_AI_BUFFER_FORMAT_INT8:
        case NI_AI_BUFFER_FORMAT_UINT8:
        case NI_AI_BUFFER_FORMAT_INT16:
        {
            int32_t dst_value = 0;
            switch (quant_format)
            {
                case NI_AI_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT:
                    dst_value =
                        ni_ai_fp32_to_dfp(src, fixed_point_pos, data_type);
                    break;
                case NI_AI_BUFFER_QUANTIZE_TF_ASYMM:
                    dst_value = ni_ai_fp32_to_affine(src, tf_scale, tf_zerop,
                                                     data_type);
                    break;
                case NI_AI_BUFFER_QUANTIZE_NONE:
                    dst_value = (int32_t)src;
                    break;
                default:
                    break;
            }
            ni_ai_integer_convert(&dst_value, dst, NI_AI_BUFFER_FORMAT_INT32,
                                  data_type);
        }
        break;
        default:;
    }
}

ni_retcode_t ni_network_layer_convert_output(float *dst, uint32_t dst_len,
                                             ni_packet_t *p_packet,
                                             ni_network_data_t *p_network,
                                             uint32_t layer)
{
    uint8_t *data;
    uint32_t data_len;
    ni_network_layer_params_t *p_param;

    if (!p_network || !dst || dst_len == 0 || !p_packet || !p_packet->p_data)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (layer >= p_network->output_num)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    p_param = &p_network->linfo.out_param[layer];
    data = (uint8_t *)p_packet->p_data + p_network->outset[layer].offset;
    data_len = ni_ai_network_layer_size(p_param);
    return ni_network_convert_data_to_tensor(dst, dst_len, data, data_len,
                                             p_param);
}

struct tensor_rsrc
{
    void *private;
    uint32_t total_num;
    uint32_t next_idx;
};

static int open_tensor_rsrc_file(struct tensor_rsrc *rsrc, void *data,
                                 uint32_t num)
{
    rsrc->private = (void *)fopen((const char *)data, "r");
    return rsrc->private ? 0 : -1;
}

static int get_tensor_rsrc_from_file(struct tensor_rsrc *rsrc, float *value)
{
    FILE *fp = (FILE *)rsrc->private;
    float tensor;

    if (fscanf(fp, "%f ", &tensor) == 1)
    {
        *value = tensor;
        return 0;
    }

    return -1;
}

static void close_tensor_rsrc_file(struct tensor_rsrc *rsrc)
{
    FILE *fp = (FILE *)rsrc->private;
    fclose(fp);
}

static int open_tensor_rsrc_rawdata(struct tensor_rsrc *rsrc, void *data,
                                    uint32_t len)
{
    rsrc->private = (void *)data;
    rsrc->total_num = len / sizeof(float);
    rsrc->next_idx = 0;
    return 0;
}

static int get_tensor_rsrc_from_rawdata(struct tensor_rsrc *rsrc, float *value)
{
    float *tensors = (float *)rsrc->private;

    if (rsrc->next_idx < rsrc->total_num)
    {
        *value = tensors[rsrc->next_idx];
        rsrc->next_idx++;
        return 0;
    }

    return -1;
}

static void close_tensor_rsrc_rawdata(struct tensor_rsrc *rsrc)
{
}

static ni_retcode_t ni_network_tensor_to_data(
    uint8_t *dst, uint32_t dst_len, void *tensor, uint32_t src_len,
    ni_network_layer_params_t *p_param,
    int (*open_tensor_rsrc)(struct tensor_rsrc *, void *, uint32_t),
    int (*get_tensor_rsrc)(struct tensor_rsrc *, float *),
    void (*close_tensor_rsrc)(struct tensor_rsrc *))
{
    uint8_t *tensor_data;
    uint32_t i, sz;
    uint32_t stride;
    float fval;
    int32_t data_format;
    int32_t quant_format;
    int32_t fixed_point_pos;
    float tf_scale;
    int32_t tf_zerop;
    struct tensor_rsrc *rsrc;

    data_format = p_param->data_format;
    quant_format = p_param->quant_format;
    fixed_point_pos = p_param->quant_data.dfp.fixed_point_pos;
    tf_scale = p_param->quant_data.affine.scale;
    tf_zerop = p_param->quant_data.affine.zeroPoint;

    sz = ni_ai_get_element_num((int32_t *)p_param->sizes, p_param->num_of_dims,
                               data_format);
    stride = ni_ai_type_get_bytes(data_format);

    if (sz * stride * sizeof(uint8_t) != dst_len)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    rsrc = (struct tensor_rsrc *)calloc(1, sizeof(struct tensor_rsrc));
    if (!rsrc)
    {
        return NI_RETCODE_FAILURE;
    }

    if (open_tensor_rsrc(rsrc, tensor, src_len) != 0)
    {
        free(rsrc);
        return NI_RETCODE_FAILURE;
    }

    memset(dst, 0, sz * stride * sizeof(uint8_t));
    tensor_data = dst;
    for (i = 0; i < sz; i++)
    {
        if (get_tensor_rsrc(rsrc, &fval) == 0)
        {
            ni_ai_float32_to_dtype(fval, &tensor_data[stride * i], data_format,
                                   quant_format, fixed_point_pos, tf_scale,
                                   tf_zerop);
        } else
        {
            break;
        }
    }

    close_tensor_rsrc(rsrc);
    free(rsrc);
    return NI_RETCODE_SUCCESS;
}

ni_retcode_t ni_network_layer_convert_tensor(uint8_t *dst, uint32_t dst_len,
                                             const char *tensor_file,
                                             ni_network_layer_params_t *p_param)
{
    return ni_network_tensor_to_data(
        dst, dst_len, (void *)tensor_file, 0, p_param, open_tensor_rsrc_file,
        get_tensor_rsrc_from_file, close_tensor_rsrc_file);
}

ni_retcode_t
ni_network_convert_tensor_to_data(uint8_t *dst, uint32_t dst_len, float *src,
                                  uint32_t src_len,
                                  ni_network_layer_params_t *p_param)
{
    return ni_network_tensor_to_data(
        dst, dst_len, src, src_len, p_param, open_tensor_rsrc_rawdata,
        get_tensor_rsrc_from_rawdata, close_tensor_rsrc_rawdata);
}

ni_retcode_t
ni_network_convert_data_to_tensor(float *dst, uint32_t dst_len, uint8_t *src,
                                  uint32_t src_len,
                                  ni_network_layer_params_t *p_param)
{
    uint32_t i = 0;
    uint8_t *data;
    uint32_t ele_size;
    float *dst_float = dst;
    uint32_t type_size;

    if (!src || src_len == 0 || !dst || dst_len == 0 || !p_param)
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    type_size = ni_ai_type_get_bytes(p_param->data_format);

    data = src;

    ele_size = 1;
    for (i = 0; i < p_param->num_of_dims; i++)
    {
        ele_size *= p_param->sizes[i];
    }

    if (dst_len != ele_size * sizeof(float))
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (src_len != ni_ai_network_layer_size(p_param))
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    if (p_param->data_format == NI_AI_BUFFER_FORMAT_INT8)
    {
        int32_t quant_format = p_param->quant_format;

        if (quant_format == NI_AI_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT)
        {
            uint8_t fix_pos = (uint8_t)p_param->quant_data.dfp.fixed_point_pos;

            for (i = 0; i < ele_size; i++, data += type_size)
            {
                dst_float[i] = ni_ai_int8_to_fp32(*data, fix_pos);
            }
        } else if (quant_format == NI_AI_BUFFER_QUANTIZE_TF_ASYMM)
        {
            int32_t zero_point = p_param->quant_data.affine.zeroPoint;
            float scale = p_param->quant_data.affine.scale;

            for (i = 0; i < ele_size; i++, data += type_size)
            {
                int32_t src_value = 0;
                ni_ai_integer_convert(data, &src_value,
                                      NI_AI_BUFFER_FORMAT_INT8,
                                      NI_AI_BUFFER_FORMAT_INT32);
                dst_float[i] =
                    ni_ai_affine_to_fp32(src_value, zero_point, scale);
            }
        } else
        {
            for (i = 0; i < ele_size; i++, data += type_size)
            {
                void *float_data = (void *)data;
                dst_float[i] = *((float *)float_data);
            }
        }
    } else if (p_param->data_format == NI_AI_BUFFER_FORMAT_FP16)
    {
        for (i = 0; i < ele_size; i++, data += type_size)
        {
            dst_float[i] = ni_ai_fp16_to_fp32(*((short *)data));
        }
    } else if (p_param->data_format == NI_AI_BUFFER_FORMAT_UINT8)
    {
        int32_t zero_point = p_param->quant_data.affine.zeroPoint;
        float scale = p_param->quant_data.affine.scale;

        for (i = 0; i < ele_size; i++, data += type_size)
        {
            dst_float[i] = ni_ai_uint8_to_fp32(*data, zero_point, scale);
        }
    } else if (p_param->data_format == NI_AI_BUFFER_FORMAT_INT16)
    {
        uint8_t fix_pos = p_param->quant_data.dfp.fixed_point_pos;

        for (i = 0; i < ele_size; i++, data += type_size)
        {
            dst_float[i] = ni_ai_int16_to_fp32(*((short *)data), fix_pos);
        }
    } else if (p_param->data_format == NI_AI_BUFFER_FORMAT_FP32)
    {
        for (i = 0; i < ele_size; i++, data += type_size)
        {
            void *float_data = (void *)data;
            dst_float[i] = *((float *)float_data);
        }
    } else
    {
        return NI_RETCODE_INVALID_PARAM;
    }

    return NI_RETCODE_SUCCESS;
}

uint32_t ni_ai_network_layer_size(ni_network_layer_params_t *p_param)
{
    return ni_ai_get_tensor_size((int32_t *)p_param->sizes,
                                 p_param->num_of_dims, p_param->data_format);
}

uint32_t ni_ai_network_layer_dims(ni_network_layer_params_t *p_param)
{
    uint32_t i, dims = 1;

    for (i = 0; i < p_param->num_of_dims; i++)
    {
        dims *= p_param->sizes[i];
    }

    return dims;
}

/* sha256 */
// SHA256 outputs a 32 byte digest
#define SHA256_BLOCK_SIZE 32

typedef struct SHA256CTX
{
    uint8_t aui8Data[64];
    uint32_t ui32DataLength;
    uint64_t ui64BitLength;
    uint32_t aui32State[8];
} SHA256CTX;

#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t ui32k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void ni_SHA256Transform(SHA256CTX *psCtx, const uint8_t aui8Data[])
{
    uint32_t ui32a, ui32b, ui32c, ui32d, ui32e, ui32f, ui32g, ui32h, ui32i,
        ui32j, ui32t1, ui32t2, ui32m[64];

    for (ui32i = 0, ui32j = 0; ui32i < 16; ++ui32i, ui32j += 4)
        ui32m[ui32i] = (aui8Data[ui32j] << 24) | (aui8Data[ui32j + 1] << 16) |
            (aui8Data[ui32j + 2] << 8) | (aui8Data[ui32j + 3]);
    for (; ui32i < 64; ++ui32i)
        ui32m[ui32i] = SIG1(ui32m[ui32i - 2]) + ui32m[ui32i - 7] +
            SIG0(ui32m[ui32i - 15]) + ui32m[ui32i - 16];

    ui32a = psCtx->aui32State[0];
    ui32b = psCtx->aui32State[1];
    ui32c = psCtx->aui32State[2];
    ui32d = psCtx->aui32State[3];
    ui32e = psCtx->aui32State[4];
    ui32f = psCtx->aui32State[5];
    ui32g = psCtx->aui32State[6];
    ui32h = psCtx->aui32State[7];

    for (ui32i = 0; ui32i < 64; ++ui32i)
    {
        ui32t1 = ui32h + EP1(ui32e) + CH(ui32e, ui32f, ui32g) + ui32k[ui32i] +
            ui32m[ui32i];
        ui32t2 = EP0(ui32a) + MAJ(ui32a, ui32b, ui32c);
        ui32h = ui32g;
        ui32g = ui32f;
        ui32f = ui32e;
        ui32e = ui32d + ui32t1;
        ui32d = ui32c;
        ui32c = ui32b;
        ui32b = ui32a;
        ui32a = ui32t1 + ui32t2;
    }

    psCtx->aui32State[0] += ui32a;
    psCtx->aui32State[1] += ui32b;
    psCtx->aui32State[2] += ui32c;
    psCtx->aui32State[3] += ui32d;
    psCtx->aui32State[4] += ui32e;
    psCtx->aui32State[5] += ui32f;
    psCtx->aui32State[6] += ui32g;
    psCtx->aui32State[7] += ui32h;
}

void ni_SHA256Init(SHA256CTX *psCtx)
{
    psCtx->ui32DataLength = 0;
    psCtx->ui64BitLength = 0;
    psCtx->aui32State[0] = 0x6a09e667;
    psCtx->aui32State[1] = 0xbb67ae85;
    psCtx->aui32State[2] = 0x3c6ef372;
    psCtx->aui32State[3] = 0xa54ff53a;
    psCtx->aui32State[4] = 0x510e527f;
    psCtx->aui32State[5] = 0x9b05688c;
    psCtx->aui32State[6] = 0x1f83d9ab;
    psCtx->aui32State[7] = 0x5be0cd19;
}

void ni_SHA256Update(SHA256CTX *psCtx, const uint8_t aui8Data[],
                     size_t ui32Length)
{
    uint32_t ui32i;

    for (ui32i = 0; ui32i < ui32Length; ++ui32i)
    {
        psCtx->aui8Data[psCtx->ui32DataLength] = aui8Data[ui32i];
        psCtx->ui32DataLength++;
        if (psCtx->ui32DataLength == 64)
        {
            ni_SHA256Transform(psCtx, psCtx->aui8Data);
            psCtx->ui64BitLength += 512;
            psCtx->ui32DataLength = 0;
        }
    }
}

void ni_SHA256Final(SHA256CTX *psCtx, uint8_t aui8Hash[])
{
    uint32_t ui32i;

    ui32i = psCtx->ui32DataLength;

    // Pad whatever data is left in the buffer.
    if (psCtx->ui32DataLength < 56)
    {
        psCtx->aui8Data[ui32i++] = 0x80;
        while (ui32i < 56)
            psCtx->aui8Data[ui32i++] = 0x00;
    } else
    {
        psCtx->aui8Data[ui32i++] = 0x80;
        while (ui32i < 64)
            psCtx->aui8Data[ui32i++] = 0x00;
        ni_SHA256Transform(psCtx, psCtx->aui8Data);
        memset(psCtx->aui8Data, 0, 56);
    }

    // Append to the padding the total message's length in bits and transform.
    psCtx->ui64BitLength += psCtx->ui32DataLength * 8;
    psCtx->aui8Data[63] = (uint8_t)psCtx->ui64BitLength;
    psCtx->aui8Data[62] = (uint8_t)(psCtx->ui64BitLength >> 8);
    psCtx->aui8Data[61] = (uint8_t)(psCtx->ui64BitLength >> 16);
    psCtx->aui8Data[60] = (uint8_t)(psCtx->ui64BitLength >> 24);
    psCtx->aui8Data[59] = (uint8_t)(psCtx->ui64BitLength >> 32);
    psCtx->aui8Data[58] = (uint8_t)(psCtx->ui64BitLength >> 40);
    psCtx->aui8Data[57] = (uint8_t)(psCtx->ui64BitLength >> 48);
    psCtx->aui8Data[56] = (uint8_t)(psCtx->ui64BitLength >> 56);
    ni_SHA256Transform(psCtx, psCtx->aui8Data);

    // Since this implementation uses little endian byte ordering and SHA uses big endian,
    // reverse all the bytes when copying the final state to the output hash.
    for (ui32i = 0; ui32i < 4; ++ui32i)
    {
        aui8Hash[ui32i] =
            (psCtx->aui32State[0] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 4] =
            (psCtx->aui32State[1] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 8] =
            (psCtx->aui32State[2] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 12] =
            (psCtx->aui32State[3] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 16] =
            (psCtx->aui32State[4] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 20] =
            (psCtx->aui32State[5] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 24] =
            (psCtx->aui32State[6] >> (24 - ui32i * 8)) & 0x000000ff;
        aui8Hash[ui32i + 28] =
            (psCtx->aui32State[7] >> (24 - ui32i * 8)) & 0x000000ff;
    }
}

void ni_calculate_sha256(const uint8_t aui8Data[], size_t ui32DataLength,
                         uint8_t aui8Hash[])
{
    // Create the context structure
    SHA256CTX sCtx;

    // Initialize the context
    ni_SHA256Init(&sCtx);
    // Update the context with data and data length
    ni_SHA256Update(&sCtx, aui8Data, ui32DataLength);
    // Finalize the result and put it into a hash buffer
    ni_SHA256Final(&sCtx, aui8Hash);
}

/*!*****************************************************************************
 *  \brief  Copy Descriptor data to Netint HW descriptor frame layout to be sent
 *          to encoder for encoding. Data buffer (dst) is usually allocated by
 *          ni_encoder_frame_buffer_alloc. Only necessary when metadata size in
 *          source is insufficient
 *
 *  \param[out] p_dst  pointers of Y/Cb/Cr to which data is copied
 *  \param[in]  p_src  pointers of Y/Cb/Cr from which data is copied
 *
 *  \return descriptor data
 *
 ******************************************************************************/
void ni_copy_hw_descriptors(uint8_t *p_dst[NI_MAX_NUM_DATA_POINTERS],
                            uint8_t *p_src[NI_MAX_NUM_DATA_POINTERS])
{
    // return to avoid self copy
    if (p_dst[0] == p_src[0] && p_dst[1] == p_src[1] && p_dst[2] == p_src[2] &&
        p_dst[3] == p_src[3])
    {
        ni_log(NI_LOG_DEBUG, "ni_copy_hw_yuv420p: src and dst identical, return\n");
        return;
    }
    niFrameSurface1_t *src_desc = (niFrameSurface1_t *)p_src[3];
    niFrameSurface1_t *dest_desc = (niFrameSurface1_t *)p_dst[3];
    memcpy(dest_desc, src_desc, sizeof(niFrameSurface1_t));
    ni_log(NI_LOG_DEBUG, "ni_copy_hw_descriptors dst FID Handle %d/%d src FID handle "
                   "%d/%d\n",
                   src_desc->ui16FrameIdx, src_desc->device_handle,
                   dest_desc->ui16FrameIdx, dest_desc->device_handle);
}

/*!*****************************************************************************
 *  \brief  Get libxcoder API version
 *
 *  \return char pointer to libxcoder API version
 ******************************************************************************/
char* ni_get_libxcoder_api_ver(void)
{
    static char* libxcoder_api_ver = LIBXCODER_API_VERSION;
    return libxcoder_api_ver;
}

/*!*****************************************************************************
 *  \brief  Get FW API version libxcoder is compatible with.
 *          Deprecated in favour of `ni_fmt_fw_api_ver_str(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX], &char_buf[0]);`
 *
 *  \return char pointer to FW API version libxcoder is compatible with
 ******************************************************************************/
NI_DEPRECATED char* ni_get_compat_fw_api_ver(void)
{
    static char compat_fw_api_ver_str[5] = "";
    // init static array one byte at a time to avoid msvc compiler error C2099
    if (!compat_fw_api_ver_str[0])
    {
        compat_fw_api_ver_str[0] = \
            NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX];
        compat_fw_api_ver_str[1] = '.';
        compat_fw_api_ver_str[2] = \
            NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX];
        if (ni_cmp_fw_api_ver(&NI_XCODER_REVISION[NI_XCODER_REVISION_API_MAJOR_VER_IDX], "6r") >= 0)
            compat_fw_api_ver_str[3] = \
                NI_XCODER_REVISION[NI_XCODER_REVISION_API_MINOR_VER_IDX+1];
        else
            compat_fw_api_ver_str[3] = 0;
        compat_fw_api_ver_str[4] = 0;
    }
    return &compat_fw_api_ver_str[0];
}

/*!*****************************************************************************
 *  \brief  Get formatted FW API version string from unformatted FW API version
 *          string
 *
 *  \param[in]   ver_str  pointer to string containing FW API. Only up to 3
 *                        characters will be read
 *  \param[out]  fmt_str  pointer to string buffer of at least size 5 to output
 *                        formated version string to
 *
 *  \return none
 ******************************************************************************/
void ni_fmt_fw_api_ver_str(const char ver_str[], char fmt_str[])
{
    if (!ver_str || !fmt_str) {
        return;
    }

    fmt_str[0] = ver_str[0];
    fmt_str[1] = '.';
    fmt_str[2] = ver_str[1];

    if ((ver_str[0] < '6' || (ver_str[0] == '6' && ver_str[1] <= 'q')) ||
        (ver_str[2] == 0)) {
        fmt_str[3] = 0;
    } else {
        fmt_str[3] = ver_str[2];
    }
    fmt_str[4] = 0;
}

/*!*****************************************************************************
 *  \brief  Compare two 3 character strings containing a FW API version. Handle
 *          comparision when FW API version format length changed from 2 to 3.
 *
 *  \param[in]  ver1  pointer to string containing FW API. Only up to 3
 *                    characters will be read
 *  \param[in]  ver2  pointer to string containing FW API. Only up to 3
 *                    characters will be read
 *
 *  \return 0 if ver1 == ver2, 1 if ver1 > ver2, -1 if ver1 < ver2
 ******************************************************************************/
int ni_cmp_fw_api_ver(const char ver1[], const char ver2[])
{
    int index;

    index = 0;

    if (ver1[index] > ver2[index])
        return 1;
    else if (ver1[index] < ver2[index])
        return -1;

    index++;

    if (ver1[index] > ver2[index])
        return 1;
    else if (ver1[index] < ver2[index])
        return -1;

    if ((ver1[index - 1] < '6') || ((ver1[index - 1] == '6') && (ver1[index] <= 'q')))
        return 0;

    index++;

    if (ver1[index] > ver2[index])
        return 1;
    else if (ver1[index] < ver2[index])
        return -1;

    return 0;
}

/*!*****************************************************************************
 *  \brief  Get libxcoder SW release version
 *
 *  \return char pointer to libxcoder SW release version
 ******************************************************************************/
char* ni_get_libxcoder_release_ver(void)
{
    static char release_ver_str[6] = "";
    // init static array one byte at a time to avoid compiler error C2099
    if (!release_ver_str[0])
    {
        release_ver_str[0] = NI_XCODER_REVISION[0];
        release_ver_str[1] = '.';
        release_ver_str[2] = NI_XCODER_REVISION[1];
        release_ver_str[3] = '.';
        release_ver_str[4] = NI_XCODER_REVISION[2];
        release_ver_str[5] = 0;
    }
    return &release_ver_str[0];
}

/*!*****************************************************************************
 *  \brief  Get text string for the provided error
 *
 *  \return char pointer for the provided error
 ******************************************************************************/
const char *ni_get_rc_txt(ni_retcode_t rc)
{
    int i;
    for (i = 0;
         i < sizeof(ni_err_rc_description) / sizeof(ni_err_rc_txt_entry_t); i++)
    {
        if (rc == ni_err_rc_description[i].rc)
        {
            return ni_err_rc_description[i].txt;
        }
    }
    return "rc not supported";
}

/*!*****************************************************************************
 *  \brief  retrieve key and value from 'key=value' pair
 *
 *  \param[in]   p_str    pointer to string to extract pair from
 *  \param[out]  key      pointer to key 
 *  \param[out]  value    pointer to value
 *
 *  \return return 0 if successful, otherwise 1
 *
 ******************************************************************************/
int ni_param_get_key_value(char *p_str, char *key, char *value)
{
    if (!p_str || !key || !value)
    {
        return 1;
    }

    char *p = strchr(p_str, '=');
    if (!p)
    {
        return 1;
    } else
    {
        *p = '\0';
        key[0] = '\0';
        value[0] = '\0';
        strcpy(key, p_str);
        strcpy(value, p + 1);
        return 0;
    }
}

/*!*****************************************************************************
 *  \brief  retrieve encoder config parameter values from --xcoder-params
 *
 *  \param[in]   xcoderParams    pointer to string containing xcoder params
 *  \param[out]  params          pointer to xcoder params to fill out 
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
int ni_retrieve_xcoder_params(char xcoderParams[],
                                      ni_xcoder_params_t *params,
                                      ni_session_context_t *ctx)
{
    char key[64], value[64];
    char *curr = xcoderParams, *colon_pos;
    int ret = 0;

    while (*curr)
    {
        colon_pos = strchr(curr, ':');

        if (colon_pos)
        {
            *colon_pos = '\0';
        }

        if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
            ni_param_get_key_value(curr, key, value))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: xcoder-params p_config key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_encoder_params_set_value(params, key, value);
        switch (ret)
        {
            case NI_RETCODE_PARAM_INVALID_NAME:
                ni_log(NI_LOG_ERROR, "Error: unknown option: %s.\n", key);
                break;
            case NI_RETCODE_PARAM_INVALID_VALUE:
                ni_log(NI_LOG_ERROR, "Error: invalid value for %s: %s.\n", key,
                        value);
                break;
            default:
                break;
        }

        if (NI_RETCODE_SUCCESS != ret)
        {
            ni_log(NI_LOG_ERROR, "Error: config parsing failed %d: %s\n", ret,
                    ni_get_rc_txt(ret));
            break;
        }

        if (colon_pos)
        {
            curr = colon_pos + 1;
        } else
        {
            curr += strlen(curr);
        }
    }
    ctx->keep_alive_timeout = params->cfg_enc_params.keep_alive_timeout;
    // reuse decoder_low_delay for low delay encoding to store wait interval
    // in send/recv multi-thread mode.
    ctx->decoder_low_delay = params->low_delay_mode;

    return ret;
}

/*!*****************************************************************************
 *  \brief  Retrieve custom gop config values from --xcoder-gop
 *
 *  \param[in]   xcoderGop       pointer to string containing xcoder gop
 *  \param[out]  params          pointer to xcoder params to fill out
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
int ni_retrieve_xcoder_gop(char xcoderGop[],
                           ni_xcoder_params_t *params,
                           ni_session_context_t *ctx)
{
    char key[64], value[64];
    char *curr = xcoderGop, *colon_pos;
    int ret = 0;

    while (*curr)
    {
        colon_pos = strchr(curr, ':');

        if (colon_pos)
        {
            *colon_pos = '\0';
        }

        if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
            ni_param_get_key_value(curr, key, value))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: xcoder-params p_config key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_encoder_gop_params_set_value(params, key, value);
        switch (ret)
        {
            case NI_RETCODE_PARAM_INVALID_NAME:
                ni_log(NI_LOG_ERROR, "Error: unknown option: %s.\n", key);
                break;
            case NI_RETCODE_PARAM_INVALID_VALUE:
                ni_log(NI_LOG_ERROR, "Error: invalid value for %s: %s.\n", key,
                        value);
                break;
            default:
                break;
        }

        if (NI_RETCODE_SUCCESS != ret)
        {
            ni_log(NI_LOG_ERROR, "Error: gop config parsing failed %d: %s\n", ret,
                    ni_get_rc_txt(ret));
            break;
        }

        if (colon_pos)
        {
            curr = colon_pos + 1;
        } else
        {
            curr += strlen(curr);
        }
    }

    return ret;
}

/*!*****************************************************************************
 *  \brief  retrieve decoder config parameter values from --decoder-params
 *
 *  \param[in]   xcoderParams    pointer to string containing xcoder params
 *  \param[out]  params          pointer to xcoder params to fill out 
 *  \param[out]  ctx             pointer to session context
 *
 *  \return return 0 if successful, -1 otherwise
 *
 ******************************************************************************/
int ni_retrieve_decoder_params(char xcoderParams[],
                                       ni_xcoder_params_t *params,
                                       ni_session_context_t *ctx)
{
    char key[64], value[64];
    char *curr = xcoderParams, *colon_pos;
    int ret = 0;

    while (*curr)
    {
        colon_pos = strchr(curr, ':');

        if (colon_pos)
        {
            *colon_pos = '\0';
        }

        if (strlen(curr) > sizeof(key) + sizeof(value) - 1 ||
            ni_param_get_key_value(curr, key, value))
        {
            ni_log(NI_LOG_ERROR,
                    "Error: decoder-params p_config key/value not "
                    "retrieved: %s\n",
                    curr);
            ret = -1;
            break;
        }
        ret = ni_decoder_params_set_value(params, key, value);
        switch (ret)
        {
            case NI_RETCODE_PARAM_INVALID_NAME:
                ni_log(NI_LOG_ERROR, "Error: unknown option: %s.\n", key);
                break;
            case NI_RETCODE_PARAM_INVALID_VALUE:
                ni_log(NI_LOG_ERROR, "Error: invalid value for %s: %s.\n", key,
                        value);
                break;
            default:
                break;
        }

        if (NI_RETCODE_SUCCESS != ret)
        {
            ni_log(NI_LOG_ERROR, "Error: config parsing failed %d: %s\n", ret,
                    ni_get_rc_txt(ret));
            break;
        }

        if (colon_pos)
        {
            curr = colon_pos + 1;
        } else
        {
            curr += strlen(curr);
        }
    }
    ctx->keep_alive_timeout = params->dec_input_params.keep_alive_timeout;
    ctx->decoder_low_delay = params->dec_input_params.decoder_low_delay;

    return ret;
}

/*!*****************************************************************************
 *  \brief  initialize a mutex
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_mutex_init(ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    bool rc = false;
    // error return zero 
    rc = InitializeCriticalSectionEx(mutex, 0, CRITICAL_SECTION_NO_DEBUG_INFO);
    if (rc)
    {
        return 0;
    }
    else
    {
        return -1;
    }
#else
    int rc;
    ni_pthread_mutexattr_t attr;

    rc = pthread_mutexattr_init(&attr);
    if (rc != 0)
    {
        return -1;
    }

    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    return pthread_mutex_init(mutex, &attr);
#endif
}

/*!*****************************************************************************
 *  \brief  destory a mutex
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_mutex_destroy(ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    DeleteCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_destroy(mutex);
#endif
}

/*!*****************************************************************************
 *  \brief  thread mutex lock
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_mutex_lock(ni_pthread_mutex_t *mutex)
{
    int rc = 0;
    if (mutex != NULL)
    {
#ifdef _WIN32
        EnterCriticalSection(mutex);
#else
        rc = pthread_mutex_lock(mutex);
#endif
    } else
    {
        rc = -1;
    }

    return rc;
}

/*!*****************************************************************************
 *  \brief  thread mutex unlock
 *
 *  \param[in]  thread mutex
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_mutex_unlock(ni_pthread_mutex_t *mutex)
{
    int rc = 0;
    if (mutex != NULL)
    {
#ifdef _WIN32
        LeaveCriticalSection(mutex);
#else
        rc = pthread_mutex_unlock(mutex);
#endif
    } else
    {
        rc = -1;
    }

    return rc;
}

#ifdef _WIN32
static unsigned __stdcall __thread_worker(void *arg)
{
    ni_pthread_t *t = (ni_pthread_t *)arg;
    t->rc = t->start_routine(t->arg);
    return 0;
}
#endif

/*!*****************************************************************************
 *  \brief  create a new thread
 *
 *  \param[in] thread          thread id 
 *  \param[in] attr            attributes to the new thread 
 *  \param[in] start_routine   entry of the thread routine 
 *  \param[in] arg             sole argument of the routine 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_create(ni_pthread_t *thread, const ni_pthread_attr_t *attr,
                      void *(*start_routine)(void *), void *arg)
{
#ifdef _WIN32
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->handle =
#if HAVE_WINRT
    (void *)CreateThread(NULL, 0, win32thread_worker, thread, 0, NULL);
#else
    (void *)_beginthreadex(NULL, 0, __thread_worker, thread, 0, NULL);
#endif
    return !thread->handle;
#else
    return pthread_create(thread, attr, start_routine, arg);
#endif
}

/*!*****************************************************************************
 *  \brief  join with a terminated thread
 *
 *  \param[in]  thread     thread id 
 *  \param[out] value_ptr  return status 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_join(ni_pthread_t thread, void **value_ptr)
{
#ifdef _WIN32
    DWORD rc = WaitForSingleObject(thread.handle, INFINITE);
    if (rc != WAIT_OBJECT_0)
    {
        if (rc == WAIT_ABANDONED)
            return EINVAL;
        else
            return EDEADLK;
    }
    if (value_ptr)
        *value_ptr = thread.rc;
    CloseHandle(thread.handle);
    return 0;
#else
    return pthread_join(thread, value_ptr);
#endif
}

/*!*****************************************************************************
 *  \brief  initialize condition variables
 *
 *  \param[in] cond  condition variable 
 *  \param[in] attr  attribute to the condvar 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_cond_init(ni_pthread_cond_t *cond,
                         const ni_pthread_condattr_t *attr)
{
#ifdef _WIN32
    InitializeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_init(cond, attr);
#endif
}

/*!*****************************************************************************
 *  \brief  destroy condition variables
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_cond_destroy(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    /* native condition variables do not destroy */
    return 0;
#else
    return pthread_cond_destroy(cond);
#endif
}

/*!*****************************************************************************
 *  \brief  broadcast a condition
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_cond_broadcast(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    WakeAllConditionVariable(cond);
    return 0;
#else
    return pthread_cond_broadcast(cond);
#endif
}

/*!*****************************************************************************
 *  \brief  wait on a condition
 *
 *  \param[in] cond  condition variable
 *  \param[in] mutex mutex related to the condvar
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_cond_wait(ni_pthread_cond_t *cond, ni_pthread_mutex_t *mutex)
{
#ifdef _WIN32
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
#else
    return pthread_cond_wait(cond, mutex);
#endif
}

/*!******************************************************************************
 *  \brief  signal a condition
 *
 *  \param[in] cond  condition variable
 *
 *  \return On success returns 0
 *          On failure returns <0
 *******************************************************************************/
int ni_pthread_cond_signal(ni_pthread_cond_t *cond)
{
#ifdef _WIN32
    WakeConditionVariable(cond);
    return 0;
#else
    return pthread_cond_signal(cond);
#endif
}

/*!*****************************************************************************
 *  \brief  wait on a condition
 *
 *  \param[in] cond    condition variable
 *  \param[in] mutex   mutex related to the condvar
 *  \param[in[ abstime abstract value of timeout
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_cond_timedwait(ni_pthread_cond_t *cond,
                              ni_pthread_mutex_t *mutex,
                              const struct timespec *abstime)
{
#ifdef _WIN32
    int64_t abs_ns = abstime->tv_sec * 1000000000LL + abstime->tv_nsec;
    DWORD t = (uint32_t)((abs_ns - ni_gettime_ns()) / 1000000);

    if (!SleepConditionVariableCS(cond, mutex, t))
    {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT)
            return ETIMEDOUT;
        else
            return EINVAL;
    }
    return 0;
#else
    return pthread_cond_timedwait(cond, mutex, abstime);
#endif
}

/*!*****************************************************************************
 *  \brief  examine and change mask of blocked signals
 *
 *  \param[in] how     behavior of this call, can be value of SIG_BLOCK,
 *                     SIG_UNBLOCK and  SIG_SETMASK 
 *  \param[in] set     current value of the signal mask. If NULL, the mask keeps
 *                     unchanged. 
 *  \param[in] old_set previous value of the signal mask, can be NULL. 
 *
 *  \return On success returns 0
 *          On failure returns <0
 ******************************************************************************/
int ni_pthread_sigmask(int how, const ni_sigset_t *set, ni_sigset_t *oldset)
{
#ifdef _WIN32
    return 0;
#else
    return pthread_sigmask(how, set, oldset);
#endif
}

/*!*****************************************************************************
 *  \brief  return error string according to error code from firmware
 *
 *  \param[in] rc      error code return from firmware
 *
 *  \return error string
 ******************************************************************************/
const char *ni_ai_errno_to_str(int rc)
{
    switch (rc)
    {
        case NI_AI_STATUS_SUCCESS:
            return "Success";
        case NI_AI_STATUS_GENERIC_ERROR:
            return "General Error";
        case NI_AI_STATUS_NOT_INITIALIZED:
            return "Not Initialized";
        case NI_AI_STATUS_ALREADY_INITIALIZED:
            return "Already Initialized";
        case NI_AI_STATUS_IO_BUSY:
            return "IO Busy";
        case NI_AI_STATUS_RESOURCE_NOT_AVAILABLE:
            return "Resource Not Available";
        case NI_AI_STATUS_CREATE_NETWORK_FAILED:
            return "Create Network Failed";
        case NI_AI_STATUS_INPUT_BUFFER_FULL:
            return "Input Buffer Full";
        case NI_AI_STATUS_OUTPUT_BUFFER_EMPTY:
            return "Output Buffer Empty";
        case NI_AI_STATUS_INVALID_PARAMS:
            return "Invalid Params";
        case NI_AI_STATUS_ERROR_START_NETWORK:
            return "Error Start Network";
        case NI_AI_STATUS_ERROR_SET_INOUT:
            return "Erorr Set Inout";
        case NI_AI_STATUS_BAD_OPTION:
            return "Bad Option";
        case NI_AI_STATUS_MAP_ERROR:
            return "Map Error";
        case NI_AI_STATUS_CONTEXT_NOT_AVAILABLE:
            return "Context Not Available";
        case NI_AI_STATUS_MODEL_NOT_FOUND:
            return "Model Not Found";
        case NI_AI_STATUS_IO_ERROR:
            return "IO Error";
        case NI_AI_STATUS_INVALID_ADDRESS:
            return "Invalid Address";
        case NI_AI_STATUS_OUT_OF_MEMORY:
            return "Out Of Memory";
        case NI_AI_STATUS_BAD_INOUT:
            return "Bad Inout";
        case NI_AI_STATUS_INVALID_INSTANCE:
            return "Invalid Instance";
        case NI_AI_STATUS_IO_NOT_ALLOWED:
            return "IO Not Allowed";
        case NI_AI_STATUS_NETWORK_NOT_READY:
            return "Network Not Ready";
        default:
            return "Other Error";
    }
}
/*!******************************************************************************
 *  \brief  decode the raw current obtained and determine power
 *
 *  \param[in] current_value     current value
 *  \param[in] serial_number     board SN
 *
 *  \return On success returns power
 *          On failure returns -1
 ********************************************************************************/
uint32_t ni_decode_power_measurement(uint32_t current_data, const uint8_t *serial_number)
{
    uint32_t power_mw;
    float current_ma,voltage_mv;

    if ((!serial_number) || (current_data == NI_INVALID_POWER))
    {
        return NI_INVALID_POWER;
    }
    char pcb_config[3];
    snprintf(pcb_config, sizeof(pcb_config), "%.2s", &serial_number[2]);

    float current_value = (float)current_data;
    voltage_mv = 12000.0f;
    if (strncmp(pcb_config, "A1", 2) == 0) 
    {
        current_ma = (((current_value * 1000) / TPS25940_R_IMON) - TPS25940_IMON_OS) / TPS25940_GAIN_IMON;
    }
    else if ((strncmp(pcb_config, "A2", 2) == 0) || (strncmp(pcb_config, "A3", 2) == 0))
    {
        current_ma = ((current_value * MAX15162AAWE_C_IRATIO) / MAX15162AAWE_R_IMON);
    }
    else if ((strncmp(pcb_config, "AA", 2) == 0) || (strncmp(pcb_config, "AB", 2) == 0))
    {
        current_ma = (((current_value * 1000) / (TPS25946_R_IMON_T2A * TPS25946_GAIN_IMON)) * 1000);
    }
    else if (strncmp(pcb_config, "U0", 2) == 0) 
    {
        current_ma = ((((current_value) * MAX17613B_R_ISET_TOTAL / MAX17613B_R_ISET_R2) *
                           MAX17613B_C_IRATIO) / MAX17613B_R_ISET_TOTAL);
    }
    else if ((strncmp(pcb_config, "U1", 2) == 0) || (strncmp(pcb_config, "U2", 2) == 0) || (strncmp(pcb_config, "U3", 2) == 0))
    {
        current_ma = ((1 * current_value * MAX15162AAWE_C_IRATIO) / MAX15162AAWE_R_IMON);
    }
    else if (strncmp(pcb_config, "UA", 2) == 0)
    {
        current_ma = (((current_value * 1000) / (TPS25946_R_IMON_T1U_UA * TPS25946_GAIN_IMON)) * 1000);
    }
    else if (strncmp(pcb_config, "S0", 2) == 0)
    {
        current_ma = (((current_value * 1000) / (TPS25946_R_IMON_T1S * TPS25946_GAIN_IMON)) * 1000);
    }
    else if (strncmp(pcb_config, "M0", 2) == 0)
    {
        current_ma = ((((current_value * 1000) / (TPS25946_R_IMON_T1M * TPS25946_GAIN_IMON_T1M)) * 1000) * MCU_REF_VOLTAGE) / MCU_FSR_ADC;
        voltage_mv = 3300.0f;
    }
    else
    {
        return NI_INVALID_POWER;
    }
    power_mw = (uint32_t)((voltage_mv * current_ma) / 1000.0);
    return power_mw;
}



/*!******************************************************************************
 *  \brief   Check a device can be read by ni_device_capability_query()
 *           by checking the size of the device using blockdev
 * 
 *           INFO OR ERROR logs will not be printed in this function
 *
 *  \param[in]   p_dev device path string. eg: "/dev/nvme1n2"
 *  \param[in]   size_needed The minimum required size
 *
 *  \return
 *           returns -1
 *           when the device can not be read by ni_device_capability_query()
 *           
 *           returns 1
 *           when the device can be read by ni_device_capability_query()
 *
 *           returns 0 when the result can not be determined 
 *
 *******************************************************************************/
static int ni_device_size_precheck_blockdev(const char *p_dev, const uint64_t size_needed )
{
#ifndef __linux__
    (void) p_dev;
    (void) size_needed;
    return 0;
#else

    int ret = 0;

    const char blockdev_command_dev[] = "blockdev --getsize64 %s 2>/dev/null";
    FILE *blockdev_file = NULL;
    char *blockdev_command = NULL;
    char blockdev_result [70] = {0};

    if (!p_dev)
    {
        return -1;
    }

    const size_t path_len = strlen(p_dev);

    blockdev_command = (char *)calloc(1, sizeof(blockdev_command_dev) + path_len);
    if (!blockdev_command)
    {
        LRETURN;
    }

    snprintf(blockdev_command, sizeof(blockdev_command_dev) + path_len, blockdev_command_dev, p_dev);

    ni_log2(NULL, NI_LOG_DEBUG, "%s() blockdev command %s\n", __func__, blockdev_command);

    blockdev_file = popen(blockdev_command, "r");

    if (!blockdev_file)
    {
        LRETURN;
    }
    
    if (fgets(blockdev_result, sizeof(blockdev_result), blockdev_file) == NULL)
    {
        LRETURN;
    }

    int status = pclose(blockdev_file);
    blockdev_file = NULL;

    if (status == 0) // no errors
    {
        if (*blockdev_result != '\0')
        {
            char *endptr = NULL;
            errno = 0;
            unsigned long long this_size = strtoull(blockdev_result, &endptr, 10);

            if (errno != 0 || endptr == blockdev_result)
            {
                ret = 0;
            }
            else if (this_size < size_needed)
            {
                ni_log2(NULL, NI_LOG_DEBUG, "%s() blockdev size check failed. size: %" PRIu64 "\n", __func__, this_size);
                ret = -1;
            }
            else
            {
                ret = 1;
            }
        }
    }

END:
    if (blockdev_command)
    {
        free(blockdev_command);
    }

    if (blockdev_file)
    {
        fclose(blockdev_file);
    }

    return ret;

#endif
}

/*!******************************************************************************
 *  \brief   Check a device can be read by ni_device_capability_query()
 *           by checking the size of the device using lsblk
 * 
 *           INFO OR ERROR logs will not be printed in this function
 *
 *  \param[in]   p_dev Device path string. eg: "/dev/nvme1n2"
 *  \param[in]   size_needed The minimum required size
 * 
 *  \return
 *           returns -1
 *           when the device can not be read by ni_device_capability_query()
 *           
 *           returns 1
 *           when the device can be read by ni_device_capability_query()
 *
 *           returns 0 when the result can not be determined 
 *
 *******************************************************************************/
static int ni_device_size_precheck_lsblk(const char *p_dev, const uint64_t size_needed)
{
#ifndef __linux__
    (void) p_dev;
    (void) size_needed;
    return 0;
#else

    int ret = 0;
    const char lsblk_command_dev[] = "lsblk -b -o SIZE %s 2>/dev/null";
    char *lsblk_command = NULL;
    FILE *lsblk_file = NULL;
    char lsblk_result [70] = {0};
    char *fgets_result = NULL;

    if (!p_dev)
    {
        return -1;
    }

    const size_t path_len = strlen(p_dev);

    lsblk_command = (char *)calloc(1, sizeof(lsblk_command_dev) + path_len);
    if (!lsblk_command)
    {
        LRETURN;
    }

    snprintf(lsblk_command, sizeof(lsblk_command_dev) + path_len, lsblk_command_dev, p_dev);

    ni_log2(NULL, NI_LOG_DEBUG, "%s() lsblk command %s\n", __func__, lsblk_command);

    lsblk_file = popen(lsblk_command, "r");
    if (!lsblk_file)
    {
        LRETURN;
    }

    fgets_result = fgets(lsblk_result, sizeof(lsblk_result), lsblk_file);//get the first line
    if (!fgets_result)
    {
        LRETURN;
    }

    memset(lsblk_result, 0, sizeof(lsblk_result));

    fgets_result = fgets(lsblk_result, sizeof(lsblk_result), lsblk_file);//get the next line
    if (fgets_result)
    {
        LRETURN;
    }

    int status = pclose(lsblk_file);
    lsblk_file = NULL;

    if (status == 0) // no errors
    {
        if (*lsblk_result != '\0')
        {
            char *endptr = NULL;
            errno = 0;
            unsigned long long this_size = strtoull(lsblk_result, &endptr, 10);

            if (errno != 0 || endptr == lsblk_result)
            {
                ret = 0;
            }
            else if (this_size < size_needed)
            {
                ni_log2(NULL, NI_LOG_DEBUG, "%s() lsblk size check failed. size: %" PRIu64 "\n", __func__, this_size);
                ret = -1;
            }
            else
            {
                ret = 1;
            }
        }
    }

END:
    if (lsblk_command)
    {
        free(lsblk_command);
    }

    if (lsblk_file)
    {
        pclose(lsblk_file);
    }

    return ret;

#endif
}

/*!******************************************************************************
 *  \brief   Check a device can be read by ni_device_capability_query()
 *           by checking the vendor id
 * 
 *           INFO OR ERROR logs will not be printed in this function
 *
 *  \param[in]   p_dev device path string. eg: "/dev/nvme1n2"
 *
 *  \return
 *           returns -1
 *           when the device can not be read by ni_device_capability_query()
 *           
 *           returns 1
 *           when the device can be read by ni_device_capability_query()
 *
 *           returns 0 when the result can not be determined 
 *
 *******************************************************************************/
static int ni_device_vendor_id_precheck(const char *p_dev)
{
#ifndef __linux__
    (void) p_dev;
    return 0;
#else

    int ret = 0;

    if (!p_dev)
    {
        return -1;
    }

    const char *vendor_path_devs[] = {"/sys/class/block/%s/device/vendor", "/sys/class/block/%s/device/device/vendor"};

    const char *last_slash = strrchr(p_dev, '/'); 
    const char *device_name = (last_slash ? (last_slash + 1) : p_dev);

    const size_t path_len = strlen(p_dev);

    for (size_t i = 0; i < sizeof(vendor_path_devs)/sizeof(vendor_path_devs[0]) && ret == 0; ++i)
    {
        size_t template_len = strlen(vendor_path_devs[i]);
        char *vendor_path = (char *)calloc(1, template_len + path_len);
        if (!vendor_path)
        {
            continue;
        }
        snprintf(vendor_path, template_len + path_len, vendor_path_devs[i], device_name);

        int fd = open(vendor_path, O_RDONLY);
        if (fd >= 0)
        {
            char vendor_id[10] = {0};
            int read_size = read(fd, vendor_id, sizeof(vendor_id));
            if (read_size >= 4 && (size_t)read_size < sizeof(vendor_id))
            {
                char *find_1d82 = strstr(vendor_id, "1d82");
                if (find_1d82)
                {
                    ret = 1;
                }
                else
                {
                    ni_log2(NULL, NI_LOG_DEBUG, "%s() vendor check failed. vendor id: %s", vendor_id);
                    ret = -1;
                }
            }

            close(fd);
        }

        free(vendor_path);
    }

    return ret;

#endif
}


/*!******************************************************************************
 *  \brief   Check a device can be read by ni_device_capability_query()
 *           by reading size from /sys/class/block/<devname>/size and 
 *           /sys/class/block/<devname>/queue/logical_block_size
 * 
 *           INFO OR ERROR logs will not be printed in this function
 *
 *  \param[in]   p_dev device path string. eg: "/dev/nvme1n2"
 *  \param[in]   size_needed The minimum required size
 *
 *  \return
 *           returns -1
 *           when the device can not be read by ni_device_capability_query()
 *           
 *           returns 1
 *           when the device can be read by ni_device_capability_query()
 *
 *           returns 0 when the result can not be determined 
 *
 *******************************************************************************/
static int ni_device_size_precheck_system_information(const char *p_dev, const uint64_t size_needed)
{
#ifndef __linux__
    (void) p_dev;
    (void) size_needed;
    return 0;
#else

    int ret = 0;

    if (!p_dev)
    {
        return -1;
    }

    const char block_path_dev[] = "/sys/class/block/%s/size";
    const char logical_block_size_path_dev[] = "/sys/class/block/%s/queue/logical_block_size";

    const char *last_slash = strrchr(p_dev, '/'); 
    const char *device_name = (last_slash ? (last_slash + 1) : p_dev);

    const size_t path_len = strlen(p_dev);

    int size_fd = -1;
    int logical_block_fd = -1;

    char *block_path = (char *)calloc(1, sizeof(block_path_dev) + path_len);
    char *logical_block_size_path = (char *)calloc(1, sizeof(logical_block_size_path_dev) + path_len);

    if (!block_path || !logical_block_size_path)
    {
        LRETURN;
    }

    snprintf(block_path, sizeof(block_path_dev) + path_len, block_path_dev, device_name);
    snprintf(logical_block_size_path, sizeof(logical_block_size_path_dev) + path_len, logical_block_size_path_dev, device_name);

    size_fd = open(block_path, O_RDONLY);
    logical_block_fd = open(logical_block_size_path, O_RDONLY);

    if (size_fd < 0 || logical_block_fd < 0)
    {
        LRETURN;
    }

    char this_size_str [20] = {0};
    char this_block_size_str [20] = {0};
    
    if (read(size_fd, this_size_str, sizeof(this_size_str)) <= 0 || read(logical_block_fd, this_block_size_str, sizeof(this_block_size_str)) <= 0)
    {
        ret = 0;
    }
    else
    {
        char *endptr1 = NULL;
        char *endptr2 = NULL;
        errno = 0;
        unsigned long long this_size = strtoull(this_size_str, &endptr1, 10);
        unsigned long long this_block_size = strtoull(this_block_size_str, &endptr2, 10);

        if (errno == 0 && endptr1 != this_size_str && endptr2 != this_block_size_str)
        {
            if (this_size * this_block_size >= size_needed)
            {
                ret = 1;
            }
            else
            {
                ni_log2(NULL, NI_LOG_DEBUG, "%s() read size check failed. size: %" PRIu64 "\n", __func__, this_size * this_block_size);
                ret = -1;
            }
        }
    }

END:
    if (block_path)
    {
        free(block_path);
    }
    if (logical_block_size_path)
    {
        free(logical_block_size_path);
    }

    if (size_fd >= 0)
    {
        close(size_fd);
    }
    if (logical_block_fd >= 0)
    {
        close(logical_block_fd);
    }

    return ret;

#endif
}

/*!******************************************************************************
 *  \brief   precheck a device can be read by ni_device_capability_query()
 *           INFO OR ERROR logs will not be printed in this function
 *  \param[in]   p_dev device path string. eg: "/dev/nvme1n2"
 *
 *  \return
 *           returns NI_RETCODE_FAILURE
 *           when the device can not be read by ni_device_capability_query()
 *
 *           returns NI_RETCODE_SUCCESS when
 *           1. the device can not be read by ni_device_capability_query()
 *           2. the result can not be determined to prevent query failures due to 
 *           some reasons such as missing commands on the system
 *******************************************************************************/
ni_retcode_t ni_quadra_card_identify_precheck(const char *p_dev)
{
#ifndef __linux__
    (void) p_dev;
    return 0;
#else
    int ret = 0;

    const uint64_t SIZE_NEEDED = ((IDENTIFY_DEVICE_R) << (LBA_BIT_OFFSET)) + (NI_NVME_IDENTITY_CMD_DATA_SZ);

    if (!p_dev)
    {
        return NI_RETCODE_FAILURE;
    }

    const size_t path_len = strlen(p_dev);

    if (path_len > NI_MAX_DEVICE_NAME_LEN)
    {
        return NI_RETCODE_FAILURE;
    }

    ret = ni_device_size_precheck_blockdev(p_dev, SIZE_NEEDED);
    if (ret > 0)
    {
        return NI_RETCODE_SUCCESS;
    }
    if (ret < 0)
    {
        return NI_RETCODE_FAILURE;
    }

    ret = ni_device_size_precheck_lsblk(p_dev, SIZE_NEEDED);
    if (ret > 0)
    {
        return NI_RETCODE_SUCCESS;
    }
    if (ret < 0)
    {
        return NI_RETCODE_FAILURE;
    }

    ret = ni_device_vendor_id_precheck(p_dev);
    if (ret > 0)
    {
        return NI_RETCODE_SUCCESS;
    }
    if (ret < 0)
    {
        return NI_RETCODE_FAILURE;
    }

    ret = ni_device_size_precheck_system_information(p_dev, SIZE_NEEDED);
    if (ret > 0)
    {
        return NI_RETCODE_SUCCESS;
    }
    if (ret < 0)
    {
        return NI_RETCODE_FAILURE;
    }

    // Even if all previous prechecks cannot determine the result, still return SUCCESS
    // This is to prevent query failures due to some reasons such as missing commands on the system
    return (ret >= 0) ? NI_RETCODE_SUCCESS : NI_RETCODE_FAILURE;

#endif
}
