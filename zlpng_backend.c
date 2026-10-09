/* OpenZL 0.3.0 adapter. No Zstd symbols are exposed by this interface. */
#include "zlpng_backend.h"

#include <stdint.h>
#include <stdio.h>

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_lz.h"

#if defined(_MSC_VER)
static __declspec(thread) char zlpng_error[2048];
#else
static _Thread_local char zlpng_error[2048];
#endif

static size_t zlpng_fail(const char* message)
{
    snprintf(zlpng_error, sizeof(zlpng_error), "%s",
             message ? message : "unknown OpenZL error");
    return 0;
}

static int zlpng_valid_codec(int codec)
{
    return codec == ZLPNG_OPENZL_LZ || codec == ZLPNG_OPENZL_NUMERIC;
}

const char* zlpng_backend_error(void) { return zlpng_error; }

const char* zlpng_backend_version(int codec)
{
    return zlpng_valid_codec(codec) ? "OpenZL 0.3.0, format 27" : "invalid";
}

size_t zlpng_backend_bound(int codec, size_t n)
{
    zlpng_error[0] = '\0';
    if (!zlpng_valid_codec(codec)) return zlpng_fail("invalid codec");
    if (!n) return zlpng_fail("empty image payload");
    /* The upstream bound is guaranteed only for serial input. Reserve ample
     * additional per-chunk overhead for a single numeric input as well. The
     * encoder still checks capacity and reports an error if it cannot fit. */
    if (n > SIZE_MAX - n / 256 - 4096)
        return zlpng_fail("input size overflows compression bound");
    return n + n / 256 + 4096;
}

size_t zlpng_backend_compress(int codec, int level, int window_log,
                             unsigned element_bytes, const void* src, size_t n,
                             void* dst, size_t capacity)
{
    zlpng_error[0] = '\0';
    if (!zlpng_valid_codec(codec)) return zlpng_fail("invalid codec");
    if (!src || !dst || !n || !capacity)
        return zlpng_fail("empty or null image buffer");
    if (level < 1 || level > 7)
        return zlpng_fail("compression level must be 1..7");
    if (codec == ZLPNG_OPENZL_NUMERIC) {
        if ((element_bytes != 1 && element_bytes != 2) || n % element_bytes)
            return zlpng_fail("numeric input requires 8-bit or 16-bit samples");
        if ((uintptr_t)src % element_bytes)
            return zlpng_fail("numeric input alignment is invalid");
        if (window_log)
            return zlpng_fail("window override is supported only for native LZ");
    } else {
        if (element_bytes != 1)
            return zlpng_fail("serial input element width must be 1");
        if (window_log && (window_log < ZL_LZPARAM_WINDOWLOG_MIN ||
                           window_log > ZL_LZPARAM_WINDOWLOG_MAX))
            return zlpng_fail("invalid native LZ window size");
    }

    ZL_Compressor* compressor = ZL_Compressor_create();
    ZL_CCtx* ctx = ZL_CCtx_create();
    ZL_TypedRef* input = NULL;
    size_t result = 0;
    if (!compressor || !ctx) {
        zlpng_fail("OpenZL compression context allocation failed");
        goto cleanup;
    }
#define ZLPNG_CONFIG(parameter, value) do { \
    ZL_Report config_report = ZL_Compressor_setParameter(compressor, parameter, value); \
    if (ZL_isError(config_report)) { \
        zlpng_fail(ZL_Compressor_getErrorContextString(compressor, config_report)); \
        goto cleanup; \
    } \
} while (0)
    ZLPNG_CONFIG(ZL_CParam_formatVersion, 27);
    ZLPNG_CONFIG(ZL_CParam_compressionLevel, level);
    /* Match ZPNG's checksum-free backend configuration. */
    ZLPNG_CONFIG(ZL_CParam_compressedChecksum, ZL_TernaryParam_disable);
    ZLPNG_CONFIG(ZL_CParam_contentChecksum, ZL_TernaryParam_disable);
