// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MkwPs5AudioStats {
    int opens, closes, blocks, last_error, retained_buffers;
} MkwPs5AudioStats;
void mkw_ps5_audio_get_stats(MkwPs5AudioStats* output);
#ifdef __cplusplus
}
#endif
