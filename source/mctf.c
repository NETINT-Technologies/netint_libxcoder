#include "../build/xcoder_auto_headers.h"

#ifdef NI_HAVE_MCTF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>

#include <unistd.h>
#include "mctf.h"
#include "ni_mctf.h"
#include "ni_log.h"
#include "ni_util.h"

#define MCTF_PERIOD 4
#define WINDOW_SIZE (MCTF_LOOKBACK + 1 + MCTF_LOOKAHEAD)
static const int lookback = MCTF_LOOKBACK;
static const int lookahead = MCTF_LOOKAHEAD;

typedef struct MCTFContext {
    ni_frame_t frameBuffer[MCTF_LOOKAHEAD];
    uint64_t frame_number;
    void *mctf_handle;
} MCTFContext;

static void copy_ni_frame(ni_frame_t* dst, ni_frame_t* src)
{
    void *p_metadata_buffer = dst->p_metadata_buffer;
    uint8_t separate_metadata = src->separate_metadata;
    uint8_t separate_start = (src->separate_start && src->total_start_len) ? 1 : 0;
    if (separate_metadata) {
        if ((p_metadata_buffer == NULL) ||
            (dst->metadata_buffer_size != src->metadata_buffer_size))
        {
            if (p_metadata_buffer)
                free(p_metadata_buffer);

            if (ni_posix_memalign(&p_metadata_buffer, sysconf(_SC_PAGESIZE), src->metadata_buffer_size))
            {
                ni_log(NI_LOG_ERROR, "ERROR: memory allocation failed for metadata buffer\n");
                return;
            }
        }
        memcpy(p_metadata_buffer, src->p_metadata_buffer, src->metadata_buffer_size);
    }

    void* p_start_buffer = dst->p_start_buffer;
    if (separate_start) {
        if ((dst->p_start_buffer == NULL) ||
            (dst->start_buffer_size != src->start_buffer_size))
        {
            if (dst->p_start_buffer)
                free(dst->p_start_buffer);

            if (ni_posix_memalign(&p_start_buffer, sysconf(_SC_PAGESIZE), src->start_buffer_size))
            {
                ni_log(NI_LOG_ERROR, "ERROR: memory allocation failed for start buffer\n");
                return;
            }
        }
        memcpy(p_start_buffer, src->p_start_buffer, src->start_buffer_size);
    }

    void* frame_buffer = dst->p_buffer;
    if (frame_buffer == NULL || dst->buffer_size < src->buffer_size)
    {
        if (frame_buffer)
            free(frame_buffer);
        if (ni_posix_memalign(&frame_buffer, sysconf(_SC_PAGESIZE), src->buffer_size))
        {
            ni_log(NI_LOG_ERROR, "ERROR: memory allocation failed for frame buffer\n");
            return;
        }
    }
    uint32_t frame_size_bytes = src->data_len[0] + src->data_len[1] + src->data_len[2] + src->data_len[3] + src->extra_data_len;
    memcpy(frame_buffer, src->p_buffer, frame_size_bytes);

    memcpy(dst, src, sizeof(ni_frame_t));
    dst->p_buffer = (uint8_t*)frame_buffer;
    dst->p_metadata_buffer = (uint8_t*)p_metadata_buffer;
    dst->p_start_buffer = (uint8_t*)p_start_buffer;
    if (src->p_data[0] != NULL)
        dst->p_data[0] = dst->p_buffer + (src->p_data[0] - src->p_buffer);
    if (src->p_data[1] != NULL)
        dst->p_data[1] = dst->p_buffer + (src->p_data[1] - src->p_buffer);
    if (src->p_data[2] != NULL)
        dst->p_data[2] = dst->p_buffer + (src->p_data[2] - src->p_buffer);
    if (src->p_data[3] != NULL)
        dst->p_data[3] = dst->p_buffer + (src->p_data[3] - src->p_buffer);
}

