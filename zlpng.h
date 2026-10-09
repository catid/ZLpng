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

/* Losslessly compress an image, trying the first min(effort, pool size)
 * methods and retaining the smallest complete stream. Effort must be 1..8.
 * Input has 1..4 channels, 1 or 2 bytes per channel, dimensions 1..65535,
 * and little-endian 16-bit samples. StrideBytes may include row padding;
 * Buffer.Bytes must cover the last packed row. Failure returns {NULL, 0}.
 * Release successful output with ZLPNG_Free(). */
ZLPNG_Buffer ZLPNG_Compress(const ZLPNG_ImageData* image, unsigned effort);

/* Decode a self-contained ZLP1 stream. Encoder policy and effort are not
 * needed. Output rows are packed. Failure returns an all-zero image.
 * This API does not decode original ZPNG or experimental ZPF streams.
 * The default maximum raw image size is UINT_MAX. For files from untrusted
 * sources, use ZLPNG_DecompressWithLimit with an application-specific limit. */
ZLPNG_ImageData ZLPNG_Decompress(ZLPNG_Buffer encoded);

/* As above, rejecting images larger than max_raw_bytes before allocating
 * pixel storage. This limits returned image size, not total decoder scratch
 * memory. A zero limit rejects all images. */
ZLPNG_ImageData ZLPNG_DecompressWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes);

/* Release library-owned storage and reset the buffer to {NULL, 0}. */
void ZLPNG_Free(ZLPNG_Buffer* buffer);

#ifdef __cplusplus
}
#endif
#endif
