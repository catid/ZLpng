# Zpng
Small experimental lossless photographic image compression library with a C API and command-line interface.

It's much faster than PNG and compresses better for photographic images.
This compressor often takes less than 6% of the time of a PNG compressor and produces a file that is 66% of the size.
It was written in just 500 lines of C code thanks to Facebook's Zstd library.

The goal was to see if I could create a better lossless compressor than PNG in just one evening (a few hours) using Zstd and some past experience writing my GCIF library.  Zstd is magical.

I'm not expecting anyone else to use this, but feel free if you need some fast compression in just a few hundred lines of C code.


#### Example results

```
$ ./ZpngApp.exe -c IMG_0008.jpg IMG_0008.zpng
Compressing IMG_0008.jpg to IMG_0008.zpng
Loaded IMG_0008.jpg in 338.712 msec
Compressed ZPNG in 248.737 msec
ZPNG compression size: 27731886 bytes

$ ./ZpngApp.exe -d IMG_0008.zpng IMG_0008.png
Decompressing IMG_0008.zpng to IMG_0008.png (output will be PNG format)
Decompressed ZPNG in 123.028 msec
Compressed PNG in 4339.82 msec
Wrote decompressed PNG file: IMG_0008.png

$ ll IMG_0008.*
-rw-r--r-- 1 leon 197121 13991058 Jun 25  2017 IMG_0008.jpg
-rw-r--r-- 1 leon 197121 41897485 May  2 22:29 IMG_0008.png
-rw-r--r-- 1 leon 197121 27731886 May  2 22:29 IMG_0008.zpng
```

FLIF and other formats get better compression ratios but you will wait like 20 seconds for them to finish.

This compressor runs faster than some JPEG decoders!
This compressor takes less than 6% of the time of the PNG compressor and produces a file that is 66% of the size.


#### How it works

This library is similar to PNG in that the image is first filtered, and then submitted to a data compressor.
The filtering step is a bit simpler and faster but somehow more effective than the one used in PNG.
The data compressor used is Zstd, which makes it significantly faster than PNG to compress and decompress.

Filtering:

(1) Reversible color channel transformation.
(2) Split each color channel into a separate color plane.
(3) Subtract each color value from the one to its left.

This kind of filtering works great for large photographic images and is very fast.


#### Experimental results

I ran a few experiments to arrive at this simple codec:

Interleaving is a 1% compression win, and a 0.3% performance win: Not used.

Splitting the data into blocks of 4 at a time actually reduces compression.

```
No filtering:
Total size = 2629842851
Total compress time = 12942621 usec

Subtract only:
Total size = 1570514796
Total compress time = 16961469 usec

Color filter, followed by subtract:
Total size = 1514724952
Total compress time = 16554638 usec

Subtract, followed by color filter:
Total size = 1514724952
Total compress time = 16376380 usec
Total de-compress time = 6511436 usec
Notes: Order of applying filter does not matter but this way is faster.

Subtract, followed by color filter YUVr from JPEG2000:
Total size = 1506802640
Total compress time = 17169743 usec
Total de-compress time = 7107897 usec
Note: Only 0.5% better compression ratio in trade for performance impact.

Subtract, followed by color filter, splitting into YUV color planes:
Total size = 1486938616
Total compress time = 14514563 usec
Total de-compress time = 6596546 usec
Note: Huge improvement!  Let's call this Zpng!
```

#### Credits

Software by Christopher A. Taylor mrcatid@gmail.com

Please reach out if you need support or would like to collaborate on a project.

#### Reproducible prefilter experiments

The [prefilter laboratory](experiments/README.md) contains a diverse downloaded
and generated corpus, a native reversible-filter sweep, grouped development/test
analysis, and a [staged experiment plan](experiments/PLAN.md). See the
[findings](experiments/FINDINGS.md) and [measured results](experiments/results/REPORT.md)
for filter and blend comparisons. These experiments leave the original codec and
file format unchanged.

The [second experiment round](experiments/round2/README.md) explores ten more
optimized filter families and includes a [source-backed backlog of 25 further
ideas and blends](experiments/round2/LITERATURE.md).

The [third experiment round](experiments/round3/README.md) implements that
backlog, adds seven compositions and sampled blend selection, and expands the
dataset with native camera, medical and scientific images. Its completed
[findings](experiments/round3/FINDINGS.md) cover 3.23 million verified trials,
size/speed tradeoffs on fresh sources, and domain-specific recommendations.

