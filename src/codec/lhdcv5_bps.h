#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct lhdcv5_enc lhdcv5_enc_t;
/* Targets an exact bitrate on an encoder handle.
   The pinned encoder addresses its rates through an index table, so a rate
   outside that table cannot be selected through the public API. This entry
   point writes the kilobit target the library keeps, and the library
   reprograms the frame layout on the next encoded block. */
int lhdcv5_enc_set_target_bps(lhdcv5_enc_t* handle, uint32_t bits_per_second);
#ifdef __cplusplus
}
#endif
