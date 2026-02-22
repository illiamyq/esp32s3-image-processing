#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void image_processing_init(void);

// placeholder
void image_processing_start(void);

// first_chunk = true -- start
bool image_receive_chunk(const char *buf, size_t len, bool first_chunk);

// called after last chunk
void image_receive_finish(void);

// url of stored
const char* image_get_url(void);

bool image_get_metadata_json(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
