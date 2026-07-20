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
 *  \file   ni_quadraprobe.h
 *
 *  \brief  Quadraprobe definitions
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "ni_defs.h"

#if defined(__linux__)
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#define NUM_CORES       6
#define NUM_CORES_NO_FL 5

#define NOR_CMD_WRITE     0x00000001
#define NOR_CMD_READ      0x00000002
#define NOR_CMD_ERASE     0x00000003
#define NOR_CMD_EXCEPTION 0x00000004

typedef struct {
    char sysfs_path[256];
    int domain;
    int bus;
    int slot;
    int func;
    int bar4_fd;
    unsigned char *bar4_base;
    off_t barsize;
    uint32_t core_log_offsets[NUM_CORES];
    int log_size;
    uint32_t log_artifacts_offset;
} QuadraDevice;

/* Core names indexed [0..NUM_CORES-1]: "np","dp","ep","tp","fp","fl" */
extern const char *const core_names[NUM_CORES];

/*!*****************************************************************************
 *  \brief  Dump raw firmware logs from all detected Quadra devices
 *
 *  This function enumerates all detected Quadra cards in the system,
 *  mmaps each card's BAR4, and dumps per-core logs to files in the specified output directory.
 *  It summarizes success and captures partial failures across multiple devices.
 *
 *  \param[in] outdir   Directory to write log file outputs (use "." for current directory)
 *  \param[in] core_reset_log   whether to get core reset log
 *
 *  \return 0 if all device logs were dumped successfully,
 *          1 if no Quadra devices were found,
 *          2 if one or more logs failed to dump but some succeeded,
 *         <0 for unexpected fatal/internal errors.
 ******************************************************************************/
LIB_API int ni_rsrc_log_dump(const char *outdir, bool core_reset_log);

/*!*****************************************************************************
 *  \brief  Execute a NOR flash persistent log command on all detected devices.
 *          Enumerate every Quadra card on the host, decodes nor_commands (read,
 *          write, erase, exception) and dispatches them.
 *
 *  \param[in] nor_cmd      "w", "r", "e", or "ex"
 *  \param[in] outdir       Output directory
 *
 *  \return  0 - all succeeded,
 *           1 - no devices,
 *           2 - partial failure,
 *          -1 - invalid args.
 ******************************************************************************/
LIB_API int ni_rsrc_nor_log_command(const char *nor_cmd, const char *outdir);

int  quadra_device_find(QuadraDevice **qd);
int  quadra_init_mmap(QuadraDevice *qd);
void quadra_cleanup(QuadraDevice *qd);

void fill_core_log_offsets(QuadraDevice *qd);
void fill_core_reset_log_offsets(QuadraDevice *qd);

int  dump_raw_logs(QuadraDevice *qd, const char *outdir, bool core_reset_log);
int  dump_nor_logs(QuadraDevice *qd, const char *outdir);

int  nor_cmd_decode(const char *nor_cmd, uint32_t *out_cmd);
int  quadra_nor_command(QuadraDevice *qd, uint32_t command);
int  ni_rsrc_nor_cmd_run(QuadraDevice *devs, int ndev,
                         uint32_t cmd, const char *outdir);

int  quadra_device_find_by_bdf(const char *domain_str,
                               const char *slot_str,
                               QuadraDevice **out);
int  quadra_device_find_by_block_dev(const char *blkdev, QuadraDevice **out);

#endif /* __linux__ */

#ifdef __cplusplus
}
#endif
