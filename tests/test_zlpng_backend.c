/* Standalone adapter tests; link with the production OpenZL adapter target. */
#include "zlpng_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, \
                #expression, zlpng_backend_error()); \
        exit(1); \
    } \
} while (0)

static unsigned cases;

static void roundtrip(int codec, int level, int window, unsigned width,
                      const unsigned char* source, size_t bytes)
{
    const size_t bound = zlpng_backend_bound(codec, bytes);
    CHECK(bound > bytes && bound <= SIZE_MAX / 2);
    unsigned char* encoded = malloc(bound * 2);
    unsigned char* decoded = malloc(bytes + 8);
    CHECK(encoded && decoded);
    const size_t size = zlpng_backend_compress(codec, level, window, width,
                                               source, bytes, encoded, bound);
    CHECK(size && !zlpng_backend_error()[0]);
    size_t inspected_bytes = 999;
    unsigned inspected_width = 999;
    CHECK(zlpng_backend_inspect(encoded, size, &inspected_bytes, &inspected_width));
    CHECK(inspected_bytes == bytes);
    CHECK(inspected_width == (codec == ZLPNG_OPENZL_LZ ? 1u : 0u));
    CHECK(zlpng_backend_decompress_typed(codec, width, encoded, size,
                                         decoded, bytes) == bytes);
    CHECK(!memcmp(source, decoded, bytes));
    CHECK(zlpng_backend_decompress(codec, encoded, size, decoded, bytes) == bytes);
    CHECK(!memcmp(source, decoded, bytes));

    CHECK(!zlpng_backend_decompress_typed(codec, width, encoded, size,
                                          decoded, bytes - 1));
    CHECK(zlpng_backend_error()[0]);
    CHECK(!zlpng_backend_decompress(codec == ZLPNG_OPENZL_LZ ? ZLPNG_OPENZL_NUMERIC :
                                     ZLPNG_OPENZL_LZ, encoded, size, decoded, bytes));
    if (codec == ZLPNG_OPENZL_NUMERIC) {
        CHECK(!zlpng_backend_decompress_typed(codec, width == 1 ? 2 : 1,
                                              encoded, size, decoded, bytes));
        CHECK(!zlpng_backend_decompress_typed(codec, width, encoded, size,
                                              decoded + 1, bytes));
    }
    const size_t truncation_step = size < 8192 ? 1 : size / 64;
    for (size_t truncated = 0; truncated < size; truncated += truncation_step) {
        inspected_bytes = 999;
        inspected_width = 999;
        CHECK(!zlpng_backend_inspect(encoded, truncated, &inspected_bytes, &inspected_width));
        CHECK(!inspected_bytes && !inspected_width);
    }
    CHECK(!zlpng_backend_inspect(encoded, size - 1, &inspected_bytes, &inspected_width));
    encoded[size] = 0;
    CHECK(!zlpng_backend_inspect(encoded, size + 1, &inspected_bytes, &inspected_width));
    CHECK(!zlpng_backend_decompress(codec, encoded, size + 1, decoded, bytes));
    memcpy(encoded + size, encoded, size);
    CHECK(!zlpng_backend_inspect(encoded, size * 2, &inspected_bytes, &inspected_width));
    CHECK(!zlpng_backend_decompress(codec, encoded, size * 2, decoded, bytes));

    /* Error state must not leak into a subsequent successful call. */
    CHECK(zlpng_backend_inspect(encoded, size, &inspected_bytes, &inspected_width));
    CHECK(!zlpng_backend_error()[0]);
    free(decoded);
    free(encoded);
    ++cases;
}

int main(void)
{
    unsigned char* source = malloc(514);
    CHECK(source);
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        uint32_t state = 42;
        for (unsigned i = 0; i < 512; ++i) {
            state = state * 1664525u + 1013904223u;
            source[i] = pattern == 0 ? 0 : pattern == 1 ? (unsigned char)i :
                        (unsigned char)(state >> 24);
        }
        for (unsigned width = 1; width <= 2; ++width) {
            const int levels[] = {1, 6, 7};
            for (unsigned index = 0; index < sizeof(levels) / sizeof(levels[0]); ++index)
                roundtrip(ZLPNG_OPENZL_NUMERIC, levels[index], 0, width, source, 512);
        }
        roundtrip(ZLPNG_OPENZL_LZ, 1, 0, 1, source, 512);
        roundtrip(ZLPNG_OPENZL_LZ, 1, 16, 1, source, 512);
        roundtrip(ZLPNG_OPENZL_LZ, 3, 0, 1, source, 511);
    }
    unsigned char output[32];
    size_t bytes = 123;
    unsigned width = 123;
    CHECK(!zlpng_backend_inspect(NULL, 0, &bytes, &width));
    CHECK(!bytes && !width);
    CHECK(!zlpng_backend_bound(0, 512));
    CHECK(!zlpng_backend_bound(ZLPNG_OPENZL_LZ, 0));
    CHECK(!zlpng_backend_bound(ZLPNG_OPENZL_LZ, SIZE_MAX));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_NUMERIC, 1, 0, 2,
                                   source + 1, 512, output, sizeof(output)));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_NUMERIC, 1, 0, 2,
                                   source, 511, output, sizeof(output)));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_NUMERIC, 1, 16, 2,
                                   source, 512, output, sizeof(output)));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_LZ, 1, 9, 1,
                                   source, 512, output, sizeof(output)));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_LZ, 8, 0, 1,
                                   source, 512, output, sizeof(output)));
    CHECK(!zlpng_backend_compress(ZLPNG_OPENZL_LZ, 1, 0, 1,
                                   source, 512, output, 1));
    free(source);
    /* Numeric graphs need additional stream metadata beyond the serial bound.
     * Exercise large incompressible inputs and the store-on-expansion path. */
    const size_t large_bytes = 1u << 20;
    source = malloc(large_bytes);
    CHECK(source);
    uint32_t random_state = 12345;
    for (size_t i = 0; i < large_bytes; ++i) {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        source[i] = (unsigned char)random_state;
    }
    roundtrip(ZLPNG_OPENZL_LZ, 1, 16, 1, source, large_bytes);
    roundtrip(ZLPNG_OPENZL_NUMERIC, 1, 0, 1, source, large_bytes);
    roundtrip(ZLPNG_OPENZL_NUMERIC, 1, 0, 2, source, large_bytes);
    roundtrip(ZLPNG_OPENZL_NUMERIC, 7, 0, 2, source, large_bytes);
    free(source);
    printf("Passed %u backend roundtrip/framing cases and invalid-argument checks\n", cases);
    return 0;
}
