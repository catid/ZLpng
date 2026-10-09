// Paired complete-API timing. Original ZPNG stays isolated in a shared library.
#include "../zlpng.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <png.h>
#include <random>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

// Original public ABI; no original source or headers are required here.
struct ZPNG_Buffer { unsigned char* Data; unsigned Bytes; };
struct ZPNG_ImageData {
    ZPNG_Buffer Buffer;
    unsigned BytesPerChannel, Channels, WidthPixels, HeightPixels, StrideBytes;
};
static void check(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}
static unsigned integer(const std::string& s) {
    size_t n = 0;
    const unsigned long x = std::stoul(s, &n);
    check(n == s.size() && x <= 0xfffffffful, "Invalid integer");
    return unsigned(x);
}
static unsigned u32(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 |
           unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
}
static long long median(std::vector<long long> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
static std::string samples(const std::vector<long long>& values) {
    std::ostringstream out;
    out << "\"[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
    }
    out << "]\"";
    return out.str();
}
struct Original {
    void* handle;
    ZPNG_Buffer (*compress)(const ZPNG_ImageData*);
    ZPNG_ImageData (*decompress)(ZPNG_Buffer);
    void (*release)(ZPNG_Buffer*);
    explicit Original(const std::string& path) {
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        check(handle != nullptr, "Cannot load original ZPNG reference");
        compress = reinterpret_cast<decltype(compress)>(dlsym(handle, "ZPNG_Compress"));
        decompress = reinterpret_cast<decltype(decompress)>(dlsym(handle, "ZPNG_Decompress"));
        release = reinterpret_cast<decltype(release)>(dlsym(handle, "ZPNG_Free"));
        check(compress && decompress && release, "Missing reference API symbols");
    }
    ~Original() { dlclose(handle); }
};
struct Result {
    std::string codec;
    unsigned effort = 0, threads = 1, bytes = 0;
    std::vector<long long> encode, decode;
};

struct PngMemory {
    std::vector<unsigned char> bytes;
    size_t cursor = 0;
};
static void png_write_memory(png_structp png, png_bytep bytes, png_size_t count) {
    auto& target = *static_cast<PngMemory*>(png_get_io_ptr(png));
    try { target.bytes.insert(target.bytes.end(), bytes, bytes + count); }
    catch (...) { png_error(png, "PNG output allocation failed"); }
}
static void png_read_memory(png_structp png, png_bytep bytes, png_size_t count) {
    auto& source = *static_cast<PngMemory*>(png_get_io_ptr(png));
    if (count > source.bytes.size() - source.cursor) png_error(png, "Short PNG input");
    std::memcpy(bytes, source.bytes.data() + source.cursor, count);
    source.cursor += count;
}
static void png_flush_memory(png_structp) {}

