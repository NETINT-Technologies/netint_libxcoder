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
#if defined(__linux__)
LIB_API int ni_rsrc_log_dump(const char *outdir, bool core_reset_log);
#endif

#ifdef __cplusplus
}
#endif
