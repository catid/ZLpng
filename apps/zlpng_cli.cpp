#include "../zlpng.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
static void check(bool success, const char* message) {
    if (!success) throw std::runtime_error(message);
}
static uint32_t read32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static void write32(unsigned char* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
}
static Bytes read_file(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    check(bool(file), "Cannot open input file");
    const std::streamoff size = file.tellg();
    check(size >= 0 && uint64_t(size) <= UINT_MAX, "Input exceeds buffer interface");
    file.seekg(0);
    Bytes data(static_cast<size_t>(size));
    if (size) file.read(reinterpret_cast<char*>(data.data()), size);
    check(bool(file), "Cannot read input file");
    return data;
}
static void write_file(const char* path, const unsigned char* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    check(bool(file), "Cannot create output file");
    file.write(reinterpret_cast<const char*>(data), size);
    check(bool(file), "Cannot write output file");
}
struct BufferOwner { ZLPNG_Buffer value = {}; ~BufferOwner() { ZLPNG_Free(&value); } };
struct ImageOwner { ZLPNG_ImageData value = {}; ~ImageOwner() { ZLPNG_Free(&value.Buffer); } };
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage:\n  zlpng compress INPUT.zraw OUTPUT.zlp [--effort 1..8]\n"
                         "  zlpng decompress INPUT.zlp OUTPUT.zraw\n"
                         "Effort defaults to 1. ZRAW preserves 1..4 channels and 8/16-bit samples.\n";
            return 0;
        }
        check(argc == 4 || argc == 6, "Use --help for command syntax");
        const std::string command = argv[1];
        Bytes input = read_file(argv[2]);
        if (command == "compress") {
            unsigned effort = 1;
            if (argc == 6) {
                check(std::string(argv[4]) == "--effort", "Unknown compression option");
                const std::string text = argv[5];
                check(text.size() == 1 && text[0] >= '1' && text[0] <= '8', "Effort must be 1..8");
                effort = unsigned(text[0] - '0');
            }
            check(input.size() >= 20 && !std::memcmp(input.data(), "ZRAW", 4), "Invalid ZRAW header");
            ZLPNG_ImageData image = {};
            image.WidthPixels = read32(input.data() + 4); image.HeightPixels = read32(input.data() + 8);
            image.Channels = read32(input.data() + 12);
            const unsigned bits = read32(input.data() + 16);
            check(bits == 8 || bits == 16, "Unsupported sample depth");
            check(image.WidthPixels >= 1 && image.WidthPixels <= 65535 &&
                  image.HeightPixels >= 1 && image.HeightPixels <= 65535 &&
                  image.Channels >= 1 && image.Channels <= 4, "Unsupported image dimensions or channel count");
            image.BytesPerChannel = bits / 8;
            const uint64_t row = uint64_t(image.WidthPixels) * image.Channels * image.BytesPerChannel;
            check(row <= UINT_MAX && image.HeightPixels && row * image.HeightPixels == input.size() - 20,
                  "ZRAW dimensions do not match payload");
            image.StrideBytes = unsigned(row);
            image.Buffer = {input.data() + 20, unsigned(input.size() - 20)};
            BufferOwner output; output.value = ZLPNG_Compress(&image, effort);
            check(output.value.Data != nullptr, "Image compression failed");
            write_file(argv[3], output.value.Data, output.value.Bytes);
            std::cout << image.Buffer.Bytes << " raw bytes -> " << output.value.Bytes << " bytes\n";
        } else if (command == "decompress") {
            check(argc == 4, "Decompression takes no effort parameter");
            ZLPNG_Buffer encoded = {input.data(), unsigned(input.size())};
            ImageOwner image; image.value = ZLPNG_Decompress(encoded);
            check(image.value.Buffer.Data != nullptr, "Image decompression failed");
            Bytes output(20 + size_t(image.value.Buffer.Bytes));
            std::memcpy(output.data(), "ZRAW", 4);
            write32(output.data() + 4, image.value.WidthPixels); write32(output.data() + 8, image.value.HeightPixels);
            write32(output.data() + 12, image.value.Channels); write32(output.data() + 16, image.value.BytesPerChannel * 8);
            std::memcpy(output.data() + 20, image.value.Buffer.Data, image.value.Buffer.Bytes);
            write_file(argv[3], output.data(), output.size());
            std::cout << image.value.Buffer.Bytes << " raw bytes restored\n";
        } else throw std::runtime_error("Unknown command; use --help");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zlpng: " << error.what() << '\n';
        return 1;
    }
}