#undef ZLPNG_CONFIG
    ZL_GraphID graph = codec == ZLPNG_OPENZL_NUMERIC ? ZL_GRAPH_NUMERIC : ZL_GRAPH_LZ;
    ZL_Report report = ZL_Compressor_selectStartingGraphID(compressor, graph);
    if (ZL_isError(report)) {
        zlpng_fail(ZL_Compressor_getErrorContextString(compressor, report));
        goto cleanup;
    }
    report = ZL_CCtx_refCompressor(ctx, compressor);
    if (ZL_isError(report)) {
        zlpng_fail(ZL_CCtx_getErrorContextString(ctx, report));
        goto cleanup;
    }
    if (window_log) {
        ZL_IntParam p = { ZL_LzParam_windowLog, window_log };
        ZL_LocalParams local = { .intParams = { &p, 1 } };
        ZL_GraphParameters params = { .localParams = &local };
        report = ZL_CCtx_selectStartingGraphID(ctx, NULL, graph, &params);
        if (ZL_isError(report)) {
            zlpng_fail(ZL_CCtx_getErrorContextString(ctx, report));
            goto cleanup;
        }
    }
    if (codec == ZLPNG_OPENZL_NUMERIC) {
        input = ZL_TypedRef_createNumeric(src, element_bytes, n / element_bytes);
        if (!input) {
            zlpng_fail("OpenZL typed input allocation failed");
            goto cleanup;
        }
        report = ZL_CCtx_compressTypedRef(ctx, dst, capacity, input);
    } else {
        report = ZL_CCtx_compress(ctx, dst, capacity, src, n);
    }
    if (ZL_isError(report)) zlpng_fail(ZL_CCtx_getErrorContextString(ctx, report));
    else result = ZL_validResult(report);

cleanup:
    ZL_TypedRef_free(input);
    ZL_CCtx_free(ctx);
    ZL_Compressor_free(compressor);
    return result;
}

static int zlpng_inspect(const void* src, size_t n,
                         size_t* decompressed_bytes, unsigned* element_bytes,
                         int check_frame_length)
{
    zlpng_error[0] = '\0';
    if (decompressed_bytes) *decompressed_bytes = 0;
    if (element_bytes) *element_bytes = 0;
    if (!src || !n || !decompressed_bytes || !element_bytes)
        return (int)zlpng_fail("null or empty frame inspection argument");

    if (check_frame_length) {
        const ZL_Report compressed = ZL_getCompressedSize(src, n);
        if (ZL_isError(compressed) || ZL_validResult(compressed) != n)
            return (int)zlpng_fail("invalid frame length, truncation or trailing data");
    }
    ZL_FrameInfo* frame = ZL_FrameInfo_create(src, n);
    if (!frame) return (int)zlpng_fail("invalid OpenZL frame header");
    int result = 0;
    const ZL_Report outputs = ZL_FrameInfo_getNumOutputs(frame);
    if (ZL_isError(outputs) || ZL_validResult(outputs) != 1) {
        zlpng_fail("exactly one frame output is required");
        goto cleanup;
    }
    if (ZL_FrameInfo_getBundleID(frame)) {
        zlpng_fail("external dictionaries are not supported");
        goto cleanup;
    }
    const ZL_Report type = ZL_FrameInfo_getOutputType(frame, 0);
    const ZL_Report bytes = ZL_FrameInfo_getDecompressedSize(frame, 0);
    if (ZL_isError(type) || ZL_isError(bytes)) {
        zlpng_fail("invalid frame output metadata");
        goto cleanup;
    }
    const size_t kind = ZL_validResult(type);
    const size_t size = ZL_validResult(bytes);
    if (!size) {
        zlpng_fail("empty frame output");
        goto cleanup;
    }
    unsigned width = 0;
    if (kind == ZL_Type_serial) {
        const ZL_Report elements = ZL_FrameInfo_getNumElts(frame, 0);
        if (ZL_isError(elements) || ZL_validResult(elements) != size) {
            zlpng_fail("invalid serial frame dimensions");
            goto cleanup;
        }
        width = 1;
    } else if (kind == ZL_Type_numeric) {
        /* Numeric width lives in chunk metadata. The public 0.3.0 frame API
         * cannot expose it; the typed decompressor validates it below. */
        width = 0;
    } else {
        zlpng_fail("unsupported frame output type");
        goto cleanup;
    }
    *decompressed_bytes = size;
    *element_bytes = width;
    result = 1;
cleanup:
    ZL_FrameInfo_free(frame);
    return result;
}

