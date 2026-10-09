# Zpng

Lossless image compression with a C API and command-line tools. The current
encoder combines reversible image filters with Zstd and offers **compression
effort 1–8** to trade encoding time for smaller files. It supports one to four
channels and 8-bit or 16-bit samples, preserving every sample including alpha
and hidden color values.

**Effort 1 is the default.** Higher levels try more filters from a fixed,
format-specific list and keep the smallest result. Decoding runs only the
stored method and needs no effort setting. Increasing effort cannot increase
an image's compressed size.

## Compression performance

Measured on **324 images / 852.58 MiB**, covering photos, graphics, documents,
scientific and medical imagery, camera mosaics, and synthetic patterns. The
splits contain 205 development images, 83 evaluation-A images, and 36
evaluation-B images. They include tuning data and previously used evaluation
images, so the results describe this corpus.

![Compression savings and processing time at every effort level](experiments/round4/results/readme_effort_performance.svg)

Positive savings mean smaller output than the original ZPNG codec; negative
values mean expansion. Mean savings use the geometric mean of per-image size
ratios, with equal weight per image. “All bytes saved” uses summed file sizes;
“Raw / stored” is the total compression ratio, where higher is better.

| Effort | Development mean saving | Evaluation A mean saving | Evaluation B mean saving | All 324 mean saving | All bytes saved | Raw / stored |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original ZPNG | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% | 2.113× |
| **1 (default)** | 29.33% | −6.15% | 13.17% | 19.75% | 4.12% | 2.204× |
| 2 | 37.30% | 10.12% | 31.84% | 30.60% | 9.34% | 2.331× |
| 3 | 41.22% | 14.58% | 35.69% | 34.66% | 9.61% | 2.337× |
| 4 | 42.84% | 15.25% | 36.24% | 36.00% | 9.90% | 2.345× |
| 5 | 44.54% | 18.25% | 38.58% | 38.05% | 12.98% | 2.428× |
| 6 | 45.27% | 18.62% | 38.73% | 38.65% | 13.06% | 2.430× |
| 7 | 45.75% | 19.10% | 40.34% | 39.26% | 13.67% | 2.447× |
| 8 | 45.99% | 19.40% | 40.50% | 39.51% | 13.68% | 2.448× |

## Processing time

Measured on one logical CPU of a Threadripper PRO 9985WX with Zstd level 1.
Each image contributes the median of three API runs after one warmup. Totals
sum those medians across the corpus; means divide by 324 images. Timing
includes all attempted encodings, allocations and output copies, and excludes
file I/O.

| Effort | Encode total (s) | Decode total (s) | Mean encode (ms/image) | Mean decode (ms/image) | Encode (MiB/s) | Decode (MiB/s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original ZPNG | 1.348 | 0.676 | 4.16 | 2.09 | 632.51 | 1261.64 |
| **1 (default)** | 4.263 | 3.169 | 13.16 | 9.78 | 200.00 | 269.07 |
| 2 | 8.751 | 3.098 | 27.01 | 9.56 | 97.42 | 275.23 |
| 3 | 12.797 | 3.160 | 39.50 | 9.75 | 66.62 | 269.81 |
| 4 | 18.165 | 3.135 | 56.07 | 9.68 | 46.93 | 271.96 |
| 5 | 26.429 | 6.007 | 81.57 | 18.54 | 32.26 | 141.92 |
| 6 | 53.073 | 6.170 | 163.81 | 19.04 | 16.06 | 138.17 |
| 7 | 59.210 | 6.351 | 182.75 | 19.60 | 14.40 | 134.25 |
| 8 | 61.600 | 6.093 | 190.12 | 18.80 | 13.84 | 139.93 |

Effort 2 provides a useful lower-cost option. Effort 5 captures most of the
whole-corpus byte savings before the larger cost increase at effort 6.
Effort 8 produces the smallest output in the list. Some images remain larger
than the original codec, and a smaller file can select a slower decoder.

## Build and use

The effort-based encoder is built with the optional experiment targets:

```sh
cmake -S . -B experiments/build-r4 -DCMAKE_BUILD_TYPE=Release \
  -DZPNG_BUILD_EXPERIMENTS=ON
cmake --build experiments/build-r4 --target zpng_effort -j

experiments/build-r4/zpng_effort compress input.zraw output.zpf --effort 4
experiments/build-r4/zpng_effort decompress output.zpf restored.zraw
```

Omit `--effort` to use level 1. The CLI reads `.zraw` files containing exact
sample data; [format details and API examples](experiments/round4/README.md)
are in the encoder documentation. Applications can link `zpng_effortlib` and
call `ZPNG_CompressWithEffort` and `ZPNG_DecompressWithFilters`.

The output identifies its own filter and parameters. Use the expanded
decoder for these streams; the original `ZPNG_Decompress` supports original
ZPNG streams only.

## How it works

The encoder applies a reversible prediction, color transform, or sample
packing method before Zstd compression. Each sample format has an ordered
list beginning with the best standalone filter under a domain-balanced
size score, followed by complementary filters. Effort k tries the first k
entries, capped by the list's length. The stored result includes all metadata
needed to invert the winning transform exactly.

See the [encoder documentation](experiments/round4/README.md),
[ordered filter list](experiments/round4/results/selection/policy.tsv), and
[benchmark metrics](experiments/round4/results/readme_metrics.csv).

## Credits

Software by Christopher A. Taylor — mrcatid@gmail.com.
