#ifndef ZLPNG_BACKEND_H
#define ZLPNG_BACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private one-shot OpenZL adapter used by zlpng.cpp. */
enum {
    ZLPNG_OPENZL_LZ = 1,
    ZLPNG_OPENZL_NUMERIC = 2
};

const char* zlpng_backend_version(int codec);
size_t zlpng_backend_bound(int codec, size_t input_size);
size_t zlpng_backend_compress(int codec, int level, int window_log,
                             unsigned element_bytes, const void* src, size_t n,
                             void* dst, size_t capacity);
size_t zlpng_backend_decompress(int codec, const void* src, size_t n,
                               void* dst, size_t capacity);
size_t zlpng_backend_decompress_typed(int codec, unsigned element_bytes,
                                     const void* src, size_t n,
                                     void* dst, size_t capacity);

/* Validate a complete, nonempty, self-contained, single-output frame.
 * Reject concatenated/trailing data and unsupported types.
 * Returns 1 on success. element_bytes is 1 for serial bytes, 0 for numeric
 * samples whose width is unavailable before decoding in OpenZL 0.3.0.
 * Call decompress_typed with the container's expected width (1 or 2) to
 * validate numeric width after decoding. Numeric output needs 8-byte alignment
 * because its width is untrusted until decoding completes.
 * This inspects framing; decompression validates the payload.
 * Both outputs are zeroed on failure. */
int zlpng_backend_inspect(const void* src, size_t n,
                          size_t* decompressed_bytes, unsigned* element_bytes);

/* Zero means failure for these nonempty image-payload operations. The most
 * recent bound/compress/decompress/inspect call supplies a thread-local error,
 * or an empty string on success. Returned text survives until the next call. */
const char* zlpng_backend_error(void);

#ifdef __cplusplus
}
#endif

#endif
