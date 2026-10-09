# Reproduce the benchmark

The current results compare complete ZLpng files at efforts 1–8 using one or up to eight threads, original ZPNG using one thread, and PNG using one thread. PNG uses libpng with compression level 6, all five adaptive scanline filters, no interlacing or metadata, and the original channel count and 8/16-bit precision. Versions, executable hashes, CPU topology and measurement settings are recorded in [results.json](results.json).

Install a C++ compiler, CMake, libpng development headers, Python 3 and Matplotlib. Build the optional benchmark and isolated original reference. CMake downloads the pinned original ZPNG source into the build directory and produces `build/libzpng_reference.so`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DZLPNG_NATIVE=ON -DZLPNG_BUILD_BENCHMARKS=ON
cmake --build build -j
```

[corpus.json](corpus.json) identifies 324 external ZRAW files with source references, SHA-256 hashes, dimensions, domains and cohort labels. It contains no image pixels. Set `CORPUS_ROOT` to the directory containing the manifest's relative paths. Source references document provenance; they are not a promise that downloading the original source reproduces every crop, conversion or generated fixture. Exact replay requires the listed ZRAW files and their matching hashes.

Each ZRAW file begins with `ZRAW`, followed by four little-endian 32-bit integers: width, height, channels, bits per sample. Packed interleaved pixels follow, with little-endian 16-bit samples.

```sh
python3 benchmarks/run.py --binary build/zlpng_bench \
  --reference build/libzpng_reference.so --data-root "$CORPUS_ROOT" \
  --cpus 120,121,122,123,124,125,126,127 --output benchmarks/results.csv
python3 benchmarks/summarize.py
```

Select eight available logical CPUs belonging to distinct physical cores. Serial cases run on the highest selected CPU; parallel cases use the selected set. Run on an otherwise idle machine. For a quick check, add `--smoke --efforts 1,3,8 --output /tmp/smoke.csv`. No benchmark changes the source images.

Every image runs one warmup and three timed passes. Each pass shuffles the codec/effort/thread cases. Encoding and decoding time includes complete API work and allocation; file I/O, affinity changes and pixel verification are outside the timer. Every decoded image, including warmups, is compared byte for byte. PNG output storage is reserved before encoding and decoded pixels use uninitialized allocation, both inside the timer. Each case records its median and all three samples in [results.csv](results.csv).

[summary.csv](summary.csv) reports each effort and thread count overall, by image type, domain and cohort. File-size savings use the geometric mean of complete-file ratios, with one vote per image. Pooled savings and compression ratios use summed file sizes. Throughput uses total raw bytes divided by summed per-image median times; milliseconds per image average those medians. `MB/s` means decimal megabytes per second. Eight-thread results are explicitly parallel comparisons against serial PNG and ZPNG baselines; small images may use fewer workers.

“Synthetic” includes generated images and synthetic domains. “Specialized” includes microscopy, medical, astronomy, sensor CFA, materials and multiband scientific images. “Real” includes the remaining photographs, graphics, documents, textures and alpha images. Cohort labels record previously inspected data and do not represent a fresh held-out evaluation.

![Compression size and speed by image type](performance.svg)
