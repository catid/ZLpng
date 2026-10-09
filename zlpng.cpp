// MED + OpenZL. Independent horizontal bands permit parallel encode and decode.
#include "zlpng.h"
#include "zlpng_backend.h"
#include "internal/med_filter.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Allocation = std::unique_ptr<unsigned char, decltype(&std::free)>;
constexpr size_t HeaderBytes = 12;
constexpr size_t BandBytes = 256 * 1024;

void require(bool ok) { if (!ok) throw std::runtime_error("Invalid ZLpng input"); }
Allocation allocate(size_t size) {
    Allocation p(static_cast<unsigned char*>(std::malloc(size)), &std::free);
    if (!p) throw std::bad_alloc();
    return p;
}
unsigned read16(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
unsigned read32(const unsigned char* p) { return read16(p) | read16(p + 2) << 16; }
void write16(unsigned char* p, unsigned n) { p[0] = n; p[1] = n >> 8; }
void write32(unsigned char* p, unsigned n) { write16(p, n); write16(p + 2, n >> 16); }
size_t raw_size(unsigned w, unsigned h, unsigned c, unsigned b) {
    require(w && h && w <= 65535 && h <= 65535 && c >= 1 && c <= 4 && (b == 1 || b == 2));
    const uint64_t size = uint64_t(w) * h * c * b;
    require(size <= UINT_MAX);
    return size_t(size);
}
zlpng_med::Format format(unsigned w, unsigned h, unsigned c, unsigned b) {
    return {w, h, c, b * 8};
}
void numeric_endian(unsigned char* p, size_t n, unsigned element) {
    const uint16_t one = 1;
    if (element == 2 && *reinterpret_cast<const unsigned char*>(&one) == 0)
        for (size_t i = 0; i < n; i += 2) std::swap(p[i], p[i + 1]);
}

// Worker state is local to this call. Thread count never changes encoded bytes.
template<class Function> void parallel(unsigned count, unsigned threads, const Function& fn) {
    if (!threads) threads = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    threads = std::min({threads, count, 64u});
    std::atomic<unsigned> next{0};
    std::atomic<bool> failed{false};
    const auto worker = [&] {
        try {
            for (;;) {
                const unsigned i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= count || failed.load(std::memory_order_relaxed)) break;
                fn(i);
            }
        } catch (...) { failed.store(true, std::memory_order_relaxed); }
    };
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (unsigned i = 1; i < threads; ++i) {
        try { workers.emplace_back(worker); }
        catch (...) { break; } // Fall back to the workers already available.
    }
    worker();
    for (auto& thread : workers) thread.join();
    require(!failed.load(std::memory_order_relaxed));
}
struct Band { size_t offset, bytes; };
}

extern "C" ZLPNG_Buffer ZLPNG_CompressThreads(const ZLPNG_ImageData* image, unsigned effort, unsigned threads) {
    try {
        require(image && image->Buffer.Data && effort >= 1 && effort <= 8);
        const unsigned w = image->WidthPixels, h = image->HeightPixels;
        const unsigned c = image->Channels, b = image->BytesPerChannel;
        raw_size(w, h, c, b);
        const size_t row = size_t(w) * c * b;
        require(image->StrideBytes >= row && uint64_t(h - 1) * image->StrideBytes + row <= image->Buffer.Bytes);
        const unsigned rows = unsigned(std::max(size_t(1), BandBytes / row));
        const unsigned count = (h + rows - 1) / rows;
        const int codec = effort >= 2 ? ZLPNG_OPENZL_NUMERIC : ZLPNG_OPENZL_LZ;
        const unsigned element = codec == ZLPNG_OPENZL_NUMERIC ? b : 1;
        std::vector<Band> bands(count);
        size_t capacity = HeaderBytes;
        for (unsigned i = 0; i < count; ++i) {
            const size_t bound = zlpng_backend_bound(codec, std::min(rows, h - i * rows) * row);
            require(bound && capacity + 4 + bound <= UINT_MAX);
            bands[i] = {capacity, bound}; capacity += 4 + bound;
        }
        auto output = allocate(capacity);
        std::memcpy(output.get(), "ZLP2", 4);
        write16(output.get() + 4, w); write16(output.get() + 6, h);
        output.get()[8] = c; output.get()[9] = b;
        output.get()[10] = codec; output.get()[11] = 0;
        parallel(count, threads, [&](unsigned i) {
            const unsigned y = i * rows, height = std::min(rows, h - y);
            const size_t raw = height * row;
            auto residuals = allocate(raw);
            zlpng_med::encode(image->Buffer.Data + size_t(y) * image->StrideBytes,
                image->StrideBytes, residuals.get(), format(w, height, c, b));
            if (codec == ZLPNG_OPENZL_NUMERIC) numeric_endian(residuals.get(), raw, element);
            Band& band = bands[i];
            const size_t stored = zlpng_backend_compress(codec, effort == 1 ? 1 : int(effort - 1),
                effort == 1 ? 16 : 0, element, residuals.get(), raw,
                output.get() + band.offset + 4, band.bytes);
            require(stored && stored <= band.bytes);
            write32(output.get() + band.offset, unsigned(stored)); band.bytes = stored;
        });
        size_t stored = HeaderBytes;
        for (const auto& band : bands) {
            std::memmove(output.get() + stored, output.get() + band.offset, band.bytes + 4);
            stored += band.bytes + 4;
        }
        return {output.release(), unsigned(stored)};
    } catch (...) { return {}; }
}