static std::vector<unsigned char> encode_png(const ZLPNG_ImageData& image) {
    PngMemory output;
    // Avoid geometric output growth; include scanline filter bytes and framing.
    output.bytes.reserve(size_t(image.Buffer.Bytes) + image.Buffer.Bytes / 8 + image.HeightPixels + 4096);
    std::vector<png_bytep> rows(image.HeightPixels);
    for (unsigned y = 0; y < image.HeightPixels; ++y)
        rows[y] = image.Buffer.Data + size_t(y) * image.StrideBytes;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    check(png != nullptr, "Cannot create PNG encoder");
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); throw std::runtime_error("Cannot create PNG info"); }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("PNG encode failed");
    }
    const int colors[] = {0, PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_GRAY_ALPHA,
        PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGB_ALPHA};
    png_set_write_fn(png, &output, png_write_memory, png_flush_memory);
    png_set_compression_level(png, 6);
    png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_ALL_FILTERS);
    png_set_IHDR(png, info, image.WidthPixels, image.HeightPixels,
        image.BytesPerChannel * 8, colors[image.Channels], PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);
    if (image.BytesPerChannel == 2) png_set_swap(png); // ZRAW samples are little endian.
    png_write_image(png, rows.data());
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return std::move(output.bytes);
}
using PngPixels = std::unique_ptr<unsigned char, decltype(&std::free)>;
static PngPixels decode_png(std::vector<unsigned char>& input, const ZLPNG_ImageData& expected) {
    // Move ownership into the callback state, then restore it before returning.
    PngMemory source;
    source.bytes.swap(input);
    PngPixels output(nullptr, &std::free);
    std::vector<png_bytep> rows;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    check(png != nullptr, "Cannot create PNG decoder");
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); throw std::runtime_error("Cannot create PNG info"); }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        source.bytes.swap(input);
        throw std::runtime_error("PNG decode failed");
    }
    png_set_read_fn(png, &source, png_read_memory);
    png_read_info(png, info);
    if (png_get_image_width(png, info) != expected.WidthPixels ||
        png_get_image_height(png, info) != expected.HeightPixels ||
        png_get_channels(png, info) != expected.Channels ||
        png_get_bit_depth(png, info) != expected.BytesPerChannel * 8)
        png_error(png, "PNG geometry mismatch");
    if (expected.BytesPerChannel == 2) png_set_swap(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != expected.StrideBytes) png_error(png, "PNG stride mismatch");
    output.reset(static_cast<unsigned char*>(std::malloc(expected.Buffer.Bytes)));
    if (!output) png_error(png, "PNG pixel allocation failed");
    rows.resize(expected.HeightPixels);
    for (unsigned y = 0; y < expected.HeightPixels; ++y)
        rows[y] = output.get() + size_t(y) * expected.StrideBytes;
    png_read_image(png, rows.data());
    png_read_end(png, info);
    png_destroy_read_struct(&png, &info, nullptr);
    source.bytes.swap(input);
    return output;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--versions") {
            std::cout << "{\"libpng\":\"" << png_get_libpng_ver(nullptr)
                << "\",\"zlib\":\"" << zlibVersion()
                << "\",\"png_settings\":\"level 6; PNG_ALL_FILTERS adaptive; noninterlaced; original 8/16-bit channels; no metadata\"}\n";
            return 0;
        }
        check(argc >= 2, "Usage: zlpng_bench IMAGE.zraw --reference ORIGINAL.so --output ROWS.csv [--image-id ID --efforts 1,2,3,4,5,6,7,8 --threads 1,8 --repeats 3 --warmups 1 --seed N]");
        std::string output, reference, id = argv[1], efforts = "1,2,3,4,5,6,7,8", threads = "1,8";
        unsigned repeats = 3, warmups = 1, seed = 20261010;
        for (int i = 2; i < argc; i += 2) {
            check(i + 1 < argc, "Missing argument value");
            const std::string key = argv[i], value = argv[i + 1];
            if (key == "--output") output = value;
            else if (key == "--reference") reference = value;
            else if (key == "--image-id") id = value;
            else if (key == "--efforts") efforts = value;
            else if (key == "--threads") threads = value;
            else if (key == "--repeats") repeats = integer(value);
            else if (key == "--warmups") warmups = integer(value);
            else if (key == "--seed") seed = integer(value);
            else throw std::runtime_error("Unknown argument: " + key);
        }
        check(!output.empty() && !reference.empty() && repeats >= 3 &&
              repeats % 2 && repeats <= 99 && warmups >= 1 && warmups <= 10,
              "Invalid timing protocol");
        check(id.find_first_of(",\"\r\n") == std::string::npos, "Invalid image ID");
        std::vector<unsigned> thread_counts;
        std::istringstream thread_list(threads);
        for (std::string item; std::getline(thread_list, item, ',');) {
            unsigned count = integer(item);
            check(count >= 1 && count <= 64, "Invalid thread count");
            check(std::find(thread_counts.begin(), thread_counts.end(), count) == thread_counts.end(), "Duplicate thread count");
            thread_counts.push_back(count);
        }
        check(!thread_counts.empty(), "No thread counts requested");
        std::vector<Result> results(2);
        results[0].codec = "original_zpng";
        results[1].codec = "png";
        std::istringstream list(efforts);
        for (std::string item; std::getline(list, item, ',');) {
            const unsigned effort = integer(item);
            check(effort >= 1 && effort <= 8, "Effort must be 1..8");
            for (const auto& r : results) check(r.effort != effort, "Duplicate effort");
            for (unsigned count : thread_counts) {
                Result r; r.codec = "zlpng"; r.effort = effort; r.threads = count;
                results.push_back(r);
            }
        }
        check(results.size() > 2, "No efforts requested");
        std::ifstream input(argv[1], std::ios::binary);
        check(bool(input), "Cannot read image");
        std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)), {});
        check(file.size() >= 20 && !std::memcmp(file.data(), "ZRAW", 4), "Invalid ZRAW header");
        const unsigned w = u32(file.data() + 4), h = u32(file.data() + 8);
        const unsigned c = u32(file.data() + 12), bits = u32(file.data() + 16);
        check(w && h && w <= 65535 && h <= 65535 && c >= 1 && c <= 4 &&
              (bits == 8 || bits == 16), "Invalid image dimensions");
        const size_t raw = size_t(w) * h * c * (bits / 8);
        check(raw <= 0xffffffffu && file.size() == raw + 20, "Invalid raw data length");
        ZLPNG_ImageData image = {};
        image.WidthPixels = w; image.HeightPixels = h; image.Channels = c;
        image.BytesPerChannel = bits / 8; image.StrideBytes = w * c * (bits / 8);
        image.Buffer = {file.data() + 20, unsigned(raw)};
        const ZPNG_ImageData original = {{image.Buffer.Data, image.Buffer.Bytes},
            image.BytesPerChannel, c, w, h, image.StrideBytes};
        Original old(reference);
        cpu_set_t allowed, serial;
        check(sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "Cannot read CPU affinity");
        CPU_ZERO(&serial);
        for (int cpu = CPU_SETSIZE - 1; cpu >= 0; --cpu)
            if (CPU_ISSET(cpu, &allowed)) { CPU_SET(cpu, &serial); break; }
        check(CPU_COUNT(&allowed) >= int(*std::max_element(thread_counts.begin(), thread_counts.end())),
              "CPU affinity has fewer cores than requested threads");
        std::vector<unsigned> order(results.size());
        for (unsigned i = 0; i < order.size(); ++i) order[i] = i;
        std::mt19937 random(seed);
        using Clock = std::chrono::steady_clock;
        // Each pass visits every codec; shuffle anew to distribute drift fairly.
        for (int repeat = -int(warmups); repeat < int(repeats); ++repeat) {
            std::shuffle(order.begin(), order.end(), random);
            for (unsigned index : order) {
                Result& result = results[index];
                check(sched_setaffinity(0, sizeof(allowed), result.threads == 1 ? &serial : &allowed) == 0,
                      "Cannot set CPU affinity");
                Clock::time_point a, b, d, e;
                unsigned bytes;
                bool valid;
                if (result.codec == "png") {
                    a = Clock::now(); auto packed = encode_png(image); b = Clock::now();
                    d = Clock::now(); auto restored = decode_png(packed, image); e = Clock::now();
                    valid = restored && !std::memcmp(restored.get(), image.Buffer.Data, raw);
                    check(packed.size() <= 0xffffffffu, "PNG exceeds benchmark size limit");
                    bytes = unsigned(packed.size());
                } else if (result.effort) {
                    a = Clock::now(); auto packed = ZLPNG_CompressThreads(&image, result.effort, result.threads); b = Clock::now();
                    check(packed.Data, "ZLpng encode failed");
                    d = Clock::now(); auto restored = ZLPNG_DecompressThreadsWithLimit(packed, 0xffffffffu, result.threads); e = Clock::now();
                    valid = restored.WidthPixels == w && restored.HeightPixels == h &&
                        restored.Channels == c && restored.BytesPerChannel == bits / 8 &&
                        restored.StrideBytes == image.StrideBytes && restored.Buffer.Bytes == raw &&
                        restored.Buffer.Data && !std::memcmp(restored.Buffer.Data, image.Buffer.Data, raw);
                    bytes = packed.Bytes;
                    ZLPNG_Free(&restored.Buffer); ZLPNG_Free(&packed);
                } else {
                    a = Clock::now(); auto packed = old.compress(&original); b = Clock::now();
                    check(packed.Data, "Original ZPNG encode failed");
                    d = Clock::now(); auto restored = old.decompress(packed); e = Clock::now();
                    // Original reports width*channels for 16-bit data as well.
                    valid = restored.WidthPixels == w && restored.HeightPixels == h &&
                        restored.Channels == c && restored.BytesPerChannel == bits / 8 &&
                        restored.StrideBytes == w * c && restored.Buffer.Bytes == raw &&
                        restored.Buffer.Data && !std::memcmp(restored.Buffer.Data, image.Buffer.Data, raw);
                    bytes = packed.Bytes;
                    old.release(&restored.Buffer); old.release(&packed);
                }
                check(valid, "Lossless roundtrip mismatch in " + result.codec + " effort " + std::to_string(result.effort));
                check(!result.bytes || result.bytes == bytes, "Nondeterministic compressed size");
                result.bytes = bytes;
                if (repeat >= 0) {
                    result.encode.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
                    result.decode.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(e - d).count());
                }
            }
        }
        std::ofstream out(output);
        check(bool(out), "Cannot write results");
        out << "image_id,codec,effort,threads,bytes,raw_bytes,encode_ns,decode_ns,verified,warmups,repeats,encode_ns_samples,decode_ns_samples\n";
        for (const auto& r : results)
            out << id << ',' << r.codec << ',' << r.effort << ',' << r.threads << ','
                << r.bytes << ',' << raw << ',' << median(r.encode) << ',' << median(r.decode)
                << ",1," << warmups << ',' << repeats << ',' << samples(r.encode) << ',' << samples(r.decode) << '\n';
        check(bool(out), "Cannot finish results");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zlpng_bench: " << error.what() << '\n';
        return 1;
    }
}
