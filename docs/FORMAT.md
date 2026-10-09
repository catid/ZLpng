# ZLpng format and API

ZLpng stores exact image samples in a self-contained `ZLP1` container. It uses
OpenZL 0.3.0, frame format 27. It does not decode original ZPNG files.

The C API is declared in [`zlpng.h`](../zlpng.h). A `ZLPNG_ImageData` describes
1–4 channels, 8-bit or little-endian 16-bit samples, dimensions 1–65535, and
row stride. The buffer must cover the last packed row. Row padding is omitted
from the file; all sample bits, alpha, and hidden RGB values are preserved.
Buffers use unsigned 32-bit lengths, limiting a single image to less than
4 GiB. Compression can also fail when the compressed allocation exceeds that
interface.

```c
#include "zlpng.h"

/* Fill image.Buffer, dimensions, channel depth, and stride first. */
ZLPNG_Buffer file = ZLPNG_Compress(&image, 4);
if (file.Data) {
    ZLPNG_ImageData restored = ZLPNG_Decompress(file);
    /* A non-null restored.Buffer.Data indicates success. */
    ZLPNG_Free(&restored.Buffer);
    ZLPNG_Free(&file);
}
```

Effort must be 1–8. The CLI defaults to 1; the API takes an explicit effort.
An encoder tries the first *k* entries in its format's ordered list, capped
by that list's length. The first smallest complete file wins. Decoding does
not depend on the encoder's list or effort. Increasing effort cannot enlarge
a file, but there is no size bound relative to the separate ZPNG format.

Failures return a zero buffer or image. `ZLPNG_Free` resets the released
buffer and accepts a null pointer. Input remains caller-owned. Calls use
independent contexts and can run concurrently.

`ZLPNG_DecompressWithLimit(file, max_raw_bytes)` rejects a larger declared
image before allocating pixel storage. This bounds the returned image, not
all temporary decoder memory. The ordinary decoder uses `UINT_MAX` as its
limit.

## Container

All image metadata is little-endian.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | `ZLP1` magic |
| 4 | 1 | OpenZL input type: 1 = serial LZ, 2 = numeric |
| 5 | 1 | Sample width: 1 for serial, 1 or 2 for numeric |
| 6 | 2 | Transform metadata size: 8, 24, 32, or 48 |
| 8 | variable | Geometry and complete reversible-transform description |
| following | remainder | Exactly one OpenZL frame |

Transform metadata uses the existing tested binary layouts for the fast
left/color transform (8 bytes), fixed/adaptive predictors (24), sample
representations (32), and compound filters (48). These metadata layouts do
not imply that a ZPNG/ZPF decoder can read the OpenZL payload. The parsing
and inverse-transform implementation is in
[`internal/filter_runtime.hpp`](../internal/filter_runtime.hpp).

The decoder checks geometry, reserved fields, transform parameters, declared
payload size, OpenZL output size/type, numeric sample width, and exact frame
length. Trailing data and concatenated frames are rejected. Numeric samples
are converted between the format's little-endian bytes and host order at
the OpenZL boundary. Optional OpenZL checksums are disabled, as in the
benchmarks; the format provides no content integrity checksum.

## ZRAW interchange

The command-line tool accepts `.zraw` to preserve all supported sample
formats without an image loader changing depth or channels:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 4 | `ZRAW` magic |
| 4 | 4 | Width |
| 8 | 4 | Height |
| 12 | 4 | Channels, 1–4 |
| 16 | 4 | Bits per channel, 8 or 16 |
| 20 | remainder | Packed row-major interleaved samples |

All integers and 16-bit samples are little-endian. The payload must match
the geometry exactly. ZLpng preserves sample data; image-file metadata such
as EXIF and ICC profiles is outside this interface.