extern "C" ZLPNG_ImageData ZLPNG_DecompressThreadsWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes, unsigned threads) {
    try {
        require(encoded.Data && encoded.Bytes > HeaderBytes && !std::memcmp(encoded.Data, "ZLP2", 4));
        const unsigned char* p = encoded.Data;
        const unsigned w = read16(p + 4), h = read16(p + 6), c = p[8], b = p[9];
        const int codec = p[10];
        require((codec == ZLPNG_OPENZL_LZ || codec == ZLPNG_OPENZL_NUMERIC) && p[11] == 0);
        const size_t raw = raw_size(w, h, c, b), row = size_t(w) * c * b;
        require(raw <= max_raw_bytes);
        const unsigned rows = unsigned(std::max(size_t(1), BandBytes / row));
        const unsigned count = (h + rows - 1) / rows;
        std::vector<Band> bands(count);
        size_t offset = HeaderBytes;
        for (auto& band : bands) {
            require(offset + 4 <= encoded.Bytes);
            const unsigned n = read32(p + offset); offset += 4;
            require(n && n <= encoded.Bytes - offset);
            band = {offset, n}; offset += n;
        }
        require(offset == encoded.Bytes);
        auto pixels = allocate(raw);
        parallel(count, threads, [&](unsigned i) {
            const unsigned y = i * rows, height = std::min(rows, h - y);
            const size_t size = height * row;
            auto residuals = allocate(size);
            const unsigned element = codec == ZLPNG_OPENZL_NUMERIC ? b : 1;
            const Band& band = bands[i];
            require(zlpng_backend_decompress_typed(codec, element, p + band.offset, band.bytes,
                residuals.get(), size) == size);
            if (codec == ZLPNG_OPENZL_NUMERIC) numeric_endian(residuals.get(), size, element);
            zlpng_med::decode(residuals.get(), pixels.get() + size_t(y) * row, row, format(w, height, c, b));
        });
        return {{pixels.release(), unsigned(raw)}, b, c, w, h, unsigned(row)};
    } catch (...) { return {}; }
}
extern "C" ZLPNG_Buffer ZLPNG_Compress(const ZLPNG_ImageData* image, unsigned effort) {
    return ZLPNG_CompressThreads(image, effort, 0);
}
extern "C" ZLPNG_ImageData ZLPNG_DecompressWithLimit(ZLPNG_Buffer encoded, unsigned max_raw_bytes) {
    return ZLPNG_DecompressThreadsWithLimit(encoded, max_raw_bytes, 0);
}
extern "C" ZLPNG_ImageData ZLPNG_Decompress(ZLPNG_Buffer encoded) {
    return ZLPNG_DecompressWithLimit(encoded, UINT_MAX);
}
extern "C" void ZLPNG_Free(ZLPNG_Buffer* buffer) {
    if (buffer) { std::free(buffer->Data); *buffer = {}; }
}