int performMCTF(ni_session_context_t *p_ctx, ni_frame_t *ni_frame, int32_t source_width, int32_t source_height)
{
    int ret = 0;
    int is_ref = 0;
    int mctf_filter_poc = 0;
    MCTFContext *ctx = (MCTFContext *)(p_ctx->mctf);
    if (ni_frame->end_of_stream) {
        if (p_ctx->frame_num < ctx->frame_number) {
            return MCTF_RET_EOS_PENDING;
        } else {
            return MCTF_RET_EOS_DONE;
        }
    }

    // check if current pic needs copy
    mctf_filter_poc = ni_mctf_get_filter_poc(ctx->mctf_handle);
    if ((ctx->frame_number >= mctf_filter_poc && ctx->frame_number - mctf_filter_poc <= lookahead) ||
        (ctx->frame_number < mctf_filter_poc && mctf_filter_poc - ctx->frame_number <= lookback)) {
        is_ref = true;
    }

    if (is_ref) {
        //construct niYUVFrame from ni_frame_t
        niYUVFrame ref_frame;
        int plane_stride[4] = {0};
        int plane_height[4] = {0};

        memset(&ref_frame, 0, sizeof(niYUVFrame));
        ni_get_hw_yuv420p_dim(source_width, source_height, 1, 0, plane_stride, plane_height);
        for (int i = 0; i < 3; i++) {
            ref_frame.planes[i].ptr = ni_frame->p_data[i];
            ref_frame.planes[i].width = source_width;
            ref_frame.planes[i].height = plane_height[i];
            ref_frame.planes[i].stride = plane_stride[i];

          if (i > 0) {
              ref_frame.planes[i].width /= 2;
          }
        }

        //frame will be buffered in the internally specified YUV layout
        //once the frames collected are ready, the MCTF filter process will be performed
        ni_mctf_buffer_ref_frame(ctx->mctf_handle, &ref_frame, ctx->frame_number);

        if ((ctx->frame_number >= mctf_filter_poc) && (ctx->frame_number - mctf_filter_poc < lookahead)) {
            // need to wait for future reference frames
            // buffer current frame
            int idx = (ctx->frame_number - mctf_filter_poc);
            copy_ni_frame(&(ctx->frameBuffer[idx]), ni_frame);
            ret = MCTF_RET_BUFFERED;
        } else if (ctx->frame_number == (mctf_filter_poc + lookahead)) {
            // do MCTF now
            niYUVFrame filtered_frame;
            memset(&filtered_frame, 0, sizeof(niYUVFrame));

            for (int i = 0; i < 3; i++) {
                filtered_frame.planes[i].ptr = ctx->frameBuffer[0].p_data[i];
                filtered_frame.planes[i].width = source_width;
                filtered_frame.planes[i].height = plane_height[i];
                filtered_frame.planes[i].stride = plane_stride[i];

              if (i > 0) {
                  filtered_frame.planes[i].width /= 2;
              }
            }

            ni_mctf_process_directly (ctx->mctf_handle, &filtered_frame);
            ret = MCTF_RET_FILTERED;
        } else {
            ret = MCTF_RET_LOOKBACK_REF;
        }
    } else {
        ret = MCTF_RET_NOT_REF;
    }

    ctx->frame_number++;

    return ret;
}

ni_frame_t* get_mctf_result(ni_session_context_t *p_ctx, int waitCnt)
{
    MCTFContext *ctx = (MCTFContext *)(p_ctx->mctf);
    if (waitCnt > 0) {
        int idx = lookahead - waitCnt;
        if (ctx && ctx->frameBuffer[idx].p_buffer) {
            ni_frame_t *result_frame = &(ctx->frameBuffer[idx]);
            return result_frame;
        }
    }
    return NULL; // No result available
}

int initMCTF(ni_session_context_t *p_ctx)
{
    MCTFContext *ctx = (MCTFContext *)calloc(1, sizeof(MCTFContext));
    if (!ctx) {
        return -1; // Memory allocation failed
    }

    ctx->mctf_handle = ni_mctf_handle_create();
    if (!ctx->mctf_handle) {
        free(ctx);
        return -1; // MCTF handle allocation failed
    }

    if (!ni_mctf_initialize(ctx->mctf_handle, p_ctx->active_video_width, p_ctx->active_video_height,
        MCTF_PERIOD, MCTF_LOOKBACK, MCTF_LOOKAHEAD, WINDOW_SIZE)) {
        ni_mctf_handle_release(ctx->mctf_handle);
        free(ctx);
        return -1; // init failed
    }

    ni_log(NI_LOG_INFO, "MCTF init, wxh %dx%d\n", p_ctx->active_video_width, p_ctx->active_video_height);
    p_ctx->mctf = ctx;

    return 0;
}

void clearMCTF(ni_session_context_t *p_ctx)
{
    if (p_ctx->mctf) {
        MCTFContext *ctx = (MCTFContext *)p_ctx->mctf;
        for (int i = 0; i < lookahead; i++) {
            ni_frame_buffer_free(&(ctx->frameBuffer[i]));
        }
        ni_mctf_handle_release(ctx->mctf_handle);
        free(p_ctx->mctf);
        p_ctx->mctf = NULL;
    }
}
#endif
