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
 *  \file   ni_host_ts.c
 *
 *  \brief  Set Host timestamp
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/utsname.h>

#include "ni_host_ts.h"
#include "ni_quadraprobe.h"
#include "ni_nvme.h"
#include "ni_defs.h"
#include "ni_device_api.h"
#include "ni_util.h"

#if defined(__linux__)

#define TS_DEBOUNCE_MS       2000ULL

static uint64_t s_last_ts_ms = 0;

static uint64_t get_epoch_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/*!*****************************************************************************
 *  \brief  Resolve the NVMe namespace block device from a sysfs PCI path.
 *
 *  \param[in]  sysfs_path  PCI device sysfs directory.
 *  \param[out] blk_dev     Output buffer for the /dev/nvmeXnY path.
 *  \param[in]  blk_dev_sz  Size of blk_dev buffer.
 *
 *  \return  0 on success, -1 if not found.
 ******************************************************************************/
static int sysfs_to_blk_dev(const char *sysfs_path, char *blk_dev,
                             size_t blk_dev_sz)
{
    char nvme_dir[PATH_MAX];
    int n = snprintf(nvme_dir, sizeof(nvme_dir), "%s/nvme", sysfs_path);
    if (n < 0 || n >= (int)sizeof(nvme_dir))
        return -1;

    DIR *d = opendir(nvme_dir);
    if (!d)
        return -1;

    char ctrl_name[64] = {0};
    struct dirent *entry;
    while ((entry = readdir(d))) // NOLINT(concurrency-mt-unsafe) - single-threaded
    {
        if (entry->d_name[0] == '.')
            continue;
        if (strncmp(entry->d_name, "nvme", 4) == 0)
        {
            int m = snprintf(ctrl_name, sizeof(ctrl_name), "%s", entry->d_name);
            if (m >= 0 && m < (int)sizeof(ctrl_name))
                break;
        }
    }
    (void)closedir(d);

    if (ctrl_name[0] == '\0')
        return -1;

    char ns_dir[PATH_MAX];
    n = snprintf(ns_dir, sizeof(ns_dir), "%s/nvme/%s", sysfs_path, ctrl_name);
    if (n < 0 || n >= (int)sizeof(ns_dir))
        return -1;

    d = opendir(ns_dir);
    if (!d)
        return -1;

    int found = 0;
    while ((entry = readdir(d))) // NOLINT(concurrency-mt-unsafe) - single-threaded
    {
        if (entry->d_name[0] == '.')
            continue;
        if (strncmp(entry->d_name, ctrl_name, strlen(ctrl_name)) == 0 &&
            entry->d_name[strlen(ctrl_name)] == 'n')
        {
            int k = snprintf(blk_dev, blk_dev_sz, "/dev/%s", entry->d_name);
            if (k >= 0 && k < (int)blk_dev_sz)
                found = 1;
            break;
        }
    }
    (void)closedir(d);
    return found ? 0 : -1;
}

/*!*****************************************************************************
 *  \brief  Send a custom config-session IO write command to set the host
 *          timestamp on the Quadra firmware.
 *
 *          Failures are silently ignored; timestamp setting is best-effort.
 ******************************************************************************/
static void send_set_features_timestamp(const char *blk_dev, uint64_t epoch_ms)
{
    ni_device_handle_t fd = ni_device_open2(blk_dev, NI_DEVICE_READ_WRITE);
    if (fd == NI_INVALID_DEVICE_HANDLE)
        return;

    ni_device_capability_t cap;
    memset(&cap, 0, sizeof(cap));
    if (ni_device_capability_query2(fd, &cap, false) != NI_RETCODE_SUCCESS)
    {
        ni_log(NI_LOG_DEBUG,
               "%s: skipping host timestamp on %s: capability query failed\n",
               __func__, blk_dev);
        ni_device_close(fd);
        return;
    }

    if (ni_cmp_fw_api_ver(
            (char *)&cap.fw_rev[NI_XCODER_REVISION_API_MAJOR_VER_IDX],
            "6tB") < 0)
    {
        ni_log(NI_LOG_DEBUG,
               "%s: skipping host timestamp on %s: FW API version needs to be >= 6tB\n",
               __func__, blk_dev);
        ni_device_close(fd);
        return;
    }

    void *buf = NULL;
    if (ni_posix_memalign(&buf, sysconf(_SC_PAGESIZE), NI_DATA_BUFFER_LEN))
    {
        ni_log(NI_LOG_ERROR,
               "%s: failed to allocate aligned buffer for %s\n",
               __func__, blk_dev);
        ni_device_close(fd);
        return;
    }
    memset(buf, 0, NI_DATA_BUFFER_LEN);
    memcpy(buf, &epoch_ms, sizeof(epoch_ms));

    uint32_t lba = CONFIG_SESSION_SetHostTime_W(0);
    (void)ni_nvme_send_write_cmd(fd, NI_INVALID_EVENT_HANDLE, buf,
                                 NI_DATA_BUFFER_LEN, lba);
    ni_aligned_free(buf);
    ni_device_close(fd);
}

static int get_kernel_version(void)
{
    struct utsname uts;
    if (uname(&uts) != 0)
        return 0;

    int major = 0, minor = 0, patch = 0;
    if (sscanf(uts.release, "%d.%d.%d", &major, &minor, &patch) != 3) // NOLINT(cert-err34-c) - return value checked
        return 0;

    return major * 10000 + minor * 100 + patch;
}

void ni_host_ts_set(void)
{
    uint64_t epoch_ms = get_epoch_ms();
    if (epoch_ms == 0)
        return;

    if (s_last_ts_ms != 0 && (epoch_ms - s_last_ts_ms) < TS_DEBOUNCE_MS)
        return;

    s_last_ts_ms = epoch_ms;

    time_t now = (time_t)(epoch_ms / 1000);
    char datebuf[64];
    struct tm tm_buf;

    int kernel_ver = get_kernel_version();

    if (kernel_ver >= 41400)
        return;

    if (localtime_r(&now, &tm_buf) &&
        strftime(datebuf, sizeof(datebuf), "%c", &tm_buf) > 0)
        ni_log(NI_LOG_DEBUG, "Setting the date to %s (%d)\n", datebuf, kernel_ver);

    QuadraDevice *devs = NULL;
    int ndev = quadra_device_find(&devs);
    if (ndev <= 0)
    {
        free(devs);
        return;
    }

    for (int i = 0; i < ndev; i++)
    {
        char blk_dev[PATH_MAX];
        if (sysfs_to_blk_dev(devs[i].sysfs_path, blk_dev, sizeof(blk_dev)) != 0)
            continue;

        ni_log(NI_LOG_DEBUG, "Setting timestamp on %s\n", blk_dev);
        send_set_features_timestamp(blk_dev, epoch_ms);
    }

    free(devs);
}

#endif /* __linux__ */

