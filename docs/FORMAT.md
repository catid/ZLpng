# Format and API

ZLpng stores exact 1–4 channel images with 8-bit or little-endian 16-bit
samples. Dimensions are 1–65535; buffers use unsigned 32-bit lengths.
Alpha and hidden RGB are preserved. Image-file metadata is outside this API.

```c
#include "zlpng.h"

ZLPNG_Buffer file = ZLPNG_Compress(&image, 1);
if (file.Data) {
    ZLPNG_ImageData restored = ZLPNG_Decompress(file);
    /* restored.Buffer.Data is non-null on success. */
    ZLPNG_Free(&restored.Buffer);
    ZLPNG_Free(&file);
}
```

Input rows may include padding; output rows are packed. Failures return
an all-zero buffer/image. Input stays caller-owned; release output with
`ZLPNG_Free`. Calls are thread-safe. Ordinary calls use up to eight workers;
`ZLPNG_CompressThreads` and `ZLPNG_DecompressThreadsWithLimit` accept an
explicit worker limit (0 = automatic, 1 = serial, maximum 64).
Thread count never changes file contents.

`ZLPNG_DecompressWithLimit(file, max_raw_bytes)` rejects larger images before
allocating pixels. The limit bounds output size, not total scratch memory.

Effort 1–8 changes only OpenZL's backend settings:

| Effort | OpenZL graph | Level | Window |
| ---: | --- | ---: | --- |
| 1 | Native LZ | 1 | 64 KiB |
| 2 | Numeric | 1 | Default |
| 3 | Numeric | 2 | Default |
| 4 | Numeric | 3 | Default |
| 5 | Numeric | 4 | Default |
| 6 | Numeric | 5 | Default |
| 7 | Numeric | 6 | Default |
| 8 | Numeric | 7 | Default |

Every effort uses the same predictor and one backend pass. Higher effort
can occasionally produce a larger file. Decoding needs no effort setting.

## ZLP2 container

All metadata integers are little-endian.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | `ZLP2` magic |
| 4 | 2 | Width |
| 6 | 2 | Height |
| 8 | 1 | Channels, 1–4 |
| 9 | 1 | Bytes per sample, 1 or 2 |
| 10 | 1 | OpenZL graph: 1 = serial LZ, 2 = numeric |
| 11 | 1 | Reserved, zero |
| 12 | variable | Consecutive bands: uint32 frame length, then one OpenZL frame |

A band contains `max(1, floor(262144 / row_bytes))` complete rows, except
the final shorter band. Each band resets the predictor and stores planar
residuals. Eight-bit RGB(A) first becomes `(G, R-G, B-G[, A])`, with modular
arithmetic; other sample formats keep their channels unchanged. Missing
neighbors are zero. The MED prediction is `median(L, U, L+U-UL)` and the
residual is `sample-prediction`, modulo the sample range.

OpenZL 0.3.0 uses frame format 27. Numeric frames retain sample widths;
serial frames contain little-endian residual bytes. The implementation
converts numeric data to/from host endianness. The decoder validates shape,
reserved fields, band/frame lengths, output type/width/size and exact input
consumption. Trailing bytes are rejected. Checksums are disabled.

ZLP2 is incompatible with original ZPNG and earlier experimental ZLP1 files.

## ZRAW interchange

The CLI reads/writes exact samples using a 20-byte header:
`ZRAW`, then four little-endian uint32 values: width, height, channels,
bits per channel. The remainder is packed, row-major, interleaved samples;
16-bit samples are little-endian. The payload must match the shape exactly.
