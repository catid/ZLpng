# Zpng

Lossless image compression with a C API and command-line tools. The current
encoder combines reversible image filters with Zstd and offers **compression
effort 1–8** to trade encoding time for smaller files. It supports one to four
channels and 8-bit or 16-bit samples, preserving every sample including alpha
and hidden color values.

**Effort 1 is the default:** an optimized implementation that produces exactly
the same files as original ZPNG. Higher levels try more filters from a fixed,
format-specific list and keep the smallest result. Decoding runs only the
stored method and needs no effort setting. Increasing effort cannot increase
an image's compressed size, and every level is bounded by original ZPNG size.

On the corpus below, level 1 encodes at **736 MiB/s versus 657 MiB/s** for
original ZPNG. Level 6 saves **13.04% of total bytes**; level 8 saves **13.73%**.

<!-- ROUND5_PERFORMANCE_START -->
## Performance at every effort level

The current encoder was measured on **324 images / 852.58 MiB** of exact sample data: 205 Development, 83 Evaluation A, and 36 Evaluation B images. Development shaped the policy; evaluation images were previously used. Results describe this corpus.

![Current compression savings and processing time at every effort level](experiments/round5/results/readme_effort_performance.svg)

**Compression performance.** Positive savings mean smaller files than original ZPNG. The mean columns use the geometric mean of each image’s compressed-size ratio to original ZPNG, giving every image equal weight. “All bytes” uses summed file sizes; raw/stored is the whole corpus compression ratio (higher is better).

| Effort | Development mean saving | Evaluation A mean saving | Evaluation B mean saving | All 324 mean saving | All bytes saved | Raw / stored |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original ZPNG | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% | 2.113× |
| **1 (default)** | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% | 2.113× |
| 2 | 33.06% | 5.85% | 17.85% | 25.27% | 5.83% | 2.244× |
| 3 | 37.87% | 10.88% | 35.83% | 31.61% | 9.35% | 2.331× |
| 4 | 41.51% | 16.14% | 36.59% | 35.28% | 10.20% | 2.353× |
| 5 | 43.13% | 16.77% | 37.14% | 36.60% | 10.34% | 2.357× |
| 6 | 44.63% | 19.23% | 39.03% | 38.35% | 13.04% | 2.430× |
| 7 | 45.36% | 19.60% | 39.15% | 38.95% | 13.13% | 2.432× |
| 8 | 45.77% | 19.84% | 40.75% | 39.46% | 13.73% | 2.449× |

**Processing time.** One pinned logical CPU on a Threadripper PRO 9985WX, bundled Zstd level 1, one warmup and three measured repetitions. Each image contributes its median API time; totals sum those medians and means divide by 324. Times include attempted encodings, allocations and output copies, and exclude file I/O. They estimate the cost of processing the corpus once at that level, not the duration of the full repeated benchmark.

| Effort | Encode total (s) | Decode total (s) | Mean encode (ms/image) | Mean decode (ms/image) | Encode (MiB/s) | Decode (MiB/s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original ZPNG | 1.297 | 0.714 | 4.00 | 2.20 | 657.25 | 1194.18 |
| **1 (default)** | 1.158 | 0.724 | 3.57 | 2.23 | 736.41 | 1177.97 |
| 2 | 3.957 | 1.372 | 12.21 | 4.23 | 215.44 | 621.39 |
| 3 | 6.456 | 1.471 | 19.92 | 4.54 | 132.07 | 579.40 |
| 4 | 8.802 | 1.589 | 27.17 | 4.90 | 96.86 | 536.58 |
| 5 | 12.746 | 1.556 | 39.34 | 4.80 | 66.89 | 547.80 |
| 6 | 15.839 | 1.954 | 48.89 | 6.03 | 53.83 | 436.38 |
| 7 | 19.967 | 2.127 | 61.63 | 6.56 | 42.70 | 400.91 |
| 8 | 25.787 | 2.334 | 79.59 | 7.20 | 33.06 | 365.23 |

Effort 1 produces identical files to original ZPNG with an optimized encoder. Effort k tries the first k ordered methods, capped by the format’s list length, and keeps the smallest complete stream. Every prefix includes legacy, so increasing effort cannot increase file size and no image exceeds original ZPNG size. Different winning filters can change decoder speed.

[Exact metrics (CSV)](experiments/round5/results/readme_metrics.csv) · [Formulas and provenance](experiments/round5/results/readme_metrics.json)
<!-- ROUND5_PERFORMANCE_END -->

## Build and use

The effort-based encoder is built with the optional experiment targets:

```sh
cmake -S . -B experiments/build-r5 -DCMAKE_BUILD_TYPE=Release \
  -DZPNG_BUILD_EXPERIMENTS=ON
cmake --build experiments/build-r5 --target zpng_effort -j

experiments/build-r5/zpng_effort compress input.zraw output.zpf --effort 4
experiments/build-r5/zpng_effort decompress output.zpf restored.zraw
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
list beginning with the fast ZPNG filter, followed by complementary filters
chosen on development images with equal weight per image domain. Effort k
tries the first k entries, capped by the list's length. The stored result includes all metadata
needed to invert the winning transform exactly.

See the [encoder documentation](experiments/round4/README.md),
[ordered filter list](experiments/round5/results/selection/policy.csv), and
[benchmark metrics](experiments/round5/results/readme_metrics.csv).

## Credits

Software by Christopher A. Taylor — mrcatid@gmail.com.
