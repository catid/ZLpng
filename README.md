# ZLpng

Lossless image compression built around [OpenZL](https://openzl.org/), with
simple reversible image filters and **compression effort 1–8**. ZLpng
supports one to four channels and 8-bit or 16-bit samples, preserving every
sample bit, including alpha and hidden color values.

**Effort 1 is the fast default:** vectorized left/color filtering followed
by OpenZL native LZ. Higher efforts try complementary filters and OpenZL
numeric graphs from a fixed list for each sample format, then keep the
smallest complete file. The decoder runs only the stored method. Increasing
effort cannot increase compressed size.

ZLpng has its own `ZLP1` format and C API. The original
[Zpng](https://github.com/catid/Zpng) remains a separate historical project.
ZLpng files are not compatible with its decoder.

<!-- ZLPNG_PERFORMANCE_START -->
## Compression performance

On 324 images, effort 1 encodes at **814 MiB/s** and decodes at **1611 MiB/s** (1.28× and 1.37× original ZPNG). Its files are 0.76% larger in total. Effort 8 saves **13.77%** of the original codec's stored bytes.

![ZLpng compression, encode time and decode time by effort](experiments/round6/results/zlpng_analysis/readme_performance.svg)

The corpus contains 852.58 MiB of raw samples. Per-image geometric mean savings versus original ZPNG; higher is better. Total-byte savings weight images by compressed size. Raw/stored is the ratio of total raw bytes to complete compressed bytes.

| Effort | Development (205) | Evaluation A (83) | Evaluation B (36) | All (324) | Total-byte saving | Raw/stored |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | -17.35% | -5.60% | -20.71% | -14.58% | -0.76% | 2.097× |
| 2 | 29.18% | 4.63% | 20.32% | 22.56% | 8.20% | 2.301× |
| 3 | 34.42% | 10.93% | 28.54% | 28.39% | 8.62% | 2.312× |
| 4 | 37.70% | 15.97% | 32.51% | 32.13% | 9.99% | 2.347× |
| 5 | 39.49% | 15.97% | 33.15% | 33.45% | 10.21% | 2.353× |
| 6 | 41.50% | 18.45% | 39.48% | 36.06% | 13.67% | 2.447× |
| 7 | 42.21% | 18.72% | 39.61% | 36.62% | 13.75% | 2.450× |
| 8 | 42.69% | 19.67% | 39.61% | 37.15% | 13.77% | 2.450× |

Complete API processing time across all 324 images:

| Effort | Encode ms/image | Decode ms/image | Encode MiB/s | Decode MiB/s | Total encode s | Total decode s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original ZPNG | 4.145 | 2.231 | 634.9 | 1179.4 | 1.343 | 0.723 |
| 1 | 3.234 | 1.634 | 813.7 | 1610.8 | 1.048 | 0.529 |
| 2 | 13.147 | 5.415 | 200.2 | 485.9 | 4.260 | 1.755 |
| 3 | 25.931 | 5.153 | 101.5 | 510.7 | 8.402 | 1.669 |
| 4 | 33.422 | 5.529 | 78.7 | 475.9 | 10.829 | 1.791 |
| 5 | 42.677 | 5.424 | 61.7 | 485.1 | 13.827 | 1.757 |
| 6 | 49.631 | 6.375 | 53.0 | 412.8 | 16.080 | 2.066 |
| 7 | 62.076 | 6.720 | 42.4 | 391.6 | 20.113 | 2.177 |
| 8 | 78.603 | 6.772 | 33.5 | 388.6 | 25.467 | 2.194 |

Measured on an AMD Ryzen Threadripper PRO 9985WX, one pinned logical CPU, with a native-optimized release build. Each image uses the median of three complete API calls after one warmup; totals sum those medians. Filtering, all attempted candidates, allocations, framing and inverse transforms are included; file I/O is excluded. Every repetition reconstructs the exact input.

The corpus blends photographs, graphics/text, synthetic patterns, and scientific/specialized images, including alpha and 16-bit samples. Development selected the lists; both evaluation sets were used in earlier experiments. These are descriptive corpus results, not a fresh holdout.

[Detailed measurements](experiments/round6/results/zlpng_analysis/readme_metrics.csv) · [Verification record](experiments/round6/results/zlpng_analysis/provenance.json)

<!-- ZLPNG_PERFORMANCE_END -->

## Build and use

Requires CMake 3.20.2+, a C11/C++17 compiler, and a 64-bit platform. CMake fetches
the pinned OpenZL 0.3.0 source and its dependencies into the build directory.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

build/zlpng compress input.zraw output.zlp --effort 4
build/zlpng decompress output.zlp restored.zraw
```

Omit `--effort` to use 1. The CLI uses `.zraw` files containing exact sample
data; see the [format and API guide](docs/FORMAT.md). Applications can link
`ZLpng::ZLpng` (or `zlpnglib`) and call `ZLPNG_Compress`, `ZLPNG_Decompress`,
and `ZLPNG_Free`. The image API accepts padded input rows and returns packed
rows.

For maximum speed on the machine doing the build, configure with
`-DZLPNG_NATIVE=ON`. Leave it off when distributing binaries to other CPUs.
The current build and benchmark validation is on Linux x86-64.

## Filters and effort

Each format begins with the fast left/color transform using OpenZL LZ level
1 and a 64 KiB window. Complementary entries include gradient and MED
predictors, green subtraction, planar layouts, reversible sample packing,
run handling, and local predictor blends. Some 16-bit entries use OpenZL's
numeric graph to retain sample structure through compression.

The [ordered lists](experiments/round6/results/zlpng_selection/policy.csv)
were selected using only the 205 development images. Greedy selection gives
each image domain equal weight, allows one backend per transform, and caps
lists at eight entries. Formats with fewer useful development candidates
stop earlier. These are measured choices within the tested candidate pool,
not a claim that every possible OpenZL graph has been explored.

See the [benchmark methodology](experiments/round6/PLAN.md) and
[selection record](experiments/round6/results/zlpng_selection/policy.json).
Downloaded images and codec dependencies are kept out of git.

## Credits

Software by Christopher A. Taylor — mrcatid@gmail.com.
OpenZL is developed by Meta and its contributors.
