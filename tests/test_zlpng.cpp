#include "../zlpng.h"
#include "../zlpng_backend.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
size_t roundtrips = 0, rejected = 0, parity_checks = 0;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct BufferOwner { ZLPNG_Buffer value = {}; ~BufferOwner() { ZLPNG_Free(&value); } };
struct ImageOwner { ZLPNG_ImageData value = {}; ~ImageOwner() { ZLPNG_Free(&value.Buffer); } };
ZLPNG_Buffer view(Bytes& data) { return {data.data(), unsigned(data.size())}; }
void reject(Bytes& bytes) {
    ImageOwner image; image.value = ZLPNG_DecompressWithLimit(view(bytes), 1048576);
    check(!image.value.Buffer.Data && !image.value.Buffer.Bytes && !image.value.WidthPixels &&
          !image.value.HeightPixels && !image.value.Channels && !image.value.BytesPerChannel &&
          !image.value.StrideBytes, "Malformed stream accepted or nonzero error result");
    ++rejected;
}
void verify(const ZLPNG_Buffer& encoded, const Bytes& raw, unsigned width, unsigned height,
            unsigned channels, unsigned bytes) {
    check(encoded.Data && encoded.Bytes > 12 && !std::memcmp(encoded.Data, "ZLP2", 4), "Wrong native stream");
    check(encoded.Data[10] == 1 || encoded.Data[10] == 2, "Non-OpenZL backend emitted");
    ImageOwner restored; restored.value = ZLPNG_Decompress(encoded);
    const auto& image = restored.value;
    check(image.Buffer.Data && image.Buffer.Bytes == raw.size() && image.WidthPixels == width &&
          image.HeightPixels == height && image.Channels == channels && image.BytesPerChannel == bytes &&
          image.StrideBytes == width * channels * bytes && !std::memcmp(image.Buffer.Data, raw.data(), raw.size()),
          "Complete image roundtrip mismatch");
    ++roundtrips;
}
Bytes numeric_frame(unsigned element, unsigned header_element) {
    const unsigned short samples[] = {0x1234, 0xabcd, 0x0102, 0xff00};
    const size_t size = sizeof(samples), offset = 16;
    Bytes encoded(offset + zlpng_backend_bound(2, size));
    std::memcpy(encoded.data(), "ZLP2", 4);
    encoded[4] = size / header_element; encoded[6] = 1;
    encoded[8] = 1; encoded[9] = header_element; encoded[10] = 2;
    const size_t result = zlpng_backend_compress(2, 1, 0, element, samples, size,
                                                encoded.data() + offset, encoded.size() - offset);
    check(result != 0, "Cannot create numeric fixture");
    for (unsigned i = 0; i < 4; ++i) encoded[12 + i] = static_cast<unsigned char>(result >> (8 * i));
    encoded.resize(offset + result);
    return encoded;
}
}