The [fourth round](experiments/round4/README.md) selects a best standalone
method per image format and grows an ordered list to eight, with no mandatory
legacy slot. It adds an experimental native API and CLI with compression
effort 1–8; [findings](experiments/round4/FINDINGS.md) describe the learned lists
and actual API size/time tradeoffs. The original codec entry points remain
available; experimental streams use the expanded decoder.

<!-- ROUND4_PERFORMANCE_START -->
#### Compression effort: performance and processing time

All eight effort levels were measured on **324 images / 852.58 MiB** of exact sample data: 205 development, 83 continuity, and 36 confirmation-manifest images. Both evaluation sets were previously inspected; the combined results include development and are descriptive.

![Compression savings and processing time at every effort level](experiments/round4/results/readme_effort_performance.svg)

**Compression performance.** Positive savings mean smaller files than legacy ZPNG; negative savings mean expansion. The mean columns use the geometric mean of each image’s compressed-size ratio to legacy, giving every image equal weight. The experiment’s selection score instead gives each domain equal weight. “All bytes” uses summed file sizes; raw/stored is the whole corpus compression ratio (higher is better).

| Effort | DEV mean saving | Continuity mean saving | Reused confirmation mean saving | All 324 mean saving | All bytes saved | Raw / stored |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Legacy | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% | 2.113× |
| Old4 | 34.45% | 13.61% | 31.03% | 29.25% | 12.66% | 2.419× |
| **1 (default)** | 29.33% | −6.15% | 13.17% | 19.75% | 4.12% | 2.204× |
| 2 | 37.30% | 10.12% | 31.84% | 30.60% | 9.34% | 2.331× |
| 3 | 41.22% | 14.58% | 35.69% | 34.66% | 9.61% | 2.337× |
| 4 | 42.84% | 15.25% | 36.24% | 36.00% | 9.90% | 2.345× |
| 5 | 44.54% | 18.25% | 38.58% | 38.05% | 12.98% | 2.428× |
| 6 | 45.27% | 18.62% | 38.73% | 38.65% | 13.06% | 2.430× |
| 7 | 45.75% | 19.10% | 40.34% | 39.26% | 13.67% | 2.447× |
| 8 | 45.99% | 19.40% | 40.50% | 39.51% | 13.68% | 2.448× |

**Processing time.** One pinned logical CPU on a Threadripper PRO 9985WX, bundled Zstd level 1, one warmup and three measured repetitions. Each image contributes its median API time; totals sum those medians and means divide by 324. Times include attempted encodings, allocations and output copies, and exclude file I/O. They are the cost of processing the corpus once at that level, not the duration of the full repeated benchmark.

| Effort | Encode total (s) | Decode total (s) | Mean encode (ms/image) | Mean decode (ms/image) | Encode (MiB/s) | Decode (MiB/s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Legacy | 1.348 | 0.676 | 4.16 | 2.09 | 632.51 | 1261.64 |
| Old4 | 24.014 | 4.669 | 74.12 | 14.41 | 35.50 | 182.62 |
| **1 (default)** | 4.263 | 3.169 | 13.16 | 9.78 | 200.00 | 269.07 |
| 2 | 8.751 | 3.098 | 27.01 | 9.56 | 97.42 | 275.23 |
| 3 | 12.797 | 3.160 | 39.50 | 9.75 | 66.62 | 269.81 |
| 4 | 18.165 | 3.135 | 56.07 | 9.68 | 46.93 | 271.96 |
| 5 | 26.429 | 6.007 | 81.57 | 18.54 | 32.26 | 141.92 |
| 6 | 53.073 | 6.170 | 163.81 | 19.04 | 16.06 | 138.17 |
| 7 | 59.210 | 6.351 | 182.75 | 19.60 | 14.40 | 134.25 |
| 8 | 61.600 | 6.093 | 190.12 | 18.80 | 13.84 | 139.93 |

Old4 is the older four-method blend, separate from effort 4. Effort 1 is the default; effort k tries the first k methods, capped by the format’s list length. More effort cannot increase an image’s file size, but some images remain larger than legacy. Smaller files can also select a slower decoder.

[Full split/domain results](experiments/round4/FINDINGS.md) · [Exact metrics (CSV)](experiments/round4/results/readme_metrics.csv) · [Formulas and provenance](experiments/round4/results/readme_metrics.json)
<!-- ROUND4_PERFORMANCE_END -->
