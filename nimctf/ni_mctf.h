#ifndef NI_MCTF_FILTER_H
#define NI_MCTF_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#define MACRO_TO_STR(s) #s
#define MACROS_TO_VER_STR(a, b) MACRO_TO_STR(a.b)

#define NI_MCTF_VERSION_MAJOR 1
#define NI_MCTF_VERSION_MINOR 0
#define NI_MCTF_VERSION_STRING MACROS_TO_VER_STR(NI_MCTF_VERSION_MAJOR,NI_MCTF_VERSION_MINOR)

typedef struct {
    uint8_t* ptr;
    int width;
    int height;
    int stride;
} niYUVPlane;

typedef struct {
    niYUVPlane planes[3];
} niYUVFrame;

//ni mctf API(s):
void * ni_mctf_handle_create();

bool ni_mctf_initialize(void *handle, int width, int height, int period, int lookback, int lookahead, int window_size);

int ni_mctf_get_filter_poc(void *handle);

void ni_mctf_buffer_ref_frame(void *handle, niYUVFrame *pic, uint64_t frame_num);

void ni_mctf_process_directly(void *handle, niYUVFrame *filtered_frame);

void ni_mctf_process(void *handle, niYUVFrame **pic_list, uint64_t frame_num);

void ni_mctf_handle_release(void *handle);

#endif /* NI_MCTF_FILTER_H */