int main() {
    try {
        std::mt19937 random(912603);
        const unsigned shapes[][2] = {{1, 1}, {1, 7}, {17, 1}, {15, 3}, {16, 3},
                                       {17, 3}, {31, 2}, {32, 2}, {33, 2}, {65, 9}};
        for (unsigned channels = 1; channels <= 4; ++channels) for (unsigned bytes : {1u, 2u})
            for (const auto& shape : shapes) for (unsigned pattern = 0; pattern < 4; ++pattern) {
                const unsigned width = shape[0], height = shape[1], row = width * channels * bytes;
                Bytes raw(size_t(row) * height);
                for (size_t i = 0; i < raw.size(); ++i)
                    raw[i] = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ?
                             static_cast<unsigned char>(i * 17 + i / 31) : static_cast<unsigned char>(random());
                ZLPNG_ImageData source = {{raw.data(), unsigned(raw.size())}, bytes, channels, width, height, row};
                const unsigned stride = row + 13;
                Bytes padded(size_t(stride) * (height - 1) + row, 0x5a);
                for (unsigned y = 0; y < height; ++y)
                    std::memcpy(padded.data() + size_t(y) * stride, raw.data() + size_t(y) * row, row);
                ZLPNG_ImageData strided = source;
                strided.Buffer = view(padded); strided.StrideBytes = stride;
                for (unsigned effort = 1; effort <= 8; ++effort) {
                    BufferOwner encoded; encoded.value = ZLPNG_Compress(&source, effort);
                    verify(encoded.value, raw, width, height, channels, bytes);
                    BufferOwner padded_encoded; padded_encoded.value = ZLPNG_Compress(&strided, effort);
                    verify(padded_encoded.value, raw, width, height, channels, bytes);
                    check(padded_encoded.value.Bytes == encoded.value.Bytes &&
                          !std::memcmp(padded_encoded.value.Data, encoded.value.Data, encoded.value.Bytes),
                          "Row padding changed encoded bytes");
                    ++parity_checks;
                    ImageOwner limited;
                    limited.value = ZLPNG_DecompressWithLimit(encoded.value, unsigned(raw.size()) - 1);
                    check(!limited.value.Buffer.Data, "Decode limit ignored"); ++rejected;
                    limited.value = ZLPNG_DecompressWithLimit(encoded.value, unsigned(raw.size()));
                    check(limited.value.Buffer.Data && limited.value.Buffer.Bytes == raw.size(), "Exact decode limit rejected");
                }
                for (unsigned invalid : {0u, 9u, UINT_MAX}) {
                    BufferOwner bad; bad.value = ZLPNG_Compress(&source, invalid);
                    check(!bad.value.Data && !bad.value.Bytes, "Invalid effort accepted"); ++rejected;
                }
                ZLPNG_ImageData invalid = source; invalid.StrideBytes = row - 1;
                BufferOwner short_stride; short_stride.value = ZLPNG_Compress(&invalid, 1);
                check(!short_stride.value.Data, "Short stride accepted"); ++rejected;
                invalid = source; --invalid.Buffer.Bytes;
                BufferOwner short_input; short_input.value = ZLPNG_Compress(&invalid, 1);
                check(!short_input.value.Data, "Short source buffer accepted"); ++rejected;
            }

        // Cross-band SIMD/stride boundaries, independent callers, and deterministic threading.
        for (unsigned channels = 1; channels <= 4; ++channels) for (unsigned bytes : {1u, 2u}) {
            const unsigned width = 1031, height = 513, row = width * channels * bytes;
            Bytes raw(size_t(row) * height);
            for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<unsigned char>(random());
            ZLPNG_ImageData source = {view(raw), bytes, channels, width, height, row};
            for (unsigned effort : {1u, 3u, 8u}) {
                BufferOwner serial; serial.value = ZLPNG_CompressThreads(&source, effort, 1);
                verify(serial.value, raw, width, height, channels, bytes);
                for (unsigned threads : {2u, 8u, 64u, UINT_MAX}) {
                    BufferOwner parallel; parallel.value = ZLPNG_CompressThreads(&source, effort, threads);
                    check(parallel.value.Data && parallel.value.Bytes == serial.value.Bytes &&
                          !std::memcmp(parallel.value.Data, serial.value.Data, serial.value.Bytes),
                          "Thread count changed encoded bytes");
                    ImageOwner restored;
                    restored.value = ZLPNG_DecompressThreadsWithLimit(parallel.value, unsigned(raw.size()), threads);
                    check(restored.value.Buffer.Data && restored.value.Buffer.Bytes == raw.size() &&
                          !std::memcmp(restored.value.Buffer.Data, raw.data(), raw.size()), "Parallel roundtrip failed");
                    ++roundtrips; ++parity_checks;
                }
            }
        }

        Bytes raw(63 * 7 * 3, 0x35);
        ZLPNG_ImageData source = {view(raw), 1, 3, 63, 7, 63 * 3};
        BufferOwner encoded; encoded.value = ZLPNG_Compress(&source, 1);
        check(encoded.value.Data != nullptr, "Malformed-test fixture failed");
        Bytes good(encoded.value.Data, encoded.value.Data + encoded.value.Bytes);
        for (size_t length = 0; length < good.size(); ++length) {
            Bytes truncated(good.begin(), good.begin() + length); reject(truncated);
        }
        for (const auto& mutation : std::vector<std::pair<size_t, unsigned char>>{
                 {0, 'X'}, {3, '1'}, {4, 0}, {6, 0}, {8, 0}, {8, 5},
                 {9, 0}, {9, 3}, {10, 0}, {10, 3}, {11, 1}, {12, 0xff}, {15, 0xff}}) {
            Bytes corrupt = good; corrupt[mutation.first] = mutation.second; reject(corrupt);
        }
        Bytes trailing = good; trailing.push_back(0); reject(trailing);
        Bytes joined = good; joined.insert(joined.end(), good.begin(), good.end()); reject(joined);
        for (unsigned width : {1u, 2u}) {
            Bytes numeric = numeric_frame(width, width);
            ImageOwner image; image.value = ZLPNG_Decompress(view(numeric));
            check(image.value.Buffer.Data && image.value.Buffer.Bytes == 8, "Valid numeric fixture rejected");
            ++roundtrips;
            Bytes wrong_width = numeric_frame(width, 3 - width); reject(wrong_width);
            Bytes wrong_codec = numeric; wrong_codec[10] = 1; reject(wrong_codec);
            Bytes numeric_trailing = numeric; numeric_trailing.push_back(0); reject(numeric_trailing);
            Bytes reserved = numeric; reserved[11] = 1; reject(reserved);
        }
        // A bad later band must fail cleanly after other workers have started.
        Bytes large(1027 * 1101 * 3, 0x39);
        ZLPNG_ImageData multi = {view(large), 1, 3, 1027, 1101, 1027 * 3};
        BufferOwner bands; bands.value = ZLPNG_CompressThreads(&multi, 1, 8);
        check(bands.value.Data != nullptr, "Band fixture failed");
        Bytes damaged(bands.value.Data, bands.value.Data + bands.value.Bytes);
        size_t second = 16;
        for (unsigned i = 0; i < 4; ++i) second += size_t(damaged[12 + i]) << (8 * i);
        check(second + 8 < damaged.size(), "Missing second band");
        damaged[second + 4] ^= 0xff;
        ImageOwner failure;
        failure.value = ZLPNG_DecompressThreadsWithLimit(view(damaged), unsigned(large.size()), 8);
        check(!failure.value.Buffer.Data, "Corrupt later band accepted"); ++rejected;

        ZLPNG_Free(nullptr);
        ZLPNG_Buffer empty = {}; ZLPNG_Free(&empty); ZLPNG_Free(&empty);
        check(!empty.Data && !empty.Bytes, "Empty free failed");
        BufferOwner null_input; null_input.value = ZLPNG_Compress(nullptr, 1);
        check(!null_input.value.Data && !null_input.value.Bytes, "Null source accepted"); ++rejected;
        std::cout << "ZLpng tests passed: " << roundtrips << " complete roundtrips, " << parity_checks
                  << " stride/size checks, " << rejected << " rejected cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ZLpng test failed: " << error.what() << '\n';
        return 1;
    }
}
