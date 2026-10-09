#ifndef CAT_ZLPNG_H
#define CAT_ZLPNG_H

/* C and C++ API for the OpenZL-based ZLpng format. */
#include <stdint.h>

typedef struct ZLPNG_Buffer {
    unsigned char* Data;
    unsigned Bytes;
} ZLPNG_Buffer;

typedef struct ZLPNG_ImageData {
    ZLPNG_Buffer Buffer;
    unsigned BytesPerChannel;
    unsigned Channels;
    unsigned WidthPixels;
    unsigned HeightPixels;
    unsigned StrideBytes;
} ZLPNG_ImageData;

#ifdef __cplusplus
extern "C" {
#endif

/* Losslessly compress with median edge prediction and OpenZL. Effort 1..8
 * controls backend work; 1 is fastest. Higher effort may increase file size.
 * Input: 1..4 channels, 1 or 2 bytes per channel, dimensions 1..65535,
 * little-endian 16-bit samples. StrideBytes may include row padding;
 * Buffer.Bytes must cover the last packed row. Failure returns {NULL, 0}.
 * Release successful output with ZLPNG_Free(). Calls are thread-safe. */
ZLPNG_Buffer ZLPNG_Compress(const ZLPNG_ImageData* image, unsigned effort);

/* Decode a self-contained ZLP2 stream. Encoder effort is not
 * needed. Output rows are packed. Failure returns an all-zero image.
 * This API does not decode original ZPNG or earlier ZLP1 streams.
 * The default maximum raw image size is UINT_MAX. For files from untrusted
 * sources, use ZLPNG_DecompressWithLimit with an application-specific limit. */
ZLPNG_ImageData ZLPNG_Decompress(ZLPNG_Buffer encoded);

/* As above, rejecting images larger than max_raw_bytes before allocating
 * pixel storage. This limits returned image size, not total decoder scratch
 * memory. A zero limit rejects all images. */
ZLPNG_ImageData ZLPNG_DecompressWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes);

/* Explicit worker limit: 0 = automatic (up to 8), 1 = single thread.
 * The regular API uses 0. Thread count does not affect compressed bytes. */
ZLPNG_Buffer ZLPNG_CompressThreads(const ZLPNG_ImageData* image, unsigned effort, unsigned threads);
ZLPNG_ImageData ZLPNG_DecompressThreadsWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes, unsigned threads);

/* Release library-owned storage and reset the buffer to {NULL, 0}. */
void ZLPNG_Free(ZLPNG_Buffer* buffer);

#ifdef __cplusplus
}
#endif
#endif
