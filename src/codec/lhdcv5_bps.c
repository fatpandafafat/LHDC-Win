#include "lhdcv5_bps.h"
/* The internal layout of the pinned encoder. The submodule is a fixed
   dependency, so the target is written here instead of changing it. */
#include "lhdcv5_int.h"

int lhdcv5_enc_set_target_bps(lhdcv5_enc_t* handle, uint32_t bits_per_second) {
    if (handle == NULL) return LHDC_FRET_INVALID_HANDLE_CB;
    if (bits_per_second < 1000u || bits_per_second > 1000000u)
        return LHDC_FRET_INVALID_INPUT_PARAM;
    /* The library keeps the target in kilobits and reprograms the frame
       length, the achieved bitrate and the packet layout when the next
       block is encoded. */
    handle->enc.last_bitrate = bits_per_second / 1000u;
    handle->enc.update_frame_info = true;
    return LHDC_FRET_SUCCESS;
}
