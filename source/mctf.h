#ifdef NI_HAVE_MCTF
#include "ni_device_api.h"

#define MCTF_LOOKBACK 1
#define MCTF_LOOKAHEAD 1

typedef enum {
    MCTF_RET_NOT_REF       = 0,
    MCTF_RET_BUFFERED      = 1,
    MCTF_RET_FILTERED      = 2,
    MCTF_RET_LOOKBACK_REF  = 3,
    MCTF_RET_EOS_PENDING   = 4,
    MCTF_RET_EOS_DONE      = 5,
} MCTFResult;

int performMCTF(ni_session_context_t *p_ctx, ni_frame_t *ni_frame, int32_t source_width, int32_t source_height);
int initMCTF(ni_session_context_t *p_ctx);
void clearMCTF(ni_session_context_t *p_ctx);
ni_frame_t* get_mctf_result(ni_session_context_t *p_ctx, int waitCnt);
#endif