int zlpng_backend_inspect(const void* src, size_t n,
                          size_t* decompressed_bytes, unsigned* element_bytes)
{
    return zlpng_inspect(src, n, decompressed_bytes, element_bytes, 1);
}

static size_t zlpng_decompress(int codec, unsigned expected_element,
                              const void* src, size_t n,
                              void* dst, size_t capacity)
{
    zlpng_error[0] = '\0';
    if (!zlpng_valid_codec(codec)) return zlpng_fail("invalid codec");
    if (!dst || !capacity) return zlpng_fail("null or empty output buffer");
    size_t expected = 0;
    unsigned element = 0;
    /* The decoder itself rejects truncated frames and trailing bytes. Inspect
     * only the header here, avoiding a redundant walk over every chunk. */
    if (!zlpng_inspect(src, n, &expected, &element, 0)) return 0;
    if ((codec == ZLPNG_OPENZL_LZ && element != 1) ||
        (codec == ZLPNG_OPENZL_NUMERIC && element != 0))
        return zlpng_fail("frame type disagrees with the selected codec");
    if (expected > capacity) return zlpng_fail("output buffer is too small");
    if (expected_element && expected % expected_element)
        return zlpng_fail("frame size disagrees with the expected sample width");
    /* A malformed numeric frame can declare a width greater than the wrapper
     * expects. Align for every upstream numeric width before validating it. */
    if (codec == ZLPNG_OPENZL_NUMERIC && (uintptr_t)dst % 8)
        return zlpng_fail("numeric output alignment is invalid");
    ZL_DCtx* ctx = ZL_DCtx_create();
    if (!ctx) return zlpng_fail("OpenZL decompression context allocation failed");
    ZL_OutputInfo info;
    const ZL_Report report = ZL_DCtx_decompressTyped(ctx, &info, dst, capacity, src, n);
    size_t result = 0;
    if (ZL_isError(report)) {
        zlpng_fail(ZL_DCtx_getErrorContextString(ctx, report));
    } else if (info.decompressedByteSize != expected ||
               info.type != (codec == ZLPNG_OPENZL_NUMERIC ? ZL_Type_numeric : ZL_Type_serial) ||
               (info.fixedWidth != 1 && info.fixedWidth != 2) ||
               (expected_element && info.fixedWidth != expected_element) ||
               (codec == ZLPNG_OPENZL_LZ && info.fixedWidth != 1) ||
               expected % info.fixedWidth ||
               info.numElts != expected / info.fixedWidth) {
        zlpng_fail("decoded frame metadata disagrees with its header");
    } else {
        result = expected;
    }
    ZL_DCtx_free(ctx);
    return result;
}

size_t zlpng_backend_decompress(int codec, const void* src, size_t n,
                               void* dst, size_t capacity)
{
    return zlpng_decompress(codec, 0, src, n, dst, capacity);
}

size_t zlpng_backend_decompress_typed(int codec, unsigned element_bytes,
                                     const void* src, size_t n,
                                     void* dst, size_t capacity)
{
    zlpng_error[0] = '\0';
    if ((element_bytes != 1 && element_bytes != 2) ||
        (codec == ZLPNG_OPENZL_LZ && element_bytes != 1))
        return zlpng_fail("invalid expected sample width");
    return zlpng_decompress(codec, element_bytes, src, n, dst, capacity);
}
